// SPDX-License-Identifier: GPL-3.0-only
#include "internal.hpp"
#include <csignal>
#include <iostream>
#include <pthread.h>
#include <unistd.h>
static std::atomic_bool cancelled{false};
static void *signals(void *) {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGINT);
    sigaddset(&s, SIGTERM);
    int sig;
    sigwait(&s, &sig);
    cancelled = true;
    return nullptr;
}
int main(int argc, char **argv) {
    if (argc >= 2 &&
        (std::string(argv[1]) == "--rge-read" || std::string(argv[1]) == "--rge-write"))
        return sm::native_helper(argc, argv);
    if (argc == 1 || std::string(argv[1]) == "--help") {
        std::cout << "ScanMerge 1.0.0 — EXScanS capture alignment for Apple Silicon\n\n"
                     "Usage: scanmerge IN OUT [--ply] [--json-progress]\n\n"
                     "Arguments:\n"
                     "  IN               Directory containing an EXScanS project and its captures\n"
                     "  OUT              New or empty output directory\n\n"
                     "Options:\n"
                     "  --ply            Export a merged PLY point cloud alongside the project\n"
                     "  --json-progress  Write progress as JSON Lines to standard output\n"
                     "  --help           Show this help\n"
                     "  --version        Show the version\n\n"
                     "Requires EXScanS 3.2.0.4 ARM64 in /Applications.\n"
                     "Licensed under GPL-3.0-only. See the bundled LICENSE file.\n";
        return 0;
    }
    if (std::string(argv[1]) == "--version") {
        std::cout << "1.0.0 arm64\n";
        return 0;
    }
    try {
        if (argc < 3)
            throw std::runtime_error("Expected IN OUT [--ply]");
        scanmerge::Options options;
        options.input = argv[1];
        options.output = argv[2];
        bool json = false;
        for (int i = 3; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--ply")
                options.ply = true;
            else if (a == "--json-progress")
                json = true;
            else
                throw std::runtime_error("Unknown option: " + a);
        }
        sigset_t s;
        sigemptyset(&s);
        sigaddset(&s, SIGINT);
        sigaddset(&s, SIGTERM);
        pthread_sigmask(SIG_BLOCK, &s, nullptr);
        // Shell background jobs can inherit SIG_IGN. sigwait needs an active
        // disposition as well as the blocked mask, otherwise Ctrl-C is lost.
        struct sigaction action {};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
        pthread_t thread;
        pthread_create(&thread, nullptr, signals, nullptr);
        pthread_detach(thread);
        auto r = scanmerge::run(
            options,
            [json](const scanmerge::Progress &p) {
                if (json)
                    std::cout << sm::J{{"event", "progress"},
                                       {"stage", p.stage},
                                       {"message", p.message},
                                       {"completed", p.completed},
                                       {"total", p.total}}
                                     .dump()
                              << std::endl;
                else
                    std::cout << p.stage << ": " << p.message << std::endl;
            },
            &cancelled);
        if (json)
            std::cout << sm::J{{"event", "result"},
                               {"project", r.project.string()},
                               {"report", r.report.string()},
                               {"points", r.points},
                               {"seconds", r.seconds}}
                             .dump()
                      << std::endl;
        else
            std::cout << "Project: " << r.project << "\nPoints: " << r.points
                      << "\nSeconds: " << r.seconds << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "ScanMerge: " << e.what() << '\n';
        return cancelled ? 130 : 1;
    }
}
