// SPDX-License-Identifier: GPL-3.0-only
#include "internal.hpp"
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#include <unistd.h>
namespace scanmerge {
Result run(const Options &o, ProgressCallback cb, const std::atomic_bool *cancel) {
    using namespace sm;
    auto started = std::chrono::steady_clock::now();
    Context ctx;
    ctx.callback = std::move(cb);
    ctx.cancel = cancel;
    unsigned cores = 0;
    size_t sz = sizeof cores;
    if (sysctlbyname("hw.perflevel0.physicalcpu", &cores, &sz, nullptr, 0) != 0 || !cores)
        cores = std::max(1u, std::thread::hardware_concurrency());
    ctx.threads = cores;
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> exe(size);
    if (_NSGetExecutablePath(exe.data(), &size))
        throw std::runtime_error("Cannot locate application");
    ctx.executable = fs::canonical(exe.data()).parent_path() / "scanmerge-rge";
    if (!fs::is_regular_file(ctx.executable))
        throw std::runtime_error("Incomplete application: missing scanmerge-rge adapter");
    auto input = fs::canonical(o.input), output = fs::weakly_canonical(fs::absolute(o.output));
    auto contains = [](const fs::path &a, const fs::path &b) {
        auto i = a.begin(), j = b.begin();
        for (; j != b.end(); ++i, ++j)
            if (i == a.end() || *i != *j)
                return false;
        return true;
    };
    if (contains(output, input) || contains(input, output))
        throw std::runtime_error("Input and output directories must be separate");
    if (fs::exists(output) && (!fs::is_directory(output) || !fs::is_empty(output)))
        throw std::runtime_error(
            "Output directory must be new or empty; no files will be overwritten");
    ctx.emit("validate", "Checking EXScanS and source project");
    Dataset d = discover(input);
    fs::create_directories(output.parent_path());
    std::string pattern = (output.parent_path() / ".scanmerge-work-XXXXXX").string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    if (!mkdtemp(name.data()))
        throw std::runtime_error("Cannot create work directory next to output");
    ctx.staging = name.data();
    ctx.log.open(ctx.staging / "progress.jsonl");
    J timings;
    try {
        auto mark = std::chrono::steady_clock::now();
        load_prepare(d, ctx);
        timings["read_prepare"] =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - mark).count();
        mark = std::chrono::steady_clock::now();
        J candidates;
        auto initial = initial_alignment(d, ctx, candidates);
        save_json(ctx.staging / "orientation_candidates.json", candidates);
        timings["initial_alignment"] =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - mark).count();
        mark = std::chrono::steady_clock::now();
        auto before = quality(d, initial, ctx);
        J history;
        auto poses = joint(d, initial, ctx, history);
        auto after = quality(d, poses, ctx);
        save_json(ctx.staging / "alignment_quality.json",
                  {{"before", before},
                   {"after", after},
                   {"history", history},
                   {"metric", "point-to-local-plane on accepted matches; not physical accuracy"}});
        timings["joint_and_quality"] =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - mark).count();
        mark = std::chrono::steady_clock::now();
        export_result(d, poses, output, o.ply, ctx);
        timings["export_verify"] =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - mark).count();
        ctx.check();
        double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        save_json(ctx.staging / "report.json",
                  {{"version", "1.0.0"},
                   {"status", "completed_requires_visual_review"},
                   {"frames", d.frames.size()},
                   {"groups", d.groups.size()},
                   {"points", d.points},
                   {"output_points", d.points},
                   {"source_files_unchanged", true},
                   {"rge_roundtrip_verified", true},
                   {"output_filtering", false},
                   {"output_downsampling", false},
                   {"threads", ctx.threads},
                   {"timings", timings},
                   {"seconds", seconds},
                   {"native_library_sha256",
                    "a773ce7b6b9f271aee2c5eb16a492903749b2f0bbb40ecb81408de9b1a279d07"}});
        fs::remove_all(ctx.staging / "cache");
        ctx.emit("publish", "All observations exported and verified; publishing result", d.frames.size(),
                 d.frames.size());
        ctx.log.close();
        if (fs::exists(output)) {
            if (!fs::is_directory(output) || !fs::is_empty(output))
                throw std::runtime_error("Output changed during run; refusing overwrite");
            fs::remove(output);
        }
        fs::rename(ctx.staging, output);
        return {output / "EXScanS/merged.sln_fix", output / "report.json", d.frames.size(),
                d.points, seconds};
    } catch (...) {
        ctx.log.close();
        std::error_code ec;
        fs::remove_all(ctx.staging, ec);
        throw;
    }
}
} // namespace scanmerge
