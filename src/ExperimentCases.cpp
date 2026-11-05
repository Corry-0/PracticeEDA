// 实验案例是普通工程的模板入口；窗口、说明和加载逻辑集中在本文件。
#include "DigitalEditor.h"
#include <wx/stdpaths.h>

namespace eda::logic {
namespace {
struct ExperimentCase {
    const char *name;
    const char *file;
    const char *description;
};

const std::vector<ExperimentCase> &experimentCases() {
    static const std::vector<ExperimentCase> cases = {
        {"半加器", "half-adder.logic.json",
         "实验目的\n观察异或门与与门如何组成半加器。\n\n"
         "操作步骤\n1. 双击输入 A、B 切换 0/1。\n"
         "2. 依次测试 00、01、10、11，观察 Sum 和 Carry。\n"
         "3. 在“分析 → 当前电路真值表与表达式”中查看完整真值表。\n\n"
         "预期结果（A B → Sum Carry）\n00 → 0 0\n01 → 1 0\n10 → 1 0\n11 → 0 1\n\n"
         "逻辑关系\nSum = A XOR B\nCarry = A AND B"},
        {"全加器", "full-adder.logic.json",
         "实验目的\n理解带进位输入的一位二进制加法，观察两个半加器与或门的组合。\n\n"
         "操作步骤\n1. 双击 A、B、Cin 切换输入。\n"
         "2. 观察 Sum 与 Cout，并与 A + B + Cin 比较。\n"
         "3. 使用分析菜单查看 8 种输入组合。\n\n"
         "预期结果（A B Cin → Sum Cout）\n000 → 0 0\n001 → 1 0\n010 → 1 0\n"
         "011 → 0 1\n100 → 1 0\n101 → 0 1\n110 → 0 1\n111 → 1 1\n\n"
         "逻辑关系\nSum = A XOR B XOR Cin\nCout = (A AND B) OR (Cin AND (A XOR B))"},
        {"8 位总线寄存器", "bus-register.logic.json",
         "实验目的\n观察寄存器在时钟上升沿采样，并在两个上升沿之间保持输出。\n\n"
         "操作步骤\n1. 初始 Data = 0x25，Q = 0。按 F8，首次上升沿将 Q 更新为 0x25。\n"
         "2. 选中 Data，在属性表格的“初始值”中改为 0x3C。\n"
         "3. Q 应仍为 0x25。再按 F8 到下降沿，Q 不变；再按 F8 到上升沿，Q 变为 0x3C。\n"
         "4. 按 F5 可运行或暂停，观察 Q 和十六进制显示。\n\n"
         "预期结果\n输入改变不会立即更新 Q；每个上升沿捕获当前 Data，其他时刻保持原值。\n\n"
         "提示\n每次 F8 是一个基础节拍，默认两次节拍构成一个完整时钟周期。"},
        {"4 位计数器", "counter.logic.json",
         "实验目的\n观察同步计数、计数使能、异步清零和终端计数输出。\n\n"
         "操作步骤\n1. 初始 EN = 1、CLR = 0、Q = 0。按 F8，首个上升沿得到 Q = 1。\n"
         "2. 继续按 F8，每个上升沿加 1，下降沿保持。也可按 F5 连续运行。\n"
         "3. 暂停后双击 EN 改为 0，再推进时钟，Q 应保持不变。\n"
         "4. 双击 CLR 改为 1，无需时钟沿，Q 立即清零。恢复 CLR = 0、EN = 1 可继续计数。\n\n"
         "预期结果\nQ 在 0x0–0xF 之间循环；Q = 0xF 时 CARRY = 1，下一上升沿回到 0。"},
        {"RAM 读写", "ram-read-write.logic.json",
         "实验目的\n通过独立地址、数据、写使能和时钟，验证存储器写入与异步读取。\n\n"
         "操作步骤\n1. 初始 ADDR = 0、DATA = 0x2A、WE = 0，Q = 0。\n"
         "2. 双击 WE 设为 1，再按 F8 到首个上升沿：地址 0 写入 0x2A，Q 显示 0x2A。\n"
         "3. 双击 WE 设为 0。选中 ADDR，在属性表格中改为 1，Q 显示 0。\n"
         "4. 将 DATA 改为 0x55，WE 设为 1；按 F8 到下降沿，再按 F8 到上升沿，地址 1 写入 0x55。\n"
         "5. 设 WE = 0，切换 ADDR 为 0 和 1，分别读到 0x2A 和 0x55。\n"
         "6. 选中 RAM，使用“仿真 → 查看实例运行内存”核对内容。\n\n"
         "预期结果\nWE = 1 时只在上升沿写入；WE = 0 时不会写入，地址改变立即更新 Q。\n\n"
         "参数\n地址位宽为 4（16 个地址），数据位宽为 8。复位恢复全零初值。"},
        {"层次化半加器", "hierarchical-adder.logic.json",
         "实验目的\n观察子电路封装、接口连接和运行实例内部状态。\n\n"
         "操作步骤\n1. 在主电路中双击 A、B 改变输入。\n"
         "2. 观察 Sum、Carry，结果应与普通半加器相同。\n"
         "3. 双击 HalfAdder instance，进入该运行实例，查看内部 XOR、AND 和信号。\n"
         "4. 使用“项目 → 返回上级实例”或 Alt+Up 回到主电路。\n\n"
         "预期结果\n主电路与子电路内部信号一致；进入和返回实例不会复位仿真。\n\n"
         "编辑提示\n运行实例视图用于观察；修改内部结构请使用“编辑当前电路定义”。"}
    };
    return cases;
}

std::filesystem::path experimentPath(const ExperimentCase &item) {
    auto path = fsPath(wxStandardPaths::Get().GetExecutablePath()).parent_path() / "examples" / item.file;
    if (std::filesystem::exists(path))
        return path;
    // 仅供源码构建使用；安装包由 CMake 安装完整 examples 目录中的案例。
    path = std::filesystem::path(__FILE__).parent_path().parent_path() / "examples" / item.file;
    if (!std::filesystem::exists(path))
        throw std::runtime_error("找不到案例文件：" + std::string(item.file) + "。请检查程序旁的 examples 目录。");
    return path;
}

void showGuide(wxWindow *parent, const ExperimentCase &item) {
    if (auto old = wxWindow::FindWindowByName("experiment-guide", parent)) {
        old->SetName("");
        old->Destroy();
    }
    auto dialog = new wxDialog(parent, wxID_ANY, U(std::string(item.name) + " · 案例说明"),
                               wxDefaultPosition, {560, 660}, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    dialog->SetName("experiment-guide");
    dialog->SetMinSize({400, 400});
    auto layout = new wxBoxSizer(wxVERTICAL);
    auto text = new wxTextCtrl(dialog, wxID_ANY, U(item.description), wxDefaultPosition, wxDefaultSize,
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
    text->SetName("experiment-guide-text");
    layout->Add(text, 1, wxEXPAND | wxALL, 12);
    layout->Add(new wxStaticText(dialog, wxID_ANY, U("说明窗口可保持打开，同时在主画布操作电路。")),
                0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    layout->Add(dialog->CreateButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 12);
    dialog->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent &) { dialog->Destroy(); }, wxID_CLOSE);
    dialog->SetSizer(layout);
    dialog->CentreOnParent();
    dialog->Show();
}
} // namespace

bool LogicEditor::openExperimentCase(size_t index) {
    const auto &item = experimentCases().at(index);
    auto loaded = deserialize(readFile(experimentPath(item)));
    // 先在候选工程上校验仿真，坏文件不得替换正在编辑的工程。
    Simulator candidate(loaded, loaded.main);
    if (!candidate.settle())
        throw std::runtime_error("案例电路无法稳定，请检查案例文件。");
    for (const auto &issue : candidate.issues())
        if (issue.severity == "error")
            throw std::runtime_error("案例连接错误：" + issue.message);
    if (!discard())
        return false;
    stop();
    canvas->cancel();
    canvas->appearance = false;
    canvas->tool = "Select";
    history.reset(std::move(loaded), false);
    current = history.project.main;
    savedPath.clear(); // 从模板创建副本；Ctrl+S 必须选择新路径。
    scope.clear();
    navigation.clear();
    canvas->selected.clear();
    // 案例切换结束上一工程的连续日志，避免把不同电路混入同一文件。
    traceFile.reset();
    monitoredSignals.clear();
    rebuild(true, true);
    canvas->fit();
    canvas->SetFocus();
    return true;
}

void LogicEditor::showExperimentCases() {
    wxDialog dialog(this, wxID_ANY, U("实验案例"), wxDefaultPosition, {900, 650},
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    dialog.SetName("experiment-cases");
    dialog.SetMinSize({700, 480});
    auto layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(new wxStaticText(&dialog, wxID_ANY,
        U("选择案例查看步骤；打开后可运行、修改并另存为自己的工程。")), 0, wxALL, 12);
    auto body = new wxBoxSizer(wxHORIZONTAL);
    auto list = new wxListBox(&dialog, wxID_ANY, wxDefaultPosition, {200, -1});
    list->SetName("experiment-case-list");
    for (const auto &item : experimentCases())
        list->Append(U(item.name));
    auto details = new wxTextCtrl(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                 wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
    details->SetName("experiment-case-details");
    body->Add(list, 0, wxEXPAND | wxRIGHT, 12);
    body->Add(details, 1, wxEXPAND);
    layout->Add(body, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);
    auto buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    auto open = new wxButton(&dialog, wxID_OK, U("打开案例副本"));
    open->SetDefault();
    buttons->Add(open, 0, wxRIGHT, 8);
    buttons->Add(new wxButton(&dialog, wxID_CANCEL, U("取消")));
    layout->Add(buttons, 0, wxEXPAND | wxALL, 12);
    dialog.SetSizer(layout);
    auto update = [&] {
        int index = list->GetSelection();
        open->Enable(index != wxNOT_FOUND);
        if (index != wxNOT_FOUND)
            details->ChangeValue(U(experimentCases().at(size_t(index)).description));
    };
    list->Bind(wxEVT_LISTBOX, [&](wxCommandEvent &) { update(); });
    list->Bind(wxEVT_LISTBOX_DCLICK, [&](wxCommandEvent &) {
        if (list->GetSelection() != wxNOT_FOUND)
            dialog.EndModal(wxID_OK);
    });
    list->SetSelection(0);
    update();
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK || list->GetSelection() == wxNOT_FOUND)
        return;
    auto index = size_t(list->GetSelection());
    if (openExperimentCase(index))
        showGuide(this, experimentCases().at(index));
}

// 由现有 --smoke-test 调用，验证正式加载路径、模板保护和非模态说明窗口。
void LogicEditor::runExperimentSmoke(const std::filesystem::path &folder) {
    for (size_t i = 0; i < experimentCases().size(); ++i) {
        history.markSaved();
        const auto path = experimentPath(experimentCases()[i]);
        const auto original = readFile(path);
        if (!openExperimentCase(i) || !savedPath.empty() || !history.dirty() || current != history.project.main ||
            !scope.empty() || !navigation.empty() || simulator->pending() || timer.IsRunning())
            throw std::runtime_error("Experiment copy did not reset editor state");
        if (serialize(history.project) != serialize(deserialize(original)) || readFile(path) != original)
            throw std::runtime_error("Experiment copy changed the template");
        canvas->exportPng(wxString((folder / ("experiment-" + std::to_string(i) + ".png")).wstring()));
    }
    showGuide(this, experimentCases().front());
    auto guide = dynamic_cast<wxDialog *>(wxWindow::FindWindowByName("experiment-guide", this));
    if (!guide || !guide->IsShown() || guide->IsModal())
        throw std::runtime_error("Experiment guide must allow canvas interaction");
    guide->Destroy();
    history.markSaved();
    const auto before = serialize(history.project);
    bool dialogVerified = false;
    CallAfter([&] {
        auto chooser = dynamic_cast<wxDialog *>(wxWindow::FindWindowByName("experiment-cases", this));
        if (chooser) {
            auto list = dynamic_cast<wxListBox *>(wxWindow::FindWindowByName("experiment-case-list", chooser));
            auto text = dynamic_cast<wxTextCtrl *>(wxWindow::FindWindowByName("experiment-case-details", chooser));
            dialogVerified = list && list->GetCount() == experimentCases().size() && text &&
                             text->GetValue().Contains(U("实验目的"));
            chooser->EndModal(wxID_CANCEL);
        }
    });
    showExperimentCases();
    if (!dialogVerified || serialize(history.project) != before)
        throw std::runtime_error("Experiment chooser cancel changed the project");
    dialogVerified = false;
    CallAfter([&] {
        auto chooser = dynamic_cast<wxDialog *>(wxWindow::FindWindowByName("experiment-cases", this));
        if (chooser) {
            auto list = dynamic_cast<wxListBox *>(wxWindow::FindWindowByName("experiment-case-list", chooser));
            auto text = dynamic_cast<wxTextCtrl *>(wxWindow::FindWindowByName("experiment-case-details", chooser));
            if (list && text) {
                list->SetSelection(1);
                wxCommandEvent select(wxEVT_LISTBOX, list->GetId());
                list->GetEventHandler()->ProcessEvent(select);
                dialogVerified = text->GetValue().Contains(U("Cin"));
            }
            chooser->EndModal(dialogVerified ? wxID_OK : wxID_CANCEL);
        }
    });
    showExperimentCases();
    guide = dynamic_cast<wxDialog *>(wxWindow::FindWindowByName("experiment-guide", this));
    if (!dialogVerified || !guide || !guide->IsShown() || guide->IsModal() || !history.dirty() ||
        !savedPath.empty() || history.project.name != "实验案例：一位全加器")
        throw std::runtime_error("Experiment chooser did not open the selected editable copy");
    guide->Destroy();
    history.markSaved();
    writeAtomic(folder / "EXPERIMENTS-PASS.txt",
                "Six editable experiment copies loaded and simulated; originals unchanged; save path cleared; modeless guide, chooser cancellation and selection/open passed.\n");
}
} // namespace eda::logic
