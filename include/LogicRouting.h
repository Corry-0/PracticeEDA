#pragma once
#include "Digital.h"

namespace eda::logic {
struct WireRoute {
    std::vector<Point> points;
    std::string failure;
    bool ok() const { return failure.empty() && points.size() >= 2; }
};
// 只计算几何。via 是本次布线明确点击的途经点，不改变任何电气端点。
WireRoute routeWire(const Project &, const Circuit &, const Wire &, const std::vector<Point> &via = {});
WireRoute moveWireSegment(const Project &, const Circuit &, const Wire &, const std::vector<Point> &original,
                          size_t segment, Point delta);
struct RerouteResult {
    size_t changed = 0;
    std::vector<std::string> failed;
};
RerouteResult rerouteWires(const Project &, Circuit &, const std::set<std::string> &ids,
                           bool preferClearCrossings = false);
// 仅处理几何改变对象的关联线，以及被新位置挡住的线；加载工程不调用。
RerouteResult rerouteChangedGeometry(Project &, const Project &before);
// 不同网络相交时，在竖线中留断口。同网及显式节点不留断口。
std::map<std::string, std::vector<Point>> wireCrossings(const Project &, const Circuit &);
struct WireLayoutQuality {
    size_t crowded = 0, crossings = 0, bends = 0;
    double length = 0;
    bool operator<(const WireLayoutQuality &) const;
};
WireLayoutQuality wireLayoutQuality(const Project &, const Circuit &);
// Shared geometry for paint and picking. Radius uses the same coordinates as the path.
using WireStroke = std::pair<Point,Point>;
std::vector<WireStroke> visibleWireStrokes(const std::vector<Point> &path,
                                          const std::vector<Point> &crossings, double radius);
} // namespace eda::logic
