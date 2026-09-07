// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <Accelerate/Accelerate.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>
namespace sm {
struct V {
    double x = 0, y = 0, z = 0;
    double &operator[](int i) {
        return (&x)[i];
    }
    double operator[](int i) const {
        return (&x)[i];
    }
};
inline V operator+(V a, V b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline V operator-(V a, V b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline V operator-(V a) {
    return {-a.x, -a.y, -a.z};
}
inline V operator*(V a, double s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline V operator*(double s, V a) {
    return a * s;
}
inline V operator/(V a, double s) {
    return a * (1 / s);
}
inline V &operator+=(V &a, V b) {
    a = a + b;
    return a;
}
inline double dot(V a, V b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline V cross(V a, V b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double norm(V a) {
    return std::sqrt(dot(a, a));
}
inline V unit(V a) {
    return a / std::max(norm(a), 1e-12);
}
inline bool finite(V a) {
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
struct M {
    std::array<V, 3> c{V{1, 0, 0}, V{0, 1, 0}, V{0, 0, 1}};
    V operator*(V v) const {
        return c[0] * v.x + c[1] * v.y + c[2] * v.z;
    }
    M transpose() const {
        return {{V{c[0].x, c[1].x, c[2].x}, V{c[0].y, c[1].y, c[2].y}, V{c[0].z, c[1].z, c[2].z}}};
    }
};
inline M operator*(M a, M b) {
    return {{a * b.c[0], a * b.c[1], a * b.c[2]}};
}
inline double det(M a) {
    return dot(a.c[0], cross(a.c[1], a.c[2]));
}
struct Pose {
    M r;
    V t;
    V point(V x) const {
        return r * x + t;
    }
    V normal(V n) const {
        return r * n;
    }
    Pose inverse() const {
        M q = r.transpose();
        return {q, q * (-t)};
    }
};
inline Pose operator*(Pose a, Pose b) {
    return {a.r * b.r, a.point(b.t)};
}
inline bool rigid(Pose p) {
    if (!finite(p.t) || std::abs(det(p.r) - 1) > 1e-5)
        return false;
    M a = p.r.transpose() * p.r;
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++)
            if (!std::isfinite(a.c[j][i]) || std::abs(a.c[j][i] - (i == j)) > 1e-5)
                return false;
    return true;
}
inline M rotation(V v) {
    double a = norm(v);
    if (a < 1e-15)
        return {};
    V k = v / a;
    double c = std::cos(a), s = std::sin(a);
    M R;
    for (int i = 0; i < 3; i++) {
        V e{};
        e[i] = 1;
        R.c[i] = e * c + cross(k, e) * s + k * (dot(k, e) * (1 - c));
    }
    return R;
}
inline V mean(const std::vector<V> &x) {
    V c;
    for (auto p : x)
        c += p;
    return c / double(x.size());
}
inline double quantile(std::vector<double> a, double q) {
    if (a.empty())
        return 0;
    std::sort(a.begin(), a.end());
    double f = q * (a.size() - 1);
    size_t i = size_t(f);
    return a[i] + (a[std::min(i + 1, a.size() - 1)] - a[i]) * (f - i);
}
inline std::pair<V, M> eigen(const std::vector<V> &x) {
    V c = mean(x);
    double a[9] = {};
    for (V p : x) {
        p = p - c;
        for (int j = 0; j < 3; j++)
            for (int i = 0; i < 3; i++)
                a[j * 3 + i] += p[i] * p[j];
    }
    double w[3], work[32];
    __CLPK_integer n = 3, l = 3, nw = 32, info = 0;
    char job = 'V', u = 'U';
    dsyev_(&job, &u, &n, a, &l, w, work, &nw, &info);
    if (info)
        throw std::runtime_error("Covariance eigensolver failed");
    return {{w[0], w[1], w[2]}, {{V{a[0], a[1], a[2]}, V{a[3], a[4], a[5]}, V{a[6], a[7], a[8]}}}};
}
struct Cloud {
    std::vector<V> x, n;
    size_t size() const {
        return x.size();
    }
};
inline Cloud transformed(const Cloud &c, Pose p) {
    Cloud r;
    r.x.resize(c.size());
    r.n.resize(c.size());
    for (size_t i = 0; i < c.size(); i++) {
        r.x[i] = p.point(c.x[i]);
        r.n[i] = p.normal(c.n[i]);
    }
    return r;
}
// Immutable balanced kd-tree. Queries do not allocate for the common k=4 case.
class KD {
    struct Node {
        int point, left = -1, right = -1;
        unsigned char axis;
    };
    const std::vector<V> *x_;
    std::vector<Node> nodes;
    std::vector<int> order;
    int build(int b, int e, int depth) {
        if (b >= e)
            return -1;
        int ax = depth % 3, m = (b + e) / 2;
        std::nth_element(order.begin() + b, order.begin() + m, order.begin() + e,
                         [&](int i, int j) {
                             double a = (*x_)[i][ax], v = (*x_)[j][ax];
                             return a == v ? i < j : a < v;
                         });
        int me = nodes.size();
        nodes.push_back({order[m], -1, -1, (unsigned char)ax});
        int l = build(b, m, depth + 1), r = build(m + 1, e, depth + 1);
        nodes[me].left = l;
        nodes[me].right = r;
        return me;
    }
    void search(int id, V q, int k, int *ix, double *ds, int &count, double radius2) const {
        if (id < 0)
            return;
        auto a = nodes[id];
        V p = (*x_)[a.point];
        double d = dot(q - p, q - p);
        if (d < radius2 && (count < k || d < ds[count - 1])) {
            int at = std::min(count, k - 1);
            while (at > 0 && ds[at - 1] > d) {
                if (at < k) {
                    ds[at] = ds[at - 1];
                    ix[at] = ix[at - 1];
                }
                --at;
            }
            ds[at] = d;
            ix[at] = a.point;
            if (count < k)
                count++;
        }
        double delta = q[a.axis] - p[a.axis];
        int first = delta < 0 ? a.left : a.right, second = delta < 0 ? a.right : a.left;
        search(first, q, k, ix, ds, count, radius2);
        double lim = count < k ? radius2 : std::min(radius2, ds[count - 1]);
        if (delta * delta <= lim)
            search(second, q, k, ix, ds, count, radius2);
    }

  public:
    explicit KD(const std::vector<V> &x) : x_(&x) {
        if (x.size() > size_t(INT_MAX))
            throw std::runtime_error("Registration cloud exceeds index capacity");
        order.resize(x.size());
        std::iota(order.begin(), order.end(), 0);
        nodes.reserve(x.size());
        build(0, x.size(), 0);
        order.clear();
        order.shrink_to_fit();
    }
    int query(V q, int k, int *ix, double *ds,
              double radius = std::numeric_limits<double>::infinity()) const {
        int n = 0;
        if (k > 0 && !nodes.empty())
            search(0, q, k, ix, ds, n, radius * radius);
        return n;
    }
};
struct Cell {
    long long a, b, c;
    size_t i;
    bool operator<(const Cell &o) const {
        if (a != o.a)
            return a < o.a;
        if (b != o.b)
            return b < o.b;
        if (c != o.c)
            return c < o.c;
        return i < o.i;
    }
    bool same(const Cell &o) const {
        return a == o.a && b == o.b && c == o.c;
    }
};
inline Cloud down(const Cloud &p, double v) {
    Cloud out;
    if (!p.size())
        return out;
    V lo = p.x[0];
    for (auto x : p.x)
        for (int i = 0; i < 3; i++)
            lo[i] = std::min(lo[i], x[i]);
    lo = lo - V{v / 2, v / 2, v / 2};
    std::vector<Cell> a;
    a.reserve(p.size());
    for (size_t i = 0; i < p.size(); i++) {
        V x = (p.x[i] - lo) / v;
        a.push_back({(long long)std::floor(x.x), (long long)std::floor(x.y),
                     (long long)std::floor(x.z), i});
    }
    std::sort(a.begin(), a.end());
    out.x.reserve(a.size() / 2);
    out.n.reserve(a.size() / 2);
    for (size_t i = 0; i < a.size();) {
        size_t j = i;
        V x, n;
        while (j < a.size() && a[i].same(a[j])) {
            x += p.x[a[j].i];
            n += p.n[a[j].i];
            j++;
        }
        out.x.push_back(x / double(j - i));
        out.n.push_back(unit(n));
        i = j;
    }
    return out;
}
inline void normals(Cloud &c, double radius, int k) {
    KD tree(c.x);
    std::vector<int> ix(k);
    std::vector<double> d(k);
    std::vector<V> ns(c.size()), near;
    near.reserve(k);
    for (size_t i = 0; i < c.size(); i++) {
        int count = tree.query(c.x[i], k, ix.data(), d.data(), radius);
        if (count < 3) {
            ns[i] = c.n[i];
            continue;
        }
        near.clear();
        for (int j = 0; j < count; j++)
            near.push_back(c.x[ix[j]]);
        V n = eigen(near).second.c[0];
        if (dot(n, c.n[i]) < 0)
            n = -n;
        ns[i] = n;
    }
    c.n.swap(ns);
}
inline Cloud registration_inliers(const Cloud &c) {
    if (c.size() < 21)
        return c;
    KD tree(c.x);
    std::vector<double> avg(c.size());
    int ix[20];
    double d[20];
    double s = 0, s2 = 0;
    for (size_t i = 0; i < c.size(); i++) {
        int n = tree.query(c.x[i], 20, ix, d);
        double a = 0;
        for (int j = 0; j < n; j++)
            a += std::sqrt(d[j]);
        avg[i] = a / n;
        s += avg[i];
        s2 += avg[i] * avg[i];
    }
    double mu = s / c.size(),
           sd = std::sqrt(std::max(0., (s2 - c.size() * mu * mu) / (c.size() - 1)));
    Cloud out;
    for (size_t i = 0; i < c.size(); i++)
        if (avg[i] < mu + 2.5 * sd) {
            out.x.push_back(c.x[i]);
            out.n.push_back(c.n[i]);
        }
    return out;
}
} // namespace sm
