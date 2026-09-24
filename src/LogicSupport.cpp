// Shared geometry and bounded binary file I/O; no GUI or simulator dependencies.
#include "LogicSupport.h"
#include <chrono>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace eda {
double distance(Point a, Point b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
double segmentDistance(Point p, Point a, Point b) {
    auto d = b - a;
    double l = d.x * d.x + d.y * d.y;
    if (l < 1e-12)
        return distance(p, a);
    double t = std::clamp(((p.x - a.x) * d.x + (p.y - a.y) * d.y) / l, 0.0, 1.0);
    return distance(p, a + d * t);
}
Point snap(Point p, double grid) {
    return {std::round(p.x / grid) * grid, std::round(p.y / grid) * grid};
}
void View::zoomAt(Point p, double z) {
    // Keep the world point under the cursor fixed while changing scale.
    auto w = world(p);
    zoom = std::clamp(z, 0.1, 80.0);
    offset = p - w * zoom;
}
std::vector<Point> orthogonal(const std::vector<Point> &p) {
    // Route corners are visual only; electrical joins always use explicit Endpoint objects.
    std::vector<Point> out;
    for (auto q : p) {
        if (!out.empty()) {
            auto a = out.back();
            if (distance(a, q) < 1e-8)
                continue;
            if (std::abs(a.x - q.x) > 1e-8 && std::abs(a.y - q.y) > 1e-8)
                out.push_back({q.x, a.y});
        }
        out.push_back(q);
    }
    return out;
}
std::string readFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("无法打开文件");
    in.seekg(0, std::ios::end);
    auto size = in.tellg();
    if (size < 0 || size > 32 * 1024 * 1024)
        throw std::runtime_error("文件大小无效或超过 32 MB");
    in.seekg(0);
    std::string text(size_t(size), '\0');
    in.read(text.data(), size);
    if (!in && size > 0)
        throw std::runtime_error("读取文件失败");
    return text;
}
void writeAtomic(const std::filesystem::path &path, const std::string &bytes) {
    auto temp = path;
    temp += L".tmp-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out)
                throw std::runtime_error("无法创建临时文件，请检查目录和权限");
            out.write(bytes.data(), std::streamsize(bytes.size()));
            out.flush();
            if (!out)
                throw std::runtime_error("写入失败，原文件保持不变");
            out.close();
            if (out.fail())
                throw std::runtime_error("关闭文件时发生写入错误");
        }
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("替换目标文件失败（可能被占用）");
#else
        std::filesystem::rename(temp, path);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw;
    }
}
} // namespace eda
