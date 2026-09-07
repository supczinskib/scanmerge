// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "geometry.hpp"
#include "scanmerge.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
namespace sm {
namespace fs = std::filesystem;
using J = nlohmann::json;
inline J json_pose(Pose p) {
    return {{p.r.c[0].x, p.r.c[1].x, p.r.c[2].x, p.t.x},
            {p.r.c[0].y, p.r.c[1].y, p.r.c[2].y, p.t.y},
            {p.r.c[0].z, p.r.c[1].z, p.r.c[2].z, p.t.z},
            {0, 0, 0, 1}};
}
inline Pose read_pose(const J &j) {
    Pose p;
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++)
            p.r.c[k][i] = j.at(i).at(k).get<double>();
        p.t[i] = j.at(i).at(3).get<double>();
    }
    if (!rigid(p))
        throw std::runtime_error("Non-rigid pose");
    return p;
}
inline void save_json(const fs::path &p, const J &j) {
    std::ofstream f(p);
    f << j.dump(2) << '\n';
    if (!f)
        throw std::runtime_error("Cannot write " + p.string());
}
struct Context {
    scanmerge::ProgressCallback callback;
    const std::atomic_bool *cancel = nullptr;
    unsigned threads = 1;
    fs::path staging, executable;
    std::ofstream log;
    void check() const {
        if (cancel && cancel->load())
            throw std::runtime_error("Cancelled");
    }
    void emit(std::string stage, std::string message, size_t done = 0, size_t total = 0) {
        check();
        J j = {{"stage", stage}, {"message", message}, {"completed", done}, {"total", total}};
        if (log) {
            log << j.dump() << '\n';
            log.flush();
        }
        if (callback)
            callback({stage, message, done, total});
    }
    template <class F> void parallel(size_t count, F fn) const {
        std::atomic_size_t next{0};
        std::atomic_bool stop{false};
        std::exception_ptr err;
        std::mutex mx;
        std::vector<std::thread> pool;
        auto work = [&] {
            try {
                while (!stop) {
                    size_t i = next.fetch_add(1);
                    if (i >= count)
                        break;
                    check();
                    fn(i);
                }
            } catch (...) {
                std::lock_guard<std::mutex> l(mx);
                if (!err)
                    err = std::current_exception();
                stop = true;
            }
        };
        for (unsigned i = 0; i < std::min<size_t>(threads, count); i++)
            pool.emplace_back(work);
        for (auto &t : pool)
            t.join();
        if (err)
            std::rethrow_exception(err);
    }
};
struct Frame {
    fs::path path, cache;
    std::string sha, name;
    size_t group = 0, count = 0;
    Pose camera;
    Cloud work;
};
struct Group {
    fs::path project;
    std::string name;
    std::vector<size_t> frames;
    Cloud cloud;
    M basis;
    V center;
};
struct Dataset {
    fs::path root, solution;
    std::vector<Frame> frames;
    std::vector<Group> groups;
    size_t points = 0;
};
std::string sha256(const fs::path &);
Dataset discover(const fs::path &);
Cloud read_cache(const fs::path &, Pose &);
void load_prepare(Dataset &, Context &);
void export_result(const Dataset &, const std::vector<Pose> &, const fs::path &finalOut, bool ply,
                   Context &);
int native_helper(int argc, char **argv);
M axes(const Cloud &);
std::vector<Pose> initial_alignment(const Dataset &, Context &, J &);
std::vector<Pose> joint(const Dataset &, std::vector<Pose>, Context &, J &);
J quality(const Dataset &, const std::vector<Pose> &, Context &);
} // namespace sm
