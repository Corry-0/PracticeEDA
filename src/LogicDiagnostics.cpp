// ERC 的画布提示和按需问题窗口。此文件只呈现现有规则，不修改仿真或电气拓扑。
#include "DigitalEditor.h"
#include <wx/dcclient.h>
#include <wx/tooltip.h>

namespace eda::logic {
namespace {
wxColour markerColour(bool error) { return wxColour(error ? "#d53745" : "#bd790c"); }
}

std::string LogicEditor::diagnosticText(const Issue &issue) const {
    std::string rule = "连接诊断", advice = "检查标记处的端口、连线及元件参数。";
    if (issue.message.find("位宽") != std::string::npos) {
        rule = "总线位宽一致性";
        advice = "使连接两端位宽一致，或使用分线器、合线器及位扩展器。";
    } else if (issue.message.find("高阻") != std::string::npos) {
        rule = "输入驱动 / 高阻状态";
        advice = "检查输入是否接到有效输出，以及三态缓冲器是否使能。";
    } else if (issue.message.find("冲突") != std::string::npos) {
        rule = "驱动冲突 / 非法运算";
        advice = "检查同网多个输出的驱动值，并排查除零等非法运算。";
    } else if (issue.message.find("未知") != std::string::npos) {
        rule = "未知输入状态";
        advice = "沿输入方向检查未知信号来源、控制端和存储器初值。";
    } else if (issue.message.find("预算") != std::string::npos) {
        rule = "反馈稳定性 / 事件预算";
        advice = "检查组合反馈环路，复位后逐步传播，观察振荡来源。";
    } else if (issue.message.find("导入") != std::string::npos) {
        rule = "外部电路导入兼容性";
        advice = "核对导入说明中未支持的器件或属性，并替换为可支持的实现。";
    }
    const auto severity = issue.severity == "error" ? "错误" : issue.severity == "warning" ? "警告" : "提示";
    return "规则：" + rule + "\n严重级别：" + severity + "\n涉及对象：" + issue.object +
           "\n详情：" + issue.message + "\n修复建议：" + advice;
}

std::optional<std::pair<std::string, Point>> LogicEditor::visibleDiagnostic(const Issue &issue) const {
    auto id = issue.object;
    if (!scope.empty()) {
        const auto prefix = scope + "/";
        if (id.rfind(prefix, 0) != 0) return {};
        id = id.substr(prefix.size());
    }
    id = id.substr(0, id.find('/'));
    const auto &c = history.project.circuit(current);
    if (const auto *part = c.part(id)) {
        std::vector<Port> affected;
        for (const auto &pin : ports(history.project, *part)) {
            const auto signal = read({id, pin.id});
            const bool error = issue.message.find("位宽") != std::string::npos || issue.message.find("(E)") != std::string::npos;
            const bool highZ = issue.message.find("高阻") != std::string::npos;
            const bool unknown = issue.message.find("未知") != std::string::npos;
            if ((error && signal.error) || (highZ && !pin.output && signal.z) ||
                (unknown && !pin.output && !signal.defined())) affected.push_back(pin);
        }
        if (!affected.empty()) {
            size_t occurrence = 0;
            for (const auto &previous : issues) {
                if (&previous == &issue) break;
                if (previous.object == issue.object && previous.message == issue.message) ++occurrence;
            }
            const auto &pin = affected[occurrence % affected.size()];
            return std::make_pair(id, part->at + pin.at + Point{pin.at.x < 0 ? -6.0 : 6.0, -8});
        }
        const auto size = partSize(history.project, *part);
        Point corner{size.x / 2 + 6, -size.y / 2 - 6};
        for (int turn = 0; turn < part->rotation; ++turn) corner = {-corner.y, corner.x};
        return std::make_pair(id, part->at + corner);
    }
    for (const auto &node : c.nodes)
        if (node.id == id) return std::make_pair(id, node.at + Point{7, -7});
    for (const auto &wire : c.wires)
        if (wire.id == id) {
            const auto points = wirePoints(history.project, c, wire);
            size_t longest = 1;
            for (size_t i = 2; i < points.size(); ++i)
                if (distance(points[i], points[i - 1]) > distance(points[longest], points[longest - 1])) longest = i;
            if (points.size() > 1) return std::make_pair(id, (points[longest] + points[longest - 1]) * .5);
        }
    return {};
}

void LogicEditor::openDiagnostics(const std::vector<int> &focused) {
    diagnostics->Freeze();
    diagnostics->Clear();
    diagnosticOrder.clear();
    std::set<int> added;
    auto add = [&](int index) {
        if (index < 0 || size_t(index) >= issues.size() || !added.insert(index).second) return;
        const auto &issue = issues[size_t(index)];
        diagnosticOrder.push_back(index);
        diagnostics->Append(U(std::string(issue.severity == "error" ? "[错误] " : "[警告] ") +
                              issue.object + " · " + issue.message));
    };
    for (auto index : focused) add(index);
    for (size_t index = 0; index < issues.size(); ++index) add(int(index));
    if (!diagnosticOrder.empty()) {
        diagnostics->SetSelection(0);
        showDiagnostic(diagnosticOrder.front(), false);
    }
    diagnostics->Thaw();
    if (!diagnosticsWindow->IsShown()) diagnosticsWindow->CentreOnParent();
    diagnosticsWindow->Show();
    diagnosticsWindow->Raise();
}

std::vector<LogicCanvas::DiagnosticMarker> LogicCanvas::diagnosticMarkers() const {
    std::vector<DiagnosticMarker> markers;
    if (appearance || owner->issues.empty()) return markers;
    const auto client = GetClientRect();
    const auto &project = owner->history.project;
    const auto &c = owner->circuit();
    std::vector<wxRect> obstacles;
    auto protect = [&](Point a, Point b, int margin) {
        const auto lo = view.screen({std::min(a.x, b.x), std::min(a.y, b.y)});
        const auto hi = view.screen({std::max(a.x, b.x), std::max(a.y, b.y)});
        wxRect bounds(int(std::floor(lo.x)) - margin, int(std::floor(lo.y)) - margin,
                      int(std::ceil(hi.x - lo.x)) + 2 * margin + 1,
                      int(std::ceil(hi.y - lo.y)) + 2 * margin + 1);
        if (bounds.Intersects(client)) obstacles.push_back(bounds);
    };
    // 把文字和实时值也纳入保护区；旋转后再投影，避免警示图标挡住名称或端子。
    wxClientDC metrics(const_cast<LogicCanvas *>(this));
    metrics.SetFont(wxFont(wxFontInfo(10).FaceName("Microsoft YaHei UI")));
    for (const auto &part : c.parts) {
        const auto size = partSize(project, part);
        const auto name = (part.label.empty() ? std::string{} : part.label + " · ") +
                          (part.kind == "Subcircuit" ? part.circuit : part.kind);
        const auto nameSize = metrics.GetTextExtent(U(name));
        double halfWidth = std::max(size.x / 2 + 3, nameSize.x / 2.0);
        double top = -size.y / 2 - 24, bottom = size.y / 2 + 12 + nameSize.y;
        if (part.kind == "Text") halfWidth = std::max(halfWidth, 60.0 + nameSize.x);
        const auto pins = ports(project, part);
        if (!pins.empty()) {
            auto output = std::find_if(pins.begin(), pins.end(), [](const Port &pin) { return pin.output; });
            if (output == pins.end()) output = pins.begin();
            const auto value = metrics.GetTextExtent(U(owner->read({part.id, output->id}).text()));
            halfWidth = std::max(halfWidth, std::min(130.0, double(value.x)) / 2);
        }
        if (part.kind == "Subcircuit")
            for (const auto &shape : project.circuit(part.circuit).shapes)
                if (shape.appearance) {
                    const auto b = shapeBounds(shape);
                    halfWidth = std::max({halfWidth, std::abs(b.first.x) + shape.stroke,
                                         std::abs(b.second.x) + shape.stroke});
                    top = std::min(top, b.first.y - shape.stroke - 24);
                    bottom = std::max(bottom, b.second.y + shape.stroke + 12 + nameSize.y);
                }
        auto rotated = [&](Point point) {
            for (int turn = 0; turn < part.rotation; ++turn) point = {-point.y, point.x};
            return part.at + point;
        };
        protect(rotated({-halfWidth, top}), rotated({halfWidth, bottom}), FromDIP(4));
        for (const auto &pin : pins) protect(part.at + pin.at, part.at + pin.at, FromDIP(11));
    }
    for (const auto &node : c.nodes) {
        protect(node.at, node.at, FromDIP(11));
        if (!node.label.empty()) {
            const auto extent = metrics.GetTextExtent(U(node.label));
            protect(node.at + Point{6, -23}, node.at + Point{6.0 + extent.x, -23.0 + extent.y}, FromDIP(4));
        }
    }
    for (const auto &wire : c.wires) {
        const auto points = wirePoints(project, c, wire);
        for (size_t i = 1; i < points.size(); ++i) protect(points[i - 1], points[i], FromDIP(4));
    }
    for (const auto &shape : c.shapes)
        if (!shape.appearance) {
            const auto b = shapeBounds(shape);
            protect(b.first, b.second, FromDIP(3));
        }
    for (size_t index = 0; index < owner->issues.size(); ++index) {
        const auto position = owner->visibleDiagnostic(owner->issues[index]);
        if (!position) continue;
        const auto screen = view.screen(position->second);
        if (!client.Contains({int(screen.x), int(screen.y)})) continue;
        // 每个对象显示一个图标；保留该对象全部问题，避免相邻元件的问题被合并。
        auto found = std::find_if(markers.begin(), markers.end(), [&](const DiagnosticMarker &marker) {
            return marker.object == position->first;
        });
        if (found == markers.end()) {
            DiagnosticMarker marker;
            marker.object = position->first;
            marker.anchor = screen;
            markers.push_back(marker);
            found = markers.end() - 1;
        }
        found->issues.push_back(int(index));
        found->error = found->error || owner->issues[index].severity == "error";
    }
    const int gap = FromDIP(4), width = FromDIP(18), height = FromDIP(18);
    auto available = [&](const wxRect &candidate) {
        return client.Contains(candidate) && std::none_of(obstacles.begin(), obstacles.end(),
            [&](const wxRect &obstacle) { return obstacle.Intersects(candidate); });
    };
    for (auto &marker : markers) {
        std::vector<wxRect> candidates;
        auto candidateAt = [&](double x, double y) {
            wxRect candidate(int(std::lround(x - width / 2.0)), int(std::lround(y - height / 2.0)), width, height);
            if (available(candidate)) candidates.push_back(candidate);
        };
        // 图标放在对象外围的空白处；距离相同时优先右上方。
        for (const auto &obstacle : obstacles) {
            if (!obstacle.Contains({int(marker.anchor.x), int(marker.anchor.y)}) &&
                distance(marker.anchor, {obstacle.x + obstacle.width / 2.0,
                                         obstacle.y + obstacle.height / 2.0}) > FromDIP(220)) continue;
            const double left = obstacle.x - gap - width / 2.0;
            const double right = obstacle.GetRight() + gap + width / 2.0;
            const double top = obstacle.y - gap - height / 2.0;
            const double bottom = obstacle.GetBottom() + gap + height / 2.0;
            for (double y : {marker.anchor.y, top, bottom}) { candidateAt(left, y); candidateAt(right, y); }
            for (double x : {marker.anchor.x, left, right}) { candidateAt(x, top); candidateAt(x, bottom); }
        }
        // 密集布局中继续寻找空白；没有位置时仍保留颜色、悬停详情和菜单问题列表。
        for (int radius = FromDIP(24); radius <= FromDIP(240); radius += FromDIP(24)) {
            for (int direction = 0; direction < 16; ++direction) {
                const double angle = direction * 3.14159265358979323846 / 8;
                candidateAt(marker.anchor.x + std::cos(angle) * radius,
                            marker.anchor.y + std::sin(angle) * radius);
            }
            if (!candidates.empty()) break;
        }
        if (candidates.empty()) continue;
        const auto best = std::min_element(candidates.begin(), candidates.end(), [&](const wxRect &a, const wxRect &b) {
            auto score = [&](const wxRect &r) {
                const Point center{r.x + r.width / 2.0, r.y + r.height / 2.0};
                return distance(marker.anchor, center) + (center.x < marker.anchor.x ? .1 : 0) +
                       (center.y > marker.anchor.y ? .05 : 0);
            };
            return score(a) < score(b);
        });
        marker.bounds = *best;
        auto reserved = marker.bounds;
        reserved.Inflate(gap);
        obstacles.push_back(reserved);
    }
    markers.erase(std::remove_if(markers.begin(), markers.end(), [](const DiagnosticMarker &marker) {
        return marker.bounds.IsEmpty();
    }), markers.end());
    return markers;
}
void LogicCanvas::drawDiagnostics(wxGraphicsContext &g) {
    if (appearance) return;
    g.PushState();
    for (const auto &marker : diagnosticMarkers()) {
        const auto colour = markerColour(marker.error);
        g.SetPen(wxPen(colour, FromDIP(1)));
        g.SetBrush(wxBrush(wxColour(marker.error ? "#fff0f1" : "#fff7dd")));
        const auto &b = marker.bounds;
        const double cx = b.x + b.width / 2.0, cy = b.y + b.height / 2.0;
        if (marker.error) {
            g.DrawEllipse(b.x + FromDIP(1), b.y + FromDIP(1), b.width - FromDIP(2), b.height - FromDIP(2));
            g.SetPen(wxPen(colour, FromDIP(2)));
            const auto arm = FromDIP(3);
            g.StrokeLine(cx - arm, cy - arm, cx + arm, cy + arm);
            g.StrokeLine(cx - arm, cy + arm, cx + arm, cy - arm);
        } else {
            auto triangle = g.CreatePath();
            triangle.MoveToPoint(cx, b.y + FromDIP(1));
            triangle.AddLineToPoint(b.GetRight(), b.GetBottom() - FromDIP(1));
            triangle.AddLineToPoint(b.x, b.GetBottom() - FromDIP(1));
            triangle.CloseSubpath();
            g.DrawPath(triangle);
            g.SetPen(wxPen(colour, FromDIP(2)));
            g.StrokeLine(cx, b.y + b.height * .36, cx, b.y + b.height * .61);
            g.SetPen(*wxTRANSPARENT_PEN);
            g.SetBrush(wxBrush(colour));
            g.DrawEllipse(cx - FromDIP(1), b.y + b.height * .74, FromDIP(2), FromDIP(2));
        }
    }
    g.PopState();
}

bool LogicCanvas::handleDiagnosticClick(wxPoint point) {
    if (appearance || tool != "Select" || start || dragging || panning) return false;
    for (const auto &marker : diagnosticMarkers())
        if (marker.bounds.Contains(point) && !pinAt(view.world({double(point.x), double(point.y)}))) {
            owner->openDiagnostics(marker.issues); return true;
        }
    return false;
}

void LogicCanvas::updateDiagnosticTooltip(Point screen) {
    wxString next;
    if (!appearance && !dragging && !panning && !box && tool != "Place") {
        const wxPoint point{int(screen.x), int(screen.y)};
        for (const auto &marker : diagnosticMarkers())
            if (marker.bounds.Contains(point)) {
                std::string detail = "点击查看此对象的 ERC 问题\n\n";
                for (size_t i = 0; i < std::min<size_t>(3, marker.issues.size()); ++i) {
                    if (i) detail += "\n\n";
                    detail += owner->diagnosticText(owner->issues[size_t(marker.issues[i])]);
                }
                next = U(detail);
                break;
            }
        if (next.empty()) {
            const auto at = view.world(screen);
            auto id = hit(at);
            if (auto pin = pinAt(at)) id = pin->object;
            for (const auto &issue : owner->issues) {
                auto position = owner->visibleDiagnostic(issue);
                if (position && position->first == id) { next = U(owner->diagnosticText(issue)); break; }
            }
        }
    }
    if (next != diagnosticTooltip) { diagnosticTooltip = next; SetToolTip(next); }
}
} // namespace eda::logic
