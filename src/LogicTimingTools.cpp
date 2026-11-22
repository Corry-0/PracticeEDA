#include "DigitalEditor.h"
#include <chrono>
#include <deque>
#include <tuple>
#include <wx/choicdlg.h>
#include <wx/dcbuffer.h>
#include <wx/display.h>
#include <wx/textdlg.h>
#include <wx/tglbtn.h>

namespace eda::logic {
namespace {
constexpr unsigned rates[] = {1, 2, 5, 10, 20};
constexpr size_t maxChannels = 32, maxChanges = 4096;
const wxColour paper("#F0F3F7"), plot("#182533"), grid("#344758"), ink("#24384C");
const wxColour plotText("#B7C9DA"), clockColour("#6FD9D3"), dataColour("#A6C9FF"), marker("#F6C875");

wxString timeLabel(double ns) {
    if (ns >= 1e9) return wxString::Format("%.6g s", ns / 1e9);
    if (ns >= 1e6) return wxString::Format("%.6g ms", ns / 1e6);
    if (ns >= 1e3) return wxString::Format(U("%.6g µs"), ns / 1e3);
    return wxString::Format("%.6g ns", ns);
}
wxArrayString rateLabels() {
    wxArrayString labels;
    for (auto rate : rates)
        labels.Add(wxString::Format(U("%u 拍/秒"), rate));
    return labels;
}
int rateIndex(unsigned rate) {
    return int(std::find(std::begin(rates), std::end(rates), rate) - std::begin(rates));
}
struct WaveSignal {
    Endpoint endpoint;
    std::string label;
    bool clock = false;
};
struct WavePoint {
    uint64_t tick, event;
    Signal value;
    bool initial = false;
};
struct WaveChannel {
    WaveSignal signal;
    std::deque<WavePoint> points;
    bool truncated = false;
};
std::string portLabel(const Part &part, const Port &port, const std::string &scope) {
    const auto &name = part.label.empty() ? part.id : part.label;
    const bool simple = part.kind == "Input" || part.kind == "Clock" || part.kind == "Output" || part.kind == "Probe";
    return (scope.empty() ? "" : scope + "/") + name + (simple ? "" : "." + port.id);
}
} // namespace

class WaveformWindow : public wxFrame {
  public:
    explicit WaveformWindow(LogicEditor &editor);
    void reset(bool clear);
    void begin();
    bool add(WaveSignal signal);
    void update();

  private:
    LogicEditor &editor;
    wxPanel *graph;
    wxButton *run, *details;
    wxChoice *speed;
    wxToggleButton *followButton;
    wxScrollBar *vertical;
    wxStaticText *summary, *timeScale;
    std::vector<WaveChannel> channels;
    std::map<int, size_t> networks;
    uint64_t captureStart = 0, latest = 0;
    std::optional<uint64_t> cursorA, cursorB;
    bool capturing = false, follow = true;
    double first = 0, span = 16;
    int rowOffset = 0;
    std::optional<size_t> selected;
    std::optional<std::pair<int, double>> drag;

    int dip(int size) const { return graph->FromDIP(size); }
    int gutter() const { return dip(220); }
    int header() const { return dip(34); }
    int rowHeight() const { return dip(35); }
    int visibleRows() const { return std::max(1, (graph->GetClientSize().y - header()) / rowHeight()); }
    double plotWidth() const { return std::max(1, graph->GetClientSize().x - gutter() - dip(12)); }
    double xAt(double tick) const { return gutter() + (tick - first) * plotWidth() / span; }
    double tickAt(int x) const { return first + (x - gutter()) * span / plotWidth(); }
    void bindNetworks();
    void setFollow(bool enabled);
    void fit();
    void zoom(double factor, int x);
    void paint();
    void selectCursor(wxMouseEvent &event);
    void showChanges();
    void removeChannel(size_t index);
    friend class LogicEditor;
};

WaveformWindow::WaveformWindow(LogicEditor &owner)
    : wxFrame(&owner, wxID_ANY, U("时序波形"), wxDefaultPosition, wxDefaultSize,
              wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT | wxFRAME_TOOL_WINDOW), editor(owner) {
    SetName("timing-waveform");
    auto *panel = new wxPanel(this);
    panel->SetBackgroundColour(paper);
    auto *layout = new wxBoxSizer(wxVERTICAL);
    auto *tools = new wxBoxSizer(wxHORIZONTAL);
    auto button = [&](const char *label, std::function<void()> action) {
        auto *control = new wxButton(panel, wxID_ANY, U(label), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        tools->Add(control, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5));
        control->Bind(wxEVT_BUTTON, [this, action = std::move(action)](wxCommandEvent &) {
            try { action(); }
            catch (const std::exception &error) { editor.fail(error); }
        });
        return control;
    };
    run = button("运行", [this] { editor.togglePlayback(); });
    run->SetMinSize(FromDIP(wxSize(74, -1)));
    button("单步", [this] { editor.stop(); editor.tick(); });
    button("复位", [this] { editor.resetSimulation(); });
    tools->AddSpacer(FromDIP(7));
    speed = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, rateLabels());
    speed->SetToolTip(U("播放速度"));
    tools->Add(speed, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    speed->Bind(wxEVT_CHOICE, [this](wxCommandEvent &event) { editor.setPlaybackRate(rates[event.GetSelection()]); });
    tools->AddStretchSpacer();
    button("缩小", [this] { zoom(2.0, gutter() + int(plotWidth() / 2)); });
    button("放大", [this] { zoom(0.5, gutter() + int(plotWidth() / 2)); });
    button("全览", [this] { fit(); });
    followButton = new wxToggleButton(panel, wxID_ANY, U("跟随最新"));
    followButton->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &event) { setFollow(event.IsChecked()); update(); });
    tools->Add(followButton, 0, wxALIGN_CENTER_VERTICAL);
    layout->Add(tools, 0, wxEXPAND | wxALL, FromDIP(8));
    tools = new wxBoxSizer(wxHORIZONTAL);
    button("+ 添加信号", [this] { editor.chooseWaveformSignals(); });
    tools->AddStretchSpacer();
    timeScale = new wxStaticText(panel, wxID_ANY, wxEmptyString);
    timeScale->SetForegroundColour(ink);
    tools->Add(timeScale, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    details = button("当拍变化", [this] { showChanges(); });
    layout->Add(tools, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    auto *body = new wxBoxSizer(wxHORIZONTAL);
    graph = new wxPanel(panel);
    graph->SetName("waveform-plot");
    graph->SetBackgroundStyle(wxBG_STYLE_PAINT);
    vertical = new wxScrollBar(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL);
    body->Add(graph, 1, wxEXPAND);
    body->Add(vertical, 0, wxEXPAND);
    layout->Add(body, 1, wxEXPAND);
    summary = new wxStaticText(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    summary->SetForegroundColour(ink);
    layout->Add(summary, 0, wxEXPAND | wxALL, FromDIP(8));
    panel->SetSizer(layout);
    auto *frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(panel, 1, wxEXPAND);
    SetSizer(frameSizer);

    const int display = wxDisplay::GetFromWindow(&editor);
    const auto work = wxDisplay(display == wxNOT_FOUND ? 0 : unsigned(display)).GetClientArea();
    const auto desired = FromDIP(wxSize(1000, 500));
    const int margin = FromDIP(12);
    SetSize(work.x + std::max(margin, (work.width - desired.x) / 2),
            work.GetBottom() - std::min(desired.y, work.height - 2 * margin) - margin,
            std::min(desired.x, work.width - 2 * margin), std::min(desired.y, work.height - 2 * margin));
    auto minimum = FromDIP(wxSize(700, 265));
    SetMinSize({std::min(minimum.x, GetSize().x), std::min(minimum.y, GetSize().y)});

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &event) {
        if (!event.CanVeto()) { event.Skip(); return; }
        event.Veto();
        Hide();
    });
    auto commands = [this](wxKeyEvent &event) {
        try {
            switch (event.GetKeyCode()) {
            case WXK_F5: editor.togglePlayback(); return;
            case WXK_F7: editor.stop(); editor.simulator->step(); editor.rebuild(false); return;
            case WXK_F8: editor.stop(); editor.tick(); return;
            case WXK_DELETE:
                if (selected) { removeChannel(*selected); return; }
                break;
            }
        } catch (const std::exception &error) { editor.fail(error); return; }
        event.Skip();
    };
    Bind(wxEVT_CHAR_HOOK, commands);
    graph->Bind(wxEVT_PAINT, [this](wxPaintEvent &) { paint(); });
    graph->Bind(wxEVT_SIZE, [this](wxSizeEvent &event) { update(); event.Skip(); });
    graph->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) { selectCursor(event); });
    graph->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent &event) {
        const int y = event.GetY() - header();
        if (y < 0 || event.GetX() >= gutter()) return;
        const auto index = size_t(rowOffset + y / rowHeight());
        if (index >= channels.size()) return;
        wxMenu menu;
        menu.Append(wxID_REMOVE, U("移除信号"));
        menu.Bind(wxEVT_MENU, [this, index](wxCommandEvent &) { removeChannel(index); }, wxID_REMOVE);
        graph->PopupMenu(&menu);
    });
    graph->Bind(wxEVT_MIDDLE_DOWN, [this](wxMouseEvent &event) {
        drag = std::make_pair(event.GetX(), first);
        graph->CaptureMouse();
        setFollow(false);
        update();
    });
    graph->Bind(wxEVT_MIDDLE_UP, [this](wxMouseEvent &) {
        drag.reset();
        if (graph->HasCapture()) graph->ReleaseMouse();
    });
    graph->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { drag.reset(); });
    graph->Bind(wxEVT_MOTION, [this](wxMouseEvent &event) {
        if (drag) {
            first = drag->second + (drag->first - event.GetX()) * span / plotWidth();
            update();
        }
    });
    graph->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &event) {
        if (event.ControlDown()) {
            zoom(event.GetWheelRotation() > 0 ? 0.5 : 2.0, event.GetX());
        } else if (event.ShiftDown()) {
            setFollow(false);
            first += (event.GetWheelRotation() > 0 ? -0.2 : 0.2) * span;
            update();
        } else {
            rowOffset += event.GetWheelRotation() > 0 ? -3 : 3;
            update();
        }
    });
    vertical->Bind(wxEVT_SCROLL_CHANGED, [this](wxScrollEvent &event) { rowOffset = event.GetPosition(); update(); });
    vertical->Bind(wxEVT_SCROLL_THUMBTRACK, [this](wxScrollEvent &event) { rowOffset = event.GetPosition(); update(); });
    vertical->Bind(wxEVT_SCROLL_LINEUP, [this](wxScrollEvent &event) { rowOffset = event.GetPosition(); update(); });
    vertical->Bind(wxEVT_SCROLL_LINEDOWN, [this](wxScrollEvent &event) { rowOffset = event.GetPosition(); update(); });
    vertical->Bind(wxEVT_SCROLL_PAGEUP, [this](wxScrollEvent &event) { rowOffset = event.GetPosition(); update(); });
    vertical->Bind(wxEVT_SCROLL_PAGEDOWN, [this](wxScrollEvent &event) { rowOffset = event.GetPosition(); update(); });
}

void WaveformWindow::bindNetworks() {
    networks.clear();
    auto &sim = *editor.simulator;
    for (size_t i = 0; i < channels.size(); ++i)
        if (const auto network = sim.network(channels[i].signal.endpoint))
            networks.emplace(*network, i);
    if (networks.empty()) {
        sim.setNetworkSink({});
        return;
    }
    sim.setNetworkSink([this](int network, uint64_t tick, uint64_t event, const Signal &signal) {
        const auto found = networks.find(network);
        if (found == networks.end()) return;
        auto &channel = channels[found->second];
        if (channel.points.size() == maxChanges + 1) {
            channel.points.pop_front();
            channel.truncated = true;
        }
        channel.points.push_back({tick, event, signal});
    });
}
bool WaveformWindow::add(WaveSignal signal) {
    auto &sim = *editor.simulator;
    const auto net = sim.network(signal.endpoint);
    if (!net || networks.count(*net) || channels.size() >= maxChannels)
        return false;
    const auto value = sim.read(signal.endpoint);
    channels.push_back({std::move(signal), {{sim.ticks(), sim.time(), value, true}}, false});
    networks.emplace(*net, channels.size() - 1);
    return true;
}
void WaveformWindow::begin() {
    if (capturing) return;
    capturing = true;
    captureStart = latest = editor.simulator->ticks();
    first = double(latest);
    const auto &project = editor.history.project;
    auto addClocks = [&](const auto &walk, const Circuit &circuit, const std::string &scope) -> void {
        for (const auto &part : circuit.parts) {
            if (channels.size() >= 16) break;
            const auto path = scope.empty() ? part.id : scope + "/" + part.id;
            if (part.kind == "Clock")
                add({{path, "Y"}, portLabel(part, {"Y", true, 1, {}}, scope), true});
            if (part.kind == "Subcircuit")
                walk(walk, project.circuit(part.circuit), path);
        }
    };
    const auto &root = project.circuit(editor.simulationRoot);
    addClocks(addClocks, root, "");
    for (const auto &part : root.parts) {
        if (channels.size() >= 16) break;
        if (part.kind == "Input" || part.kind == "Output" || part.kind == "Probe") {
            const auto port = ports(project, part).front();
            add({{part.id, port.id}, portLabel(part, port, ""), false});
        }
    }
    bindNetworks();
}
void WaveformWindow::reset(bool clear) {
    editor.simulator->setNetworkSink({});
    networks.clear();
    cursorA.reset();
    cursorB.reset();
    selected.reset();
    rowOffset = 0;
    captureStart = latest = editor.simulator->ticks();
    first = double(latest);
    span = 16;
    setFollow(true);
    if (clear) {
        channels.clear();
        capturing = false;
        Hide();
    } else if (capturing) {
        auto previous = std::move(channels);
        channels.clear();
        for (auto &channel : previous)
            add(std::move(channel.signal));
        bindNetworks();
    }
    update();
}
void WaveformWindow::removeChannel(size_t index) {
    channels.erase(channels.begin() + std::ptrdiff_t(index));
    selected.reset();
    bindNetworks();
    update();
}
void WaveformWindow::setFollow(bool enabled) {
    follow = enabled;
    followButton->SetValue(enabled);
}
void WaveformWindow::fit() {
    setFollow(false);
    first = double(captureStart);
    span = std::max(2.0, double(latest - captureStart) + 1);
    update();
}
void WaveformWindow::zoom(double factor, int x) {
    const double anchor = tickAt(x);
    span = std::clamp(span * factor, 2.0, std::max(16.0, double(latest - captureStart) + 1));
    first = anchor - double(x - gutter()) * span / plotWidth();
    setFollow(false);
    update();
}
void WaveformWindow::update() {
    latest = editor.simulator->ticks();
    const auto end = std::max(double(captureStart), double(latest) + 1 - span);
    first = follow ? end : std::clamp(first, double(captureStart), end);
    if (!IsShown()) return;
    const auto title = U("时序波形 · " + editor.history.project.name + " / " + editor.simulationRoot);
    if (GetTitle() != title) SetTitle(title);
    speed->SetSelection(rateIndex(editor.playbackRate));
    const bool running = editor.timer.IsRunning();
    run->SetLabel(U(editor.batchRemaining ? "取消批量" : running ? "暂停" : "运行"));
    run->Enable(running || !editor.simulator->oscillating());
    const int rows = visibleRows();
    const int range = std::max(rows, int(channels.size()));
    rowOffset = std::clamp(rowOffset, 0, range - rows);
    vertical->SetScrollbar(rowOffset, rows, range, rows);
    details->Enable(cursorA.has_value() || !channels.empty());
    const auto duration = double(editor.history.project.tickDurationNs);
    const auto scaleLabel = U("仿真时间   ") + timeLabel(duration) + U("/拍");
    if (timeScale->GetLabel() != scaleLabel) {
        timeScale->SetLabel(scaleLabel);
        timeScale->GetParent()->Layout();
    }
    wxString info;
    if (editor.simulator->oscillating()) info = U("仿真已停止 · 事件预算耗尽");
    else if (editor.batchRemaining)
        info = wxString::Format(U("批量 %ld / %ld"), editor.batchTotal - editor.batchRemaining, editor.batchTotal);
    else info = U(running ? "运行中" : "已暂停");
    info += wxString::Format(U("   ·   第 %llu 拍"), static_cast<unsigned long long>(latest));
    if (cursorA) info += "     A " + timeLabel(double(*cursorA) * duration);
    if (cursorB) info += "     B " + timeLabel(double(*cursorB) * duration);
    if (cursorA && cursorB) {
        const auto delta = std::max(*cursorA, *cursorB) - std::min(*cursorA, *cursorB);
        info += U("     Δt ") + timeLabel(double(delta) * duration);
    }
    if (std::any_of(channels.begin(), channels.end(), [](const auto &channel) { return channel.truncated; }))
        info += U("     ·   早期记录已截断");
    summary->SetLabel(info);
    graph->Refresh(false);
}

void WaveformWindow::paint() {
    wxAutoBufferedPaintDC dc(graph);
    dc.SetBackground(wxBrush(plot));
    dc.Clear();
    const auto area = graph->GetClientSize();
    std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::Create(dc));
    if (!graphics) return;
    auto &g = *graphics;
    const auto ui = graph->GetFont();
    const wxFont mono(wxFontInfo(std::max(10, ui.GetPointSize())).Family(wxFONTFAMILY_TELETYPE));
    const double gutterX = gutter(), heading = header(), row = rowHeight();
    const double edge = gutterX + plotWidth();
    const double valueX = gutterX - dip(69);
    auto rect = [&](double x, double y, double w, double h, const wxColour &colour) {
        g.SetPen(*wxTRANSPARENT_PEN);
        g.SetBrush(wxBrush(colour));
        g.DrawRectangle(x, y, w, h);
    };
    auto line = [&](double x1, double y1, double x2, double y2, const wxColour &colour,
                    wxPenStyle style = wxPENSTYLE_SOLID, int width = 1) {
        g.SetPen(wxPen(colour, width, style));
        g.StrokeLine(x1, y1, x2, y2);
    };
    auto write = [&](const wxString &text, double x, double y, const wxFont &font, const wxColour &colour) {
        g.SetFont(font, colour);
        g.DrawText(text, x, y);
    };
    rect(0, 0, gutterX, area.y, paper);
    rect(gutterX, 0, edge - gutterX, heading, wxColour("#202F3F"));
    const auto lastRow = std::min(int(channels.size()), rowOffset + visibleRows() + 1);
    for (int index = rowOffset; index < lastRow; ++index) {
        const double y = heading + (index - rowOffset) * row;
        if (selected && *selected == size_t(index))
            rect(0, y, gutterX, row, wxColour("#DCE8F6"));
        if (index % 2 != 0)
            rect(gutterX, y, area.x - gutterX, row, wxColour("#1C2B39"));
        line(0, y + row, gutterX, y + row, wxColour("#D6DFE8"));
        line(gutterX, y + row, area.x, y + row, grid);
    }
    line(0, heading, gutterX, heading, wxColour("#D0DAE5"));
    write(U("信号"), dip(15), dip(8), ui, ink);
    write(cursorA ? U("游标值") : U("当前值"), valueX, dip(8), ui, ink);

    const auto duration = double(editor.history.project.tickDurationNs);
    const double target = std::max(1.0, span * dip(92) / plotWidth());
    const double power = std::pow(10., std::floor(std::log10(target)));
    const double normalized = target / power;
    const double step = (normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 5 ? 5 : 10) * power;
    for (double tick = std::ceil(first / step) * step; tick <= first + span; tick += step) {
        const double x = xAt(tick);
        line(x, heading, x, area.y, grid);
        if (x + dip(74) < area.x)
            write(timeLabel(tick * duration), x + dip(3), dip(8), mono, plotText);
        if (tick + step == tick) break;
    }
    if (channels.empty())
        write(U("添加信号后可查看时序波形"), gutterX + dip(19), heading + dip(20), ui, plotText);

    const auto readTick = cursorA.value_or(latest);
    for (int index = rowOffset; index < lastRow; ++index) {
        const auto &channel = channels[size_t(index)];
        const auto &points = channel.points;
        const auto colour = channel.signal.clock ? clockColour : dataColour;
        const double top = heading + (index - rowOffset) * row;
        const double centre = top + row / 2;
        const auto read = std::upper_bound(points.begin(), points.end(), readTick,
                                           [](uint64_t tick, const WavePoint &point) { return tick < point.tick; });
        rect(dip(6), top + dip(8), dip(3), row - dip(16), channel.signal.clock ? wxColour("#247B82") : wxColour("#4774AC"));
        const auto name = wxControl::Ellipsize(U(channel.signal.label), dc, wxELLIPSIZE_END, int(valueX) - dip(22));
        write(name, dip(15), top + dip(7), ui, ink);
        dc.SetFont(mono);
        const auto value = read == points.begin() ? U("—") : U(std::prev(read)->value.text());
        write(wxControl::Ellipsize(value, dc, wxELLIPSIZE_END, dip(62)), valueX, top + dip(7), mono, ink);
        dc.SetFont(ui);

        auto it = std::lower_bound(points.begin(), points.end(), first,
                                   [](const WavePoint &point, double tick) { return double(point.tick) < tick; });
        if (it != points.begin()) --it;
        g.Clip(gutterX, top, edge - gutterX, row);
        auto level = [&](const Signal &signal) {
            if (!signal.defined()) return centre;
            return centre + (signal.value ? -dip(8) : dip(8));
        };
        for (; it != points.end() && double(it->tick) <= first + span; ++it) {
            const auto next = std::next(it);
            const double start = std::max(gutterX, xAt(double(it->tick)));
            const auto until = next == points.end() ? double(latest) + 1 : double(next->tick);
            const double end = std::min(edge, xAt(until));
            const auto &signal = it->value;
            const bool defined = signal.defined();
            const auto stroke = signal.error ? wxColour("#FFA3AC") : colour;
            const auto style = defined ? wxPENSTYLE_SOLID : signal.error ? wxPENSTYLE_LONG_DASH
                                        : signal.z ? wxPENSTYLE_DOT : wxPENSTYLE_SHORT_DASH;
            if (signal.width > 1) {
                const int height = dip(8);
                line(start, centre - height, end, centre - height, stroke, style);
                line(start, centre + height, end, centre + height, stroke, style);
                if (it != points.begin())
                    line(start, centre - height, start, centre + height, stroke);
                if (end - start > dip(36)) {
                    const auto label = wxControl::Ellipsize(U(signal.text()), dc, wxELLIPSIZE_END,
                                                           int(end - start) - dip(8));
                    write(label, start + dip(4), centre - dip(9), mono, stroke);
                }
            } else {
                const double y = level(signal);
                if (it != points.begin() && double(it->tick) >= first)
                    line(start, level(std::prev(it)->value), start, y, stroke, wxPENSTYLE_SOLID, dip(2));
                line(start, y, end, y, stroke, style, dip(2));
                if (!defined && end - start > dip(15))
                    write(U(signal.text()), start + dip(4), top + dip(2), mono, stroke);
            }
            if (next != points.end() && next->tick == it->tick)
                rect(start - dip(2), top + dip(3), dip(4), dip(4), marker);
        }
        g.ResetClip();
    }
    auto cursor = [&](std::optional<uint64_t> tick, const char *label) {
        if (!tick) return;
        const double x = xAt(double(*tick));
        if (x < gutterX || x > edge) return;
        line(x, heading - dip(4), x, area.y, marker, label[0] == 'B' ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID);
        rect(x - dip(7), heading - dip(16), dip(14), dip(16), marker);
        write(U(label), x - dip(4), heading - dip(17), mono, plot);
    };
    cursor(cursorA, "A");
    cursor(cursorB, "B");
}

void WaveformWindow::selectCursor(wxMouseEvent &event) {
    graph->SetFocus();
    if (event.GetX() < gutter()) {
        if (event.GetY() < header()) return;
        const auto index = size_t(rowOffset + (event.GetY() - header()) / rowHeight());
        selected = index < channels.size() ? std::optional<size_t>(index) : std::nullopt;
        if (selected)
            graph->SetToolTip(U(channels[*selected].signal.label + " · Delete 移除信号"));
        update();
        return;
    }
    graph->SetToolTip(U("点击放置 A，Shift＋点击放置 B。Ctrl＋滚轮缩放，中键拖动平移。"));
    const double target = std::clamp(tickAt(event.GetX()), double(captureStart), double(latest));
    uint64_t tick = uint64_t(std::round(target));
    double best = dip(8);
    for (const auto &channel : channels) {
        if (selected && &channel != &channels[*selected]) continue;
        const auto &points = channel.points;
        const auto next = std::lower_bound(points.begin(), points.end(), target,
                                            [](const WavePoint &point, double t) { return double(point.tick) < t; });
        auto snap = [&](const WavePoint &point) {
            const auto distance = std::abs(xAt(double(point.tick)) - event.GetX());
            if (!point.initial && distance < best) {
                best = distance;
                tick = point.tick;
            }
        };
        if (next != points.end()) snap(*next);
        if (next != points.begin()) snap(*std::prev(next));
    }
    if (event.ShiftDown()) cursorB = tick;
    else cursorA = tick;
    setFollow(false);
    update();
}
void WaveformWindow::showChanges() {
    const auto tick = cursorA.value_or(latest);
    std::vector<std::tuple<uint64_t, std::string, std::string>> changes;
    for (const auto &channel : channels) {
        const auto &points = channel.points;
        auto it = std::lower_bound(points.begin(), points.end(), tick,
                                   [](const WavePoint &point, uint64_t t) { return point.tick < t; });
        for (; it != points.end() && it->tick == tick; ++it)
            if (!it->initial) changes.emplace_back(it->event, channel.signal.label, it->value.text());
    }
    std::sort(changes.begin(), changes.end());
    std::string report = "传播批次\t信号\t值\n";
    if (changes.empty()) report += "这一拍没有可用的跳变记录。\n";
    for (const auto &[batch, label, value] : changes)
        report += std::to_string(batch) + "\t" + label + "\t" + value + "\n";
    editor.showReport("第 " + std::to_string(tick) + " 拍 · "
                          + utf8(timeLabel(double(tick) * editor.history.project.tickDurationNs)), report, this);
}

void LogicEditor::startPlayback() {
    timer.Start(batchRemaining ? 1 : int(1000 / playbackRate));
}
void LogicEditor::setPlaybackRate(unsigned rate) {
    playbackRate = rate;
    if (timer.IsRunning() && !batchRemaining)
        startPlayback();
    refreshWaveform();
}
void LogicEditor::playbackSettings() {
    wxSingleChoiceDialog dialog(this, U("选择每秒推进的基础节拍数。"), U("播放速度"), rateLabels());
    dialog.SetSelection(rateIndex(playbackRate));
    if (dialog.ShowModal() == wxID_OK)
        setPlaybackRate(rates[dialog.GetSelection()]);
}
void LogicEditor::togglePlayback() {
    if (timer.IsRunning()) {
        stop();
    } else {
        openWaveform(true);
        if (!simulator->oscillating()) startPlayback();
    }
    rebuild(false);
}
void LogicEditor::advanceTick() {
    if (simulator->oscillating()) return;
    if (simulator->pending() && !simulator->settle()) return;
    simulator->tick();
}
void LogicEditor::requestBatch() {
    wxTextEntryDialog dialog(this, U("推进多少个基础节拍？（1–100000）"), U("运行指定节拍"), "10");
    long count;
    if (dialog.ShowModal() != wxID_OK) return;
    if (!dialog.GetValue().ToLong(&count) || count < 1 || count > 100000)
        throw std::runtime_error("节拍数应为 1–100000");
    stop();
    openWaveform(true);
    if (!simulator->oscillating()) {
        batchTotal = batchRemaining = count;
        startPlayback();
    }
    rebuild(false);
}
void LogicEditor::simulationTimer() {
    if (!batchRemaining) {
        if (timer.IsRunning()) tick();
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(12);
    for (int i = 0; i < 128 && batchRemaining; ++i) {
        advanceTick();
        if (--batchRemaining == 0 || simulator->oscillating()) { stop(); break; }
        if (std::chrono::steady_clock::now() >= deadline) break;
    }
    rebuild(false);
}
void LogicEditor::resetSimulation() {
    stop();
    simulator->setNetworkSink({});
    simulator->reset();
    simulator->settle();
    resetWaveform(false);
    rebuild(false);
}
void LogicEditor::openWaveform(bool automatic) {
    if (automatic && (waveformOpened || !simulator->hasTimingDevices())) return;
    if (!waveform) waveform = new WaveformWindow(*this);
    waveformOpened = true;
    waveform->begin();
    if (automatic) {
#ifdef __WXMSW__
        if (!waveform->IsShown()) waveform->ShowWithoutActivating();
#else
        waveform->Show();
#endif
    } else {
        waveform->Show();
        waveform->Raise();
    }
    waveform->update();
}
void LogicEditor::resetWaveform(bool clear) {
    waveformOpened = false;
    if (waveform) waveform->reset(clear);
}
void LogicEditor::refreshWaveform() {
    if (waveform) waveform->update();
}
void LogicEditor::addWaveformSignal(const Endpoint &endpoint) {
    std::string label = scoped(endpoint.object) + (endpoint.pin.empty() ? "" : "." + endpoint.pin);
    bool clock = false;
    if (const auto *part = circuit().part(endpoint.object)) {
        for (const auto &port : ports(history.project, *part))
            if (port.id == endpoint.pin) label = portLabel(*part, port, scope);
        clock = part->kind == "Clock";
    }
    const Endpoint resolved{scoped(endpoint.object), endpoint.pin};
    if (!simulator->network(resolved))
        throw std::runtime_error("无法在当前仿真中找到这个信号");
    openWaveform();
    const auto net = *simulator->network(resolved);
    if (!waveform->networks.count(net) && waveform->channels.size() >= maxChannels)
        throw std::runtime_error("时序波形最多可查看 32 个信号。移除一个信号后再添加。");
    waveform->add({resolved, std::move(label), clock});
    waveform->bindNetworks();
    waveform->selected = waveform->networks.at(net);
    waveform->rowOffset = int(*waveform->selected);
    waveform->update();
}
void LogicEditor::chooseWaveformSignals() {
    wxArrayString choices;
    std::vector<WaveSignal> signals;
    for (const auto &part : circuit().parts) {
        for (const auto &port : ports(history.project, part)) {
            Endpoint endpoint{scoped(part.id), port.id};
            if (!simulator->network(endpoint)) continue;
            const auto label = portLabel(part, port, scope);
            choices.Add(U(label + "    [" + endpoint.key() + "]    " + std::to_string(port.width) + " 位"));
            signals.push_back({std::move(endpoint), label, part.kind == "Clock"});
        }
    }
    if (signals.empty()) {
        wxMessageBox(U("当前电路没有可观察的引脚。"), U("添加信号"), wxOK | wxICON_INFORMATION, waveform);
        return;
    }
    wxMultiChoiceDialog dialog(waveform, U("从当前电路或运行实例选择引脚。连接到同一网络的引脚合并为一个通道。"),
                               U("添加波形信号"), choices);
    if (dialog.ShowModal() != wxID_OK) return;
    bool full = false;
    for (const int index : dialog.GetSelections()) {
        const auto &signal = signals[size_t(index)];
        const auto net = *simulator->network(signal.endpoint);
        if (waveform->networks.count(net)) continue;
        if (waveform->channels.size() == maxChannels) { full = true; break; }
        waveform->add(signal);
    }
    waveform->bindNetworks();
    waveform->update();
    if (full)
        wxMessageBox(U("已达到 32 通道上限。可在信号列表中右键移除通道。"), U("添加信号"), wxOK | wxICON_INFORMATION, waveform);
}

void LogicEditor::clockSettings() {
    if (!scope.empty() || canvas->selected.size() != 1) return;
    const auto id = *canvas->selected.begin();
    const auto *part = circuit().part(id);
    if (!part || part->kind != "Clock") return;
    const auto initial = clockTiming(*part);
    const auto duration = history.project.tickDurationNs;
    wxDialog dialog(this, wxID_ANY, U("时钟设置 · " + (part->label.empty() ? id : part->label)));
    dialog.SetName("clock-settings-dialog");
    auto *layout = new wxBoxSizer(wxVERTICAL);
    auto *fields = new wxFlexGridSizer(2, FromDIP(9), FromDIP(10));
    fields->AddGrowableCol(1);
    auto *frequencyRow = new wxBoxSizer(wxHORIZONTAL);
    const double factors[] = {1, 1000, 1000000};
    const auto hz = initial.frequencyHz(duration);
    const int unitIndex = hz >= factors[2] ? 2 : hz >= factors[1] ? 1 : 0;
    auto *frequency = new wxTextCtrl(&dialog, wxID_ANY, wxString::Format("%.16g", hz / factors[unitIndex]));
    frequency->SetName("clock-frequency");
    wxArrayString units;
    for (const auto *unit : {"Hz", "kHz", "MHz"}) units.Add(U(unit));
    auto *unit = new wxChoice(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, units);
    unit->SetSelection(unitIndex);
    frequencyRow->Add(frequency, 1, wxEXPAND | wxRIGHT, FromDIP(6));
    frequencyRow->Add(unit, 0, wxEXPAND);
    fields->Add(new wxStaticText(&dialog, wxID_ANY, U("频率")), 0, wxALIGN_CENTER_VERTICAL);
    fields->Add(frequencyRow, 1, wxEXPAND);
    fields->Add(new wxStaticText(&dialog, wxID_ANY, U("高电平占空比 (%)")), 0, wxALIGN_CENTER_VERTICAL);
    auto *duty = new wxTextCtrl(&dialog, wxID_ANY, wxString::Format("%.16g", initial.dutyPercent()));
    duty->SetName("clock-duty");
    fields->Add(duty, 1, wxEXPAND);
    layout->Add(fields, 0, wxEXPAND | wxALL, FromDIP(14));
    auto *preview = new wxStaticText(&dialog, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(405, 76)));
    preview->SetName("clock-timing-preview");
    layout->Add(preview, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(14));
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(12));
    dialog.SetSizerAndFit(layout);
    std::optional<ClockTiming> proposed;
    auto update = [&] {
        try {
            double number = 0, percent = 0;
            if (!frequency->GetValue().ToDouble(&number) || !duty->GetValue().ToDouble(&percent))
                throw std::runtime_error("请输入有效的频率和占空比");
            proposed = quantizeClock(number * factors[unit->GetSelection()], percent, duration);
            const double actual = proposed->frequencyHz(duration);
            const int index = actual >= factors[2] ? 2 : actual >= factors[1] ? 1 : 0;
            preview->SetForegroundColour(ink);
            preview->SetLabel(wxString::Format(U("实际 %.9g %s  /  %.8g%%\n高 %lld 拍 · 低 %lld 拍\n应用后暂停并复位仿真。"),
                actual / factors[index], units[index], proposed->dutyPercent(),
                static_cast<long long>(proposed->highTicks), static_cast<long long>(proposed->lowTicks)));
        } catch (const std::exception &error) {
            proposed.reset();
            preview->SetForegroundColour(wxColour("#A21E35"));
            preview->SetLabel(U(error.what()));
            preview->Wrap(preview->GetSize().x);
        }
        dialog.FindWindow(wxID_OK)->Enable(proposed.has_value());
    };
    frequency->Bind(wxEVT_TEXT, [&](wxCommandEvent &) { update(); });
    unit->Bind(wxEVT_CHOICE, [&](wxCommandEvent &) { update(); });
    duty->Bind(wxEVT_TEXT, [&](wxCommandEvent &) { update(); });
    update();
    frequency->SetFocus();
    frequency->SelectAll();
    if (dialog.ShowModal() != wxID_OK || !proposed) return;
    if (initial.highTicks == proposed->highTicks && initial.lowTicks == proposed->lowTicks) return;
    change([&] {
        auto *clock = circuit().part(id);
        clock->params["highTicks"] = proposed->highTicks;
        clock->params["lowTicks"] = proposed->lowTicks;
    });
}

void LogicEditor::timingSettings() {
    const auto previous = history.project.tickDurationNs;
    wxTextEntryDialog dialog(this,
        U("输入每拍对应的仿真纳秒数（1–1000000000）。\n更改会重新标定所有时钟的频率，并暂停、复位仿真。"),
        U("仿真时间分辨率"), wxString::Format("%u", previous));
    if (dialog.ShowModal() != wxID_OK) return;
    unsigned long duration;
    if (!dialog.GetValue().ToULong(&duration) || duration < 1 || duration > 1000000000)
        throw std::runtime_error("仿真每拍时长须为 1–1000000000 纳秒");
    if (duration != previous)
        change([&] { history.project.tickDurationNs = uint32_t(duration); });
}

void LogicEditor::runTimingSmoke(const std::filesystem::path &folder) {
    const auto fixtureDirectory = std::filesystem::path(__FILE__).parent_path().parent_path()
                                  / "docs" / "timing-demo-20261105";
    auto require = [](bool condition, const char *what) {
        if (!condition) throw std::runtime_error(what);
    };
    auto command = [&](const char *label) {
        const int id = GetMenuBar()->FindMenuItem(U("仿真"), U(label));
        require(id != wxNOT_FOUND, "timing menu entry missing");
        wxCommandEvent event(wxEVT_MENU, id);
        GetEventHandler()->ProcessEvent(event);
    };
    auto fixture = [&](const char *name) {
        resetWaveform(true);
        history.reset(deserialize(readFile(fixtureDirectory / name)));
        current = history.project.main;
        canvas->selected.clear();
        rebuild(true, true);
    };
    auto replyToBatch = [](const wxString &count) {
        wxTheApp->CallAfter([count] {
            for (auto *window : wxTopLevelWindows) {
                auto *dialog = dynamic_cast<wxTextEntryDialog *>(window);
                if (dialog && dialog->IsModal() && dialog->GetTitle() == U("运行指定节拍")) {
                    dialog->SetValue(count);
                    dialog->EndModal(wxID_OK);
                    break;
                }
            }
        });
    };
    if (std::filesystem::exists(fixtureDirectory / "02-dual-clock-counter.logic.json")) {
        fixture("02-dual-clock-counter.logic.json");
        command("单步传播");
        require(!waveform || !waveform->IsShown(), "F7 opened the timing window");
        command("时钟基础节拍");
        require(waveform && waveform->IsShown() && simulator->ticks() == 1, "F8 did not open waveform");
        require(waveform->Close() == false && !waveform->IsShown(), "closing waveform should hide it");
        command("时钟基础节拍");
        require(!waveform->IsShown() && simulator->ticks() == 2, "second F8 reopened hidden waveform");
        command("时序波形");
        require(waveform->IsShown(), "waveform menu did not restore capture");
        const auto fastNet = *simulator->network({"FAST", "Y"});
        require(waveform->channels[waveform->networks.at(fastNet)].points.back().tick == 2,
                "closing waveform interrupted capture");
        setPlaybackRate(10);
        waveform->Close();
        command("复位");
        require(simulator->pending() == 0 && simulator->ticks() == 0, "reset did not settle tick zero");
        command("运行 / 暂停");
        require(waveform->IsShown() && timer.IsRunning() && timer.GetInterval() == 100, "F5 timing playback");
        command("运行 / 暂停");
        waveform->Close();
        command("复位");
        const auto csv = folder / "timing-events.csv";
        traceFile = std::make_shared<std::ofstream>(csv, std::ios::binary | std::ios::trunc);
        monitoredSignals = {"FAST:Y"};
        simulator->setTraceSink([this](const Trace &event) { writeTrace(event); });
        replyToBatch("8");
        command("运行指定节拍…");
        require(waveform->IsShown() && batchRemaining == 8, "batch did not open waveform");
        while (batchRemaining) simulationTimer();
        require(simulator->ticks() == 8 && simulator->read({"Q_FAST", "A"}).text() == "0x4" &&
                    simulator->read({"Q_SLOW", "A"}).text() == "0x2", "dual clock capture output mismatch");
        simulator->setTraceSink({});
        traceFile.reset();
        monitoredSignals.clear();
        require(readFile(csv).find("\"FAST\",\"Y\",\"1\"") != std::string::npos, "CSV did not capture alongside waveform");
        auto risingPeriod = [&](const char *id) {
            const auto network = *simulator->network({id, "Y"});
            const auto &points = waveform->channels[waveform->networks.at(network)].points;
            std::vector<uint64_t> rises;
            for (const auto &point : points)
                if (!point.initial && point.value == Signal::number(1, 1)) rises.push_back(point.tick);
            require(rises.size() >= 2, "wave did not capture two clock edges");
            return (rises[1] - rises[0]) * history.project.tickDurationNs;
        };
        require(risingPeriod("FAST") == 1000000 && risingPeriod("SLOW") == 2000000, "incorrect waveform periods");
        waveform->fit();
        wxMouseEvent click(wxEVT_LEFT_DOWN);
        click.SetPosition({int(waveform->xAt(1)) + 2, waveform->header() + 5});
        waveform->selectCursor(click);
        click.SetShiftDown(true);
        click.SetPosition({int(waveform->xAt(3)) - 2, waveform->header() + 5});
        waveform->selectCursor(click);
        require(waveform->cursorA == 1 && waveform->cursorB == 3 &&
                    waveform->summary->GetLabel().Contains(U("Δt 1 ms")), "waveform cursor period measurement");
        waveform->Update();
        waveform->graph->Update();
        const auto size = waveform->GetClientSize();
        wxBitmap screenshot(size.x, size.y);
        wxMemoryDC destination(screenshot);
        wxClientDC source(waveform);
        destination.Blit(0, 0, size.x, size.y, &source, 0, 0);
        destination.SelectObject(wxNullBitmap);
        require(screenshot.ConvertToImage().SaveFile(wxString((folder / "timing-waveform.png").wstring()), wxBITMAP_TYPE_PNG),
                "could not save waveform screenshot");
        replyToBatch("100000");
        command("运行指定节拍…");
        simulationTimer();
        command("运行 / 暂停");
        require(!timer.IsRunning() && batchRemaining == 0 && simulator->ticks() > 8 && simulator->ticks() < 100008,
                "long batch did not cancel at an intermediate tick");

        canvas->selected = {"SLOW"};
        inspect();
        require(clockSettingsButton->IsShown(), "selected clock has no settings button");
        bool sawPreview = false;
        wxTheApp->CallAfter([&] {
            for (auto *window : wxTopLevelWindows) {
                auto *dialog = dynamic_cast<wxDialog *>(window);
                if (!dialog || dialog->GetName() != "clock-settings-dialog" || !dialog->IsModal()) continue;
                auto *field = wxDynamicCast(dialog->FindWindowByName("clock-frequency", dialog), wxTextCtrl);
                field->SetValue("800");
                auto *preview = dialog->FindWindowByName("clock-timing-preview", dialog);
                sawPreview = preview->GetLabel().Contains("666.666");
                dialog->EndModal(wxID_OK);
                break;
            }
        });
        wxCommandEvent settingsClick(wxEVT_BUTTON, clockSettingsButton->GetId());
        settingsClick.SetEventObject(clockSettingsButton);
        require(clockSettingsButton->GetEventHandler()->ProcessEvent(settingsClick), "clock settings button has no action");
        require(sawPreview, "rounded frequency is absent from dialog");
        require(clockTiming(*circuit().part("SLOW")).highTicks == 2 &&
                    clockTiming(*circuit().part("SLOW")).lowTicks == 1 &&
                    simulator->ticks() == 0, "Hz edit did not apply and reset");
        undo();
        require(clockTiming(*circuit().part("SLOW")).lowTicks == 2, "clock settings undo");

        fixture("01-edge-and-clear.logic.json");
        openWaveform();
        simulator->setInput("D", 1);
        simulator->settle();
        tick();
        tick();
        require(simulator->read({"Q_UP", "A"}).text() == "1" &&
                    simulator->read({"Q_DOWN", "A"}).text() == "1", "edge demo outputs before clear");
        simulator->setInput("CLR", 1);
        simulator->settle();
        for (const auto *id : {"Q_UP", "Q_DOWN"}) {
            const auto net = *simulator->network({id, "A"});
            const auto &points = waveform->channels[waveform->networks.at(net)].points;
            require(points.back().tick == 2 && points.back().value.text() == "0", "asynchronous clear not captured");
        }

        fixture("03-register-pipeline.logic.json");
        openWaveform();
        simulator->setInput("D", 1);
        simulator->settle();
        tick();
        require(simulator->read({"Q1", "A"}).text() == "1" && simulator->read({"Q2", "A"}).text() == "0",
                "pipeline stages sampled together incorrectly");
        tick();
        tick();
        require(simulator->read({"Q2", "A"}).text() == "1", "pipeline second edge missing");
        resetWaveform(true);
        writeAtomic(folder / "TIMING-PASS.txt", "时钟设置、播放入口、波形记录及三个时序演示样例检查完成。\n");
    }
}
} // namespace eda::logic
