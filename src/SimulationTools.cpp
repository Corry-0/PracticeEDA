#include "DigitalEditor.h"
#include <wx/choicdlg.h>
#include <wx/dcbuffer.h>
#include <wx/scrolwin.h>
#include <wx/textdlg.h>
#include <wx/settings.h>
#include <wx/modalhook.h>
#include <wx/msgdlg.h>
#include <sstream>

namespace eda::logic {
namespace {
struct WatchedPin {
    Endpoint endpoint;
    std::string label;
    int width;
};

class LiveDialog : public wxDialog {
  public:
    LiveDialog(wxWindow *parent, const wxString &title)
        : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, {1000, 600},
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), timer(this) {
        SetMinSize({620, 350});
        Bind(wxEVT_TIMER, [this](wxTimerEvent &) { if (update) update(); });
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { timer.Stop(); Destroy(); });
    }
    wxTimer timer;
    std::function<void()> update;
};

class WavePanel : public wxScrolledWindow {
  public:
    WavePanel(wxWindow *parent, std::vector<WatchedPin> selected)
        : wxScrolledWindow(parent), pins(std::move(selected)) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetScrollRate(16, 16);
        Bind(wxEVT_PAINT, &WavePanel::paint, this);
    }
    std::vector<WatchedPin> pins;
    std::vector<Trace> events;
    std::vector<std::string> currentValues;
    uint64_t now = 0;
    void draw(wxDC &dc) {
        const auto background = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
        const auto foreground = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
        const auto accent = wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
        dc.SetBackground(wxBrush(background)); dc.Clear();
        dc.SetTextForeground(foreground);
        dc.SetFont(GetFont());
        const int left = 255, rowHeight = 72;
        const int width = std::max(820, GetClientSize().x - 20);
        SetVirtualSize(width + 20, 65 + int(pins.size()) * rowHeight);
        const uint64_t start = events.empty() ? now : events.front().time;
        const uint64_t end = std::max(start + 1, now);
        auto x = [&](uint64_t t) { return left + int(double(t - start) / double(end - start) * (width - left)); };
        dc.DrawText(U("传播时间 t（各信号共用横轴）"), left, 5);
        for (int i = 0; i <= 5; ++i) {
            auto t = start + (end - start) * uint64_t(i) / 5;
            dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT), 1, wxPENSTYLE_DOT));
            dc.DrawLine(x(t), 45, x(t), 55 + int(pins.size()) * rowHeight);
            dc.DrawText(U(std::to_string(t)), x(t) - (i == 5 ? 30 : 0), 26);
        }
        for (size_t row = 0; row < pins.size(); ++row) {
            const auto &pin = pins[row];
            const int y = 66 + int(row) * rowHeight;
            dc.DrawText(wxControl::Ellipsize(U(pin.label), dc, wxELLIPSIZE_MIDDLE, left - 15), 5, y);
            if (row < currentValues.size()) dc.DrawText(U("当前 " + currentValues[row]), 5, y + 23);
            std::vector<std::pair<uint64_t, std::string>> changes;
            for (const auto &event : events)
                if (event.object == pin.endpoint.object && event.pin == pin.endpoint.pin)
                    changes.push_back({event.time, event.value});
            std::string value = "X";
            uint64_t at = start;
            auto segment = [&](uint64_t until) {
                dc.SetPen(wxPen(accent, 2));
                if (pin.width == 1 && (value == "0" || value == "1")) {
                    int level = y + (value == "1" ? 2 : 30);
                    dc.DrawLine(x(at), level, x(until), level);
                } else {
                    dc.SetBrush(*wxTRANSPARENT_BRUSH);
                    dc.DrawRectangle(x(at), y + 2, std::max(1, x(until) - x(at)), 28);
                    if (x(until) - x(at) > dc.GetTextExtent(U(value)).x + 8)
                        dc.DrawText(U(value), x(at) + 4, y + 6);
                }
            };
            for (const auto &change : changes) {
                segment(change.first);
                if (pin.width == 1 && value != change.second) {
                    dc.SetPen(wxPen(accent, 2));
                    dc.DrawLine(x(change.first), y + 2, x(change.first), y + 30);
                }
                at = change.first; value = change.second;
            }
            segment(end);
        }
    }
  private:
    void paint(wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        PrepareDC(dc);
        draw(dc);
    }
};
} // namespace

void LogicEditor::showWaveforms() {
    wxArrayString choices;
    std::vector<WatchedPin> pins;
    for (const auto &part : circuit().parts)
        for (const auto &port : ports(history.project, part))
            if (port.output) {
                Endpoint endpoint{scoped(part.id), port.id};
                const auto label = (part.label.empty() ? part.id : part.label) + " / " + endpoint.key();
                pins.push_back({endpoint, label, port.width});
                choices.Add(U(label));
            }
    if (pins.empty()) throw std::runtime_error("当前电路没有可观察的输出端口");
    wxMultiChoiceDialog signalDialog(this, U("选择 1–8 路信号，可同时选择 CLK、Data、Q。"), U("时序波形"), choices);
    signalDialog.SetName("simulation-wave-selection");
    wxArrayInt defaults;
    for (size_t i = 0; i < std::min(size_t(8), pins.size()); ++i) defaults.Add(int(i));
    signalDialog.SetSelections(defaults);
    if (signalDialog.ShowModal() != wxID_OK) return;
    const auto picked = signalDialog.GetSelections();
    if (picked.empty() || picked.size() > 8) throw std::runtime_error("请选择 1–8 路信号");
    std::vector<WatchedPin> watched;
    for (int index : picked) watched.push_back(pins.at(size_t(index)));
    auto dialog = new LiveDialog(this, U("时序波形"));
    dialog->SetName("simulation-waveforms");
    auto layout = new wxBoxSizer(wxVERTICAL);
    auto note = new wxStaticText(dialog, wxID_ANY, U("最近 2000 条输出事件；无历史的区间显示 X。复位会清空历史。"));
    layout->Add(note, 0, wxALL, 10);
    auto panel = new WavePanel(dialog, watched);
    panel->SetName("simulation-wave-panel");
    layout->Add(panel, 1, wxEXPAND | wxALL, 5);
    dialog->SetSizer(layout);
    const auto source = simulator.get();
    const auto generation = simulationGeneration;
    dialog->update = [this, dialog, panel, note, source, generation] {
        if (simulator.get() != source || generation != simulationGeneration) {
            note->SetLabel(U("工程或运行模型已更换，请关闭此窗口并重新选择信号。"));
            dialog->timer.Stop(); return;
        }
        panel->events = simulator->trace();
        panel->now = simulator->time();
        panel->currentValues.clear();
        for (const auto &pin : panel->pins) panel->currentValues.push_back(simulator->read(pin.endpoint).text());
        panel->Refresh();
    };
    dialog->update(); dialog->timer.Start(150); dialog->Show();
}

void LogicEditor::showPropagation() {
    auto dialog = new LiveDialog(this, U("事件传播过程"));
    dialog->SetName("simulation-propagation");
    auto layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(new wxStaticText(dialog, wxID_ANY, U("F7 处理最早的一批事件；F8 推进时钟并传播。显示最近 128 批，最新在上方。")), 0, wxALL, 10);
    auto text = new wxTextCtrl(dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
    text->SetName("simulation-propagation-text");
    layout->Add(text, 1, wxEXPAND | wxALL, 5); dialog->SetSizer(layout);
    dialog->update = [this, text] {
        std::ostringstream content;
        const auto &batches = simulator->batches();
        for (auto it = batches.rbegin(); it != batches.rend(); ++it) {
            content << "t=" << it->time << "  求值元件：";
            for (const auto &path : it->evaluated) content << path << "  ";
            content << "\n统一发布的网络变化：\n";
            if (it->changedNets.empty()) content << "  无变化\n";
            for (const auto &net : it->changedNets) content << "  " << net << '\n';
            content << '\n';
        }
        const auto value = U(content.str());
        if (text->GetValue() != value) text->ChangeValue(value);
    };
    dialog->update(); dialog->timer.Start(150); dialog->Show();
}

void LogicEditor::configurePause() {
    stop();
    wxArrayString choices;
    std::vector<Endpoint> endpoints;
    for (const auto &part : circuit().parts)
        for (const auto &port : ports(history.project, part))
            if (port.output) {
                endpoints.push_back({scoped(part.id), port.id});
                choices.Add(U((part.label.empty() ? part.id : part.label) + " / " + endpoints.back().key()));
            }
    if (endpoints.empty()) throw std::runtime_error("当前电路没有可设置条件的输出端口");
    wxSingleChoiceDialog select(this, U("选择要观察的信号"), U("条件暂停"), choices);
    select.SetName("simulation-pause-selection");
    if (select.ShowModal() != wxID_OK) return;
    wxTextEntryDialog value(this, U("信号下一次进入这个值时暂停。支持十进制或 0x 十六进制。"), U("暂停值"), "1");
    value.SetName("simulation-pause-value");
    if (value.ShowModal() != wxID_OK) return;
    const auto input = utf8(value.GetValue().Trim().Trim(false));
    if (input.empty() || input[0] == '-') throw std::runtime_error("请输入非负整数");
    size_t used = 0;
    const int base = input.size() > 2 && input[0] == '0' && (input[1] == 'x' || input[1] == 'X') ? 16 : 10;
    const auto number = std::stoull(input, &used, base);
    if (used != input.size() || number > UINT32_MAX) throw std::runtime_error("暂停值必须是 32 位无符号整数");
    simulator->setPauseCondition(endpoints.at(size_t(select.GetSelection())), uint32_t(number));
    rebuild(false);
    status->SetLabel(status->GetLabel() + U("   |   暂停条件已设置；信号下一次进入目标值时停止"));
}

void LogicEditor::runSimulationToolsSmoke(const std::filesystem::path &folder) {
    history.markSaved();
    if (!openExperimentCase(3)) throw std::runtime_error("Register chain did not open");
    class Responses : public wxModalDialogHook {
      public:
        Responses() { Register(); }
        std::string error;
        int Enter(wxDialog *dialog) override {
            if (auto message = dynamic_cast<wxMessageDialog *>(dialog)) {
                error = utf8(message->GetMessage());
                return wxID_OK;
            }
            if (dialog->GetName() == "simulation-wave-selection") {
                dialog->CallAfter([dialog] {
                    wxCommandEvent event(wxEVT_BUTTON, wxID_OK);
                    dialog->GetEventHandler()->ProcessEvent(event);
                });
                return wxID_NONE;
            }
            if (dialog->GetName() == "simulation-pause-selection") {
                static_cast<wxSingleChoiceDialog *>(dialog)->SetSelection(4);
                return wxID_OK;
            }
            if (dialog->GetName() == "simulation-pause-value") {
                static_cast<wxTextEntryDialog *>(dialog)->SetValue("9");
                return wxID_OK;
            }
            return wxID_NONE;
        }
    } responses;
    auto invoke = [this, &responses, &folder](const wxString &label) {
        writeAtomic(folder / "simulation-test-stage.txt", utf8(label));
        const auto id = GetMenuBar()->FindMenuItem(U("仿真"), label);
        if (id == wxNOT_FOUND) throw std::runtime_error("Simulation menu missing");
        wxCommandEvent event(wxEVT_MENU, id);
        GetEventHandler()->ProcessEvent(event);
        if (!responses.error.empty()) throw std::runtime_error(responses.error);
    };
    invoke(U("时序波形…"));
    invoke(U("事件传播过程…"));
    auto waves = dynamic_cast<LiveDialog *>(wxWindow::FindWindowByName("simulation-waveforms", this));
    auto events = dynamic_cast<LiveDialog *>(wxWindow::FindWindowByName("simulation-propagation", this));
    if (!waves || !events || !waves->IsShown() || !events->IsShown())
        throw std::runtime_error("Simulation tool windows did not open");
    invoke(U("设置条件暂停…"));
    for (int i = 0; i < 3; ++i) tick();
    if (!simulator->conditionPaused() || simulator->read({"r2", "Q"}).value != 9 || timer.IsRunning())
        throw std::runtime_error("Menu pause condition did not stop on R2=9");
    waves->update(); events->update();
    auto panel = dynamic_cast<WavePanel *>(wxWindow::FindWindowByName("simulation-wave-panel", waves));
    auto text = dynamic_cast<wxTextCtrl *>(wxWindow::FindWindowByName("simulation-propagation-text", events));
    if (!panel || panel->events.empty() || !text || !text->GetValue().Contains("r2"))
        throw std::runtime_error("Simulation windows did not receive live event data");
    panel->Refresh(); panel->Update();
    wxBitmap image(1100, 650);
    wxMemoryDC imageDc(image);
    panel->draw(imageDc);
    imageDc.SelectObject(wxNullBitmap);
    if (!image.SaveFile(wxString((folder / "simulation-waveforms.png").wstring()), wxBITMAP_TYPE_PNG))
        throw std::runtime_error("Waveform rendering could not be saved");
    invoke(U("清除暂停条件"));
    if (simulator->conditionPaused()) throw std::runtime_error("Pause condition did not clear");
    simulator->reset(); simulator->settle(); waves->update();
    if (simulator->read({"r1", "Q"}).value != 5) throw std::runtime_error("Reset altered chain initial state");
    waves->timer.Stop(); events->timer.Stop(); waves->Destroy(); events->Destroy();
    history.markSaved();
    writeAtomic(folder / "SIMULATION-TOOLS-PASS.txt",
                "Register chain, waveform signal selection/render data, propagation batches, menu pause/clear and reset passed.\n");
}
} // namespace eda::logic
