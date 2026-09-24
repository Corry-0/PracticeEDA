// Vector artwork shared by the canvas, placement preview and image export; ports() owns pin positions.
#include "LogicSymbols.h"
#include <cmath>

namespace eda::logic {
namespace {
wxColour signalColour(const Signal &s) {
    if (s.error)
        return wxColour("#dc2626");
    if (s.z)
        return wxColour("#929bab");
    if (!s.defined())
        return wxColour("#d28b13");
    return wxColour(s.value ? "#139c59" : "#31577e");
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
           int align = 0, double maxWidth = 0) {
    g.SetFont(wxFont(wxFontInfo(size).FaceName("Microsoft YaHei UI")), colour);
    wxString display = U(s);
    double w = 0, h = 0;
    g.GetTextExtent(display, &w, &h);
    if (maxWidth > 0 && w > maxWidth) {
        while (display.size() > 1) {
            display.RemoveLast();
            g.GetTextExtent(display + U("…"), &w, &h);
            if (w <= maxWidth)
                break;
        }
        display += U("…");
    }
    g.DrawText(display, x - (align == 0 ? w / 2 : align > 0 ? w : 0), y);
}
std::string blockName(const Part &p) {
    static const std::map<std::string, std::string> names = {
        {"Mux", "MUX"},      {"Demux", "DEMUX"},  {"Decoder", "DEC"}, {"Encoder", "ENC"},
        {"Extender", "0→n"}, {"DFF", "D"},        {"TFF", "T"},       {"JKFF", "JK"},
        {"SRFF", "SR"},      {"Register", "REG"}, {"Counter", "CTR"}, {"ShiftRegister", "SRG"},
        {"RAM", "RAM"},      {"ROM", "ROM"},      {"Add", "+"},       {"Subtract", "−"},
        {"Multiply", "×"},   {"Divide", "÷"},     {"Negate", "−x"},   {"Compare", "CMP"},
        {"ShiftLeft", "<<"}, {"ShiftRight", ">>"}};
    if (p.kind == "Subcircuit")
        return p.circuit;
    auto it = names.find(p.kind);
    return it == names.end() ? std::string{} : it->second;
}
} // namespace

void drawSymbol(wxGraphicsContext &g, const Project &project, const Part &part,
                const std::function<Signal(const Port &)> &read, bool selected, bool error, bool preview) {
    const auto &k = part.kind;
    auto pp = ports(project, part);
    double h = partHeight(project, part);
    wxColour ink(error ? "#dc2626" : selected ? "#6366f1" : preview ? "#8b98a8" : "#26384b");
    wxColour fill(selected ? "#eef2ff" : "#ffffff");
    auto outline = [&] {
        g.SetPen(
            wxPen(ink, selected || error ? 2.8 : 1.8, preview ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID));
        g.SetBrush(wxBrush(fill));
    };
    g.PushState();
    g.Translate(part.at.x, part.at.y);
    outline();
    if (k == "Text") {
        label(g, part.label.empty() && preview ? "文字标签" : part.label, -55, -10, 12, ink, -1);
        if (selected) {
            g.SetBrush(*wxTRANSPARENT_BRUSH);
            g.DrawRectangle(-60, -30, 120, 60);
        }
        g.PopState();
        return;
    }
    bool triangle = k == "Not" || k == "Buffer" || k == "TriState";
    bool andGate = k == "And" || k == "Nand";
    bool orGate = k == "Or" || k == "Nor" || k == "Xor" || k == "Xnor";
    bool inverted = k == "Not" || k == "Nand" || k == "Nor" || k == "Xnor";
    bool fan = k == "Splitter" || k == "Joiner";
    bool compact = k == "Input" || k == "Output" || k == "Constant" || k == "Clock" || k == "Button" ||
                   k == "LED" || k == "Probe";
    bool supply = k == "Power" || k == "Ground";
    bool block = !triangle && !andGate && !orGate && !fan && !compact && !supply && k != "Hex";
    bool trapezoid = k == "Mux" || k == "Demux" || k == "Extender";
    auto mainPin = std::find_if(pp.begin(), pp.end(), [](const Port &p) { return p.output; });
    if (mainPin == pp.end())
        mainPin = pp.begin();
    Signal value = mainPin == pp.end() ? Signal::unknown(part.width) : read(*mainPin);
    auto liveColour = preview ? ink : signalColour(value);

    // Stubs end on the actual outline; the connection dots remain at model anchors.
    for (const auto &pin : pp) {
        auto c = preview ? ink : signalColour(read(pin));
        g.SetPen(wxPen(c, pin.width > 1 ? 3 : 1.8));
        double end = pin.output ? 55 : -55;
        if (triangle || andGate || orGate) {
            end = pin.output ? (inverted ? 52 : 40) : -36;
            if (orGate && !pin.output) {
                double t = (pin.at.y + 30) / 60;
                end = -38 + 28 * t * (1 - t);
            }
        } else if (compact)
            end = pin.output ? 24 : -24;
        else if (k == "Hex")
            end = -52;
        else if (supply || fan)
            end = 0;
        else if (trapezoid) {
            bool slanted = (k == "Mux" && pin.output) || (k != "Mux" && !pin.output);
            end = (pin.output ? 1 : -1) * (slanted ? 39 : 50);
        }
        bool bottomControl = (k == "Mux" || k == "Demux") && pin.id == "S";
        if ((k == "TriState" && pin.id == "EN") || bottomControl) {
            double bottom = bottomControl ? h / 2 - 6 : 14;
            path(g, {pin.at, {-60, pin.at.y}, {-60, h / 2 + 12}, {0, h / 2 + 12}, {0, bottom}});
        } else if (fan) {
            bool branch = k == "Splitter" ? pin.output : !pin.output;
            if (branch)
                path(g, {pin.at, {pin.output ? 30.0 : -30.0, pin.at.y}, {0, 0}});
            else
                g.StrokeLine(pin.at.x, pin.at.y, 0, 0);
        } else
            g.StrokeLine(pin.at.x, pin.at.y, end, pin.at.y);
        g.SetBrush(wxBrush(c));
        g.DrawEllipse(pin.at.x - 2.5, pin.at.y - 2.5, 5, 5);
        if (pin.width > 1) {
            double x = pin.output ? 61 : -65;
            g.StrokeLine(x - 2, pin.at.y + 4, x + 2, pin.at.y - 4);
            label(g, std::to_string(pin.width), x, pin.at.y - 17, 7, ink);
        }
    }
    outline();
    if (triangle) {
        path(g, {{-36, -30}, {40, 0}, {-36, 30}}, true);
    } else if (andGate) {
        auto p = g.CreatePath();
        p.MoveToPoint(-36, -30);
        p.AddLineToPoint(10, -30);
        p.AddCurveToPoint(50, -30, 50, 30, 10, 30);
        p.AddLineToPoint(-36, 30);
        p.CloseSubpath();
        g.DrawPath(p);
    } else if (orGate) {
        auto p = g.CreatePath();
        p.MoveToPoint(-38, -30);
        p.AddCurveToPoint(-1, -30, 26, -22, 40, 0);
        p.AddCurveToPoint(26, 22, -1, 30, -38, 30);
        p.AddQuadCurveToPoint(-24, 0, -38, -30);
        p.CloseSubpath();
        g.DrawPath(p);
        if (k == "Xor" || k == "Xnor") {
            auto extra = g.CreatePath();
            extra.MoveToPoint(-47, -30);
            extra.AddQuadCurveToPoint(-33, 0, -47, 30);
            g.StrokePath(extra);
        }
    } else if (fan) {
        g.SetPen(wxPen(ink, 4));
        g.StrokeLine(0, -h / 2 + 10, 0, h / 2 - 10);
    } else if (supply) {
        if (k == "Power") {
            g.StrokeLine(0, 0, 0, -10);
            path(g, {{-12, -10}, {0, -28}, {12, -10}}, true);
        } else {
            g.StrokeLine(0, 0, 0, 10);
            g.StrokeLine(-16, 10, 16, 10);
            g.StrokeLine(-10, 17, 10, 17);
            g.StrokeLine(-4, 24, 4, 24);
        }
    } else if (compact) {
        if (k == "LED") {
            g.SetBrush(wxBrush(value.defined() && value.value ? wxColour("#2bdd72") : wxColour("#e2e8f0")));
            g.DrawEllipse(-24, -24, 48, 48);
            path(g, {{4, -4}, {13, -13}});
            path(g, {{6, -13}, {13, -13}, {13, -6}});
        } else if (k == "Output" || k == "Probe") {
            g.DrawRoundedRectangle(-24, -24, 48, 48, 24);
        } else
            g.DrawRectangle(-24, -24, 48, 48);
        if (k == "Clock") {
            path(g, {{-17, 10}, {-10, 10}, {-10, -10}, {0, -10}, {0, 10}, {10, 10}, {10, -10}, {17, -10}});
        } else if (k == "Button") {
            g.SetBrush(wxBrush(value.defined() && value.value ? wxColour("#bbf7d0") : wxColour("#e2e8f0")));
            g.DrawRoundedRectangle(-16, -16, 32, 32, 4);
            label(g, preview ? "0" : value.text(), 0, -11, 12, liveColour, 0, 28);
        } else if (k != "LED") {
            label(g, preview ? (k == "Probe" ? "?" : "0") : value.text(), 0, -12, 13, liveColour, 0, 42);
        }
    } else if (k == "Hex") {
        g.SetBrush(wxBrush(wxColour("#152536")));
        g.DrawRoundedRectangle(-52, -27, 104, 54, 4);
        static const unsigned segments[16] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07,
                                              0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71};
        int count = std::clamp((part.width + 3) / 4, 1, 8);
        double w = std::min(24.0, 94.0 / count), top = -17, bottom = 17;
        for (int i = 0; i < count; ++i) {
            double x = (i - count / 2.0) * w + 3, right = x + w - 6;
            unsigned bits = value.defined() ? segments[(value.value >> (4 * (count - i - 1))) & 15] : 0x40;
            std::pair<Point, Point> lines[] = {{{x, top}, {right, top}},      {{right, top}, {right, 0}},
                                               {{right, 0}, {right, bottom}}, {{x, bottom}, {right, bottom}},
                                               {{x, 0}, {x, bottom}},         {{x, top}, {x, 0}},
                                               {{x, 0}, {right, 0}}};
            for (int j = 0; j < 7; ++j) {
                g.SetPen(wxPen(wxColour((bits & (1u << j)) ? "#55ef9b" : "#284252"), 2));
                g.StrokeLine(lines[j].first.x, lines[j].first.y, lines[j].second.x, lines[j].second.y);
            }
        }
    } else if (trapezoid) {
        if (k == "Mux")
            path(g, {{-50, -h / 2}, {39, -h / 2 + 12}, {39, h / 2 - 12}, {-50, h / 2}}, true);
        else
            path(g, {{-39, -h / 2 + 12}, {50, -h / 2}, {50, h / 2}, {-39, h / 2 - 12}}, true);
    } else
        g.DrawRectangle(-55, -h / 2, 110, h);
    outline();
    if (inverted)
        g.DrawEllipse(40, -6, 12, 12);

    // Named block terminals sit inside the body; ordinary gate pins need no A/B/Y labels.
    for (const auto &pin : pp) {
        if (fan) {
            if ((k == "Splitter" && pin.output) || (k == "Joiner" && !pin.output))
                label(g, pin.id.substr(1), pin.output ? 39 : -39, pin.at.y - 16, 7, ink);
            continue;
        }
        if (k == "TriState" && pin.id == "EN") {
            label(g, "EN", 7, h / 2 - 5, 7, ink, -1);
            continue;
        }
        if (!block)
            continue;
        if ((k == "Mux" || k == "Demux") && pin.id == "S") {
            label(g, "S", 7, h / 2 - 3, 7, ink, -1);
            continue;
        }
        double x = pin.output ? 48 : -48;
        if (trapezoid)
            x = pin.output ? (k == "Mux" ? 32 : 43) : (k == "Mux" ? -43 : -32);
        if (pin.id == "CLK") {
            path(g, {{-55, pin.at.y - 6}, {-47, pin.at.y}, {-55, pin.at.y + 6}});
            label(g, "CLK", -43, pin.at.y - 7, 7, ink, -1);
        } else {
            std::string name = pin.id;
            if (k == "Subcircuit") {
                auto p = project.circuit(part.circuit).part(pin.id);
                if (p && !p->label.empty())
                    name = p->label;
            }
            label(g, name, x, pin.at.y - 7, 8, ink, pin.output ? 1 : -1, 42);
        }
    }
    std::string name = blockName(part);
    if (block && k != "Subcircuit") {
        label(g, name, 0, k == "ROM" ? -h / 2 + 3 : -10, k == "Demux" ? 9 : 11, ink, 0,
              k == "Demux" ? 48 : 38);
        name.clear();
    }
    if (!part.label.empty())
        name = name.empty() ? part.label : part.label + " · " + name;
    if (!name.empty())
        label(g, name, 0, -h / 2 - 24, 10, ink);
    if (!preview && !pp.empty()) {
        bool insideValue = compact && k != "Clock" && k != "LED" && k != "Button" && part.width == 1;
        if (!insideValue)
            label(g, value.text(), 0, h / 2 + 16, 9, liveColour, 0, 130);
    }
    g.PopState();
}
} // namespace eda::logic
