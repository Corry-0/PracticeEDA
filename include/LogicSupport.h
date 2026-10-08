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
// 模型坐标独立于当前视口与显示 DPI。
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
    // pin 为空表示“节点”；非空表示元件上稳定的端口 ID。
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
// 先写同目录的临时文件，flush/close 成功后再替换目标文件。
void writeAtomic(const std::filesystem::path &path, const std::string &bytes);
} // namespace eda
