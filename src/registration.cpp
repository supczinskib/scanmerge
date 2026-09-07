// SPDX-License-Identifier: GPL-3.0-only
#include "internal.hpp"
#include <memory>
#include <set>
namespace sm {
M axes(const Cloud &c) {
    if (c.size() < 12)
        throw std::runtime_error("Too few group samples");
    std::mt19937 rng(52);
    std::uniform_int_distribution<size_t> pick(0, c.size() - 1);
    std::vector<V> best;
    double besterr = 1e100;
    for (int iteration = 0; iteration < 1500; iteration++) {
        size_t i = pick(rng), j = pick(rng), k = pick(rng);
        if (i == j || i == k || j == k)
            continue;
        V n = cross(c.x[j] - c.x[i], c.x[k] - c.x[i]);
        if (norm(n) < 1e-12)
            continue;
        n = unit(n);
        double d = dot(n, c.x[i]), err = 0;
        std::vector<V> in;
        for (V p : c.x) {
            double r = std::abs(dot(p, n) - d);
            if (r < .35) {
                in.push_back(p);
                err += r * r;
            }
        }
        if (in.size() > best.size() || (in.size() == best.size() && err < besterr)) {
            best = std::move(in);
            besterr = err;
        }
    }
    if (best.size() < .15 * c.size()) {
        auto B = eigen(c.x).second;
        std::swap(B.c[0], B.c[2]);
        if (det(B) < 0)
            B.c[2] = -B.c[2];
        return B;
    }
    V z = eigen(best).second.c[0];
    int axis = 0;
    for (int i = 1; i < 3; i++)
        if (std::abs(z[i]) < std::abs(z[axis]))
            axis = i;
    V a{};
    a[axis] = 1;
    a = unit(a - z * dot(a, z));
    V b = cross(z, a);
    std::vector<V> uv;
    uv.reserve(c.size());
    for (V p : c.x)
        uv.push_back({dot(p, a), dot(p, b), 0});
    auto sorted = uv;
    std::sort(sorted.begin(), sorted.end(),
              [](V p, V q) { return p.x == q.x ? p.y < q.y : p.x < q.x; });
    std::vector<V> h;
    auto turn = [](V a, V b, V c) { return cross(b - a, c - a).z; };
    for (V p : sorted) {
        while (h.size() >= 2 && turn(h[h.size() - 2], h.back(), p) <= 0)
            h.pop_back();
        h.push_back(p);
    }
    size_t lower = h.size();
    for (auto it = sorted.rbegin() + 1; it != sorted.rend(); ++it) {
        while (h.size() > lower && turn(h[h.size() - 2], h.back(), *it) <= 0)
            h.pop_back();
        h.push_back(*it);
    }
    if (h.size() > 1)
        h.pop_back();
    double bestarea = 1e100, angle = 0;
    std::vector<double> xx(uv.size()), yy(uv.size());
    for (size_t i = 0; i < h.size(); i++) {
        V e = h[(i + 1) % h.size()] - h[i];
        double t = std::fmod(std::atan2(e.y, e.x) + 2 * M_PI, M_PI / 2), co = std::cos(t),
               si = std::sin(t);
        for (size_t j = 0; j < uv.size(); j++) {
            xx[j] = uv[j].x * co + uv[j].y * si;
            yy[j] = -uv[j].x * si + uv[j].y * co;
        }
        double area =
            (quantile(xx, .997) - quantile(xx, .003)) * (quantile(yy, .997) - quantile(yy, .003));
        if (area < bestarea) {
            bestarea = area;
            angle = t;
        }
    }
    return {{a * std::cos(angle) + b * std::sin(angle),
             a * (-std::sin(angle)) + b * std::cos(angle), z}};
}
static int match(const Cloud &y, const KD &tree, V x, V n, double limit, double cosLimit,
                 double &dist) {
    int ix[4];
    double dd[4];
    // Neighbors outside the acceptance radius cannot contribute. Bounding
    // the query preserves the four-neighbor rule and avoids distant searches.
    int count = tree.query(x, 4, ix, dd, limit);
    for (int i = 0; i < count; i++)
        if (dot(n, y.n[ix[i]]) >= cosLimit) {
            dist = std::sqrt(dd[i]);
            if (dist < limit)
                return ix[i];
            return -1;
        }
    return -1;
}
static Pose fit(const Cloud &x, const Cloud &y, const KD &tree, Pose T, double limit,
                int iterations, const Context &ctx) {
    std::vector<double> A, b;
    A.reserve(x.size() * 6);
    b.reserve(x.size());
    for (int iteration = 0; iteration < iterations; iteration++) {
        ctx.check();
        A.clear();
        b.clear();
        for (size_t i = 0; i < x.size(); i++) {
            V a = T.point(x.x[i]), n = T.normal(x.n[i]);
            double d;
            int j = match(y, tree, a, n, limit, .75, d);
            if (j < 0)
                continue;
            V v = y.n[j];
            double r = dot(a - y.x[j], v),
                   w = std::sqrt(std::min(1., .25 / std::max(std::abs(r), 1e-12)));
            V rot = cross(a, v);
            for (int k = 0; k < 3; k++)
                A.push_back(rot[k] * w);
            for (int k = 0; k < 3; k++)
                A.push_back(v[k] * w);
            b.push_back(-r * w);
        }
        size_t rows = b.size();
        if (rows < std::min<size_t>(100, std::max<size_t>(12, x.size() / 10)))
            break;
        // Rank-revealing least squares; avoid squaring the condition number.
        std::vector<double> col(rows * 6);
        for (size_t i = 0; i < rows; i++)
            for (int k = 0; k < 6; k++)
                col[k * rows + i] = A[i * 6 + k];
        __CLPK_integer m = rows, n = 6, nrhs = 1, lda = rows, ldb = std::max<size_t>(rows, 6),
                       rank = 0, info = 0, nwork = -1;
        double rcond = std::numeric_limits<double>::epsilon() * std::max<size_t>(rows, 6), sing[6],
               query;
        b.resize(ldb);
        dgelss_(&m, &n, &nrhs, col.data(), &lda, b.data(), &ldb, sing, &rcond, &rank, &query,
                &nwork, &info);
        nwork = std::max(64, int(query));
        std::vector<double> work(nwork);
        dgelss_(&m, &n, &nrhs, col.data(), &lda, b.data(), &ldb, sing, &rcond, &rank, work.data(),
                &nwork, &info);
        if (info)
            throw std::runtime_error("Rigid fit solver failed");
        V rv, mv;
        for (int k = 0; k < 3; k++) {
            rv[k] = std::clamp(b[k], -.035, .035);
            mv[k] = std::clamp(b[k + 3], -2., 2.);
        }
        T = Pose{rotation(rv), mv} * T;
        if (std::hypot(norm(rv), norm(mv)) < 1e-5)
            break;
    }
    return T;
}
static std::pair<double, double> score(const Cloud &x, const Cloud &y, const KD &tree, Pose T) {
    double total = 0;
    size_t in = 0;
    for (size_t i = 0; i < x.size(); i++) {
        double d;
        int j = match(y, tree, T.point(x.x[i]), T.normal(x.n[i]), 2., .75, d);
        total += j < 0 ? 2 : d;
        in += j >= 0 && d < .8;
    }
    return {total / x.size(), double(in) / x.size()};
}
// FPFH-style 33-bin descriptors used only when principal-axis hypotheses fail.
// Three pair-angle histograms, then inverse-distance neighbor aggregation.
using Feature = std::array<double, 33>;
static std::vector<Feature> features(const Cloud &c) {
    KD tree(c.x);
    std::vector<Feature> sp(c.size()), fp(c.size());
    int ids[80];
    double ds[80];
    for (size_t i = 0; i < c.size(); i++) {
        int n = tree.query(c.x[i], 80, ids, ds, 4.);
        int used = 0;
        for (int j = 0; j < n; j++) {
            int k = ids[j];
            if (k == int(i) || ds[j] < 1e-20)
                continue;
            V delta = unit(c.x[k] - c.x[i]), u = c.n[i], other = c.n[k];
            double p = dot(u, delta), q = dot(other, delta);
            if (std::acos(std::clamp(std::abs(p), 0., 1.)) >
                std::acos(std::clamp(std::abs(q), 0., 1.))) {
                std::swap(u, other);
                delta = -delta;
                p = -q;
            }
            V v = unit(cross(delta, u)), w = cross(u, v);
            double vals[3] = {std::atan2(dot(w, other), dot(u, other)) / M_PI, dot(v, other), p};
            for (int t = 0; t < 3; t++) {
                int bin = std::clamp(int((vals[t] + 1) * 5.5), 0, 10);
                sp[i][t * 11 + bin] += 1;
            }
            used++;
        }
        if (used)
            for (auto &a : sp[i])
                a *= 100. / used;
    }
    for (size_t i = 0; i < c.size(); i++) {
        int n = tree.query(c.x[i], 80, ids, ds, 4.);
        Feature f{};
        for (int j = 0; j < n; j++) {
            if (ids[j] == int(i) || ds[j] < 1e-20)
                continue;
            for (int b = 0; b < 33; b++)
                f[b] += sp[ids[j]][b] / ds[j];
        }
        for (int part = 0; part < 3; part++) {
            double sum = 0;
            for (int b = part * 11; b < (part + 1) * 11; b++)
                sum += f[b];
            for (int b = part * 11; b < (part + 1) * 11; b++)
                fp[i][b] = sp[i][b] + (sum ? 100 * f[b] / sum : 0);
        }
    }
    return fp;
}
class FeatureTree {
    struct N {
        int id, l = -1, r = -1, axis;
    };
    const std::vector<Feature> &f;
    std::vector<N> nodes;
    std::vector<int> idx;
    int build(int b, int e, int depth) {
        if (b == e)
            return -1;
        int axis = depth % 33, m = (b + e) / 2;
        std::nth_element(idx.begin() + b, idx.begin() + m, idx.begin() + e,
                         [&](int i, int j) { return f[i][axis] < f[j][axis]; });
        int at = nodes.size();
        nodes.push_back({idx[m], -1, -1, axis});
        int l = build(b, m, depth + 1), r = build(m + 1, e, depth + 1);
        nodes[at].l = l;
        nodes[at].r = r;
        return at;
    }
    void search(int n, const Feature &q, int &best, double &distance) const {
        if (n < 0)
            return;
        auto a = nodes[n];
        double d = 0;
        for (int i = 0; i < 33; i++) {
            double v = q[i] - f[a.id][i];
            d += v * v;
        }
        if (d < distance) {
            distance = d;
            best = a.id;
        }
        double delta = q[a.axis] - f[a.id][a.axis];
        search(delta < 0 ? a.l : a.r, q, best, distance);
        if (delta * delta < distance)
            search(delta < 0 ? a.r : a.l, q, best, distance);
    }

  public:
    explicit FeatureTree(const std::vector<Feature> &f) : f(f) {
        idx.resize(f.size());
        std::iota(idx.begin(), idx.end(), 0);
        nodes.reserve(f.size());
        build(0, f.size(), 0);
    }
    int nearest(const Feature &q) const {
        int best = -1;
        double d = 1e100;
        search(0, q, best, d);
        return best;
    }
};
static Pose triangle_pose(V a, V b, V c, V p, V q, V r) {
    auto basis = [](V a, V b, V c) {
        V x = unit(b - a), z = unit(cross(x, c - a)), y = cross(z, x);
        return M{{x, y, z}};
    };
    M R = basis(p, q, r) * basis(a, b, c).transpose();
    return {R, p - R * a};
}
static Pose feature_fallback(const Cloud &x, const Cloud &y, const KD &tree, Context &ctx) {
    ctx.emit("alignment", "Trying feature-based alignment");
    auto fx = features(x), fy = features(y);
    FeatureTree tx(fx), ty(fy);
    std::vector<int> xy(x.size()), yx(y.size());
    ctx.parallel(x.size(), [&](size_t i) { xy[i] = ty.nearest(fx[i]); });
    ctx.parallel(y.size(), [&](size_t i) { yx[i] = tx.nearest(fy[i]); });
    std::vector<std::pair<int, int>> pairs;
    for (size_t i = 0; i < x.size(); i++)
        if (xy[i] >= 0 && yx[xy[i]] == int(i))
            pairs.emplace_back(i, xy[i]);
    if (pairs.size() < 3)
        throw std::runtime_error("Not enough mutually matching features");
    std::mt19937 rng(52);
    std::uniform_int_distribution<size_t> pick(0, pairs.size() - 1);
    Pose best;
    double bestCost = 1e100;
    for (int it = 0; it < 50000; it++) {
        if (it % 1000 == 0)
            ctx.check();
        auto a = pairs[pick(rng)], b = pairs[pick(rng)], c = pairs[pick(rng)];
        if (a.first == b.first || a.first == c.first || b.first == c.first)
            continue;
        V xs[3] = {x.x[a.first], x.x[b.first], x.x[c.first]},
          ys[3] = {y.x[a.second], y.x[b.second], y.x[c.second]};
        bool ok = true;
        for (int i = 0; i < 3; i++) {
            double da = norm(xs[i] - xs[(i + 1) % 3]), db = norm(ys[i] - ys[(i + 1) % 3]);
            if (std::min(da, db) < .9 * std::max(da, db) || da < 1e-9)
                ok = false;
        }
        if (!ok || norm(cross(xs[1] - xs[0], xs[2] - xs[0])) < 1e-8)
            continue;
        Pose T = triangle_pose(xs[0], xs[1], xs[2], ys[0], ys[1], ys[2]);
        if (!rigid(T))
            continue;
        double cost = 0;
        size_t count = 0;
        for (size_t i = 0; i < x.size(); i += std::max<size_t>(1, x.size() / 200)) {
            int ids[1];
            double d[1];
            tree.query(T.point(x.x[i]), 1, ids, d);
            cost += std::min(4., d[0]);
            count++;
        }
        cost /= count;
        if (cost < bestCost) {
            bestCost = cost;
            best = T;
        }
    }
    return best;
}
std::vector<Pose> initial_alignment(const Dataset &d, Context &ctx, J &report) {
    auto &g0 = d.groups[0];
    Pose root{g0.basis.transpose(), g0.basis.transpose() * (-g0.center)};
    Cloud y = transformed(g0.cloud, root);
    std::vector<Pose> groupPose(d.groups.size());
    groupPose[0] = root;
    std::vector<size_t> pending;
    for (size_t i = 1; i < d.groups.size(); i++)
        pending.push_back(i);
    std::vector<M> proper;
    std::array<int, 3> perm{0, 1, 2};
    do {
        for (int a : {-1, 1})
            for (int b : {-1, 1})
                for (int c : {-1, 1}) {
                    M m;
                    for (auto &v : m.c)
                        v = {};
                    int signs[3] = {a, b, c};
                    for (int j = 0; j < 3; j++)
                        m.c[j][perm[j]] = signs[j];
                    if (det(m) > .5)
                        proper.push_back(m);
                }
    } while (std::next_permutation(perm.begin(), perm.end()));
    report = J::array();
    while (!pending.empty()) {
        KD tree(y.x);
        std::vector<size_t> accepted, remaining;
        for (auto i : pending) {
            ctx.check();
            const auto &g = d.groups[i];
            struct Candidate {
                Pose T;
                double cost, support;
            };
            std::vector<Candidate> candidates(proper.size());
            ctx.parallel(proper.size(), [&](size_t k) {
                Pose T{proper[k] * g.basis.transpose(), {}};
                T.t = T.r * (-g.center);
                T = fit(g.cloud, y, tree, T, 4., 15, ctx);
                T = fit(g.cloud, y, tree, T, 1.5, 20, ctx);
                auto s = score(g.cloud, y, tree, T);
                candidates[k] = {T, s.first, s.second};
            });
            auto best = *std::min_element(candidates.begin(), candidates.end(),
                                          [](auto &a, auto &b) { return a.cost < b.cost; });
            if (best.support < .30) {
                Pose T = feature_fallback(g.cloud, y, tree, ctx);
                T = fit(g.cloud, y, tree, T, 1.5, 30, ctx);
                auto s = score(g.cloud, y, tree, T);
                if (s.first < best.cost)
                    best = {T, s.first, s.second};
            }
            report.push_back({{"group", g.name},
                              {"support", best.support},
                              {"cost", best.cost},
                              {"pose", json_pose(best.T)}});
            if (best.support >= .20) {
                accepted.push_back(i);
                groupPose[i] = best.T;
                ctx.emit("alignment",
                         "Aligned " + g.name + ", overlap " +
                             std::to_string(int(best.support * 100)) + "%",
                         d.groups.size() - pending.size() + accepted.size(), d.groups.size());
            } else
                remaining.push_back(i);
        }
        if (remaining.empty())
            break;
        if (accepted.empty())
            throw std::runtime_error(
                "No reliable connection between scan groups; more common geometry required");
        for (auto i : accepted) {
            auto c = transformed(d.groups[i].cloud, groupPose[i]);
            y.x.insert(y.x.end(), c.x.begin(), c.x.end());
            y.n.insert(y.n.end(), c.n.begin(), c.n.end());
        }
        y = down(y, .8);
        pending = remaining;
    }
    std::vector<Pose> poses;
    for (auto &f : d.frames)
        poses.push_back(groupPose[f.group]);
    return poses;
}
struct Row {
    size_t i, j;
    std::array<double, 6> a, b;
    double rhs;
};
static double length(const std::vector<double> &x) {
    double s = 0;
    for (double v : x)
        s += v * v;
    return std::sqrt(s);
}
static std::vector<double> solve(const std::vector<Row> &rows, size_t N, Context &ctx) {
    size_t cols = N * 6, m = rows.size() + cols;
    std::vector<double> u(m), v(cols), w(cols), x(cols), av(m), atu(cols), anchor(cols, .15);
    std::fill(anchor.begin(), anchor.begin() + 6, 100.);
    auto multiply = [&](const std::vector<double> &z, std::vector<double> &o) {
        for (size_t k = 0; k < rows.size(); k++) {
            const auto &r = rows[k];
            double a = 0;
            for (int j = 0; j < 6; j++)
                a += r.a[j] * z[r.i * 6 + j] + r.b[j] * z[r.j * 6 + j];
            o[k] = a;
        }
        for (size_t j = 0; j < cols; j++)
            o[rows.size() + j] = anchor[j] * z[j];
    };
    auto transpose = [&](const std::vector<double> &z, std::vector<double> &o) {
        std::fill(o.begin(), o.end(), 0.);
        for (size_t k = 0; k < rows.size(); k++) {
            const auto &r = rows[k];
            double a = z[k];
            for (int j = 0; j < 6; j++) {
                o[r.i * 6 + j] += r.a[j] * a;
                o[r.j * 6 + j] += r.b[j] * a;
            }
        }
        for (size_t j = 0; j < cols; j++)
            o[j] += anchor[j] * z[rows.size() + j];
    };
    for (size_t k = 0; k < rows.size(); k++)
        u[k] = rows[k].rhs;
    double beta = length(u), bnorm = beta;
    if (beta == 0)
        return x;
    for (auto &a : u)
        a /= beta;
    transpose(u, v);
    double alpha = length(v);
    if (alpha == 0)
        return x;
    for (auto &a : v)
        a /= alpha;
    w = v;
    double rhobar = alpha, phibar = beta, anorm = 0;
    for (int it = 0; it < 180; it++) {
        ctx.check();
        multiply(v, av);
        for (size_t k = 0; k < m; k++)
            u[k] = av[k] - alpha * u[k];
        beta = length(u);
        if (beta > 0)
            for (auto &a : u)
                a /= beta;
        anorm = std::hypot(anorm, std::hypot(alpha, beta));
        transpose(u, atu);
        for (size_t k = 0; k < cols; k++)
            v[k] = atu[k] - beta * v[k];
        alpha = length(v);
        if (alpha > 0)
            for (auto &a : v)
                a /= alpha;
        double rho = std::hypot(rhobar, beta);
        if (rho == 0)
            break;
        double c = rhobar / rho, s = beta / rho, theta = s * alpha;
        rhobar = -c * alpha;
        double phi = c * phibar;
        phibar = s * phibar;
        for (size_t k = 0; k < cols; k++) {
            x[k] += phi / rho * w[k];
            w[k] = v[k] - theta / rho * w[k];
        }
        double rnorm = std::abs(phibar), arnorm = alpha * std::abs(s * phi);
        if (rnorm <= 1e-6 * (bnorm + anorm * length(x)) || arnorm <= 1e-6 * anorm * rnorm)
            break;
    }
    return x;
}
std::vector<Pose> joint(const Dataset &d, std::vector<Pose> poses, Context &ctx, J &history) {
    size_t N = d.frames.size();
    history = J::array();
    if (N == 1)
        return poses;
    std::vector<std::vector<size_t>> sub(N);
    std::mt19937 rng(129);
    for (size_t i = 0; i < N; i++) {
        auto &v = sub[i];
        v.resize(d.frames[i].work.size());
        std::iota(v.begin(), v.end(), 0);
        std::shuffle(v.begin(), v.end(), rng);
        v.resize(std::min<size_t>(1800, v.size()));
    }
    auto world = [&] {
        std::vector<Cloud> w(N);
        ctx.parallel(N, [&](size_t i) { w[i] = transformed(d.frames[i].work, poses[i]); });
        return w;
    };
    auto w = world();
    std::vector<std::unique_ptr<KD>> trees(N);
    ctx.parallel(N, [&](size_t i) { trees[i] = std::make_unique<KD>(w[i].x); });
    std::vector<V> lo(N), hi(N);
    for (size_t i = 0; i < N; i++) {
        lo[i] = hi[i] = w[i].x[0];
        for (auto p : w[i].x)
            for (int k = 0; k < 3; k++) {
                lo[i][k] = std::min(lo[i][k], p[k]);
                hi[i][k] = std::max(hi[i][k], p[k]);
            }
    } // Sparse scores avoid an N*N allocation for long captures.
    std::vector<std::vector<std::pair<size_t, double>>> neighbors(N);
    std::mutex mx;
    ctx.parallel(N, [&](size_t i) {
        for (size_t j = i + 1; j < N; j++) {
            bool overlap = true;
            for (int k = 0; k < 3; k++)
                if (std::min(hi[i][k], hi[j][k]) - std::max(lo[i][k], lo[j][k]) < -.5)
                    overlap = false;
            if (!overlap)
                continue;
            size_t count = 0, total = 0;
            for (size_t k = 0; k < sub[i].size(); k += 9) {
                size_t p = sub[i][k];
                double dist;
                count += match(w[j], *trees[j], w[i].x[p], w[i].n[p], 1.8, .9, dist) >= 0;
                total++;
            }
            double s = double(count) / total;
            if (s > .1) {
                std::lock_guard<std::mutex> l(mx);
                neighbors[i].push_back({j, s});
                neighbors[j].push_back({i, s});
            }
        }
    });
    std::set<std::pair<size_t, size_t>> edges;
    for (size_t i = 0; i < N; i++)
        for (size_t g = 0; g < d.groups.size(); g++) {
            std::vector<std::pair<size_t, double>> v;
            for (auto p : neighbors[i])
                if (d.frames[p.first].group == g)
                    v.push_back(p);
            std::sort(v.begin(), v.end(), [](auto a, auto b) {
                return a.second == b.second ? a.first < b.first : a.second > b.second;
            });
            for (size_t k = 0; k < std::min<size_t>(3, v.size()); k++)
                edges.insert(std::minmax(i, v[k].first));
        }
    std::set<size_t> connected{0};
    for (;;) {
        size_t prev = connected.size();
        for (auto [i, j] : edges) {
            if (connected.count(i))
                connected.insert(j);
            if (connected.count(j))
                connected.insert(i);
        }
        if (connected.size() == prev)
            break;
    }
    if (connected.size() != N)
        throw std::runtime_error("Disconnected frame graph: " + std::to_string(connected.size()) +
                                 "/" + std::to_string(N));
    ctx.emit("refinement", "Joint alignment: " + std::to_string(edges.size()) + " links");
    std::vector<std::pair<size_t, size_t>> links(edges.begin(), edges.end());
    double schedule[12] = {2, 1.8, 1.6, 1.4, 1.2, 1.1, 1, .9, .85, .8, .75, .7};
    std::vector<std::vector<Row>> blocks(links.size());
    for (int iteration = 0; iteration < 12; iteration++) {
        auto start = std::chrono::steady_clock::now();
        ctx.check();
        trees.clear();
        w = world();
        trees.resize(N);
        std::vector<V> centers(N);
        ctx.parallel(N, [&](size_t i) {
            trees[i] = std::make_unique<KD>(w[i].x);
            centers[i] = mean(w[i].x);
        });
        ctx.parallel(links.size(), [&](size_t e) {
            auto [i, j] = links[e];
            if ((iteration + i + j) % 2)
                std::swap(i, j);
            auto &block = blocks[e];
            block.clear();
            block.reserve(1800);
            size_t accepted = 0;
            for (auto p : sub[i]) {
                double dist;
                int k = match(w[j], *trees[j], w[i].x[p], w[i].n[p], schedule[iteration], .9, dist);
                if (k < 0)
                    continue;
                accepted++;
                V a = w[i].x[p], b = w[j].x[k], v = unit(w[i].n[p] + w[j].n[k]);
                double r = dot(a - b, v);
                if (std::sqrt(std::max(0., dist * dist - r * r)) >= .8)
                    continue;
                double robust = std::max(.09, .3 * (1 - iteration / 16.)),
                       weight = std::sqrt(1 / (1 + (r / robust) * (r / robust)));
                V ai = cross(a - centers[i], v) / 40., aj = cross(b - centers[j], v) / 40.;
                Row row;
                row.i = i;
                row.j = j;
                row.rhs = -r * weight;
                for (int t = 0; t < 3; t++) {
                    row.a[t] = ai[t] * weight;
                    row.a[t + 3] = v[t] * weight;
                    row.b[t] = -aj[t] * weight;
                    row.b[t + 3] = -v[t] * weight;
                }
                block.push_back(row);
            }
            if (accepted < 40 || block.size() < 30) {
                block.clear();
                return;
            }
            double scale = std::sqrt(700. / std::max<size_t>(200, block.size()));
            for (auto &r : block) {
                r.rhs *= scale;
                for (int t = 0; t < 6; t++) {
                    r.a[t] *= scale;
                    r.b[t] *= scale;
                }
            }
        });
        size_t amount = 0;
        for (auto &b : blocks)
            amount += b.size();
        if (!amount)
            throw std::runtime_error("No usable correspondences in joint alignment");
        std::vector<Row> rows;
        rows.reserve(amount);
        for (auto &b : blocks)
            rows.insert(rows.end(), b.begin(), b.end());
        auto delta = solve(rows, N, ctx);
        double maxMove = 0;
        for (size_t i = 0; i < N; i++) {
            V rot{delta[i * 6] / 40., delta[i * 6 + 1] / 40., delta[i * 6 + 2] / 40.},
                move{delta[i * 6 + 3], delta[i * 6 + 4], delta[i * 6 + 5]};
            double factor =
                std::min({1., .012 / std::max(norm(rot), 1e-9), .55 / std::max(norm(move), 1e-9)});
            rot = rot * factor;
            move = move * factor;
            M R = rotation(rot);
            poses[i] = Pose{R, centers[i] - R * centers[i] + move} * poses[i];
            maxMove = std::max(maxMove, norm(move));
        }
        double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        history.push_back({{"iteration", iteration + 1},
                           {"correspondences", amount},
                           {"seconds", seconds},
                           {"max_move", maxMove}});
        ctx.emit("refinement",
                 "Iteration " + std::to_string(iteration + 1) + "/12, " + std::to_string(seconds) +
                     " s",
                 iteration + 1, 12);
    }
    return poses;
}
J quality(const Dataset &d, const std::vector<Pose> &poses, Context &ctx) {
    std::vector<Cloud> groups(d.groups.size());
    ctx.parallel(groups.size(), [&](size_t g) {
        Cloud c;
        for (auto i : d.groups[g].frames) {
            auto w = transformed(d.frames[i].work, poses[i]);
            c.x.insert(c.x.end(), w.x.begin(), w.x.end());
            c.n.insert(c.n.end(), w.n.begin(), w.n.end());
        }
        groups[g] = down(c, .4);
    });
    std::vector<std::unique_ptr<KD>> trees(groups.size());
    for (size_t i = 0; i < groups.size(); i++)
        trees[i] = std::make_unique<KD>(groups[i].x);
    J report = J::array();
    for (size_t i = 0; i < groups.size(); i++)
        for (size_t j = i + 1; j < groups.size(); j++) {
            ctx.check();
            std::vector<double> res;
            size_t total = 0;
            for (auto [a, b] : {std::pair{i, j}, std::pair{j, i}}) {
                auto &x = groups[a];
                auto &y = groups[b];
                total += x.size();
                for (size_t k = 0; k < x.size(); k++) {
                    double dist;
                    int p = match(y, *trees[b], x.x[k], x.n[k], 2., .9, dist);
                    if (p >= 0)
                        res.push_back(std::abs(dot(x.x[k] - y.x[p], y.n[p])));
                }
            }
            report.push_back(
                {{"groups", {d.groups[i].name, d.groups[j].name}},
                 {"support", double(res.size()) / total},
                 {"q50_q90_q95", {quantile(res, .5), quantile(res, .9), quantile(res, .95)}}});
        }
    return report;
}
} // namespace sm
