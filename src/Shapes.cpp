#include "Digital.h"

namespace eda::logic {
bool shapeKind(const std::string &k) {
    return k == "Line" || k == "Curve" || k == "Rectangle" || k == "Ellipse" || k == "Circle";
}
std::pair<Point, Point> shapeBounds(const Shape &s) {
    Point lo{std::min(s.a.x, s.b.x), std::min(s.a.y, s.b.y)};
    Point hi{std::max(s.a.x, s.b.x), std::max(s.a.y, s.b.y)};
    if (s.kind == "Curve") {
        // The control hull conservatively bounds the quadratic curve.
        lo = {std::min(lo.x, s.control.x), std::min(lo.y, s.control.y)};
        hi = {std::max(hi.x, s.control.x), std::max(hi.y, s.control.y)};
    }
    return {lo, hi};
}
bool shapeHit(const Shape &s, Point p, double tolerance) {
    tolerance += s.stroke / 2;
    if (s.kind == "Line")
        return segmentDistance(p, s.a, s.b) <= tolerance;
    if (s.kind == "Curve") {
        // Adaptive subdivision keeps hit testing accurate at every supported zoom level.
        std::function<bool(Point, Point, Point, int)> hit = [&](Point a, Point c, Point b, int depth) {
            if (depth == 16 || segmentDistance(c, a, b) < tolerance / 4)
                return segmentDistance(p, a, b) <= tolerance;
            Point ac = (a + c) * .5, cb = (c + b) * .5, mid = (ac + cb) * .5;
            return hit(a, ac, mid, depth + 1) || hit(mid, cb, b, depth + 1);
        };
        return hit(s.a, s.control, s.b, 0);
    }
    auto bounds = shapeBounds(s);
    auto lo = bounds.first, hi = bounds.second;
    if (p.x < lo.x - tolerance || p.x > hi.x + tolerance || p.y < lo.y - tolerance || p.y > hi.y + tolerance)
        return false;
    if (s.kind == "Rectangle")
        return s.filled || std::min({std::abs(p.x - lo.x), std::abs(p.x - hi.x), std::abs(p.y - lo.y),
                                     std::abs(p.y - hi.y)}) <= tolerance;
    auto center = (lo + hi) * .5;
    double rx = (hi.x - lo.x) / 2, ry = (hi.y - lo.y) / 2;
    if (rx == 0 || ry == 0)
        return segmentDistance(p, lo, hi) <= tolerance;
    if (s.filled && std::pow((p.x - center.x) / rx, 2) + std::pow((p.y - center.y) / ry, 2) <= 1)
        return true;
    int steps = std::clamp(
        int(std::ceil(6.283185307179586 * std::sqrt(std::max(rx, ry) / std::max(.001, tolerance)))), 32,
        32768);
    Point prev{center.x + rx, center.y};
    for (int i = 1; i <= steps; ++i) {
        double t = i * 6.283185307179586 / steps;
        Point next{center.x + rx * std::cos(t), center.y + ry * std::sin(t)};
        if (segmentDistance(p, prev, next) <= tolerance)
            return true;
        prev = next;
    }
    return false;
}
} // namespace eda::logic
