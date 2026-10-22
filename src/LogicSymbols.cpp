// 画布、放置预览与图像导出共享的矢量绘图；引脚位置由 ports() 决定。
#include "LogicSymbols.h"
#include <cmath>

namespace eda::logic {
wxColour signalColour(const Signal &s) {
    if (s.error)
        return wxColour("#dc2626");
    if (s.z)
        return wxColour("#929bab");
    if (!s.defined())
        return wxColour("#d28b13");
    return wxColour(s.value ? "#139c59" : "#31577e");
}
namespace {
wxColour signalFill(const Signal &s) {
    if (s.error)
        return wxColour("#fee2e2");
    if (s.z)
        return wxColour("#e2e8f0");
    if (!s.defined())
        return wxColour("#fef3c7");
    return wxColour(s.value ? "#bbf7d0" : "#dbeafe");
}
void path(wxGraphicsContext &g, std::initializer_list<Point> points, bool closed = false) {
    auto p = g.CreatePath();
    bool first = true;
    for (auto a : points) {
        if (first)
            p.MoveToPoint(a.x, a.y);
        else
            p.AddLineToPoint(a.x, a.y);
        first = false;
    }
    if (closed) {
        p.CloseSubpath();
        g.DrawPath(p);
    } else
        g.StrokePath(p);
}
void label(wxGraphicsContext &g, const std::string &s, double x, double y, int size, wxColour colour,
           int align = 0, double maxWidth = 0, bool icon = false) {
    g.SetFont(wxFont(wxFontInfo(size).FaceName("Microsoft YaHei UI")), colour);
    wxString display = U(s);
    double w = 0, h = 0;
    g.GetTextExtent(display, &w, &h);
    if (icon && maxWidth > 0)
        while (w > maxWidth && size > 1) {
            g.SetFont(wxFont(wxFontInfo(--size).FaceName("Microsoft YaHei UI")), colour);
            g.GetTextExtent(display, &w, &h);
        }
    if (maxWidth > 0 && w > maxWidth) {
        while (display.size() > 1) {
            display.RemoveLast();
            g.GetTextExtent(display + U("…"), &w, &h);
            if (w <= maxWidth)
                break;
        }
        display += U("…");
    }
    g.DrawText(display, x - (align == 0 ? w / 2 : align > 0 ? w : 0), icon ? y - h / 2 : y);
}
std::string blockName(const Part &p) {
    static const std::map<std::string, std::string> names = {
        {"Mux", "MUX"},      {"Demux", "DEMUX"},  {"Decoder", "DEC"}, {"Encoder", "ENC"},
        {"Extender", "0→n"}, {"DFF", "D"},        {"TFF", "T"},       {"JKFF", "JK"},
        {"SRFF", "SR"},      {"Register", "REG"}, {"Counter", "CTR"}, {"ShiftRegister", "SRG"},
        {"RAM", "RAM"},      {"ROM", "ROM"},      {"Add", "+"},       {"Subtract", "−"},
        {"Multiply", "×"},   {"Divide", "÷"},     {"Negate", "−x"},   {"Compare", "CMP"},
        {"ShiftLeft", "<<"}, {"ShiftRight", ">>"}, {"ControlledInverter", "INV / EN"},
        {"EvenParity", "EVEN"}, {"OddParity", "ODD"}, {"BitSelector", "BIT SEL"},
        {"BitAdder", "POPCNT"}, {"BitFinder", "FIND"}, {"Random", "RNG"},
        {"Transistor", "FET"}, {"TransmissionGate", "TG"}, {"Keyboard", "KEY"},
        {"Joystick", "JOY"}};
    if (p.kind == "Subcircuit")
        return p.circuit;
    auto it = names.find(p.kind);
    return it == names.end() ? p.kind : it->second;
}
} // namespace

void drawShape(wxGraphicsContext &g, const Shape &s, bool selected, bool preview, double zoom) {
    g.PushState();
    g.SetPen(wxPen(wxColour(selected || preview ? "#6366f1" : "#26384b"), s.stroke,
                   preview ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID));
    g.SetBrush(s.filled ? wxBrush(wxColour("#e2e8f0")) : *wxTRANSPARENT_BRUSH);
    auto bounds = shapeBounds(s);
    auto lo = bounds.first, hi = bounds.second;
    if (s.kind == "Line")
        g.StrokeLine(s.a.x, s.a.y, s.b.x, s.b.y);
    else if (s.kind == "Curve") {
        auto path = g.CreatePath();
        path.MoveToPoint(s.a.x, s.a.y);
        path.AddQuadCurveToPoint(s.control.x, s.control.y, s.b.x, s.b.y);
        g.StrokePath(path);
    } else if (s.kind == "Rectangle")
        g.DrawRectangle(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
    else
        g.DrawEllipse(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
    if (selected) {
        g.SetPen(wxPen(wxColour("#6366f1"), 1 / zoom, wxPENSTYLE_SHORT_DASH));
        if (s.kind == "Curve") {
            g.StrokeLine(s.a.x, s.a.y, s.control.x, s.control.y);
            g.StrokeLine(s.control.x, s.control.y, s.b.x, s.b.y);
        }
        g.SetBrush(*wxWHITE_BRUSH);
        for (auto p : std::vector<Point>{s.a, s.b})
            g.DrawRectangle(p.x - 4 / zoom, p.y - 4 / zoom, 8 / zoom, 8 / zoom);
        if (s.kind == "Curve")
            g.DrawEllipse(s.control.x - 4 / zoom, s.control.y - 4 / zoom, 8 / zoom, 8 / zoom);
    }
    g.PopState();
}

void drawSymbol(wxGraphicsContext &g, const Project &project, const Part &part,
                const std::function<Signal(const Port &)> &read, bool selected, bool error, bool preview,
                bool annotations, bool warning) {
    const auto &k = part.kind;
    auto canonical = part;
    canonical.rotation = 0;
    const auto pp = ports(project, canonical);
    const auto size = partSize(project, part);
    const double h = size.y, half = size.x / 2;
    const wxColour ink(error ? "#dc2626" : warning ? "#bd790c" : selected ? "#6366f1" : preview ? "#8b98a8" : "#26384b");
    const wxColour fill(selected ? "#eef2ff" : "#f8fbff");
    auto outline = [&] {
        g.SetPen(wxPen(ink, selected || error || warning ? 2.6 : 1.8,
                       preview ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID));
        g.SetBrush(wxBrush(fill));
    };
    auto mainPin = std::find_if(pp.begin(), pp.end(), [](const Port &p) { return p.output; });
    if (mainPin == pp.end())
        mainPin = pp.begin();
    const Signal value = mainPin == pp.end() ? Signal::unknown(part.width) : read(*mainPin);
    const auto liveColour = preview ? ink : signalColour(value);
    const bool triangle = k == "Not" || k == "Buffer" || k == "TriState";
    const bool andGate = k == "And" || k == "Nand";
    const bool orGate = k == "Or" || k == "Nor" || k == "Xor" || k == "Xnor";
    const bool inverted = k == "Not" || k == "Nand" || k == "Nor" || k == "Xnor";
    const bool fan = k == "Splitter" || k == "Joiner";
    const bool io = k == "Input" || k == "Output";
    const bool supply = k == "Power" || k == "Ground" || k == "PullUp" || k == "PullDown";
    const bool compact = io || supply || k == "Constant" || k == "Clock" || k == "Button" ||
                         k == "LED" || k == "Probe";
    const bool display = k == "Hex" || k == "SevenSegment" || k == "LEDMatrix";
    const bool trapezoid = k == "Mux" || k == "Demux" || k == "Extender";
    const bool block = !triangle && !andGate && !orGate && !fan && !compact && !display;
    const bool custom = k == "Subcircuit" &&
        std::any_of(project.circuit(part.circuit).shapes.begin(),
                    project.circuit(part.circuit).shapes.end(), [](const Shape &s) { return s.appearance; });
    g.PushState();
    g.Translate(part.at.x, part.at.y);
    g.Rotate(part.rotation * 3.14159265358979323846 / 2);
    outline();
    if (k == "Text") {
        if (annotations)
            label(g, part.label.empty() && preview ? "文字标签" : part.label, -55, -10, 12, ink, -1);
        if (selected) {
            g.SetBrush(*wxTRANSPARENT_BRUSH);
            g.DrawRectangle(-60, -30, 120, 60);
        }
        g.PopState();
        return;
    }
    if (custom) {
        for (const auto &s : project.circuit(part.circuit).shapes)
            if (s.appearance)
                drawShape(g, s, false, preview);
        outline();
    } else if (triangle) {
        path(g, {{-40, -30}, {inverted ? 30.0 : 40.0, 0}, {-40, 30}}, true);
    } else if (andGate) {
        const double tip = inverted ? 30 : 40;
        auto p = g.CreatePath();
        p.MoveToPoint(-40, -h / 2);
        p.AddLineToPoint(0, -h / 2);
        p.AddCurveToPoint(tip * 4 / 3, -h / 2, tip * 4 / 3, h / 2, 0, h / 2);
        p.AddLineToPoint(-40, h / 2);
        p.CloseSubpath();
        g.DrawPath(p);
    } else if (orGate) {
        const double tip = inverted ? 30 : 40;
        auto p = g.CreatePath();
        p.MoveToPoint(-40, -h / 2);
        p.AddCurveToPoint(-3, -h / 2, tip - 14, -h * .37, tip, 0);
        p.AddCurveToPoint(tip - 14, h * .37, -3, h / 2, -40, h / 2);
        p.AddQuadCurveToPoint(-12, 0, -40, -h / 2);
        p.CloseSubpath();
        g.DrawPath(p);
        if (k == "Xor" || k == "Xnor") {
            // 第二条异或曲线置于主体内，不在输入端产生一段外伸引线。
            auto extra = g.CreatePath();
            extra.MoveToPoint(-32, -h / 2 + 7);
            extra.AddQuadCurveToPoint(-6, 0, -32, h / 2 - 7);
            g.StrokePath(extra);
        }
    } else if (trapezoid) {
        if (k == "Mux")
            path(g, {{-55, -h / 2}, {39, -h / 2 + 12}, {39, h / 2 - 12}, {-55, h / 2}}, true);
        else
            path(g, {{-39, -h / 2 + 12}, {55, -h / 2}, {55, h / 2}, {-39, h / 2 - 12}}, true);
    } else if (compact) {
        g.SetPen(wxPen(liveColour, 2, preview ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID));
        g.SetBrush(wxBrush(preview ? wxColour("#f1f5f9") : signalFill(value)));
        if (k == "Output" || k == "Probe" || k == "LED")
            g.DrawEllipse(-half, -h / 2, size.x, h);
        else
            g.DrawRoundedRectangle(-half, -h / 2, size.x, h, io ? 1 : 4);
        if (selected || error) {
            g.SetPen(wxPen(ink, 1.5, wxPENSTYLE_SHORT_DASH));
            g.SetBrush(*wxTRANSPARENT_BRUSH);
            g.DrawRoundedRectangle(-half - 4, -h / 2 - 4, size.x + 8, h + 8, 4);
        }
        g.SetPen(wxPen(liveColour, 1.8));
        if (k == "Clock") {
            path(g, {{-17, 10}, {-10, 10}, {-10, -10}, {0, -10}, {0, 10}, {10, 10}, {10, -10}, {17, -10}});
        } else if (k == "Power" || k == "PullUp") {
            path(g, {{0, 14}, {0, -13}, {-9, -4}, {0, -13}, {9, -4}});
            if (k == "PullUp")
                path(g, {{-4, 12}, {4, 8}, {-4, 4}, {4, 0}});
        } else if (k == "Ground" || k == "PullDown") {
            path(g, {{0, -15}, {0, 4}});
            for (int i = 0; i < 3; ++i)
                g.StrokeLine(-12 + i * 4, 4 + i * 5, 12 - i * 4, 4 + i * 5);
            if (k == "PullDown")
                path(g, {{-4, -14}, {4, -10}, {-4, -6}, {4, -2}});
        } else if (k == "LED") {
            g.SetBrush(wxBrush(value.defined() && value.value ? wxColour("#2bdd72") : wxColour("#d8e4ee")));
            g.DrawEllipse(-14, -14, 28, 28);
            path(g, {{4, -4}, {13, -13}});
            path(g, {{6, -13}, {13, -13}, {13, -6}});
        } else {
            if (k == "Button") {
                g.SetBrush(wxBrush(value.defined() && value.value ? wxColour("#bbf7d0") : wxColour("#e2e8f0")));
                g.DrawRoundedRectangle(-16, -16, 32, 32, 4);
            }
            const auto word = preview ? (k == "Output" || k == "Probe" ? "?" : "0") :
                              io && part.width > 1 ? "BUS" : value.text();
            label(g, word, 0, io ? -9 : -11, io ? (part.width > 1 ? 7 : 11) : 12,
                  liveColour, 0, io ? 24 : 40);
        }
    } else if (display) {
        g.SetBrush(wxBrush(wxColour("#152536")));
        g.DrawRoundedRectangle(-half, -h / 2, size.x, h, 4);
        if (k == "Hex") {
            static const unsigned segments[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
                                                 0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71};
            int count = std::clamp((part.width + 3) / 4, 1, 8);
            double w = std::min(24.0, 94.0 / count);
            for (int i = 0; i < count; ++i) {
                double x = (i - count / 2.0) * w + 3, right = x + w - 6;
                unsigned bits = value.defined() ? segments[(value.value >> (4 * (count - i - 1))) & 15] : 0x40;
                const std::pair<Point, Point> lines[] = {
                    {{x, -17}, {right, -17}}, {{right, -17}, {right, 0}}, {{right, 0}, {right, 17}},
                    {{x, 17}, {right, 17}}, {{x, 0}, {x, 17}}, {{x, -17}, {x, 0}}, {{x, 0}, {right, 0}}};
                for (int j = 0; j < 7; ++j) {
                    g.SetPen(wxPen(wxColour(bits & (1u << j) ? "#55ef9b" : "#284252"), 2));
                    g.StrokeLine(lines[j].first.x, lines[j].first.y, lines[j].second.x, lines[j].second.y);
                }
            }
        } else if (k == "SevenSegment") {
            const std::pair<Point, Point> segments[] = {
                {{-5, -40}, {20, -40}}, {{23, -36}, {23, -4}}, {{23, 4}, {23, 36}},
                {{-5, 40}, {20, 40}}, {{-8, 4}, {-8, 36}}, {{-8, -36}, {-8, -4}}, {{-5, 0}, {20, 0}}};
            for (size_t i = 0; i < 7; ++i) {
                auto pin = std::find_if(pp.begin(), pp.end(), [&](const Port &p) { return p.id == std::string(1, char('a' + i)); });
                const auto signal = read(*pin);
                bool active = signal.defined() && bool(signal.value) != bool(parameter(part, "activeLow", 0));
                g.SetPen(wxPen(wxColour(active ? "#55ef9b" : "#284252"), 4));
                g.StrokeLine(segments[i].first.x, segments[i].first.y, segments[i].second.x, segments[i].second.y);
            }
            auto dot = std::find_if(pp.begin(), pp.end(), [](const Port &p) { return p.id == "dp"; });
            const auto signal = read(*dot);
            bool lit = signal.defined() && bool(signal.value) != bool(parameter(part, "activeLow", 0));
            g.SetBrush(wxBrush(wxColour(lit ? "#55ef9b" : "#284252")));
            g.DrawEllipse(28, 39, 5, 5);
        } else {
            int rows = std::clamp(int(parameter(part, "rows", 8)), 1, 32);
            int cols = std::clamp(int(parameter(part, "cols", 8)), 1, 32);
            auto rowPin = std::find_if(pp.begin(), pp.end(), [](const Port &p) { return p.id == "ROWS"; });
            auto colPin = std::find_if(pp.begin(), pp.end(), [](const Port &p) { return p.id == "COLS"; });
            auto rowSignal = read(*rowPin), colSignal = read(*colPin);
            double dx = 48.0 / cols, dy = 72.0 / rows;
            for (int row = 0; row < rows; ++row)
                for (int col = 0; col < cols; ++col) {
                    bool activeLow = parameter(part, "activeLow", 0) != 0;
                    bool lit = rowSignal.defined() && colSignal.defined() &&
                        bool(rowSignal.value & (uint32_t(1) << row)) != activeLow &&
                        bool(colSignal.value & (uint32_t(1) << col)) != activeLow;
                    g.SetPen(*wxTRANSPARENT_PEN);
                    g.SetBrush(wxBrush(wxColour(lit ? "#55ef9b" : "#284252")));
                    g.DrawEllipse(-9 + col * dx + .4, -36 + row * dy + .4,
                                  std::max(.6, dx - .8), std::max(.6, dy - .8));
                }
        }
    } else {
        g.DrawRectangle(-half, -h / 2, size.x, h);
        if (fan) {
            const bool split = k == "Splitter";
            const double trunk = split ? -13 : 13;
            g.SetPen(wxPen(ink, 2.4));
            g.StrokeLine(trunk, -h / 2 + 30, trunk, h / 2 - 14);
            for (const auto &pin : pp)
                path(g, {pin.at, {trunk, pin.at.y}});
        } else if (annotations) {
            g.SetPen(wxPen(wxColour(selected ? "#c7d2fe" : "#dce6ef"), 1));
            g.StrokeLine(-half + 1, -h / 2 + 24, half - 1, -h / 2 + 24);
        }
    }
    outline();
    if (inverted)
        g.DrawEllipse(30, -5, 10, 10);
    // 圆点最后绘制，连接中心与 ports() 给出的主体边缘坐标完全相同。
    for (const auto &pin : pp) {
        const auto c = preview ? ink : signalColour(read(pin));
        if (!pin.output && (andGate || orGate)) {
            int index = pin.id == "A" ? 0 : pin.id == "B" ? 1 : std::stoi(pin.id.substr(1));
            if (uint64_t(parameter(part, "invertMask", 0)) & (uint64_t(1) << index)) {
                outline();
                g.DrawEllipse(pin.at.x, pin.at.y - 4, 8, 8);
            }
        }
        g.SetPen(wxPen(c, 1));
        g.SetBrush(wxBrush(c));
        double radius = pin.width > 1 ? 3 : 2.5;
        g.DrawEllipse(pin.at.x - radius, pin.at.y - radius, 2 * radius, 2 * radius);
        const bool selectPin = (k == "Mux" || k == "Demux") && pin.id == "S";
        if (annotations && (selectPin || (k == "TriState" && pin.id == "EN"))) {
            label(g, pin.id, pin.at.x, pin.at.y - 15, 7, ink, 0, 22);
        } else if (annotations && (block || fan || k == "SevenSegment" || k == "LEDMatrix")) {
            auto name = pin.id;
            if (k == "Subcircuit") {
                auto interfacePart = project.circuit(part.circuit).part(pin.id);
                if (interfacePart && !interfacePart->label.empty())
                    name = interfacePart->label;
            }
            const auto pinInk = display ? wxColour("#b8cddd") : ink;
            if (pin.id == "CLK") {
                g.SetPen(wxPen(ink, 1.5));
                path(g, {{pin.at.x + 2, pin.at.y - 5}, {pin.at.x + 8, pin.at.y}, {pin.at.x + 2, pin.at.y + 5}});
            }
            if (custom && std::abs(pin.at.y) > std::abs(pin.at.x)) {
                label(g, name, pin.at.x, pin.at.y + (pin.at.y > 0 ? -22 : 8), 8, pinInk, 0, 65);
            } else {
                const bool right = pin.at.x > 0;
                label(g, name, pin.at.x + (right ? -8 : pin.id == "CLK" ? 13 : 8),
                      pin.at.y - (fan ? 16 : 7), display || fan ? 7 : 8, pinInk, right ? 1 : -1,
                      display ? (k == "LEDMatrix" ? 29 : 20) : 44);
            }
        }
        if (annotations && pin.width > 1) {
            const bool bottom = selectPin || (custom && std::abs(pin.at.y) > std::abs(pin.at.x));
            label(g, std::to_string(pin.width), pin.at.x + (bottom ? 9 : pin.at.x > 0 ? 9 : -9),
                  pin.at.y - 18, 7, ink, bottom ? -1 : pin.at.x > 0 ? -1 : 1);
        }
    }
    if (block && !custom) {
        auto mark = blockName(part);
        if (!annotations) {
            static const std::map<std::string, std::string> marks = {
                {"Subcircuit", "SUB"}, {"ControlledInverter", "INV"}, {"EvenParity", "EVN"},
                {"OddParity", "ODD"}, {"Extender", "EXT"}, {"BitSelector", "SEL"},
                {"BitAdder", "CNT"}, {"BitFinder", "FND"}, {"Demux", "DMX"}};
            auto it = marks.find(k);
            if (it != marks.end())
                mark = it->second;
        }
        const int font = annotations ? 10 : int(std::max(size.x, size.y) * .27);
        label(g, mark, 0, annotations ? -h / 2 + 4 : 0, font, ink, 0,
              annotations && trapezoid ? 62 : size.x - 18, !annotations);
    }
    if (fan && annotations)
        label(g, k == "Splitter" ? "SPLIT" : "JOIN", 0, -h / 2 + 4, 9, ink, 0, size.x - 12);
    if (annotations) {
        auto name = (block && !custom) || fan ? std::string{} : blockName(part);
        if (!part.label.empty())
            name = name.empty() ? part.label : part.label + " · " + name;
        double labelTop = -h / 2 - 24, valueBottom = h / 2 + 12;
        if (custom)
            for (const auto &s : project.circuit(part.circuit).shapes)
                if (s.appearance) {
                    auto b = shapeBounds(s);
                    labelTop = std::min(labelTop, b.first.y - s.stroke / 2 - 24);
                    valueBottom = std::max(valueBottom, b.second.y + s.stroke / 2 + 12);
                }
        if (!name.empty())
            label(g, name, 0, labelTop, 10, ink);
        if (!preview && !pp.empty() && (!compact || k == "Clock" || k == "LED" || (io && part.width > 1)))
            label(g, value.text(), 0, valueBottom, 9, liveColour, 0, 130);
    }
    g.PopState();
}
} // namespace eda::logic
