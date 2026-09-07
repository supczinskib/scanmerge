// SPDX-License-Identifier: GPL-3.0-only
#include "../src/registration.cpp"
#include <iostream>
using namespace sm;
static void require(bool v, const char *m) {
    if (!v)
        throw std::runtime_error(m);
}
int main() {
    try {
        std::mt19937 rng(47);
        std::uniform_real_distribution<double> u(-10, 10);
        std::vector<V> points;
        for (int i = 0; i < 2000; i++)
            points.push_back({u(rng), u(rng), u(rng)});
        KD tree(points);
        for (int t = 0; t < 100; t++) {
            V q{u(rng), u(rng), u(rng)};
            int ids[4];
            double d[4];
            require(tree.query(q, 4, ids, d) == 4, "KD count");
            std::vector<double> brute;
            for (V p : points)
                brute.push_back(dot(p - q, p - q));
            std::sort(brute.begin(), brute.end());
            for (int k = 0; k < 4; k++)
                require(std::abs(d[k] - brute[k]) < 1e-10, "KD differs from brute force");
        }
        Pose p{rotation({.2, -.3, .1}), {12, -8, 9}};
        for (V x : points)
            require(norm(p.inverse().point(p.point(x)) - x) < 1e-12, "Rigid transform inverse");
        require(rigid(p), "Rotation must be rigid");
        Cloud c;
        for (int face = 0; face < 6; face++)
            for (int i = 0; i < 15; i++)
                for (int j = 0; j < 15; j++) {
                    V x, n;
                    int a = face / 2, b = (a + 1) % 3, k = (a + 2) % 3;
                    x[a] = (face % 2 ? 1 : -1) * (a + 1) * 8;
                    x[b] = (i - 7) * .8;
                    x[k] = (j - 7) * .8;
                    n[a] = face % 2 ? 1 : -1;
                    c.x.push_back(x);
                    c.n.push_back(n);
                }
        Context ctx;
        ctx.threads = 2;
        KD ct(c.x);
        Pose shifted{rotation({.006, -.004, .007}), {.15, -.12, .09}};
        auto fitted = fit(c, c, ct, shifted, 1.5, 30, ctx);
        require(norm(fitted.t) < 1e-5, "ICP translation correction");
        for (int k = 0; k < 3; k++)
            require(norm(fitted.r.c[k] - M{}.c[k]) < 1e-5, "ICP rotation correction");
        for (size_t frames : {1, 3, 7}) {
            Dataset data;
            Group g;
            g.name = "synthetic";
            for (size_t i = 0; i < frames; i++) {
                Frame f;
                f.work = c;
                data.frames.push_back(f);
                g.frames.push_back(i);
            }
            data.groups.push_back(g);
            std::vector<Pose> poses(frames);
            for (size_t i = 1; i < frames; i++)
                poses[i] = {rotation({.002 * double(i), -.001 * double(i), .001 * double(i)}),
                            {.05 * double(i), -.03 * double(i), .02 * double(i)}};
            J history;
            auto result = joint(data, poses, ctx, history);
            require(result.size() == frames, "Frame count retained");
            for (auto T : result)
                require(rigid(T), "Joint pose must be rigid");
            if (frames > 1) {
                double error = 0;
                Pose inv = result[0].inverse();
                for (size_t i = 1; i < frames; i++) {
                    Pose rel = inv * result[i];
                    error = std::max(error, norm(rel.t));
                }
                require(error < .003, "Joint alignment of identical surfaces");
            }
        }
        std::atomic_bool stop{true};
        ctx.cancel = &stop;
        bool caught = false;
        try {
            ctx.check();
        } catch (...) {
            caught = true;
        }
        require(caught, "Cancellation");
        std::cout << "PASS: KD vs brute force; rigid transforms; ICP; joint alignment 1/3/7 "
                     "frames; cancellation\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
