// SPDX-License-Identifier: GPL-3.0-only
#include "internal.hpp"
#include <iostream>

#include <CommonCrypto/CommonDigest.h>
#include <cstring>
#include <cctype>
#include <set>
#include <dlfcn.h>
#include <iomanip>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
namespace sm {
static const char *lib = "/Applications/EXScanS.app/Contents/Frameworks/libcommon.dylib";
static const char *libhash = "a773ce7b6b9f271aee2c5eb16a492903749b2f0bbb40ecb81408de9b1a279d07";
std::string sha256(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot read " + p.string());
    CC_SHA256_CTX c;
    CC_SHA256_Init(&c);
    char b[65536];
    while (f) {
        f.read(b, sizeof b);
        CC_SHA256_Update(&c, b, (CC_LONG)f.gcount());
    }
    unsigned char d[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(d, &c);
    std::ostringstream s;
    for (auto v : d)
        s << std::hex << std::setw(2) << std::setfill('0') << (int)v;
    return s.str();
}
static void supported() {
    if (!fs::is_regular_file(lib))
        throw std::runtime_error("EXScanS is required in /Applications/EXScanS.app");
    if (sha256(lib) != libhash)
        throw std::runtime_error("Unsupported EXScanS library version. Expected validated 3.2.0.4 "
                                 "ARM64 build; no files changed.");
}
static xmlNode *child(xmlNode *n, const std::string &name) {
    for (auto c = n ? n->children : nullptr; c; c = c->next)
        if (c->type == XML_ELEMENT_NODE && name == (const char *)c->name)
            return c;
    return nullptr;
}
static std::string value(xmlNode *n) {
    if (!n)
        throw std::runtime_error("Missing EXScan XML field");
    auto v = xmlNodeGetContent(n);
    std::string s = v ? (const char *)v : "";
    xmlFree(v);
    return s;
}
static void set(xmlNode *n, const char *key, const std::string &v) {
    auto c = child(n, key);
    if (!c)
        c = xmlNewChild(n, nullptr, BAD_CAST key, nullptr);
    xmlNodeSetContent(c, BAD_CAST v.c_str());
}
struct Doc {
    xmlDocPtr p;
    explicit Doc(const fs::path &f)
        : p(xmlReadFile(f.c_str(), nullptr, XML_PARSE_NONET | XML_PARSE_NOBLANKS)) {
        if (!p)
            throw std::runtime_error("Cannot read project XML: " + f.string());
        if (p->intSubset || p->extSubset)
            throw std::runtime_error("DTD not accepted in project XML");
    }
    ~Doc() {
        xmlFreeDoc(p);
    }
    xmlNode *root() {
        return xmlDocGetRootElement(p);
    }
    void save(const fs::path &f) {
        if (xmlSaveFormatFileEnc(f.c_str(), p, "utf-8", 1) < 0)
            throw std::runtime_error("Cannot save project XML");
    }
};
static bool within(const fs::path &p, const fs::path &r) {
    auto a = p.begin(), b = r.begin();
    for (; b != r.end(); ++a, ++b)
        if (a == p.end() || *a != *b)
            return false;
    return true;
}
static fs::path resolve_local(const fs::path &base, std::string s, const fs::path &root) {
    std::replace(s.begin(), s.end(), '\\', '/');
    fs::path p = s;
    if (p.is_absolute() || !fs::exists(base / p))
        p = p.filename();
    p = fs::weakly_canonical(base / p);
    if (!within(p, root) || !fs::is_regular_file(p))
        throw std::runtime_error("Missing or external project file: " + s);
    return p;
}
Dataset discover(const fs::path &input) {
    supported();
    Dataset d;
    d.root = fs::canonical(input);
    if (!fs::is_directory(d.root))
        throw std::runtime_error("Input must be a directory");
    std::vector<fs::path> sol, prj;
    for (auto &e : fs::directory_iterator(d.root)) {
        if (e.path().extension() == ".sln_fix")
            sol.push_back(e.path());
        if (e.path().extension() == ".fix_prj")
            prj.push_back(e.path());
    }
    if (sol.size() > 1)
        throw std::runtime_error(
            "More than one solution in input directory; use a directory containing one solution");
    if (sol.size() == 1) {
        d.solution = sol[0];
        prj.clear();
        Doc doc(d.solution);
        auto projects = child(doc.root(), "projects");
        for (auto p = projects ? projects->children : nullptr; p; p = p->next)
            if (p->type == XML_ELEMENT_NODE && std::string((char *)p->name) == "project")
                prj.push_back(resolve_local(d.root, value(child(p, "path")), d.root));
    } else
        std::sort(prj.begin(), prj.end());
    if (prj.empty())
        throw std::runtime_error("No .sln_fix or .fix_prj projects found");
    std::vector<fs::path> seen;
    std::set<std::string> outputNames;
    auto reserveName = [&](const fs::path &path) {
        auto name = path.filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!outputNames.insert(name).second)
            throw std::runtime_error("Duplicate native output filename: " + path.filename().string());
    };
    for (auto &p : prj) {
        Group g;
        reserveName(p);
        g.project = p;
        g.name = p.stem().string();
        Doc doc(p);
        auto data = child(child(doc.root(), "SCENE"), "DATA");
        std::vector<fs::path> files;
        for (auto m = data ? data->children : nullptr; m; m = m->next)
            if (m->type == XML_ELEMENT_NODE && std::string((char *)m->name) == "MESH") {
                std::string stem = value(child(m, "NAME"));
                files.push_back(resolve_local(p.parent_path(), stem + ".rge", d.root));
            }
        std::sort(files.begin(), files.end());
        if (files.empty())
            throw std::runtime_error("Empty project " + p.string());
        for (auto &f : files) {
            if (std::find(seen.begin(), seen.end(), f) != seen.end())
                throw std::runtime_error("RGE referenced more than once: " + f.string());
            seen.push_back(f);
            Frame r;
            r.path = f;
            r.group = d.groups.size();
            // EXScanS derives capture filenames from native project/capture names.
            // Generic aliases can open successfully but break subsequent processing.
            reserveName(f);
            r.name = f.stem().string();
            g.frames.push_back(d.frames.size());
            d.frames.push_back(std::move(r));
        }
        d.groups.push_back(std::move(g));
    }
    return d;
}
static void helper(Context &ctx, std::vector<std::string> args) {
    args.insert(args.begin(), ctx.executable.string());
    std::vector<char *> p;
    for (auto &s : args)
        p.push_back(s.data());
    p.push_back(nullptr);
    pid_t pid;
    int r = posix_spawn(&pid, ctx.executable.c_str(), nullptr, nullptr, p.data(), environ);
    if (r)
        throw std::runtime_error("Cannot start native RGE adapter");
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        throw std::runtime_error("RGE adapter wait failed");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status))
        throw std::runtime_error("Native RGE adapter failed (see error above)");
}
struct Blob {
    alignas(64) unsigned char b[4096] = {};
};
template <class T> T symbol(void *h, const char *s) {
    auto p = dlsym(h, s);
    if (!p)
        throw std::runtime_error("Missing native RGE symbol");
    return reinterpret_cast<T>(p);
}
static std::pair<const char *, size_t> vec(const Blob &o, int off) {
    uintptr_t b, e;
    std::memcpy(&b, o.b + off, 8);
    std::memcpy(&e, o.b + off + 8, 8);
    if (e < b || e - b > 1000000000ULL || (e != b && !b))
        throw std::runtime_error("Invalid native vector");
    return {(const char *)b, e - b};
}
static void rawwrite(std::ostream &f, const void *p, size_t n) {
    f.write((const char *)p, n);
    if (!f)
        throw std::runtime_error("Write failed (disk full?)");
}
int native_helper(int argc, char **argv) {
    try {
        supported();
        if (argc < 4)
            throw std::runtime_error("Invalid adapter arguments");
        std::string mode = argv[1];
        void *h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
        if (!h)
            throw std::runtime_error(dlerror());
        auto ctor = symbol<void (*)(void *)>(h, "_ZN13Sn3DAlgorithm9RangeDataC1Ev");
        auto read = symbol<int (*)(const std::string &, void *, int)>(
            h, "_ZN13Sn3DAlgorithm12CRangeDataIO8read_rgeERKNSt3__112basic_stringIcNS1_11char_"
               "traitsIcEENS1_9allocatorIcEEEERNS_9RangeDataEi");
        Blob a;
        ctor(a.b);
        if (read(argv[2], a.b, 0))
            throw std::runtime_error("Cannot decode RGE");
        auto xyz = vec(a, 24), normal = vec(a, 56);
        if (xyz.second % 12 || xyz.second != normal.second || xyz.second == 0)
            throw std::runtime_error("Invalid RGE point/normal arrays");
        if (fs::exists(argv[3]))
            throw std::runtime_error("Adapter output already exists");
        if (mode == "--rge-read") {
            uint64_t count = xyz.second / 12;
            std::ofstream f(argv[3], std::ios::binary);
            rawwrite(f, &count, 8);
            rawwrite(f, a.b + 248, 96);
            rawwrite(f, xyz.first, xyz.second);
            rawwrite(f, normal.first, normal.second);
            return 0;
        }
        if (mode != "--rge-write" || argc != 5)
            throw std::runtime_error("Invalid adapter mode");
        std::ifstream pf(argv[4], std::ios::binary);
        double pose[12];
        if (!pf.read((char *)pose, 96) || pf.peek() != EOF)
            throw std::runtime_error("Invalid pose file");
        Pose P;
        std::memcpy(P.r.c.data(), pose, 72);
        std::memcpy(&P.t, pose + 9, 24);
        if (!rigid(P))
            throw std::runtime_error("Invalid output rotation");
        auto write = symbol<int (*)(const std::string &, const void *)>(
            h, "_ZN13Sn3DAlgorithm12CRangeDataIO9write_rgeERKNSt3__112basic_stringIcNS1_11char_"
               "traitsIcEENS1_9allocatorIcEEEERKNS_9RangeDataE");
        std::memcpy(a.b + 248, pose, 96);
        if (write(argv[3], a.b))
            throw std::runtime_error("Cannot write RGE");
        Blob b;
        ctor(b.b);
        if (read(argv[3], b.b, 0))
            throw std::runtime_error("Cannot reread output RGE");
        for (int off : {24, 56, 200, 224, 568}) {
            auto x = vec(a, off), y = vec(b, off);
            if (x.second != y.second || (x.second && std::memcmp(x.first, y.first, x.second)))
                throw std::runtime_error("Roundtrip changed observations");
        }
        for (auto spec : {std::pair{192, 8}, std::pair{248, 96}, std::pair{368, 192}})
            if (std::memcmp(a.b + spec.first, b.b + spec.first, spec.second))
                throw std::runtime_error("Roundtrip changed camera metadata");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "RGE: " << e.what() << '\n';
        return 1;
    }
}
Cloud read_cache(const fs::path &p, Pose &H) {
    std::ifstream f(p, std::ios::binary);
    uint64_t n = 0;
    f.read((char *)&n, 8);
    if (!f || n > 100000000)
        throw std::runtime_error("Invalid RGE cache");
    f.read((char *)H.r.c.data(), 72);
    f.read((char *)&H.t, 24);
    if (!rigid(H))
        throw std::runtime_error("Non-rigid input pose");
    std::vector<float> raw(n * 3);
    Cloud c;
    c.x.resize(n);
    c.n.resize(n);
    for (int pass = 0; pass < 2; pass++) {
        f.read((char *)raw.data(), raw.size() * 4);
        if (!f)
            throw std::runtime_error("Truncated RGE cache");
        for (size_t i = 0; i < n; i++) {
            V v{raw[3 * i], raw[3 * i + 1], raw[3 * i + 2]};
            if (!finite(v))
                throw std::runtime_error("Nonfinite observation; refusing point removal");
            (pass ? c.n : c.x)[i] = v;
        }
    }
    return c;
}
void load_prepare(Dataset &d, Context &ctx) {
    fs::create_directory(ctx.staging / "cache");
    for (size_t i = 0; i < d.frames.size(); i++) {
        ctx.check();
        auto &f = d.frames[i];
        f.sha = sha256(f.path);
        f.cache = ctx.staging / "cache" / (std::to_string(i) + ".bin");
        helper(ctx, {"--rge-read", f.path.string(), f.cache.string()});
        Cloud raw = read_cache(f.cache, f.camera);
        f.count = raw.size();
        d.points += f.count;
        for (size_t k = 0; k < raw.size(); k++) {
            if (norm(raw.n[k]) < 1e-9)
                throw std::runtime_error("Missing RGE normals; unsupported input");
            raw.x[k] = f.camera.point(raw.x[k]);
            raw.n[k] = unit(f.camera.normal(raw.n[k]));
        }
        f.work = down(raw, .55);
        if (f.work.size() < 12)
            throw std::runtime_error("Too few registration samples in " + f.path.string());
        normals(f.work, 1.2, 35);
        ctx.emit("read", "Read " + f.path.filename().string(), i + 1, d.frames.size());
    }
    for (size_t g = 0; g < d.groups.size(); g++) {
        Cloud all;
        size_t amount = 0;
        for (auto i : d.groups[g].frames)
            amount += d.frames[i].count;
        all.x.reserve(amount);
        all.n.reserve(amount);
        for (auto i : d.groups[g].frames) {
            auto &f = d.frames[i];
            Pose H;
            auto c = read_cache(f.cache, H);
            for (size_t j = 0; j < c.size(); j++) {
                all.x.push_back(H.point(c.x[j]));
                all.n.push_back(unit(H.normal(c.n[j])));
            }
        }
        auto &group = d.groups[g];
        group.cloud = registration_inliers(down(all, .8));
        group.basis = axes(group.cloud);
        V center;
        for (int k = 0; k < 3; k++) {
            std::vector<double> a;
            a.reserve(group.cloud.size());
            for (V x : group.cloud.x)
                a.push_back(dot(group.basis.c[k], x));
            center[k] = (quantile(a, .003) + quantile(a, .997)) / 2;
        }
        group.center = group.basis * center;
        ctx.emit("prepare", "Prepared " + group.name, g + 1, d.groups.size());
    }
}
void export_result(const Dataset &d, const std::vector<Pose> &poses, const fs::path &finalOut,
                   bool ply, Context &ctx) {
    auto out = ctx.staging / "EXScanS";
    fs::create_directory(out);
    std::ofstream pf;
    if (ply) {
        pf.open(ctx.staging / "merged.ply", std::ios::binary);
        pf << "ply\nformat binary_little_endian 1.0\ncomment ScanMergeNative rigid merge; units "
              "mm\nelement vertex "
           << d.points
           << "\nproperty double x\nproperty double y\nproperty double z\nproperty double "
              "nx\nproperty double ny\nproperty double nz\nend_header\n";
    }
    J transforms = J::array();
    for (size_t i = 0; i < d.frames.size(); i++) {
        ctx.check();
        const auto &f = d.frames[i];
        if (sha256(f.path) != f.sha)
            throw std::runtime_error("Source changed during processing");
        Pose H = poses[i] * f.camera;
        auto posefile = ctx.staging / "cache" / "output.pose";
        {
            std::ofstream p(posefile, std::ios::binary);
            rawwrite(p, H.r.c.data(), 72);
            rawwrite(p, &H.t, 24);
        }
        auto target = out / (f.name + ".rge");
        helper(ctx, {"--rge-write", f.path.string(), target.string(), posefile.string()});
        if (ply) {
            Pose ignored;
            auto c = read_cache(f.cache, ignored);
            std::vector<double> b(c.size() * 6);
            for (size_t k = 0; k < c.size(); k++) {
                V x = H.point(c.x[k]), n = unit(H.normal(c.n[k]));
                for (int j = 0; j < 3; j++) {
                    b[k * 6 + j] = x[j];
                    b[k * 6 + j + 3] = n[j];
                }
            }
            rawwrite(pf, b.data(), b.size() * 8);
        }
        transforms.push_back({{"source", f.path.string()},
                              {"sha256", f.sha},
                              {"points", f.count},
                              {"group", f.group},
                              {"output", f.name + ".rge"},
                              {"camera_to_merged", json_pose(H)},
                              {"project_to_merged", json_pose(poses[i])}});
        ctx.emit("export", "Saved and verified " + f.name, i + 1, d.frames.size());
    }
    xmlDocPtr solution;
    if (d.solution.empty()) {
        solution = xmlNewDoc(BAD_CAST "1.0");
        xmlDocSetRootElement(solution, xmlNewNode(nullptr, BAD_CAST "solution"));
        auto root = xmlDocGetRootElement(solution);
        set(root, "version", "0.1");
        set(root, "point_dis", "-1");
        set(root, "scan_mode", "-1");
        set(root, "haveTexture", "0");
    } else {
        Doc original(d.solution);
        solution = xmlCopyDoc(original.p, 1);
    }
    auto root = xmlDocGetRootElement(solution);
    auto projects = child(root, "projects");
    if (!projects)
        projects = xmlNewChild(root, nullptr, BAD_CAST "projects", nullptr);
    std::vector<xmlNode *> entries;
    for (auto n = projects->children; n; n = n->next)
        if (n->type == XML_ELEMENT_NODE && std::string((char *)n->name) == "project")
            entries.push_back(n);
    if (!entries.empty() && entries.size() != d.groups.size()) {
        xmlFreeDoc(solution);
        throw std::runtime_error("Solution project count changed during export");
    }
    for (size_t g = 0; g < d.groups.size(); g++) {
        const auto &group = d.groups[g];
        Doc doc(group.project);
        auto scene = child(doc.root(), "SCENE"), data = child(scene, "DATA");
        if (!data)
            throw std::runtime_error("Invalid project structure");
        size_t count = 0;
        for (auto i : group.frames)
            count += d.frames[i].count;
        // Retain the original project name, capture list, order and metadata.
        std::string name = group.project.filename().string();
        set(child(doc.root(), "SYSTEM"), "POINTS", std::to_string(count));
        set(scene, "PATH", (finalOut / "EXScanS").string() + "/");
        doc.save(out / name);
        auto project = entries.empty() ? xmlNewChild(projects, nullptr, BAD_CAST "project", nullptr)
                                       : entries[g];
        set(project, "path", name);
        auto rt = child(project, "rt");
        if (!rt)
            rt = xmlNewChild(project, nullptr, BAD_CAST "rt", nullptr);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                auto key = "rot" + std::to_string(i) + std::to_string(j);
                set(rt, key.c_str(), i == j ? "1" : "0");
            }
            auto key = "tran0" + std::to_string(i);
            set(rt, key.c_str(), "0");
        }
        if (!child(project, "groupId"))
            set(project, "groupId", "0");
        set(project, "dirty", "0");
    }
    if (xmlSaveFormatFileEnc((out / "merged.sln_fix").c_str(), solution, "utf-8", 1) < 0) {
        xmlFreeDoc(solution);
        throw std::runtime_error("Cannot write solution");
    }
    xmlFreeDoc(solution);
    auto exported = discover(out);
    if (exported.frames.size() != d.frames.size() || exported.groups.size() != d.groups.size())
        throw std::runtime_error("Exported project references do not match the input");
    if (pf.is_open()) {
        pf.close();
        if (!pf)
            throw std::runtime_error("PLY close failed");
    }
    for (auto &f : d.frames)
        if (sha256(f.path) != f.sha)
            throw std::runtime_error("Source modified during export");
    save_json(ctx.staging / "transforms.json", transforms);
    std::ofstream note(ctx.staging / "README.txt");
    note << "Open EXScanS/merged.sln_fix. Keep the entire EXScanS directory.\n"
            "All points are retained; only capture rotations and translations were changed.\n"
            "Running Global Optimization in EXScanS may change the alignment.\n"
            "The native project contains individual captures. The optional merged.ply is a single point cloud.\n";
}
} // namespace sm
