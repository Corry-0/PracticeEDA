#pragma once
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace eda {
// Model-space coordinates are independent of the current viewport and display DPI.
struct Point {
    double x = 0, y = 0;
    Point operator+(Point b) const { return {x + b.x, y + b.y}; }
    Point operator-(Point b) const { return {x - b.x, y - b.y}; }
    Point operator*(double s) const { return {x * s, y * s}; }
};
double distance(Point a, Point b);
double segmentDistance(Point p, Point a, Point b);
Point snap(Point p, double grid);
struct View {
    double zoom = 1;
    Point offset{60, 60};
    Point world(Point screen) const { return (screen - offset) * (1 / zoom); }
    Point screen(Point world) const { return world * zoom + offset; }
    void zoomAt(Point screenPoint, double newZoom);
};
struct Endpoint {
    // An empty pin names a junction; a nonempty pin is the stable port ID on a part.
    std::string object, pin;
    std::string key() const { return object + ":" + pin; }
    bool operator==(const Endpoint &b) const { return object == b.object && pin == b.pin; }
};
struct Junction {
    std::string id, label;
    Point at;
};
struct Wire {
    std::string id;
    Endpoint a, b;
    std::vector<Point> bends;
};
struct Issue {
    std::string severity, object, message;
};
std::vector<Point> orthogonal(const std::vector<Point> &points);
std::string readFile(const std::filesystem::path &path);
// Write a sibling temporary file and replace only after a successful flush/close.
void writeAtomic(const std::filesystem::path &path, const std::string &bytes);
} // namespace eda
