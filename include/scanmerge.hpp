// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
namespace scanmerge {
struct Options {
    std::filesystem::path input, output;
    bool ply = false;
};
struct Progress {
    std::string stage, message;
    size_t completed = 0, total = 0;
};
using ProgressCallback = std::function<void(const Progress &)>;
struct Result {
    std::filesystem::path project, report;
    size_t frames = 0, points = 0;
    double seconds = 0;
};
// Blocking engine entry point. A GUI should call on its own worker thread.
// Callback runs on that worker; dispatch to main thread before updating widgets.
// Cancellation is cooperative. Failure/cancellation never publishes a partial project.
Result run(const Options &, ProgressCallback = {}, const std::atomic_bool *cancel = nullptr);
} // namespace scanmerge
