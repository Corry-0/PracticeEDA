#include "Assistant.h"
#include "DigitalEditor.h"
#include <future>
#include <thread>
#include <wx/config.h>
#include <wx/dcbuffer.h>
#include <wx/modalhook.h>

namespace eda::logic {
using ai::Json;
namespace {
struct Job {
    std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    std::future<ai::Reply> result;
};
struct Provider {
    wxString name, url, keyVariable;
    wxArrayString models;
};
const std::vector<Provider> providers{
    {"DeepSeek",
     "https://api.deepseek.com/chat/completions",
     "DEEPSEEK_API_KEY",
     {"deepseek-flash", "deepseek-v4-pro"}},
    {"OpenAI", "https://api.openai.com/v1/chat/completions", "OPENAI_API_KEY", {"gpt-5.4-mini", "gpt-5.4"}},
    {U("自定义"), {}, "PRACTICEEDA_AI_API_KEY", {}}};
size_t providerIndex(wxConfigBase &cfg) {
    auto saved = cfg.Read("assistant/provider", providers.front().name);
    for (size_t i = 0; i < providers.size(); ++i)
        if (providers[i].name == saved)
            return i;
    return 0;
}
wxString modelKey(size_t index) {
    return "assistant/models/" + providers[index].name;
}
} // namespace
class AssistantPanel : public wxPanel {
    LogicEditor &editor;
    wxTextCtrl *log, *prompt;
    wxStaticText *context;
    wxListBox *references;
    wxButton *send, *cancel, *apply;
    wxPanel *diagram;
    wxTimer poll;
    std::shared_ptr<Job> job;
    std::optional<ai::Proposal> draft;
    std::map<std::string,std::vector<Point>> previewCrossings;
    Json conversation = Json::array();
    uint64_t revision = 0;
    std::string base, definition, scope, question;
    bool probing = false;
    std::map<std::string, wxString> sessionKeys;
    friend class LogicEditor;

    void say(const std::string &text) { log->AppendText(U(text + "\n\n")); }
    void clearDraft() {
        draft.reset();
        previewCrossings.clear();
        apply->Disable();
        cancel->SetLabel(U("取消"));
        cancel->Enable(bool(job));
        diagram->Hide();
        Layout();
    }
    void discard() {
        if (job)
            *job->cancelled = true;
        job.reset();
        clearDraft();
        cancel->Disable();
        send->Enable();
    }
    bool fresh() const {
        return revision == editor.assistantRevision && definition == editor.current &&
               scope == editor.scope && base == serialize(editor.history.project);
    }
    void sync() {
        if (revision == editor.assistantRevision && definition == editor.current && scope == editor.scope)
            return;
        const bool outdated = job || draft.has_value();
        discard();
        conversation = Json::array();
        references->Clear();
        log->Clear();
        if (outdated)
            say("工程或观察位置已改变，请重新提问。");
        definition = editor.current;
        scope = editor.scope;
        revision = editor.assistantRevision;
        context->SetLabel(U("电路 · " + definition + (scope.empty() ? "" : " / " + scope)));
    }
    void start(bool probe, ai::Transport transport = {}) {
        sync();
        if (job)
            return;
        const auto q = utf8(prompt->GetValue().Strip(wxString::both));
        if (!probe && q.empty())
            return;
        auto pending = std::make_shared<Job>();
        auto token = pending->cancelled;
        if (!transport) {
            auto cfg = wxConfigBase::Get();
            auto index = providerIndex(*cfg);
            const auto &provider = providers[index];
            auto url =
                utf8(provider.url.empty() ? cfg->Read("assistant/customUrl", wxString{}) : provider.url);
            auto model =
                utf8(cfg->Read(modelKey(index), provider.models.empty() ? wxString{} : provider.models[0]));
            wxString secret = sessionKeys[url];
            if (secret.empty())
                wxGetEnv(provider.keyVariable, &secret);
            if (secret.empty() || url.empty() || model.empty())
                throw std::runtime_error("请打开模型设置并输入本次使用的密钥，或配置 " +
                                         utf8(provider.keyVariable));
            transport = [url, model, key = utf8(secret), token](const Json &body) {
                return ai::cloudRequest(url, model, key, body, *token);
            };
        }
        base = serialize(editor.history.project);
        ai::Snapshot snapshot;
        if (!probe)
            snapshot = ai::capture(editor.history.project, definition, scope, editor.canvas->selected,
                                   *editor.simulator);
        auto messages = conversation;
        messages.push_back({{"role", "user"}, {"content", q}});
        std::packaged_task<ai::Reply()> task([snapshot = std::move(snapshot), messages = std::move(messages),
                                              transport = std::move(transport), token, probe]() mutable {
            if (!probe)
                return ai::converse(snapshot, messages, transport, *token);
            Json request = {
                {"messages", Json::array({{{"role", "user"}, {"content", "Call ping with no arguments."}}})},
                {"tools",
                 Json::array({{{"type", "function"},
                               {"function",
                                {{"name", "ping"},
                                 {"parameters", {{"type", "object"}, {"properties", Json::object()}}}}}}})},
                {"stream", false}};
            auto result = transport(request);
            auto fn = result.at("choices").at(0).at("message").at("tool_calls").at(0).at("function");
            if (fn.at("name") != "ping" ||
                Json::parse(fn.at("arguments").get<std::string>()) != Json::object())
                throw std::runtime_error("模型未正确返回测试工具调用");
            return ai::Reply{"模型已通过工具调用测试。", {}, {}};
        });
        pending->result = task.get_future();
        std::thread(std::move(task)).detach();
        job = pending;
        probing = probe;
        question = q;
        clearDraft();
        references->Clear();
        send->Disable();
        cancel->Enable();
        if (!probe) {
            say("你 · " + q);
            prompt->Clear();
        }
        say(probe ? "正在测试模型连接…" : "正在分析…");
    }
    void finish() {
        sync();
        if (!job || job->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        auto finished = std::move(job);
        cancel->Disable();
        send->Enable();
        try {
            auto reply = finished->result.get();
            if (!fresh())
                throw std::runtime_error("工程已更新，请重新提问");
            say(reply.text);
            if (!probing) {
                conversation.push_back({{"role", "user"}, {"content", question}});
                conversation.push_back({{"role", "assistant"}, {"content", reply.text}});
                if (conversation.size() > 12)
                    conversation.erase(conversation.begin(), conversation.begin() + 2);
            }
            for (auto &ref : reply.references)
                references->Append(U(ref));
            if (reply.proposal) {
                draft = std::move(reply.proposal);
                previewCrossings=wireCrossings(draft->project,draft->project.circuit(draft->definition));
                say(draft->summary);
                apply->Enable();
                cancel->SetLabel(U("放弃候选"));
                cancel->Enable();
                diagram->Show();
                diagram->Refresh();
                Layout();
            }
        } catch (const std::exception &e) {
            say("请求结束 · " + std::string(e.what()));
        }
    }
    void commit() {
        if (!draft)
            return;
        if (!fresh() || !editor.scope.empty()) {
            clearDraft();
            say("候选已过期，请重新生成。");
            return;
        }
        auto prepared = std::move(*draft);
        clearDraft();
        editor.change([&] {
            editor.history.project = std::move(prepared.project);
            editor.current = prepared.definition;
        });
        editor.canvas->selected.clear();
        editor.canvas->fit();
        editor.inspect();
        sync();
        say("已应用，可以撤销。");
    }
    void paint(wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(diagram);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        if (!draft)
            return;
        auto &p = draft->project;
        auto &c = p.circuit(draft->definition);
        if (c.parts.empty())
            return;
        double x0 = 1e20, y0 = 1e20, x1 = -1e20, y1 = -1e20;
        auto expand = [&](Point a) {
            x0 = std::min(x0, a.x);
            x1 = std::max(x1, a.x);
            y0 = std::min(y0, a.y);
            y1 = std::max(y1, a.y);
        };
        for (auto &part : c.parts) {
            auto half = partSize(p, part) * 0.5;
            expand(part.at - half);
            expand(part.at + half);
        }
        for (auto &wire : c.wires)
            for (auto a : wirePoints(p, c, wire))
                expand(a);
        auto size = diagram->GetClientSize();
        double scale = std::min((size.x - 12) / std::max(1., x1 - x0), (size.y - 12) / std::max(1., y1 - y0));
        auto pt = [&](Point a) { return wxPoint(6 + int((a.x - x0) * scale), 6 + int((a.y - y0) * scale)); };
        dc.SetPen(wxPen(wxColour("#267e75")));
        for (auto &wire : c.wires) {
            auto points = wirePoints(p, c, wire);
            static const std::vector<Point> noGaps;
            auto found=previewCrossings.find(wire.id);
            for(const auto &[a,b]:visibleWireStrokes(points,found==previewCrossings.end()?noGaps:found->second,
                                                     1.5/std::max(scale,0.001)))
                dc.DrawLine(pt(a),pt(b));
        }
        dc.SetBrush(wxBrush(wxColour("#dfebfa")));
        for (auto &part : c.parts) {
            auto half = partSize(p, part) * 0.5;
            wxRect rect(pt(part.at - half), pt(part.at + half));
            dc.DrawRectangle(rect);
            auto label = U(part.label.empty() ? part.kind : part.label);
            if (dc.GetTextExtent(label).x <= rect.width)
                dc.DrawLabel(label, rect, wxALIGN_CENTER);
        }
    }
    void settings() {
        auto cfg = wxConfigBase::Get();
        wxDialog dialog(this, wxID_ANY, U("AI 模型设置"));
        dialog.SetName("assistant-settings");
        auto col = new wxBoxSizer(wxVERTICAL);
        auto label = [&](const wxString &text) {
            col->Add(new wxStaticText(&dialog, wxID_ANY, text), 0, wxLEFT | wxTOP, 8);
        };
        label(U("服务商"));
        auto choice = new wxChoice(&dialog, wxID_ANY);
        choice->SetName("assistant-provider");
        for (auto &provider : providers)
            choice->Append(provider.name);
        col->Add(choice, 0, wxEXPAND | wxALL, 8);
        label(U("Chat Completions 地址"));
        auto address = new wxTextCtrl(&dialog, wxID_ANY, {}, wxDefaultPosition, FromDIP(wxSize(400, -1)));
        address->SetName("assistant-endpoint");
        col->Add(address, 0, wxEXPAND | wxALL, 8);
        label(U("模型（可选择或输入模型 ID）"));
        auto model = new wxComboBox(&dialog, wxID_ANY);
        model->SetName("assistant-model");
        col->Add(model, 0, wxEXPAND | wxALL, 8);
        label(U("API Key（仅本次运行使用）"));
        auto key = new wxTextCtrl(&dialog, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        col->Add(key, 0, wxEXPAND | wxALL, 8);
        auto hint = new wxStaticText(&dialog, wxID_ANY, {});
        col->Add(hint, 0, wxLEFT | wxBOTTOM, 8);
        auto refresh = [&] {
            const auto index = size_t(choice->GetSelection());
            const auto &provider = providers[index];
            address->SetValue(provider.url.empty() ? cfg->Read("assistant/customUrl", wxString{})
                                                   : provider.url);
            address->SetEditable(provider.url.empty());
            model->Clear();
            model->Append(provider.models);
            model->SetValue(
                cfg->Read(modelKey(index), provider.models.empty() ? wxString{} : provider.models[0]));
            key->Clear();
            hint->SetLabel(U("也可从环境变量读取 ") + provider.keyVariable);
            dialog.Layout();
        };
        choice->SetSelection(int(providerIndex(*cfg)));
        refresh();
        choice->Bind(wxEVT_CHOICE, [&](wxCommandEvent &) { refresh(); });
        col->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 8);
        if (auto ok = dialog.FindWindow(wxID_OK))
            ok->SetLabel(U("保存并测试"));
        dialog.SetSizerAndFit(col);
        if (dialog.ShowModal() != wxID_OK)
            return;
        auto index = size_t(choice->GetSelection());
        auto url = address->GetValue().Strip(wxString::both);
        auto name = model->GetValue().Strip(wxString::both);
        if (url.empty() || name.empty())
            throw std::runtime_error("请填写端点和模型");
        discard();
        conversation = Json::array();
        cfg->Write("assistant/provider", providers[index].name);
        if (providers[index].url.empty())
            cfg->Write("assistant/customUrl", url);
        cfg->Write(modelKey(index), name);
        cfg->Flush();
        if (!key->IsEmpty())
            sessionKeys[utf8(url)] = key->GetValue();
        start(true);
    }

  public:
    AssistantPanel(LogicEditor &owner, wxWindow *parent) : wxPanel(parent), editor(owner), poll(this) {
        SetName("assistant-panel");
        SetMinSize(FromDIP(wxSize(335, -1)));
        auto col = new wxBoxSizer(wxVERTICAL);
        context = new wxStaticText(this, wxID_ANY, U("AI 电路助手"));
        col->Add(context, 0, wxALL, 5);
        log = new wxTextCtrl(this, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                             wxTE_MULTILINE | wxTE_READONLY);
        col->Add(log, 1, wxEXPAND | wxALL, 4);
        references = new wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 55)));
        references->SetToolTip(U("点击定位本轮查询的对象"));
        references->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &e) {
            if (fresh())
                editor.locateObject(utf8(e.GetString()));
        });
        col->Add(references, 0, wxEXPAND | wxALL, 4);
        diagram = new wxPanel(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 115)));
        diagram->SetBackgroundStyle(wxBG_STYLE_PAINT);
        diagram->Bind(wxEVT_PAINT, &AssistantPanel::paint, this);
        col->Add(diagram, 0, wxEXPAND | wxALL, 4);
        diagram->Hide();
        prompt =
            new wxTextCtrl(this, wxID_ANY, {}, wxDefaultPosition, FromDIP(wxSize(-1, 56)), wxTE_MULTILINE);
        prompt->SetHint(U("询问电路、生成子电路或描述预期修复"));
        col->Add(prompt, 0, wxEXPAND | wxALL, 4);
        auto row = new wxBoxSizer(wxHORIZONTAL);
        auto button = [&](const char *label, std::function<void()> action) {
            auto b = new wxButton(this, wxID_ANY, U(label), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            row->Add(b, 1, wxALL, 2);
            b->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent &) {
                try {
                    action();
                } catch (const std::exception &e) {
                    say(e.what());
                }
            });
            return b;
        };
        send = button("发送", [this] { start(false); });
        cancel = button("取消", [this] {
            discard();
            say("已取消。");
        });
        apply = button("应用候选", [this] { commit(); });
        button("模型设置", [this] { settings(); });
        cancel->Disable();
        apply->Disable();
        col->Add(row, 0, wxEXPAND);
        SetSizer(col);
        Bind(
            wxEVT_TIMER,
            [this](wxTimerEvent &) {
                if (IsShownOnScreen() || job)
                    finish();
            },
            poll.GetId());
        poll.Start(150);
    }
    ~AssistantPanel() override {
        if (job)
            *job->cancelled = true;
    }
};
wxWindow *LogicEditor::createAssistant(wxWindow *parent) {
    assistant = new AssistantPanel(*this, parent);
    return assistant;
}
void LogicEditor::runAssistantSmoke(const std::filesystem::path &folder) {
    class SettingsCheck final : public wxModalDialogHook {
      public:
        SettingsCheck() { Register(); }
        bool checked = false;
        int Enter(wxDialog *dialog) override {
            auto choice = dynamic_cast<wxChoice *>(dialog->FindWindowByName("assistant-provider", dialog));
            auto models = dynamic_cast<wxComboBox *>(dialog->FindWindowByName("assistant-model", dialog));
            auto address = dynamic_cast<wxTextCtrl *>(dialog->FindWindowByName("assistant-endpoint", dialog));
            if (!choice || !models || !address)
                return wxID_CANCEL;
            for (int i = 0; i < 2; ++i) {
                choice->SetSelection(i);
                wxCommandEvent event(wxEVT_CHOICE, choice->GetId());
                event.SetEventObject(choice);
                choice->GetEventHandler()->ProcessEvent(event);
                if (models->GetCount() != providers[i].models.size() ||
                    models->GetValue() != providers[i].models[0] || address->GetValue() != providers[i].url ||
                    address->IsEditable())
                    return wxID_CANCEL;
            }
            checked = true;
            return wxID_CANCEL;
        }
    };
    auto &panel = *assistant;
    auto toggle = wxWindow::FindWindowByName("toggle-assistant", this);
    if (!toggle)
        throw std::runtime_error("缺少助手入口");
    auto press = [](wxWindow *button) {
        wxCommandEvent event(wxEVT_BUTTON, button->GetId());
        event.SetEventObject(button);
        button->GetEventHandler()->ProcessEvent(event);
    };
    press(toggle);
    if (!panel.IsShown() || !canvas->IsShown() || canvas->GetClientSize().x <= 0)
        throw std::runtime_error("助手与画布布局失败");
    {
        SettingsCheck check;
        panel.settings();
        if (!check.checked)
            throw std::runtime_error("模型设置服务商切换失败");
    }
    panel.sync();
    panel.prompt->SetValue(U("生成半加器"));
    int calls = 0;
    std::string name="AI_half";
    std::string csv = "A,B,|,SUM,CARRY\n0,0,0,0\n0,1,1,0\n1,0,1,0\n1,1,0,1";
    auto model = [&](const Json &request) {
        if (request.at("tools").size() != 5)
            throw std::runtime_error("missing assistant tools");
        Json args = {{"name", name}, {"truth_csv", csv}};
        Json fn = {{"name", "propose_circuit"}, {"arguments", args.dump()}};
        Json call = {{"id", "call1"}, {"type", "function"}, {"function", fn}};
        ++calls;
        return Json{
            {"choices",
             Json::array({{{"message", {{"role", "assistant"}, {"tool_calls", Json::array({call})}}}}})}};
    };
    auto original = serialize(history.project);
    panel.start(false, model);
    panel.job->result.wait();
    panel.finish();
    if (!panel.draft || calls != 1 || serialize(history.project) != original)
        throw std::runtime_error("半加器候选生成失败：" + utf8(panel.log->GetValue()));
    auto generated = serialize(panel.draft->project);
    press(panel.apply);
    if (current != "AI_half" || truthTable(history.project, current).rows.size() != 4)
        throw std::runtime_error("AI 候选提交失败");
    undo();
    if (serialize(history.project) != original)
        throw std::runtime_error("AI 撤销失败");
    undo(true);
    if (serialize(history.project) != generated)
        throw std::runtime_error("AI 重做与预览不一致");
    undo();
    panel.prompt->SetValue(U("生成另一份半加器"));
    panel.start(false, model);
    panel.job->result.wait();
    panel.finish();
    if (!panel.draft)
        throw std::runtime_error("缺少候选");
    change([&] { history.project.name += " updated"; });
    auto edited = serialize(history.project);
    panel.commit();
    if (serialize(history.project) != edited || panel.draft)
        throw std::runtime_error("过期候选修改了工程");
    undo();
    panel.prompt->SetValue(U("超时测试"));
    panel.start(false, [](const Json &) -> Json { throw std::runtime_error("模拟请求超时"); });
    panel.job->result.wait();
    panel.finish();
    if (panel.draft || !panel.log->GetValue().Contains(U("模拟请求超时")) ||
        serialize(history.project) != original)
        throw std::runtime_error("助手异常恢复失败");
    name="AI_full";
    csv="A,B,Cin,|,SUM,COUT\n0,0,0,0,0\n0,0,1,1,0\n0,1,0,1,0\n0,1,1,0,1\n1,0,0,1,0\n1,0,1,0,1\n1,1,0,0,1\n1,1,1,1,1";
    panel.prompt->SetValue(U("生成全加器"));
    panel.start(false, model);
    panel.job->result.wait();
    panel.finish();
    if (!panel.draft) throw std::runtime_error("全加器布局失败：" + utf8(panel.log->GetValue()));
    press(panel.apply);
    if (current != name) throw std::runtime_error("未应用全加器");
    canvas->exportPng(wxString((folder/"ai-full-adder.png").wstring()),1.0);
    undo();
    press(toggle);
}
} // namespace eda::logic
