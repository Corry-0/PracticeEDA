// Editor commands share transactional model changes; read-only exports never rebuild the simulator.
#include "DigitalEditor.h"
#include "LogicIcons.h"
#include "LogicSymbols.h"
#include <sstream>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/imaglist.h>
#include <wx/stdpaths.h>
#include <wx/textdlg.h>
#include <wx/toolbar.h>

namespace eda::logic {
namespace {
enum {
    L_SAVE_AS = wxID_HIGHEST + 700,
    L_PNG,
    L_DEMO,
    L_SELECT,
    L_WIRE,
    L_NODE,
    L_TEXT,
    L_FIT,
    L_STEP,
    L_TICK,
    L_RUN,
    L_RESET,
    L_TABLE,
    L_EXPRESSION,
    L_TRACE,
    L_NEW_CIRCUIT,
    L_WRAP,
    L_RENAME,
    L_HELP,
    L_CSV,
    L_NETLIST,
    L_NEW_COMPONENT,
    L_SAVE_COMPONENT,
    L_LOAD_COMPONENT,
    L_DEL_CIRCUIT,
    L_APPEARANCE,
    L_LINE,
    L_CURVE,
    L_RECTANGLE,
    L_ELLIPSE,
    L_CIRCLE
};
enum PropertyRow { P_TYPE, P_ID, P_LABEL, P_WIDTH, P_VALUE, P_MEMORY, P_STATE, P_STROKE, P_FILL, P_COUNT };
struct Item : wxTreeItemData {
    std::string kind, circuit;
    Item(std::string k, std::string c = "") : kind(std::move(k)), circuit(std::move(c)) {}
};
uint32_t unsignedValue(const wxString &s) {
    auto trimmed = s;
    std::string text = utf8(trimmed.Trim().Trim(false));
    if (text.empty() || text[0] == '-')
        throw std::runtime_error("请输入 0–4294967295 范围内的整数或 0x 十六进制数");
    size_t used = 0;
    auto n = std::stoull(text, &used, text.size() > 2 && text.substr(0, 2) == "0x" ? 16 : 10);
    if (used != text.size() || n > UINT32_MAX)
        throw std::runtime_error("数值格式或范围错误");
    return uint32_t(n);
}
void line(wxGraphicsContext &g, const std::vector<Point> &ps) {
    if (ps.empty())
        return;
    auto p = g.CreatePath();
    p.MoveToPoint(ps[0].x, ps[0].y);
    for (size_t i = 1; i < ps.size(); ++i)
        p.AddLineToPoint(ps[i].x, ps[i].y);
    g.StrokePath(p);
}
void text(wxGraphicsContext &g, const std::string &s, Point p, int size = 10,
          wxColour c = wxColour("#26384b")) {
    g.SetFont(wxFont(wxFontInfo(size).FaceName("Microsoft YaHei UI")), c);
    g.DrawText(U(s), p.x, p.y);
}
std::string pretty(const std::string &k) {
    for (auto &info : library())
        if (info.kind == k)
            return info.name;
    return k;
}
const wxString openFilter = U("可打开的工程 (*.logic.json;*.circ)|*.logic.json;*.circ|外部电路 2.7 受限导入 "
                              "(*.circ)|*.circ|JSON 文件 (*.json)|*.json");
const wxString filter = U("数字逻辑工程 (*.logic.json)|*.logic.json|JSON 文件 (*.json)|*.json");
} // namespace
LogicEditor::LogicEditor(wxWindow *parent)
    : wxFrame(parent, wxID_ANY, U("PracticeEDA · 数字逻辑"), wxDefaultPosition, {1500, 940}), timer(this) {
    SetMinSize({1120, 720});
    SetIcon(icon("And").GetIcon({32, 32}));
    history.reset(Project{});
    auto bar = new wxMenuBar();
    auto menu = [&](const char *title, std::vector<std::pair<int, const char *>> items) {
        auto m = new wxMenu();
        for (auto &i : items)
            m->Append(i.first, U(i.second));
        bar->Append(m, U(title));
    };
    menu("文件", {{wxID_NEW, "新建数字工程\tCtrl+N"},
                  {wxID_OPEN, "打开…\tCtrl+O"},
                  {L_DEMO, "打开半加器示例"},
                  {wxID_SAVE, "保存\tCtrl+S"},
                  {L_SAVE_AS, "另存为…\tCtrl+Shift+S"},
                  {L_PNG, "导出完整电路 PNG…"},
                  {L_CSV, "导出最近分析 CSV…"},
                  {L_NETLIST, "导出网表…"},
                  {wxID_EXIT, "关闭"}});
    menu("编辑", {{wxID_UNDO, "撤销\tCtrl+Z"},
                  {wxID_REDO, "重做\tCtrl+Y"},
                  {wxID_COPY, "复制\tCtrl+C"},
                  {wxID_CUT, "剪切\tCtrl+X"},
                  {wxID_PASTE, "粘贴\tCtrl+V"},
                  {wxID_SELECTALL, "全选\tCtrl+A"},
                  {wxID_DELETE, "删除\tDelete"},
                  {wxID_PROPERTIES, "确认属性编辑\tCtrl+Return"}});
    menu("工具", {{L_SELECT, "选择 / 拖动 (S)"},
                  {L_WIRE, "导线 / 总线 (W)"},
                  {L_NODE, "节点 / 网络标签 (J)"},
                  {L_TEXT, "文字标签"},
                  {L_LINE, "直线 (L)"},
                  {L_CURVE, "曲线 (B)"},
                  {L_RECTANGLE, "矩形 (R)"},
                  {L_ELLIPSE, "椭圆 (E)"},
                  {L_CIRCLE, "圆 (C)"},
                  {L_APPEARANCE, "切换电路 / 元件外观"},
                  {L_FIT, "适合画面 (Home)"}});
    menu("项目", {{L_NEW_CIRCUIT, "新建子电路…"},
                  {L_NEW_COMPONENT, "新建自定义元件…"},
                  {L_SAVE_COMPONENT, "保存当前电路为元件…"},
                  {L_LOAD_COMPONENT, "导入自定义元件…"},
                  {L_WRAP, "封装当前电路为子电路…"},
                  {L_RENAME, "重命名当前电路…"},
                  {L_DEL_CIRCUIT, "删除当前子电路"}});
    menu("仿真", {{L_STEP, "单步传播\tF7"},
                  {L_TICK, "时钟半周期\tF8"},
                  {L_RUN, "运行 / 暂停\tF5"},
                  {L_RESET, "复位"},
                  {L_TRACE, "事件追踪"}});
    menu("分析", {{L_TABLE, "当前电路真值表与表达式"}, {L_EXPRESSION, "表达式 → 真值表…"}});
    menu("帮助", {{L_HELP, "数字逻辑使用说明"}});
    SetMenuBar(bar);
    auto root = new wxBoxSizer(wxVERTICAL);
    auto toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxTB_HORIZONTAL | wxTB_TEXT | wxTB_NODIVIDER);
    toolbar->SetName("logic-actions");
    toolbar->SetToolBitmapSize({24, 24});
    auto button = [&](int id, const char *name, const char *asset) {
        toolbar->AddTool(id, U(name), icon(asset), U(name));
        toolbar->Bind(
            wxEVT_TOOL,
            [this, id](wxCommandEvent &) {
                wxCommandEvent e(wxEVT_MENU, id);
                GetEventHandler()->ProcessEvent(e);
            },
            id);
    };
    button(wxID_NEW, "新建", "New");
    button(wxID_OPEN, "打开", "Open");
    button(wxID_SAVE, "保存", "Save");
    toolbar->AddSeparator();
    button(wxID_UNDO, "撤销", "Undo");
    button(wxID_REDO, "重做", "Redo");
    button(wxID_COPY, "复制", "Copy");
    button(wxID_PASTE, "粘贴", "Paste");
    toolbar->AddSeparator();
    button(L_SELECT, "选择 S", "Select");
    button(L_WIRE, "导线 W", "Wire");
    button(L_NODE, "节点 J", "Node");
    button(L_FIT, "适合画面", "Fit");
    toolbar->AddSeparator();
    button(L_STEP, "单步 F7", "Step");
    button(L_TICK, "时钟 F8", "Clock");
    button(L_RUN, "运行 F5", "Run");
    button(L_RESET, "复位", "Reset");
    button(L_TABLE, "真值表", "Table");
    toolbar->Realize();
    root->Add(toolbar, 0, wxEXPAND);
    auto shortcuts = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxTB_HORIZONTAL | wxTB_TEXT | wxTB_NODIVIDER);
    shortcuts->SetName("logic-components");
    shortcuts->SetToolBitmapSize({28, 28});
    for (const auto &entry : std::vector<std::pair<std::string, std::string>>{{"Input", "输入"},
                                                                              {"Output", "输出"},
                                                                              {"Clock", "时钟"},
                                                                              {"Not", "非门"},
                                                                              {"And", "与门"},
                                                                              {"Or", "或门"},
                                                                              {"Xor", "异或"},
                                                                              {"Nand", "与非"},
                                                                              {"Mux", "选择器"},
                                                                              {"Splitter", "分线器"},
                                                                              {"DFF", "D 触发器"},
                                                                              {"Register", "寄存器"},
                                                                              {"LED", "LED"}}) {
        int id = wxWindow::NewControlId();
        auto kind = entry.first;
        shortcuts->AddTool(id, U(entry.second), icon(kind), U(pretty(kind)));
        shortcuts->SetToolLongHelp(id, U(kind));
        shortcuts->Bind(
            wxEVT_TOOL,
            [this, kind](wxCommandEvent &) {
                canvas->cancel();
                setAppearance(false);
                canvas->placing = kind;
                canvas->subcircuit.clear();
                canvas->tool = "Place";
                canvas->SetFocus();
                rebuild(false);
            },
            id);
    }
    shortcuts->Realize();
    root->Add(shortcuts, 0, wxEXPAND);
    for (auto item : std::vector<std::pair<int, std::string>>{{wxID_NEW, "New"},
                                                              {wxID_OPEN, "Open"},
                                                              {wxID_SAVE, "Save"},
                                                              {wxID_COPY, "Copy"},
                                                              {wxID_CUT, "Cut"},
                                                              {wxID_PASTE, "Paste"},
                                                              {wxID_UNDO, "Undo"},
                                                              {wxID_REDO, "Redo"},
                                                              {L_HELP, "Help"}})
        bar->FindItem(item.first)->SetBitmap(icon(item.second));
    auto body = new wxBoxSizer(wxHORIZONTAL);
    auto left = new wxPanel(this);
    left->SetMinSize({240, -1});
    auto ls = new wxBoxSizer(wxVERTICAL);
    ls->Add(new wxStaticText(left, wxID_ANY, U("项目 / 数字元件库")), 0, wxALL, 10);
    circuits = new wxChoice(left, wxID_ANY);
    ls->Add(circuits, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    search = new wxTextCtrl(left, wxID_ANY);
    search->SetHint(U("搜索元件名称或类型"));
    ls->Add(search, 0, wxEXPAND | wxALL, 8);
    tree =
        new wxTreeCtrl(left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT);
    auto images = new wxImageList(20, 20, true);
    auto addIcon = [&](const std::string &kind) {
        iconIds[kind] = images->Add(icon(kind).GetBitmap({20, 20}));
    };
    addIcon("Folder");
    addIcon("Subcircuit");
    addIcon("Select");
    addIcon("Wire");
    addIcon("Node");
    for (const auto &item : library())
        addIcon(item.kind);
    tree->AssignImageList(images);
    ls->Add(tree, 1, wxEXPAND | wxALL, 6);
    auto hints = new wxStaticText(
        left, wxID_ANY,
        U("单击库元件，再点画布放置\n也可从库直接拖入画布\nShift 多选 · 空白处框选\n滚轮缩放 · "
          "中键平移\n双击输入切换 · 双击实例进入\n连线时点导线创建分支\n双击空白结束导线 · Esc 取消"));
    ls->Add(hints, 0, wxALL, 10);
    left->SetSizer(ls);
    body->Add(left, 0, wxEXPAND);
    canvas = new LogicCanvas(this, this);
    body->Add(canvas, 1, wxEXPAND);
    auto right = new wxPanel(this);
    right->SetMinSize({320, -1});
    auto rs = new wxBoxSizer(wxVERTICAL);
    selection = new wxStaticText(right, wxID_ANY, U("未选中元件"));
    rs->Add(selection, 0, wxEXPAND | wxALL, 10);
    properties = new wxGrid(right, wxID_ANY);
    properties->SetName("component-properties");
    properties->CreateGrid(P_COUNT, 2);
    properties->SetColLabelValue(0, U("属性"));
    properties->SetColLabelValue(1, U("值"));
    properties->SetRowLabelSize(0);
    properties->SetColLabelSize(30);
    properties->SetDefaultRowSize(32);
    properties->SetColSize(0, 125);
    properties->SetColSize(1, 170);
    properties->SetDefaultCellAlignment(wxALIGN_LEFT, wxALIGN_CENTER);
    properties->SetGridLineColour(wxColour("#cbd5e1"));
    properties->SetLabelBackgroundColour(wxColour("#eef2f7"));
    properties->DisableDragRowSize();
    properties->DisableDragColMove();
    properties->SetMinSize({300, 220});
    const char *captions[] = {"类型",          "对象 ID",    "名称 / 网络标签", "数据位宽 (1–32)",
                              "输入 / 初始值", "存储器初值", "当前信号",        "图形线宽 (1–20)",
                              "填充"};
    for (int row = 0; row < P_COUNT; ++row) {
        properties->SetCellValue(row, 0, U(captions[row]));
        properties->SetReadOnly(row, 0);
        properties->SetCellBackgroundColour(row, 0, wxColour("#f1f5f9"));
    }
    properties->SetCellEditor(P_FILL, 1, new wxGridCellBoolEditor());
    properties->SetCellRenderer(P_FILL, 1, new wxGridCellBoolRenderer());
    properties->SetToolTip(
        U("双击值或按 F2 编辑，回车确认。数值支持十进制和 0x；存储器初值以空格分隔，最多 256 字。"));
    properties->Bind(wxEVT_GRID_CELL_CHANGED, [this](wxGridEvent &e) {
        if (syncingProperties)
            return;
        try {
            apply();
            CallAfter([this] { inspect(); });
        } catch (const std::exception &error) {
            properties->SetCellValue(e.GetRow(), e.GetCol(), e.GetString());
            diagnosticDetail->ChangeValue(U("属性未修改：" + std::string(error.what())));
        }
    });
    properties->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
        properties->SetColSize(1, std::max(150, properties->GetClientSize().x - 145));
        e.Skip();
    });
    rs->Add(properties, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    diagnosticSummary = new wxStaticText(right, wxID_ANY, U("连接检查"));
    rs->Add(diagnosticSummary, 0, wxEXPAND | wxALL, 10);
    diagnostics = new wxListBox(right, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_HSCROLL);
    diagnostics->SetName("connection-diagnostics");
    diagnostics->SetMinSize({300, 140});
    diagnostics->SetToolTip(U("单击查看完整说明，双击定位画布中的元件或导线"));
    rs->Add(diagnostics, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
    diagnosticDetail = new wxTextCtrl(right, wxID_ANY, "", wxDefaultPosition, {300, 95},
                                      wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
    diagnosticDetail->SetName("connection-diagnostic-detail");
    rs->Add(diagnosticDetail, 0, wxEXPAND | wxALL, 8);
    right->SetSizer(rs);
    body->Add(right, 0, wxEXPAND);
    root->Add(body, 1, wxEXPAND);
    status = new wxStaticText(this, wxID_ANY, "");
    root->Add(status, 0, wxEXPAND | wxALL, 8);
    SetSizer(root);
    auto bind = [&](int id, std::function<void()> fn) {
        Bind(
            wxEVT_MENU,
            [this, fn](wxCommandEvent &) {
                try {
                    fn();
                } catch (const std::exception &e) {
                    fail(e);
                }
            },
            id);
    };
    bind(wxID_NEW, [this] {
        if (!discard())
            return;
        stop();
        canvas->cancel();
        canvas->appearance = false;
        history.reset(Project{});
        current = "main";
        savedPath.clear();
        canvas->selected.clear();
        rebuild(true, true);
        canvas->fit();
    });
    bind(wxID_OPEN, [this] {
        wxFileDialog d(this, U("打开数字工程 / 导入 外部电路"), "", "", openFilter,
                       wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (d.ShowModal() == wxID_OK)
            load(d.GetPath());
    });
    bind(L_DEMO, [this] {
        if (!discard())
            return;
        stop();
        canvas->cancel();
        canvas->appearance = false;
        history.reset(halfAdder());
        current = "main";
        savedPath.clear();
        canvas->selected.clear();
        rebuild(true, true);
        canvas->fit();
    });
    bind(wxID_SAVE, [this] { save(); });
    bind(L_SAVE_AS, [this] { save(true); });
    bind(wxID_EXIT, [this] { Close(); });
    bind(wxID_UNDO, [this] {
        if (auto t = dynamic_cast<wxTextCtrl *>(FindFocus())) {
            t->Undo();
            return;
        }
        undo();
    });
    bind(wxID_REDO, [this] {
        if (auto t = dynamic_cast<wxTextCtrl *>(FindFocus())) {
            t->Redo();
            return;
        }
        undo(true);
    });
    bind(wxID_COPY, [this] {
        if (auto t = dynamic_cast<wxTextCtrl *>(FindFocus())) {
            t->Copy();
            return;
        }
        copySelection();
    });
    bind(wxID_CUT, [this] {
        if (auto t = dynamic_cast<wxTextCtrl *>(FindFocus())) {
            t->Cut();
            return;
        }
        copySelection();
        removeSelection();
    });
    bind(wxID_PASTE, [this] {
        if (auto t = dynamic_cast<wxTextCtrl *>(FindFocus())) {
            t->Paste();
            return;
        }
        pasteSelection();
    });
    bind(wxID_DELETE, [this] {
        if (auto t = dynamic_cast<wxTextCtrl *>(FindFocus())) {
            long a, b;
            t->GetSelection(&a, &b);
            t->Remove(a, a == b ? a + 1 : b);
            return;
        }
        removeSelection();
    });
    bind(wxID_SELECTALL, [this] {
        if (auto t = dynamic_cast<wxTextCtrl *>(FindFocus())) {
            t->SelectAll();
            return;
        }
        canvas->selected.clear();
        for (const auto &s : circuit().shapes)
            if (s.appearance == canvas->appearance)
                canvas->selected.insert(s.id);
        if (canvas->appearance) {
            inspect();
            canvas->Refresh();
            return;
        }
        for (auto &p : circuit().parts)
            canvas->selected.insert(p.id);
        for (auto &n : circuit().nodes)
            canvas->selected.insert(n.id);
        for (auto &w : circuit().wires)
            canvas->selected.insert(w.id);
        inspect();
        canvas->Refresh();
    });
    bind(wxID_PROPERTIES, [this] { properties->SaveEditControlValue(); });
    for (auto pair : std::vector<std::pair<int, std::string>>{
             {L_SELECT, "Select"}, {L_WIRE, "Wire"}, {L_NODE, "Node"}, {L_TEXT, "Place"}})
        bind(pair.first, [this, pair] {
            canvas->cancel();
            if (pair.first != L_SELECT)
                setAppearance(false);
            canvas->tool = pair.second;
            if (pair.first == L_TEXT)
                canvas->placing = "Text";
            canvas->SetFocus();
            rebuild(false);
        });
    bind(L_APPEARANCE, [this] { setAppearance(!canvas->appearance); });
    for (auto entry : std::vector<std::pair<int, std::string>>{{L_LINE, "Line"},
                                                               {L_CURVE, "Curve"},
                                                               {L_RECTANGLE, "Rectangle"},
                                                               {L_ELLIPSE, "Ellipse"},
                                                               {L_CIRCLE, "Circle"}})
        bind(entry.first, [this, entry] {
            canvas->cancel();
            canvas->selected.clear();
            canvas->tool = entry.second;
            canvas->SetFocus();
            rebuild(false);
        });
    bind(L_FIT, [this] { canvas->fit(); });
    bind(L_STEP, [this] {
        stop();
        simulator->step();
        rebuild(false);
    });
    bind(L_TICK, [this] {
        stop();
        tick();
    });
    bind(L_RUN, [this] {
        if (timer.IsRunning())
            stop();
        else
            timer.Start(500);
        rebuild(false);
    });
    bind(L_RESET, [this] {
        stop();
        simulator = std::make_unique<Simulator>(history.project, current);
        rebuild(false);
    });
    bind(L_TRACE, [this] {
        std::ostringstream o;
        o << "时间\t实例路径\t引脚\t值\n";
        for (auto &t : simulator->trace())
            o << t.time << '\t' << t.object << '\t' << t.pin << '\t' << t.value << '\n';
        showReport("最近 2000 条信号事件", o.str());
    });
    bind(L_TABLE, [this] {
        stop();
        showTable(truthTable(history.project, current));
    });
    bind(L_EXPRESSION, [this] {
        wxTextEntryDialog d(this, U("变量用英文字母/下划线；支持 ! ~ & * ^ | + 和括号"), U("表达式分析"),
                            "(A & !B) | (!A & B)");
        if (d.ShowModal() == wxID_OK)
            showTable(expressionTable(utf8(d.GetValue())));
    });
    bind(L_CSV, [this] {
        if (!hasTable)
            throw std::runtime_error("请先生成真值表或分析表达式");
        wxFileDialog d(this, U("导出分析 CSV"), "", "truth-table.csv", "CSV (*.csv)|*.csv",
                       wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (d.ShowModal() == wxID_OK)
            writeAtomic(fsPath(d.GetPath()), "\xEF\xBB\xBF" + lastTable.csv());
    });
    bind(L_PNG, [this] {
        canvas->cancel();
        wxFileDialog d(this, U("导出完整电路"), "", "logic.png", "PNG (*.png)|*.png",
                       wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (d.ShowModal() == wxID_OK)
            canvas->exportPng(d.GetPath());
    });
    bind(L_NEW_CIRCUIT, [this] { newCircuit(); });
    bind(L_NEW_COMPONENT, [this] { newComponent(); });
    bind(L_SAVE_COMPONENT, [this] { saveComponent(); });
    bind(L_LOAD_COMPONENT, [this] { loadComponent(); });
    bind(L_NETLIST, [this] { exportNetlist(); });
    bind(L_WRAP, [this] { encapsulate(); });
    bind(L_RENAME, [this] { renameCircuit(); });
    bind(L_DEL_CIRCUIT, [this] {
        if (current == history.project.main)
            throw std::runtime_error("不能删除主电路");
        for (auto &c : history.project.circuits)
            for (auto &p : c.parts)
                if (p.kind == "Subcircuit" && p.circuit == current)
                    throw std::runtime_error("该子电路仍被实例引用");
        change([&] {
            auto name = current;
            auto &cs = history.project.circuits;
            cs.erase(std::remove_if(cs.begin(), cs.end(), [&](auto &c) { return c.name == name; }), cs.end());
            current = history.project.main;
            canvas->selected.clear();
        });
    });
    bind(L_HELP, [this] {
        showReport(
            "数字逻辑使用说明",
            "1. 选择左侧七类元件，在画布单击放置，或从库拖入。选择工具下拖动元件，导线跟随引脚。\n"
            "2. W 连线：点击引脚开始，空白处设置拐点，点击目标引脚结束。双击空白可结束到节点。\n"
            "3. 点击已有导线可创建分支；交叉不自动连接。J 创建节点；选中节点可设置网络标签。\n"
            "4. 双击输入/按钮切换；多位值在右侧属性表格编辑。F7 推进一个事件时刻，F8 翻转时钟，F5 运行/暂停。\n"
            "5. 项目菜单新建子电路，用输入/输出引脚定义接口；左侧子电路库放置实例，双击进入。\n"
            "   封装功能将当前电路的副本保存为子电路定义；每个实例的寄存器、RAM 状态独立。\n"
            "6. Ctrl+S 保存整个工程（包括全部子电路）；Ctrl+Z/Y 撤销重做；Ctrl+C/V 使用系统剪贴板。\n"
            "7. 分析菜单生成组合电路真值表和标准与或表达式，可导出 CSV；输入最多 10 位。\n"
            "8. 文件菜单导出完整电路 PNG，不限于屏幕区域。\n"
            "9. 项目 → 新建自定义元件：设置接口后在画布实现逻辑，可保存为 .component.json 并跨工程导入。\n"
            "10. 文件 → 导出网表：导出当前电路及嵌套元件，支持 KiCad .net 和完整数字 .net.json。\n"
            "9. 诊断列表双击定位；事件追踪显示传播时刻与实例路径。复位后可用 F7 从初始队列单步调试。\n"
            "10. 文件 → 打开支持 外部电路 2.7 .circ 受限导入；请查看诊断，保存仍使用 .logic.json。\n"
            "位宽 1–32；两输入逻辑门；MUX 2:1；译码器 2:4；RAM/ROM 256 字；零扩展器 1→N。\n"
            "时序元件均为上升沿触发，无异步复位脚；复位操作恢复属性中的初值。\n"
            "整数运算无符号、结果截断到位宽；加减法 C 表示进位/借位；移位数量由 B 给出。\n"
            "标准与或式未最小化。Text 是注释。按钮以双击保持切换。\n"
            "11. 工具菜单：L 直线、B 曲线、R 矩形、E 椭圆、C 圆。拖动绘制，曲线再单击确定弯曲。\n"
            "12. 编辑元件外观：围绕绿色接口绘制符号；返回电路后实例同步显示。图形不参与电气连接。\n"
            "13. S 选择后拖动图形或控制点；右侧属性面板修改线宽、填充。Shift 约束，Esc 取消。");
    });
    search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { populate(); });
    circuits->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
        canvas->cancel();
        stop();
        setAppearance(false);
        current = utf8(circuits->GetStringSelection());
        canvas->selected.clear();
        rebuild();
        canvas->fit();
    });
    tree->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent &e) {
        auto item = dynamic_cast<Item *>(tree->GetItemData(e.GetItem()));
        if (!item)
            return;
        canvas->cancel();
        setAppearance(false);
        canvas->placing = item->kind;
        canvas->subcircuit = item->circuit;
        canvas->tool =
            (item->kind == "Select" || item->kind == "Wire" || item->kind == "Node") ? item->kind : "Place";
        canvas->SetFocus();
        rebuild(false);
    });
    tree->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent &e) {
        auto item = dynamic_cast<Item *>(tree->GetItemData(e.GetItem()));
        if (!item || item->kind == "Select" || item->kind == "Wire" || item->kind == "Node")
            return;
        canvas->cancel();
        setAppearance(false);
        canvas->placing = item->kind;
        canvas->subcircuit = item->circuit;
        canvas->tool = "Place";
        e.Allow();
    });
    tree->Bind(wxEVT_TREE_END_DRAG, [this](wxTreeEvent &e) {
        try {
            auto p = canvas->ScreenToClient(tree->ClientToScreen(e.GetPoint()));
            if (canvas->GetClientRect().Contains(p) && canvas->tool == "Place")
                canvas->place(snap(canvas->view.world({double(p.x), double(p.y)}), 10));
        } catch (const std::exception &x) {
            fail(x);
        }
    });
    diagnostics->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &e) { showDiagnostic(e.GetSelection(), false); });
    diagnostics->Bind(wxEVT_LISTBOX_DCLICK,
                      [this](wxCommandEvent &e) { showDiagnostic(e.GetSelection(), true); });
    Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
        try {
            tick();
        } catch (const std::exception &e) {
            stop();
            fail(e);
        }
    });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &e) {
        canvas->cancel();
        if (e.CanVeto() && !discard()) {
            e.Veto();
            return;
        }
        stop();
        e.Skip();
    });
    rebuild(true, true);
    Centre();
}
void LogicEditor::fail(const std::exception &e) {
    wxMessageBox(U(e.what()), U("数字逻辑"), wxOK | wxICON_ERROR, this);
}
void LogicEditor::stop() {
    timer.Stop();
}
void LogicEditor::populate() {
    tree->Freeze();
    tree->DeleteAllItems();
    auto root = tree->AddRoot("root");
    std::map<std::string, wxTreeItemId> cats;
    wxString query = search->GetValue().Lower();
    for (auto &p : library()) {
        if (!query.empty() && !U(p.name + " " + p.kind).Lower().Contains(query))
            continue;
        if (!cats.count(p.category))
            cats[p.category] = tree->AppendItem(root, U(p.category), iconIds.at("Folder"));
        tree->AppendItem(cats[p.category], U(p.name), iconIds.at(p.kind), -1, new Item(p.kind));
    }
    if (query.empty()) {
        auto base = cats["基本 (Base)"];
        tree->AppendItem(base, U("选择 / 拖动"), iconIds.at("Select"), -1, new Item("Select"));
        tree->AppendItem(base, U("导线 / 总线"), iconIds.at("Wire"), -1, new Item("Wire"));
        tree->AppendItem(base, U("节点 / 网络标签"), iconIds.at("Node"), -1, new Item("Node"));
    }
    auto subs = tree->AppendItem(root, U("自定义元件 / 子电路"), iconIds.at("Folder"));
    for (auto &c : history.project.circuits)
        if (c.name != current && (query.empty() || U(c.name).Lower().Contains(query)))
            tree->AppendItem(subs, U(c.name), iconIds.at("Subcircuit"), -1, new Item("Subcircuit", c.name));
    for (auto &cat : cats)
        if (!query.empty() || cat.first == "逻辑门 (Gates)" || cat.first == "线路 (Wiring)")
            tree->Expand(cat.second);
    tree->Expand(subs);
    tree->Thaw();
}
void LogicEditor::rebuild(bool reset, bool lib) {
    if (reset) {
        stop();
        simulator = std::make_unique<Simulator>(history.project, current);
        simulator->settle();
        hasTable = false;
    }
    if (lib) {
        circuits->Clear();
        for (auto &c : history.project.circuits)
            circuits->Append(U(c.name));
        circuits->SetStringSelection(U(current));
        populate();
    }
    SetTitle(U(std::string(history.dirty() ? "* " : "") + history.project.name + " — " + current +
               " · PracticeEDA 数字逻辑"));
    refreshDiagnostics();
    status->SetLabel(
        U("工具：" + canvas->tool + (canvas->tool == "Place" ? " / " + pretty(canvas->placing) : "") +
          "   |   " + (timer.IsRunning() ? "运行中" : "已暂停") + "   |   t=" +
          std::to_string(simulator->time()) + "   待处理事件=" + std::to_string(simulator->pending()) +
          "   |   元件=" + std::to_string(circuit().parts.size()) +
          "   导线=" + std::to_string(circuit().wires.size())));
    GetMenuBar()
        ->FindItem(L_APPEARANCE)
        ->SetItemLabel(U(canvas->appearance ? "返回电路编辑" : "编辑元件外观"));
    if (canvas->appearance || shapeKind(canvas->tool))
        status->SetLabel(
            U(std::string(canvas->appearance ? "元件外观 · " : "电路图形 · ") + current +
              " | 拖动绘制；曲线松开后移动并单击确定弯曲；Shift 约束；Esc 取消；选择后拖动控制点"));
    inspect();
    canvas->Refresh();
}
void LogicEditor::refreshDiagnostics() {
    std::optional<Issue> picked;
    int oldIndex = diagnostics->GetSelection();
    if (oldIndex >= 0 && size_t(oldIndex) < issues.size())
        picked = issues[oldIndex];
    issues = circDiagnostics(circuit());
    auto simulationIssues = simulator->issues();
    issues.insert(issues.end(), simulationIssues.begin(), simulationIssues.end());
    diagnostics->Freeze();
    diagnostics->Clear();
    int errors = 0, warnings = 0, notes = 0, selectedIndex = -1;
    for (size_t index = 0; index < issues.size(); ++index) {
        const auto &issue = issues[index];
        std::string severity = issue.severity == "error"     ? "错误"
                               : issue.severity == "warning" ? "警告"
                                                             : "提示";
        if (issue.severity == "error")
            ++errors;
        else if (issue.severity == "warning")
            ++warnings;
        else
            ++notes;
        diagnostics->Append(U("[" + severity + "] " + issue.object + " · " + issue.message));
        if (picked && picked->object == issue.object && picked->message == issue.message &&
            picked->severity == issue.severity)
            selectedIndex = int(index);
    }
    if (issues.empty()) {
        diagnostics->Append(U("检查通过 · 未发现连接问题"));
        diagnosticSummary->SetLabel(U("连接检查 · 通过"));
        diagnosticDetail->ChangeValue(U("当前电路未发现位宽冲突、悬空输入或信号错误。"));
    } else {
        diagnosticSummary->SetLabel(U("连接检查 · " + std::to_string(errors) + " 错误 / " +
                                      std::to_string(warnings) + " 警告 / " + std::to_string(notes) +
                                      " 提示"));
        if (selectedIndex < 0)
            selectedIndex = 0;
        diagnostics->SetSelection(selectedIndex);
        showDiagnostic(selectedIndex, false);
    }
    diagnosticSummary->SetForegroundColour(wxColour(errors ? "#dc2626" : warnings ? "#a16207" : "#15803d"));
    diagnostics->Thaw();
}
void LogicEditor::showDiagnostic(int index, bool locate) {
    if (index < 0 || size_t(index) >= issues.size())
        return;
    const auto issue = issues[index];
    std::string detail = "对象：" + issue.object + "\n" + issue.message;
    if (issue.message.find("位宽") != std::string::npos)
        detail += "\n请使连接两端位宽一致，或使用分线器 / 合线器。";
    else if (issue.message.find("高阻") != std::string::npos)
        detail += "\n请检查输入是否接到有效输出，以及三态缓冲器是否使能。";
    else if (issue.message.find("冲突") != std::string::npos)
        detail += "\n请检查多个输出是否驱动同一网络，以及除零等非法运算。";
    else if (issue.message.find("未知") != std::string::npos)
        detail += "\n请沿输入方向检查未知信号来源及存储器初值。";
    diagnosticDetail->ChangeValue(U(detail));
    if (!locate)
        return;
    auto id = issue.object;
    std::optional<Point> at;
    auto find = [&] {
        if (auto part = circuit().part(id))
            at = part->at;
        for (const auto &node : circuit().nodes)
            if (node.id == id)
                at = node.at;
        for (const auto &wire : circuit().wires)
            if (wire.id == id)
                at = endpoint(history.project, circuit(), wire.a);
    };
    find();
    if (!at && id.find('/') != std::string::npos) {
        id = id.substr(0, id.find('/'));
        find();
    }
    if (!at)
        return;
    setAppearance(false);
    canvas->cancel();
    canvas->tool = "Select";
    canvas->selected = {id};
    auto size = canvas->GetClientSize();
    canvas->view.offset = Point{size.x / 2.0, size.y / 2.0} - *at * canvas->view.zoom;
    inspect();
    canvas->Refresh();
}
void LogicEditor::inspect() {
    // A simulation tick must never overwrite an in-progress cell edit.
    if (properties->IsCellEditControlEnabled() && inspectedSelection == canvas->selected)
        return;
    if (properties->IsCellEditControlEnabled()) {
        syncingProperties = true;
        properties->HideCellEditControl();
        properties->DisableCellEditControl();
        syncingProperties = false;
    }
    inspectedSelection = canvas->selected;
    properties->BeginBatch();
    for (int row = 0; row < P_COUNT; ++row) {
        properties->SetCellValue(row, 1, "");
        properties->SetReadOnly(row, 1);
        properties->SetCellBackgroundColour(row, 1, wxColour("#f1f5f9"));
        properties->HideRow(row);
    }
    auto set = [&](int row, const std::string &v, bool editable = false) {
        properties->ShowRow(row);
        properties->SetCellValue(row, 1, U(v));
        properties->SetReadOnly(row, 1, !editable);
        properties->SetCellBackgroundColour(row, 1, wxColour(editable ? "#ffffff" : "#f1f5f9"));
    };
    selection->SetLabel(U("选中 " + std::to_string(canvas->selected.size()) + " 个对象"));
    if (canvas->selected.empty() || canvas->selected.size() > 1) {
        set(P_STROKE, std::to_string(drawingStroke), true);
        set(P_FILL, drawingFilled ? "1" : "", true);
        if (canvas->selected.empty())
            selection->SetLabel(U("元件属性 · 选择对象查看 / 编辑"));
    } else {
        const auto id = *canvas->selected.begin();
        set(P_ID, id);
        if (auto p = circuit().part(id)) {
            selection->SetLabel(U("元件属性 · " + pretty(p->kind)));
            set(P_TYPE, pretty(p->kind));
            set(P_LABEL, p->label, true);
            bool configurable = p->kind != "Subcircuit" && p->kind != "Text" && p->kind != "Clock" &&
                                p->kind != "Decoder" && p->kind != "Encoder";
            if (p->kind != "Text")
                set(P_WIDTH, std::to_string(p->width), configurable);
            bool sourceValue = p->kind == "Input" || p->kind == "Button" || p->kind == "Constant" ||
                               p->kind == "Clock" || p->kind == "DFF" || p->kind == "TFF" ||
                               p->kind == "JKFF" || p->kind == "SRFF" || p->kind == "Register" ||
                               p->kind == "Counter" || p->kind == "ShiftRegister";
            if (sourceValue)
                set(P_VALUE, std::to_string(p->value), true);
            if (p->kind == "RAM" || p->kind == "ROM") {
                std::ostringstream words;
                for (auto n : p->data)
                    words << n << ' ';
                set(P_MEMORY, words.str(), true);
            }
            auto pins = ports(history.project, *p);
            if (!pins.empty()) {
                auto pin = std::find_if(pins.begin(), pins.end(), [](const Port &q) { return q.output; });
                if (pin == pins.end())
                    pin = pins.begin();
                set(P_STATE, simulator->read({id, pin->id}).text());
            }
        } else {
            for (const auto &n : circuit().nodes)
                if (n.id == id) {
                    set(P_TYPE, "节点");
                    set(P_LABEL, n.label, true);
                    set(P_STATE, simulator->read({id, ""}).text());
                }
            for (const auto &w : circuit().wires)
                if (w.id == id) {
                    set(P_TYPE, "导线 / 总线");
                    set(P_STATE, simulator->read({id, ""}).text());
                }
            for (const auto &s : circuit().shapes)
                if (s.id == id) {
                    set(P_TYPE, s.kind);
                    set(P_STROKE, std::to_string(int(s.stroke)), true);
                    set(P_FILL, s.filled ? "1" : "", true);
                    drawingStroke = int(s.stroke);
                    drawingFilled = s.filled;
                }
        }
    }
    properties->EndBatch();
}
void LogicEditor::change(const std::function<void()> &action) {
    // A failed edit must restore both the full model and the currently open definition.
    canvas->cancel();
    auto before = history.project;
    auto oldCurrent = current;
    try {
        action();
        history.project.validate();
        history.commit(before);
        rebuild(true, true);
    } catch (...) {
        history.project = std::move(before);
        current = oldCurrent;
        rebuild(true, true);
        throw;
    }
}
void LogicEditor::apply() {
    if (!properties->IsReadOnly(P_STROKE, 1)) {
        long stroke;
        if (!properties->GetCellValue(P_STROKE, 1).ToLong(&stroke) || stroke < 1 || stroke > 20)
            throw std::runtime_error("图形线宽必须为 1–20");
        bool filled = properties->GetCellValue(P_FILL, 1) == "1";
        bool hasShapes = std::any_of(circuit().shapes.begin(), circuit().shapes.end(),
                                     [&](const Shape &s) { return canvas->selected.count(s.id) != 0; });
        if (hasShapes)
            change([&] {
                for (auto &s : circuit().shapes)
                    if (canvas->selected.count(s.id)) {
                        s.stroke = double(stroke);
                        s.filled = filled;
                    }
            });
        drawingStroke = int(stroke);
        drawingFilled = filled;
        return;
    }
    if (canvas->selected.size() != 1)
        return;
    auto id = *canvas->selected.begin();
    auto name = utf8(properties->GetCellValue(P_LABEL, 1));
    if (auto p = circuit().part(id)) {
        int w = p->width;
        if (!properties->IsReadOnly(P_WIDTH, 1)) {
            long n;
            if (!properties->GetCellValue(P_WIDTH, 1).ToLong(&n) || n < 1 || n > 32)
                throw std::runtime_error("位宽必须为 1–32");
            w = int(n);
        }
        uint32_t v = !properties->IsReadOnly(P_VALUE, 1) ? unsignedValue(properties->GetCellValue(P_VALUE, 1))
                                                         : p->value;
        std::vector<uint32_t> data = p->data;
        if (!properties->IsReadOnly(P_MEMORY, 1)) {
            data.clear();
            std::istringstream in(utf8(properties->GetCellValue(P_MEMORY, 1)));
            std::string word;
            while (in >> word)
                data.push_back(unsignedValue(U(word)));
        }
        // Poking a source does not reset registers in the running simulation.
        if ((p->kind == "Input" || p->kind == "Button") && w == p->width && name == p->label) {
            auto before = history.project;
            p->value = v;
            history.commit(before);
            simulator->setInput(id, v);
            simulator->settle();
            rebuild(false);
            return;
        }
        change([&] {
            auto q = circuit().part(id);
            q->label = name;
            q->width = w;
            q->value = v;
            q->data = data;
        });
    } else
        change([&] {
            for (auto &n : circuit().nodes)
                if (n.id == id)
                    n.label = name;
        });
}
bool LogicEditor::discard() {
    properties->SaveEditControlValue();
    canvas->cancel();
    if (!history.dirty())
        return true;
    int answer = wxMessageBox(U("数字工程尚未保存，是否保存？"), U("保存工程"),
                              wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    return answer == wxNO || (answer == wxYES && save());
}
bool LogicEditor::save(bool as) {
    try {
        properties->SaveEditControlValue();
        canvas->cancel();
        wxString path = U(savedPath);
        if (as || path.empty()) {
            wxFileDialog d(this, U("保存数字逻辑工程"), "", "project.logic.json", filter,
                           wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (d.ShowModal() != wxID_OK)
                return false;
            path = d.GetPath();
        }
        if (path.Lower().EndsWith(".circ"))
            path += ".logic.json";
        history.project.validate();
        writeAtomic(fsPath(path), serialize(history.project));
        savedPath = utf8(path);
        history.markSaved();
        rebuild(false);
        return true;
    } catch (const std::exception &e) {
        fail(e);
        return false;
    }
}
bool LogicEditor::load(const wxString &path) {
    try {
        bool isCirc = path.Lower().EndsWith(".circ");
        auto bytes = readFile(fsPath(path));
        auto loaded = isCirc ? importCirc(bytes) : deserialize(bytes);
        if (!discard())
            return false;
        stop();
        canvas->cancel();
        canvas->appearance = false;
        history.reset(loaded, !isCirc);
        current = loaded.main;
        savedPath = isCirc ? std::string{} : utf8(path);
        canvas->selected.clear();
        rebuild(true, true);
        canvas->fit();
        return true;
    } catch (const std::exception &e) {
        fail(e);
        return false;
    }
}
void LogicEditor::undo(bool redo) {
    canvas->cancel();
    if (redo ? history.redo() : history.undo()) {
        if (std::none_of(history.project.circuits.begin(), history.project.circuits.end(),
                         [&](auto &c) { return c.name == current; }))
            current = history.project.main;
        canvas->selected.clear();
        rebuild(true, true);
    }
}
void LogicEditor::copySelection() {
    canvas->cancel();
    if (canvas->selected.empty())
        return;
    if (!wxTheClipboard->Open())
        throw std::runtime_error("剪贴板被占用");
    bool ok =
        wxTheClipboard->SetData(new wxTextDataObject(U(copy(history.project, circuit(), canvas->selected))));
    wxTheClipboard->Close();
    if (!ok)
        throw std::runtime_error("复制失败");
}
void LogicEditor::pasteSelection() {
    if (!wxTheClipboard->Open())
        throw std::runtime_error("剪贴板被占用");
    wxTextDataObject data;
    bool ok = wxTheClipboard->GetData(data);
    wxTheClipboard->Close();
    if (!ok)
        throw std::runtime_error("剪贴板没有数字电路数据");
    auto content = utf8(data.GetText());
    change([&] {
        if (canvas->appearance) {
            const auto clip = deserialize(content);
            const auto &picked = clip.circuit(clip.main);
            if (!picked.parts.empty() || !picked.nodes.empty() || !picked.wires.empty())
                throw std::runtime_error("元件外观只接受图形，请返回电路编辑后粘贴元件");
        }
        canvas->selected = paste(history.project, circuit(), content, {40, 40});
        for (auto &s : circuit().shapes)
            if (canvas->selected.count(s.id))
                s.appearance = canvas->appearance;
    });
}
void LogicEditor::removeSelection() {
    if (canvas->selected.empty())
        return;
    change([&] {
        erase(circuit(), canvas->selected);
        for (auto &c : history.project.circuits)
            c.wires.erase(std::remove_if(c.wires.begin(), c.wires.end(),
                                         [&](auto &w) {
                                             try {
                                                 endpoint(history.project, c, w.a);
                                                 endpoint(history.project, c, w.b);
                                                 return false;
                                             } catch (...) {
                                                 return true;
                                             }
                                         }),
                          c.wires.end());
        canvas->selected.clear();
    });
}
void LogicEditor::setAppearance(bool enabled) {
    if (canvas->appearance == enabled)
        return;
    canvas->cancel();
    stop();
    canvas->selected.clear();
    canvas->tool = "Select";
    canvas->appearance = enabled;
    rebuild(false);
    canvas->fit();
}
void LogicEditor::newComponent() {
    wxDialog dialog(this, wxID_ANY, U("新建自定义元件"), wxDefaultPosition, {520, 480});
    dialog.SetName("custom-component-dialog");
    auto layout = new wxBoxSizer(wxVERTICAL);
    auto add = [&](const char *caption, const char *initial, bool multiline) {
        layout->Add(new wxStaticText(&dialog, wxID_ANY, U(caption)), 0, wxLEFT | wxRIGHT | wxTOP, 12);
        auto control = new wxTextCtrl(&dialog, wxID_ANY, U(initial), wxDefaultPosition,
                                      wxSize(450, multiline ? 90 : -1), multiline ? wxTE_MULTILINE : 0);
        layout->Add(control, multiline ? 1 : 0, wxEXPAND | wxALL, 12);
        return control;
    };
    auto name = add("元件名称", "MyComponent", false);
    auto inputs = add("输入接口：每行 名称:位宽（1–32），可留空", "A:1\nB:1", true);
    auto outputs = add("输出接口：每行 名称:位宽（1–32），可留空", "Y:1", true);
    name->SetName("custom-component-name");
    inputs->SetName("custom-component-inputs");
    outputs->SetName("custom-component-outputs");
    layout->Add(
        new wxStaticText(&dialog, wxID_ANY,
                         U("创建后添加逻辑并连线，选择“工具 → 编辑元件外观”绘制符号；返回 main 放置实例。")),
        0, wxALL, 12);
    layout->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 12);
    dialog.SetSizerAndFit(layout);
    dialog.CentreOnParent();
    while (dialog.ShowModal() == wxID_OK) {
        try {
            std::vector<ComponentPin> pins;
            auto parse = [&](wxTextCtrl *control, bool output) {
                std::istringstream lines(utf8(control->GetValue()));
                std::string line;
                while (std::getline(lines, line)) {
                    auto row = U(line).Trim().Trim(false);
                    if (row.empty())
                        continue;
                    auto colon = row.find_last_of(':');
                    long bits = 0;
                    if (colon == wxString::npos || !row.Mid(colon + 1).ToLong(&bits) || bits < 1 || bits > 32)
                        throw std::runtime_error("接口格式应为 名称:位宽，例如 DATA:8");
                    pins.push_back({utf8(row.Left(colon).Trim().Trim(false)), output, int(bits)});
                }
            };
            parse(inputs, false);
            parse(outputs, true);
            auto componentName = utf8(name->GetValue().Trim().Trim(false));
            change([&] {
                createComponent(history.project, componentName, pins);
                canvas->appearance = false;
                current = componentName;
                canvas->selected.clear();
            });
            canvas->fit();
            return;
        } catch (const std::exception &e) {
            fail(e);
        }
    }
}

void LogicEditor::saveComponent() {
    properties->SaveEditControlValue();
    // Finish/cancel an in-progress gesture before serializing the committed definition.
    canvas->cancel();
    const auto bytes = exportComponent(history.project, current);
    wxFileDialog dialog(this, U("保存当前电路为自定义元件"), "", "custom.component.json",
                        U("自定义元件 (*.component.json)|*.component.json"),
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    dialog.SetName("save-component-dialog");
    if (dialog.ShowModal() == wxID_OK)
        writeAtomic(fsPath(dialog.GetPath()), bytes);
}

void LogicEditor::loadComponent() {
    wxFileDialog dialog(this, U("导入自定义元件"), "", "",
                        U("自定义元件 (*.component.json)|*.component.json"),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    dialog.SetName("load-component-dialog");
    if (dialog.ShowModal() != wxID_OK)
        return;
    const auto bytes = readFile(fsPath(dialog.GetPath()));
    std::string name;
    change([&] { name = importComponent(history.project, bytes); });
    // Import adds definitions only; selecting placement does not alter the active circuit.
    setAppearance(false);
    canvas->tool = "Place";
    canvas->placing = "Subcircuit";
    canvas->subcircuit = name;
    canvas->SetFocus();
    rebuild(false);
}

void LogicEditor::exportNetlist() {
    canvas->cancel();
    wxFileDialog dialog(this, U("导出当前电路网表（包含嵌套元件）"), "", "circuit.net",
                        U("KiCad 逻辑连接网表 (*.net)|*.net|完整数字网表 (*.net.json)|*.net.json"),
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    dialog.SetName("export-netlist-dialog");
    if (dialog.ShowModal() != wxID_OK)
        return;
    // Compile the committed project separately: live input changes, RAM and clock state stay intact.
    Simulator compiled(history.project, current);
    writeAtomic(fsPath(dialog.GetPath()), compiled.netlist(dialog.GetFilterIndex() == 0));
}

void LogicEditor::newCircuit() {
    wxTextEntryDialog d(this, U("子电路名称（在其中放置输入/输出引脚定义接口）"), U("新建子电路"),
                        "circuit1");
    if (d.ShowModal() != wxID_OK)
        return;
    auto name = utf8(d.GetValue());
    change([&] {
        Circuit c;
        c.name = name;
        canvas->appearance = false;
        history.project.circuits.push_back(c);
        current = name;
        canvas->selected.clear();
    });
    canvas->fit();
}
void LogicEditor::encapsulate() {
    wxTextEntryDialog d(this, U("将当前电路完整复制为可调用的子电路；接口来自输入/输出引脚"), U("封装子电路"),
                        U(current + "_module"));
    if (d.ShowModal() != wxID_OK)
        return;
    auto name = utf8(d.GetValue());
    if (std::none_of(circuit().parts.begin(), circuit().parts.end(),
                     [](auto &p) { return p.kind == "Input" || p.kind == "Output"; }))
        throw std::runtime_error("请先添加输入/输出引脚以定义接口");
    change([&] {
        auto c = circuit();
        c.name = name;
        for (auto &shape : c.shapes)
            shape.id = history.project.id("s");
        std::map<std::string, std::string> ids;
        for (auto &p : c.parts) {
            auto old = p.id;
            p.id = history.project.id("p");
            ids[old] = p.id;
        }
        for (auto &n : c.nodes) {
            auto old = n.id;
            n.id = history.project.id("j");
            ids[old] = n.id;
        }
        for (auto &w : c.wires) {
            w.id = history.project.id("w");
            w.a.object = ids.at(w.a.object);
            w.b.object = ids.at(w.b.object);
        }
        history.project.circuits.push_back(c);
    });
}
void LogicEditor::renameCircuit() {
    wxTextEntryDialog d(this, U("新电路名称"), U("重命名"), U(current));
    if (d.ShowModal() != wxID_OK)
        return;
    auto name = utf8(d.GetValue());
    change([&] {
        auto old = current;
        circuit().name = name;
        for (auto &c : history.project.circuits)
            for (auto &p : c.parts)
                if (p.circuit == old)
                    p.circuit = name;
        if (history.project.main == old)
            history.project.main = name;
        current = name;
    });
}
void LogicEditor::tick() {
    if (simulator->pending())
        simulator->settle();
    simulator->tick();
    auto found = simulator->issues();
    for (auto &i : found)
        if (i.message.find("事件预算") != std::string::npos)
            stop();
    rebuild(false);
}
void LogicEditor::showReport(const std::string &title, const std::string &content) {
    wxDialog d(this, wxID_ANY, U(title), wxDefaultPosition, {950, 700},
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto s = new wxBoxSizer(wxVERTICAL);
    auto t = new wxTextCtrl(&d, wxID_ANY, U(content), wxDefaultPosition, wxDefaultSize,
                            wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
    t->SetFont(wxFontInfo(11).Family(wxFONTFAMILY_TELETYPE));
    s->Add(t, 1, wxEXPAND | wxALL, 10);
    s->Add(d.CreateButtonSizer(wxOK), 0, wxEXPAND | wxALL, 10);
    d.SetSizer(s);
    d.ShowModal();
}
void LogicEditor::showTable(Table table) {
    lastTable = std::move(table);
    hasTable = true;
    showReport("真值表 / 等价表达式（文件菜单可导出 CSV）", lastTable.report());
}
LogicCanvas::LogicCanvas(LogicEditor *editor, wxWindow *parent)
    : wxPanel(parent, wxID_ANY), owner(editor), rippleTimer(this) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxWHITE);
    Bind(wxEVT_PAINT, &LogicCanvas::paint, this);
    auto mouseHandler = [this](auto event, auto fn) {
        Bind(event, [this, fn](wxMouseEvent &e) {
            try {
                (this->*fn)(e);
            } catch (const std::exception &x) {
                cancel();
                owner->fail(x);
            }
        });
    };
    mouseHandler(wxEVT_LEFT_DOWN, &LogicCanvas::leftDown);
    mouseHandler(wxEVT_LEFT_UP, &LogicCanvas::leftUp);
    mouseHandler(wxEVT_MOTION, &LogicCanvas::motion);
    mouseHandler(wxEVT_LEFT_DCLICK, &LogicCanvas::doubleClick);
    Bind(
        wxEVT_TIMER,
        [this](wxTimerEvent &) {
            updatePinHover();
            if (hoveredPin) {
                rippleFrame = (rippleFrame + 1) % 36;
                refreshPinHover();
            }
        },
        rippleTimer.GetId());
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
        mouseInside = false;
        clearPinHover();
        e.Skip();
    });
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &e) {
        mouseInside = true;
        hoverScreen = {double(e.GetX()), double(e.GetY())};
        updatePinHover();
        e.Skip();
    });
    Bind(wxEVT_KEY_DOWN, &LogicCanvas::key, this);
    Bind(wxEVT_MIDDLE_DOWN, [this](wxMouseEvent &e) {
        panning = true;
        clearPinHover();
        lastScreen = {double(e.GetX()), double(e.GetY())};
        if (!HasCapture())
            CaptureMouse();
    });
    Bind(wxEVT_MIDDLE_UP, [this](wxMouseEvent &) {
        panning = false;
        if (HasCapture())
            ReleaseMouse();
        updatePinHover();
    });
    Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &e) {
        hoverScreen = {double(e.GetX()), double(e.GetY())};
        mouseInside = GetClientRect().Contains(e.GetPosition());
        view.zoomAt({double(e.GetX()), double(e.GetY())},
                    std::clamp(view.zoom * (e.GetWheelRotation() > 0 ? 1.15 : 1 / 1.15), 0.15, 4.0));
        mouse = view.world(hoverScreen);
        updatePinHover();
        Refresh();
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { cancel(); });
    Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent &) {
        cancel();
        tool = "Select";
        owner->rebuild(false);
    });
}
void LogicCanvas::cancel() {
    draft.reset();
    curveControl = false;
    shapeHandle = -1;
    mouseInside = false;
    clearPinHover();
    if (before) {
        owner->history.project = *before;
        before.reset();
    }
    start.reset();
    bends.clear();
    dragging = false;
    panning = false;
    box = false;
    if (HasCapture())
        ReleaseMouse();
    Refresh();
}
std::optional<Endpoint> LogicCanvas::pinAt(Point at) const {
    if (appearance || shapeKind(tool))
        return {};
    auto &p = owner->history.project;
    auto &c = owner->circuit();
    std::optional<Endpoint> nearest;
    double closest = FromDIP(9) / view.zoom;
    auto consider = [&](Point position, Endpoint target) {
        double d = distance(at, position);
        if (d < closest) {
            closest = d;
            nearest = std::move(target);
        }
    };
    for (auto &q : c.parts)
        for (auto &pin : ports(p, q))
            consider(q.at + pin.at, {q.id, pin.id});
    for (auto &n : c.nodes)
        consider(n.at, {n.id, ""});
    return nearest;
}
void LogicCanvas::refreshPinHover() {
    auto at = view.screen(hoverAt);
    int radius = FromDIP(29);
    RefreshRect(wxRect(int(std::floor(at.x)) - radius, int(std::floor(at.y)) - radius, radius * 2 + 2,
                       radius * 2 + 2),
                false);
}
void LogicCanvas::clearPinHover() {
    rippleTimer.Stop();
    if (hoveredPin)
        refreshPinHover();
    hoveredPin.reset();
    rippleFrame = 0;
}
void LogicCanvas::updatePinHover() {
    if (!mouseInside || dragging || panning || box || tool == "Place") {
        clearPinHover();
        return;
    }
    auto candidate = pinAt(view.world(hoverScreen));
    if (!candidate) {
        clearPinHover();
        return;
    }
    auto at = endpoint(owner->history.project, owner->circuit(), *candidate);
    if (!hoveredPin || !(*hoveredPin == *candidate) || distance(at, hoverAt) > 0.01) {
        clearPinHover();
        hoveredPin = candidate;
        hoverAt = at;
        refreshPinHover();
    }
    if (!rippleTimer.IsRunning())
        rippleTimer.Start(32);
}
void LogicCanvas::drawPinHover(wxGraphicsContext &g) {
    if (!hoveredPin)
        return;
    // Draw in screen coordinates: zooming changes neither the hint nor its hit radius.
    auto at = view.screen(hoverAt);
    double scale = GetDPIScaleFactor();
    g.PushState();
    g.SetBrush(*wxTRANSPARENT_BRUSH);
    for (int ring = 0; ring < 2; ++ring) {
        double phase = std::fmod(rippleFrame / 36.0 + ring * 0.5, 1.0);
        double radius = (6 + 18 * phase) * scale;
        g.SetPen(g.CreatePen(
            wxGraphicsPenInfo(wxColour(22, 190, 100, int(180 * (1 - phase)))).Width(1.8 * scale)));
        g.DrawEllipse(at.x - radius, at.y - radius, radius * 2, radius * 2);
    }
    g.SetPen(g.CreatePen(wxGraphicsPenInfo(wxColour(22, 190, 100, 225)).Width(1.6 * scale)));
    g.SetBrush(wxBrush(wxColour(22, 190, 100, 22)));
    double radius = 5 * scale;
    g.DrawEllipse(at.x - radius, at.y - radius, radius * 2, radius * 2);
    g.PopState();
}
std::string LogicCanvas::hit(Point at) const {
    auto &p = owner->history.project;
    auto &c = owner->circuit();
    if (appearance) {
        for (auto i = c.shapes.rbegin(); i != c.shapes.rend(); ++i)
            if (i->appearance && shapeHit(*i, at, 6 / view.zoom))
                return i->id;
        return {};
    }
    for (auto i = c.parts.rbegin(); i != c.parts.rend(); ++i) {
        if (i->kind == "Input" || i->kind == "Output") {
            auto local = at - i->at;
            double side = i->kind == "Input" ? 1 : -1;
            if ((std::abs(local.x) <= 18 && std::abs(local.y) <= 18) ||
                segmentDistance(local, {side * 14, 0}, {side * 70, 0}) <= 5 / view.zoom)
                return i->id;
            continue;
        }
        if (i->kind == "Subcircuit")
            for (const auto &s : p.circuit(i->circuit).shapes)
                if (s.appearance && shapeHit(s, at - i->at, 6 / view.zoom))
                    return i->id;
        if (std::abs(at.x - i->at.x) < 62 && std::abs(at.y - i->at.y) < partHeight(p, *i) / 2)
            return i->id;
    }
    for (auto &n : c.nodes)
        if (distance(at, n.at) < 9 / view.zoom)
            return n.id;
    for (auto &w : c.wires) {
        auto ps = wirePoints(p, c, w);
        for (size_t i = 1; i < ps.size(); ++i)
            if (segmentDistance(at, ps[i - 1], ps[i]) < 7 / view.zoom)
                return w.id;
    }
    for (auto i = c.shapes.rbegin(); i != c.shapes.rend(); ++i)
        if (!i->appearance && shapeHit(*i, at, 6 / view.zoom))
            return i->id;
    return {};
}
Endpoint LogicCanvas::endpointAt(Point at) {
    if (auto e = pinAt(at))
        return *e;
    auto &p = owner->history.project;
    auto &c = owner->circuit();
    for (size_t wi = 0; wi < c.wires.size(); ++wi) {
        auto w = c.wires[wi];
        auto ps = wirePoints(p, c, w);
        for (size_t i = 1; i < ps.size(); ++i)
            if (segmentDistance(at, ps[i - 1], ps[i]) < 7 / view.zoom) {
                auto delta = ps[i] - ps[i - 1];
                double length = delta.x * delta.x + delta.y * delta.y;
                double t =
                    length ? std::clamp(((at.x - ps[i - 1].x) * delta.x + (at.y - ps[i - 1].y) * delta.y) /
                                            length,
                                        0.0, 1.0)
                           : 0;
                Point node = ps[i - 1] + delta * t;
                auto id = p.id("j");
                c.nodes.push_back({id, "", node});
                std::vector<Point> first(ps.begin() + 1, ps.begin() + ptrdiff_t(i));
                std::vector<Point> second(ps.begin() + ptrdiff_t(i), ps.end() - 1);
                c.wires[wi] = {w.id, w.a, {id, ""}, first};
                c.wires.push_back({p.id("w"), {id, ""}, w.b, second});
                return {id, ""};
            }
    }
    auto id = p.id("j");
    c.nodes.push_back({id, "", snap(at, 10)});
    return {id, ""};
}
void LogicCanvas::route(Point at, bool freeEnd) {
    auto &p = owner->history.project;
    if (!start) {
        before = p;
        start = endpointAt(at);
        bends.clear();
        Refresh();
        return;
    }
    bool onWire = false;
    auto id = hit(at);
    for (auto &w : owner->circuit().wires)
        if (w.id == id)
            onWire = true;
    if (pinAt(at) || onWire || freeEnd) {
        auto finish = endpointAt(at);
        if (finish == *start) {
            cancel();
            return;
        }
        owner->circuit().wires.push_back({p.id("w"), *start, finish, bends});
        p.validate();
        auto original = *before;
        before.reset();
        start.reset();
        bends.clear();
        owner->history.commit(original);
        owner->rebuild(true, true);
    } else {
        auto snapped = snap(at, 10);
        if (bends.empty() || distance(bends.back(), snapped) > 0.1)
            bends.push_back(snapped);
        Refresh();
    }
}
void LogicCanvas::place(Point at) {
    auto kind = placing, sub = subcircuit;
    owner->change([&] {
        Part p;
        p.id = owner->history.project.id("p");
        p.kind = kind;
        p.at = snap(at, 10);
        p.circuit = sub;
        if (kind == "Input" || kind == "Output")
            p.label = p.id;
        if (kind == "Text")
            p.label = "文字标签";
        if (kind == "Splitter" || kind == "Joiner" || kind == "Extender" || kind == "Hex" || kind == "RAM" ||
            kind == "ROM")
            p.width = 8;
        owner->circuit().parts.push_back(p);
        selected = {p.id};
    });
    SetFocus();
}
void LogicCanvas::updateDraft(Point at, bool constrain) {
    if (!draft)
        return;
    auto &s = *draft;
    at = snap(at, 10);
    if (curveControl) {
        s.control = at;
        return;
    }
    auto delta = at - s.a;
    if (s.kind == "Circle" || (constrain && (s.kind == "Rectangle" || s.kind == "Ellipse"))) {
        double side = std::max(std::abs(delta.x), std::abs(delta.y));
        delta = {delta.x < 0 ? -side : side, delta.y < 0 ? -side : side};
    } else if (constrain && (s.kind == "Line" || s.kind == "Curve")) {
        if (std::abs(delta.x) > 2 * std::abs(delta.y))
            delta.y = 0;
        else if (std::abs(delta.y) > 2 * std::abs(delta.x))
            delta.x = 0;
        else {
            double side = std::max(std::abs(delta.x), std::abs(delta.y));
            delta = {delta.x < 0 ? -side : side, delta.y < 0 ? -side : side};
        }
    }
    s.b = s.a + delta;
    s.control = (s.a + s.b) * .5;
}
void LogicCanvas::finishShape() {
    if (!draft)
        return;
    Shape s = *draft;
    cancel();
    if (distance(s.a, s.b) < .01 || (s.kind != "Line" && s.kind != "Curve" &&
                                     (std::abs(s.a.x - s.b.x) < .01 || std::abs(s.a.y - s.b.y) < .01)))
        return;
    owner->change([&] {
        s.id = owner->history.project.id("s");
        owner->circuit().shapes.push_back(s);
        selected = {s.id};
    });
}
void LogicCanvas::leftDown(wxMouseEvent &e) {
    clearPinHover();
    SetFocus();
    Point at = view.world({double(e.GetX()), double(e.GetY())});
    mouse = at;
    if (shapeKind(tool)) {
        if (draft && curveControl) {
            updateDraft(at, e.ShiftDown());
            finishShape();
            return;
        }
        Shape s;
        s.kind = tool;
        s.a = s.b = s.control = snap(at, 10);
        s.appearance = appearance;
        s.stroke = owner->drawingStroke;
        s.filled = owner->drawingFilled;
        draft = s;
        if (!HasCapture())
            CaptureMouse();
        Refresh();
        return;
    }
    if (tool == "Select" && selected.size() == 1) {
        for (const auto &s : owner->circuit().shapes) {
            if (!selected.count(s.id) || s.appearance != appearance)
                continue;
            std::vector<Point> handles{s.a, s.b};
            if (s.kind == "Curve")
                handles.push_back(s.control);
            for (size_t i = 0; i < handles.size(); ++i)
                if (distance(at, handles[i]) < 8 / view.zoom) {
                    shapeHandle = int(i);
                    before = owner->history.project;
                    dragging = true;
                    if (!HasCapture())
                        CaptureMouse();
                    return;
                }
        }
    }
    if (tool == "Place") {
        place(at);
        return;
    }
    if (tool == "Wire") {
        route(at);
        return;
    }
    if (tool == "Node") {
        owner->change([&] {
            auto id = endpointAt(at);
            selected = {id.object};
        });
        return;
    }
    auto id = hit(at);
    down = snap(at, 10);
    if (id.empty()) {
        if (!e.ShiftDown())
            selected.clear();
        box = true;
        boxStart = at;
        if (!HasCapture())
            CaptureMouse();
    } else {
        if (e.ShiftDown()) {
            if (selected.count(id))
                selected.erase(id);
            else
                selected.insert(id);
        } else if (!selected.count(id))
            selected = {id};
        before = owner->history.project;
        dragging = true;
        if (!HasCapture())
            CaptureMouse();
    }
    owner->inspect();
    Refresh();
}
void LogicCanvas::move(Point delta) {
    auto &c = owner->circuit();
    for (auto &s : c.shapes)
        if (selected.count(s.id)) {
            s.a = s.a + delta;
            s.b = s.b + delta;
            s.control = s.control + delta;
        }
    for (auto &p : c.parts)
        if (selected.count(p.id))
            p.at = p.at + delta;
    for (auto &n : c.nodes)
        if (selected.count(n.id))
            n.at = n.at + delta;
    for (auto &w : c.wires)
        if (selected.count(w.id) || (selected.count(w.a.object) && selected.count(w.b.object)))
            for (auto &b : w.bends)
                b = b + delta;
}
void LogicCanvas::motion(wxMouseEvent &e) {
    Point screen{double(e.GetX()), double(e.GetY())};
    hoverScreen = screen;
    mouseInside = GetClientRect().Contains(e.GetPosition());
    mouse = view.world(screen);
    if (panning) {
        view.offset = view.offset + (screen - lastScreen);
        lastScreen = screen;
        Refresh();
        return;
    }
    if (draft) {
        updateDraft(mouse, e.ShiftDown());
        Refresh();
        return;
    }
    if (dragging && shapeHandle >= 0 && e.LeftIsDown()) {
        for (auto &s : owner->circuit().shapes)
            if (selected.count(s.id)) {
                Point at = snap(mouse, 10);
                if (s.kind == "Circle" && shapeHandle < 2) {
                    Point anchor = shapeHandle == 0 ? s.b : s.a;
                    auto d = at - anchor;
                    double side = std::max(std::abs(d.x), std::abs(d.y));
                    at = anchor + Point{d.x < 0 ? -side : side, d.y < 0 ? -side : side};
                }
                (shapeHandle == 0 ? s.a : shapeHandle == 1 ? s.b : s.control) = at;
            }
        Refresh();
        return;
    }
    if (dragging && e.LeftIsDown()) {
        auto now = snap(mouse, 10);
        move(now - down);
        down = now;
    }
    if (dragging || box || start || tool == "Place")
        Refresh();
    updatePinHover();
}
void LogicCanvas::leftUp(wxMouseEvent &event) {
    if (draft) {
        updateDraft(view.world({double(event.GetX()), double(event.GetY())}), event.ShiftDown());
        if (HasCapture())
            ReleaseMouse();
        if (draft->kind == "Curve" && !curveControl && distance(draft->a, draft->b) > .01) {
            curveControl = true;
            Refresh();
            return;
        }
        if (!curveControl)
            finishShape();
        return;
    }
    if (dragging) {
        auto saved = *before;
        bool modified = serialize(saved) != serialize(owner->history.project);
        try {
            owner->history.project.validate();
        } catch (...) {
            cancel();
            owner->rebuild(false);
            return;
        }
        owner->history.commit(saved);
        shapeHandle = -1;
        before.reset();
        dragging = false;
        if (HasCapture())
            ReleaseMouse();
        owner->rebuild(modified, modified);
    }
    if (box) {
        auto lo = Point{std::min(mouse.x, boxStart.x), std::min(mouse.y, boxStart.y)},
             hi = Point{std::max(mouse.x, boxStart.x), std::max(mouse.y, boxStart.y)};
        auto inside = [&](Point p) { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y; };
        for (const auto &s : owner->circuit().shapes) {
            auto b = shapeBounds(s);
            if (s.appearance == appearance && inside(b.first) && inside(b.second))
                selected.insert(s.id);
        }
        if (!appearance)
            for (auto &p : owner->circuit().parts)
                if (inside(p.at))
                    selected.insert(p.id);
        if (!appearance)
            for (auto &n : owner->circuit().nodes)
                if (inside(n.at))
                    selected.insert(n.id);
        box = false;
        if (HasCapture())
            ReleaseMouse();
        owner->inspect();
        Refresh();
    }
    updatePinHover();
}
void LogicCanvas::doubleClick(wxMouseEvent &e) {
    if (appearance)
        return;
    Point at = view.world({double(e.GetX()), double(e.GetY())});
    if (tool == "Wire") {
        route(at, true);
        return;
    }
    if (tool != "Select")
        return;
    auto id = hit(at);
    auto p = owner->circuit().part(id);
    if (!p)
        return;
    if (p->kind == "Input" || p->kind == "Button") {
        cancel();
        p = owner->circuit().part(id);
        auto saved = owner->history.project;
        p->value = p->value ? 0 : 1;
        owner->history.commit(saved);
        owner->simulator->setInput(id, p->value);
        owner->simulator->settle();
        owner->rebuild(false);
    } else if (p->kind == "Subcircuit") {
        auto name = p->circuit;
        cancel();
        owner->stop();
        owner->current = name;
        selected.clear();
        owner->rebuild(true, true);
        fit();
    } else {
        selected = {id};
        owner->inspect();
        owner->properties->SetFocus();
        owner->properties->SetGridCursor(P_LABEL, 1);
        owner->properties->EnableCellEditControl();
    }
}
void LogicCanvas::key(wxKeyEvent &e) {
    try {
        int code = e.GetKeyCode();
        if (code == WXK_ESCAPE) {
            cancel();
            tool = "Select";
            owner->rebuild(false);
            return;
        }
        if (!e.ControlDown() &&
            (code == WXK_LEFT || code == WXK_RIGHT || code == WXK_UP || code == WXK_DOWN)) {
            owner->change([&] {
                move({code == WXK_LEFT    ? -10.0
                      : code == WXK_RIGHT ? 10.0
                                          : 0,
                      code == WXK_UP     ? -10.0
                      : code == WXK_DOWN ? 10.0
                                         : 0});
            });
            return;
        }
        if (!e.ControlDown() && !e.AltDown()) {
            int command = code == 'L'        ? L_LINE
                          : code == 'B'      ? L_CURVE
                          : code == 'R'      ? L_RECTANGLE
                          : code == 'E'      ? L_ELLIPSE
                          : code == 'C'      ? L_CIRCLE
                          : code == 'S'      ? L_SELECT
                          : code == 'W'      ? L_WIRE
                          : code == 'J'      ? L_NODE
                          : code == WXK_HOME ? L_FIT
                                             : 0;
            if (command) {
                wxCommandEvent action(wxEVT_MENU, command);
                owner->GetEventHandler()->ProcessEvent(action);
                return;
            }
        }
        e.Skip();
    } catch (const std::exception &x) {
        cancel();
        owner->fail(x);
    }
}
void LogicCanvas::paint(wxPaintEvent &) {
    updatePinHover();
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour("#f9fbfd")));
    dc.Clear();
    std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
    if (g)
        render(*g, GetClientSize());
}
void LogicCanvas::render(wxGraphicsContext &g, wxSize size, bool clean) {
    auto &project = owner->history.project;
    auto &c = owner->circuit();
    g.PushState();
    g.Translate(view.offset.x, view.offset.y);
    g.Scale(view.zoom, view.zoom);
    if (!clean && view.zoom >= 0.35) {
        auto lo = view.world({0, 0}), hi = view.world({double(size.x), double(size.y)});
        g.SetPen(wxPen(wxColour("#aebdce"), 1));
        for (double x = std::floor(lo.x / 20) * 20; x < hi.x; x += 20)
            for (double y = std::floor(lo.y / 20) * 20; y < hi.y; y += 20)
                g.StrokeLine(x, y, x + 1.2, y);
    }
    for (const auto &s : c.shapes)
        if (s.appearance == appearance)
            drawShape(g, s, !clean && selected.count(s.id), false, view.zoom);
    if (appearance) {
        // Show the same stable terminals that instances expose, without simulating the artwork.
        Part instance;
        instance.kind = "Subcircuit";
        instance.circuit = c.name;
        auto pins = ports(project, instance);
        g.SetPen(wxPen(wxColour("#139c59"), 1.5));
        g.SetBrush(*wxWHITE_BRUSH);
        for (const auto &pin : pins) {
            g.DrawEllipse(pin.at.x - 3, pin.at.y - 3, 6, 6);
            const auto *p = c.part(pin.id);
            std::string name = p && !p->label.empty() ? p->label : pin.id;
            text(g, name, pin.at + Point{pin.output ? 8.0 : -50.0, -17}, 8);
        }
        if (!clean && c.shapes.empty()) {
            g.SetPen(wxPen(wxColour("#b5c0cc"), 1, wxPENSTYLE_SHORT_DASH));
            g.SetBrush(*wxTRANSPARENT_BRUSH);
            double h = partHeight(project, instance);
            g.DrawRectangle(-55, -h / 2, 110, h);
        }
    }
    if (!appearance) {
        std::set<std::string> bad;
        for (auto &i : owner->issues)
            if (i.severity == "error")
                bad.insert(i.object);
        for (auto &w : c.wires) {
            auto signal = owner->simulator->read({w.id, ""});
            g.SetPen(wxPen(!clean && selected.count(w.id) ? wxColour("#6366f1") : signalColour(signal),
                           signal.width > 1 ? 4 : 2));
            line(g, wirePoints(project, c, w));
        }
        for (auto &n : c.nodes) {
            auto s = owner->simulator->read({n.id, ""});
            g.SetPen(wxPen(signalColour(s)));
            g.SetBrush(wxBrush(!clean && selected.count(n.id) ? wxColour("#6366f1") : signalColour(s)));
            g.DrawEllipse(n.at.x - 4, n.at.y - 4, 8, 8);
            if (!n.label.empty())
                text(g, n.label, {n.at.x + 6, n.at.y - 23});
        }
        for (auto &p : c.parts)
            drawSymbol(
                g, project, p, [&](const Port &pin) { return owner->simulator->read({p.id, pin.id}); },
                !clean && selected.count(p.id), bad.count(p.id) != 0);
        if (!clean && start) {
            g.SetPen(wxPen(wxColour("#6366f1"), 2, wxPENSTYLE_SHORT_DASH));
            std::vector<Point> ps{endpoint(project, c, *start)};
            ps.insert(ps.end(), bends.begin(), bends.end());
            ps.push_back(snap(mouse, 10));
            line(g, orthogonal(ps));
        }
    } // schematic layer
    if (!clean && draft)
        drawShape(g, *draft, true, true, view.zoom);
    if (!clean && box) {
        g.SetPen(wxPen(wxColour("#6366f1"), 1, wxPENSTYLE_SHORT_DASH));
        g.SetBrush(*wxTRANSPARENT_BRUSH);
        g.DrawRectangle(std::min(mouse.x, boxStart.x), std::min(mouse.y, boxStart.y),
                        std::abs(mouse.x - boxStart.x), std::abs(mouse.y - boxStart.y));
    }
    if (!appearance && !clean && tool == "Place" && !start) {
        Part p;
        p.kind = placing;
        p.circuit = subcircuit;
        p.at = snap(mouse, 10);
        if (placing == "Splitter" || placing == "Joiner" || placing == "Extender" || placing == "Hex" ||
            placing == "RAM" || placing == "ROM")
            p.width = 8;
        drawSymbol(
            g, project, p, [](const Port &pin) { return Signal::unknown(pin.width); }, false, false, true);
    }
    g.PopState();
    if (!clean)
        drawPinHover(g);
    if (!appearance && !clean && c.parts.empty() && c.nodes.empty() && c.shapes.empty())
        text(g, "从左侧元件库开始，或 文件 → 打开半加器示例", {35, 45}, 13, wxColour("#8493a3"));
}
namespace {
std::pair<Point, Point> bounds(const Project &p, const Circuit &c, bool appearance = false) {
    Point lo{1e10, 1e10}, hi{-1e10, -1e10};
    auto add = [&](Point a) {
        lo.x = std::min(lo.x, a.x);
        lo.y = std::min(lo.y, a.y);
        hi.x = std::max(hi.x, a.x);
        hi.y = std::max(hi.y, a.y);
    };
    for (const auto &s : c.shapes) {
        if (s.appearance != appearance)
            continue;
        auto b = shapeBounds(s);
        double margin = s.stroke / 2 + 6;
        add(b.first - Point{margin, margin});
        add(b.second + Point{margin, margin});
    }
    if (appearance) {
        Part instance;
        instance.kind = "Subcircuit";
        instance.circuit = c.name;
        double h = partHeight(p, instance);
        add({-130, -h / 2 - 30});
        add({130, h / 2 + 30});
        return {lo, hi};
    }
    for (auto &q : c.parts) {
        if (q.kind == "Subcircuit")
            for (const auto &s : p.circuit(q.circuit).shapes)
                if (s.appearance) {
                    auto b = shapeBounds(s);
                    double margin = s.stroke / 2 + 6;
                    add(q.at + b.first - Point{std::max(105.0, margin), margin + 35});
                    add(q.at + b.second + Point{std::max(105.0, margin), margin + 40});
                }
        double h = partHeight(p, q);
        double half = std::max(105.0, double(q.label.size()) * 7);
        add({q.at.x - half, q.at.y - h / 2 - 35});
        add({q.at.x + half, q.at.y + h / 2 + 40});
    }
    for (auto &n : c.nodes) {
        add(n.at - Point{10, 30});
        add(n.at + Point{std::max(20.0, double(n.label.size()) * 9), 10});
    }
    for (auto &w : c.wires)
        for (auto at : wirePoints(p, c, w))
            add(at);
    if (lo.x > hi.x)
        return {{0, 0}, {800, 500}};
    return {lo, hi};
}
} // namespace
void LogicCanvas::fit() {
    auto b = bounds(owner->history.project, owner->circuit(), appearance);
    auto size = GetClientSize();
    auto extent = b.second - b.first;
    view.zoom =
        std::clamp(std::min((size.x - 70) / std::max(1.0, extent.x), (size.y - 70) / std::max(1.0, extent.y)),
                   0.15, 1.4);
    view.offset = Point{size.x / 2.0, size.y / 2.0} - (b.first + b.second) * (view.zoom / 2);
    Refresh();
}
void LogicCanvas::exportPng(const wxString &path) {
    auto b = bounds(owner->history.project, owner->circuit(), appearance);
    auto extent = b.second - b.first;
    double scale = std::min(1.5, 8000 / std::max(extent.x + 60, extent.y + 60));
    int w = std::max(1, int((extent.x + 60) * scale)), h = std::max(1, int((extent.y + 60) * scale));
    if (int64_t(w) * h > 32000000)
        throw std::runtime_error("导出图像超过 3200 万像素，请缩小电路范围");
    wxBitmap bitmap(w, h, 32);
    wxMemoryDC dc(bitmap);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();
    auto saved = view;
    try {
        view.zoom = scale;
        view.offset = (Point{30, 30} - b.first) * scale;
        {
            std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
            if (!g)
                throw std::runtime_error("无法创建图形上下文");
            render(*g, {w, h}, true);
        }
        view = saved;
        dc.SelectObject(wxNullBitmap);
        if (!bitmap.ConvertToImage().SaveFile(path, wxBITMAP_TYPE_PNG))
            throw std::runtime_error("PNG 写入失败");
    } catch (...) {
        view = saved;
        throw;
    }
}
void LogicEditor::runSmoke(const std::filesystem::path &folder) {
    std::filesystem::create_directories(folder);
    std::error_code ignored;
    std::filesystem::remove(folder / "LOGIC-PASS.txt", ignored);
    std::filesystem::remove(folder / "FAILED.txt", ignored);
    if (!tree->GetImageList() || iconIds.size() != library().size() + 5)
        throw std::runtime_error("component tree icon catalog is incomplete");
    size_t componentCount = 0;
    std::function<void(wxTreeItemId)> checkTree = [&](wxTreeItemId parent) {
        wxTreeItemIdValue cookie;
        for (auto child = tree->GetFirstChild(parent, cookie); child.IsOk();
             child = tree->GetNextChild(parent, cookie)) {
            auto data = dynamic_cast<Item *>(tree->GetItemData(child));
            if (data) {
                ++componentCount;
                if (tree->GetItemImage(child) != iconIds.at(data->kind))
                    throw std::runtime_error("component tree icon/type mismatch");
            }
            checkTree(child);
        }
    };
    checkTree(tree->GetRootItem());
    if (componentCount != library().size() + 3)
        throw std::runtime_error("initial component tree is incomplete");
    auto shortcuts = dynamic_cast<wxToolBar *>(wxWindow::FindWindowByName("logic-components", this));
    if (!shortcuts || shortcuts->GetToolsCount() != 13)
        throw std::runtime_error("digital component toolbar missing");
    for (size_t i = 0; i < shortcuts->GetToolsCount(); ++i) {
        auto tool = shortcuts->GetToolByPos(int(i));
        if (!tool->GetNormalBitmapBundle().IsOk())
            throw std::runtime_error("component toolbar bitmap missing");
        wxCommandEvent event(wxEVT_TOOL, tool->GetId());
        shortcuts->GetEventHandler()->ProcessEvent(event);
        if (canvas->tool != "Place" || U(canvas->placing) != tool->GetLongHelp())
            throw std::runtime_error("component shortcut chose wrong kind");
    }
    std::vector<std::string> iconKinds;
    for (auto &part : library())
        iconKinds.push_back(part.kind);
    for (auto name : {"New", "Open", "Save", "Copy", "Cut", "Paste", "Undo", "Redo", "Help", "Folder",
                      "Select", "Wire", "Node", "Step", "Run", "Reset", "Table", "Subcircuit"})
        iconKinds.push_back(name);
    wxBitmap atlas(960, int((iconKinds.size() + 5) / 6) * 90, 32);
    {
        wxMemoryDC dc(atlas);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
        if (!g)
            throw std::runtime_error("icon atlas graphics context failed");
        for (size_t i = 0; i < iconKinds.size(); ++i) {
            auto bundle = icon(iconKinds[i]);
            auto icon32 = bundle.GetBitmap({32, 32}), icon64 = bundle.GetBitmap({64, 64});
            if (!bundle.IsOk() || !icon32.IsOk() || !icon64.IsOk() || icon32.GetWidth() != 32 ||
                icon64.GetWidth() != 64)
                throw std::runtime_error("icon failed 1x/2x loading: " + iconKinds[i]);
            double x = double(i % 6) * 160 + 16, y = double(i / 6) * 90 + 8;
            g->DrawBitmap(icon32, x, y, 32, 32);
            text(*g, iconKinds[i], {x, y + 43}, 10);
        }
    }
    if (!atlas.ConvertToImage().SaveFile(wxString((folder / "logic-icons.png").wstring()), wxBITMAP_TYPE_PNG))
        throw std::runtime_error("icon atlas export failed");
    // Exercise the same vector renderer used by canvas, preview and PNG export.
    Project symbols = halfAdder();
    const auto beforeSymbols = serialize(symbols);
    std::vector<Part> specimens;
    for (const auto &info : library()) {
        Part p;
        p.id = info.kind;
        p.kind = info.kind;
        p.label = info.name;
        if (p.kind == "Splitter" || p.kind == "Joiner" || p.kind == "Hex")
            p.width = 4;
        specimens.push_back(p);
    }
    Part instance;
    instance.id = "instance";
    instance.kind = "Subcircuit";
    instance.circuit = "main";
    specimens.push_back(instance);
    for (int mode = 0; mode < 3; ++mode) {
        wxBitmap sheet(1250, int((specimens.size() + 4) / 5) * 210, 32);
        {
            wxMemoryDC dc(sheet);
            dc.SetBackground(*wxWHITE_BRUSH);
            dc.Clear();
            std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
            if (!g)
                throw std::runtime_error("symbol sheet graphics context failed");
            for (size_t i = 0; i < specimens.size(); ++i) {
                auto p = specimens[i];
                p.at = {double(i % 5) * 250 + 125, double(i / 5) * 210 + 105};
                std::set<std::string> seen;
                drawSymbol(
                    *g, symbols, p,
                    [&](const Port &pin) {
                        seen.insert(pin.id);
                        if (mode == 2) {
                            if (i % 3 == 0)
                                return Signal::unknown(pin.width);
                            if (i % 3 == 1)
                                return Signal::floating(pin.width);
                            return Signal::failure(pin.width);
                        }
                        return Signal::number(1, pin.width);
                    },
                    mode == 2, mode == 2 && i % 3 == 2, mode == 1);
                if (mode != 1 && seen.size() != ports(symbols, p).size())
                    throw std::runtime_error("symbol omitted a terminal: " + p.kind);
            }
        }
        auto name = mode == 0   ? "logic-symbols.png"
                    : mode == 1 ? "logic-symbol-previews.png"
                                : "logic-symbol-states.png";
        if (!sheet.ConvertToImage().SaveFile(wxString((folder / name).wstring()), wxBITMAP_TYPE_PNG))
            throw std::runtime_error("symbol sheet export failed");
    }
    if (serialize(symbols) != beforeSymbols)
        throw std::runtime_error("rendering changed the circuit model");
    // Raster evidence of all five signal states on both compact I/O bodies.
    wxBitmap ioSheet(760, 540, 32);
    std::vector<Signal> ioStates{Signal::number(0, 1), Signal::number(1, 1), Signal::unknown(1),
                                 Signal::floating(1), Signal::failure(1)};
    {
        wxMemoryDC dc(ioSheet);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
        for (size_t row = 0; row < ioStates.size(); ++row) {
            const auto state = ioStates[row];
            text(*g, state.text(), {30, 45.0 + row * 100}, 14, signalColour(state));
            for (int col = 0; col < 3; ++col) {
                Part p;
                p.kind = col == 0 ? "Input" : col == 1 ? "Output" : "And";
                p.at = {180.0 + col * 220, 60.0 + row * 100};
                drawSymbol(*g, symbols, p, [&](const Port &) { return state; }, row == 1);
            }
        }
    }
    auto ioImage = ioSheet.ConvertToImage();
    std::set<unsigned> bodyColours;
    for (int row = 0; row < 5; ++row) {
        int x = 172, y = 68 + row * 100;
        unsigned rgb = (unsigned(ioImage.GetRed(x, y)) << 16) | (unsigned(ioImage.GetGreen(x, y)) << 8) |
                       ioImage.GetBlue(x, y);
        bodyColours.insert(rgb);
        if (ioImage.GetRed(172, y) != ioImage.GetRed(392, y) ||
            ioImage.GetGreen(172, y) != ioImage.GetGreen(392, y) ||
            ioImage.GetBlue(172, y) != ioImage.GetBlue(392, y))
            throw std::runtime_error("I/O state fills disagree");
        if (ioImage.GetRed(180, 80 + row * 100) != 255 || ioImage.GetBlue(180, 80 + row * 100) != 255)
            throw std::runtime_error("I/O body did not shrink");
    }
    if (bodyColours.size() != 5)
        throw std::runtime_error("I/O states need distinct body colours");
    ioImage.SaveFile(wxString((folder / "io-signal-states.png").wstring()), wxBITMAP_TYPE_PNG);
    // Reproduce the user's Input -> NOT -> Output circuit with unchanged anchors.
    Project inverter;
    inverter.nextId = 3;
    Part input, gate, output;
    input.id = "input";
    input.kind = "Input";
    input.label = "IN";
    input.value = 1;
    input.at = {130, 140};
    gate.id = "not";
    gate.kind = "Not";
    gate.at = {330, 140};
    output.id = "output";
    output.kind = "Output";
    output.label = "OUT";
    output.at = {530, 140};
    inverter.circuit("main").parts = {input, gate, output};
    inverter.circuit("main").wires = {{"w1", {"input", "Y"}, {"not", "A"}, {}},
                                      {"w2", {"not", "Y"}, {"output", "A"}, {}}};
    history.reset(inverter);
    current = "main";
    canvas->cancel();
    canvas->tool = "Select";
    canvas->selected.clear();
    rebuild(true);
    if (simulator->read({"output", "A"}) != Signal::number(0, 1))
        throw std::runtime_error("inverter symbol circuit failed");
    for (const auto &p : circuit().parts)
        for (const auto &pin : ports(history.project, p)) {
            auto hit = canvas->pinAt(p.at + pin.at);
            if (!hit || !(*hit == Endpoint{p.id, pin.id}))
                throw std::runtime_error("symbol terminal no longer matches connection hit target");
        }
    canvas->exportPng(wxString((folder / "logic-inverter.png").wstring()));
    // Hover must follow the same nearest endpoint as wiring, without clock ticks or edits.
    Layout();
    const auto hoverProject = serialize(history.project);
    const auto hoverTime = simulator->time();
    const auto hoverPending = simulator->pending();
    auto savedHoverView = canvas->view;
    const Endpoint hoverTarget{"not", "A"};
    const auto hoverAnchor = endpoint(history.project, circuit(), hoverTarget);
    auto mouseMove = [&](Point at) {
        wxMouseEvent motion(wxEVT_MOTION);
        motion.SetPosition({int(at.x), int(at.y)});
        canvas->GetEventHandler()->ProcessEvent(motion);
    };
    for (double zoom : {0.25, 1.0, 4.0}) {
        canvas->view.zoom = zoom;
        canvas->view.offset = Point{300, 160} - hoverAnchor * zoom;
        mouseMove({300.0 + canvas->FromDIP(6), 160});
        if (!canvas->hoveredPin || !(*canvas->hoveredPin == hoverTarget) || !canvas->rippleTimer.IsRunning())
            throw std::runtime_error("pin hover failed across zoom levels: " + std::to_string(zoom) +
                                     " size=" + std::to_string(canvas->GetClientSize().x) + "x" +
                                     std::to_string(canvas->GetClientSize().y) +
                                     " inside=" + std::to_string(canvas->mouseInside) +
                                     " target=" + (canvas->hoveredPin ? canvas->hoveredPin->key() : "none"));
        mouseMove({300.0 + canvas->FromDIP(12), 160});
        if (canvas->hoveredPin || canvas->rippleTimer.IsRunning())
            throw std::runtime_error("pin hover remained outside connection radius");
    }
    // At low zoom, a nearby junction must win over an earlier, more distant part pin.
    circuit().nodes.push_back({"hoverNode", "", hoverAnchor + Point{0, 2}});
    canvas->view.zoom = 0.25;
    auto nearNode = canvas->pinAt(hoverAnchor + Point{0, 2});
    if (!nearNode || !(*nearNode == Endpoint{"hoverNode", ""}))
        throw std::runtime_error("hover did not choose nearest overlapping endpoint");
    circuit().nodes.pop_back();
    canvas->view.zoom = 1;
    canvas->view.offset = {40, 20};
    mouseMove({305, 160});
    auto hoverImage = [&](bool clean) {
        wxBitmap bmp(760, 330, 32);
        {
            wxMemoryDC dc(bmp);
            dc.SetBackground(*wxWHITE_BRUSH);
            dc.Clear();
            std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
            if (!g)
                throw std::runtime_error("hover graphics context failed");
            canvas->render(*g, {760, 330}, clean);
        }
        return bmp.ConvertToImage();
    };
    auto firstRipple = hoverImage(false);
    for (int i = 0; i < 8; ++i) {
        wxTimerEvent frame(canvas->rippleTimer);
        canvas->GetEventHandler()->ProcessEvent(frame);
    }
    auto nextRipple = hoverImage(false);
    auto samePixels = [](const wxImage &a, const wxImage &b) {
        return a.GetSize() == b.GetSize() &&
               std::equal(a.GetData(), a.GetData() + a.GetWidth() * a.GetHeight() * 3, b.GetData());
    };
    if (samePixels(firstRipple, nextRipple))
        throw std::runtime_error("pin ripple is not animated");
    if (!nextRipple.SaveFile(wxString((folder / "logic-pin-ripple.png").wstring()), wxBITMAP_TYPE_PNG))
        throw std::runtime_error("pin ripple screenshot export failed");
    auto cleanWithHover = hoverImage(true);
    wxMouseEvent leave(wxEVT_LEAVE_WINDOW);
    canvas->GetEventHandler()->ProcessEvent(leave);
    if (canvas->hoveredPin || canvas->rippleTimer.IsRunning())
        throw std::runtime_error("pin ripple timer did not stop on leaving canvas");
    if (!samePixels(cleanWithHover, hoverImage(true)))
        throw std::runtime_error("hover leaked into exported circuit image");
    canvas->tool = "Place";
    mouseMove({305, 160});
    if (canvas->hoveredPin || canvas->rippleTimer.IsRunning())
        throw std::runtime_error("hover remained active during component placement");
    canvas->tool = "Wire";
    mouseMove({305, 160});
    if (!canvas->hoveredPin)
        throw std::runtime_error("hover missing in wire tool");
    canvas->panning = true;
    canvas->updatePinHover();
    if (canvas->hoveredPin || canvas->rippleTimer.IsRunning())
        throw std::runtime_error("hover remained active during panning");
    canvas->cancel();
    canvas->tool = "Select";
    canvas->view = savedHoverView;
    if (serialize(history.project) != hoverProject || simulator->time() != hoverTime ||
        simulator->pending() != hoverPending || simulator->read({"output", "A"}) != Signal::number(0, 1))
        throw std::runtime_error("hover changed project or simulation state");
    history.reset(halfAdder());
    current = "main";
    canvas->cancel();
    canvas->tool = "Select";
    rebuild(true, true);
    Layout();
    canvas->fit();
    auto original = serialize(history.project);
    auto &c = circuit();
    auto a = c.parts[0].id, b = c.parts[1].id, sum = c.parts[4].id, carry = c.parts[5].id;
    simulator->setInput(a, 1);
    simulator->setInput(b, 1);
    simulator->settle();
    if (simulator->read({sum, "A"}).value != 0 || simulator->read({carry, "A"}).value != 1)
        throw std::runtime_error("GUI half adder failed");
    {
        auto at = canvas->view.screen(circuit().part(a)->at);
        wxMouseEvent down(wxEVT_LEFT_DOWN);
        down.SetPosition({int(at.x), int(at.y)});
        canvas->leftDown(down);
        wxMouseEvent up(wxEVT_LEFT_UP);
        canvas->leftUp(up);
        if (simulator->read({carry, "A"}).value != 1)
            throw std::runtime_error("Selecting a component reset simulation state");
        properties->SetFocus();
        properties->SetGridCursor(P_VALUE, 1);
        properties->EnableCellEditControl();
        auto editor = properties->GetCellEditor(P_VALUE, 1);
        auto cellInput = dynamic_cast<wxTextCtrl *>(editor->GetControl());
        if (!cellInput) {
            editor->DecRef();
            throw std::runtime_error("property cell editor missing");
        }
        cellInput->ChangeValue("17");
        rebuild(false);
        if (cellInput->GetValue() != "17")
            throw std::runtime_error("Simulation refresh overwrote property input");
        cellInput->ChangeValue(properties->GetCellValue(P_VALUE, 1));
        editor->DecRef();
        properties->DisableCellEditControl();
        canvas->SetFocus();
    }
    canvas->selected = {a};
    canvas->SetFocus();
    copySelection();
    pasteSelection();
    if (circuit().parts.size() != 7)
        throw std::runtime_error("GUI clipboard paste failed");
    undo();
    if (serialize(history.project) != original)
        throw std::runtime_error("GUI undo paste failed");
    undo(true);
    if (circuit().parts.size() != 7)
        throw std::runtime_error("GUI redo paste failed");
    undo();
    canvas->tool = "Select";
    auto screen = canvas->view.screen(circuit().part(a)->at);
    wxMouseEvent down(wxEVT_LEFT_DOWN);
    down.SetPosition({int(screen.x), int(screen.y)});
    canvas->leftDown(down);
    auto moved = screen + Point{40, 20} * canvas->view.zoom;
    wxMouseEvent motion(wxEVT_MOTION);
    motion.SetPosition({int(moved.x), int(moved.y)});
    motion.SetLeftDown(true);
    canvas->motion(motion);
    wxMouseEvent up(wxEVT_LEFT_UP);
    canvas->leftUp(up);
    if (distance(circuit().part(a)->at, {170, 160}) > 1)
        throw std::runtime_error("GUI component drag failed");
    undo();
    auto points = wirePoints(history.project, circuit(), circuit().wires[0]);
    auto mid = (points[0] + points[1]) * 0.5;
    canvas->route(mid);
    canvas->cancel();
    if (serialize(history.project) != original)
        throw std::runtime_error("GUI branch cancel failed");
    canvas->route(mid);
    canvas->route({350, 500}, true);
    if (circuit().wires.size() != 8)
        throw std::runtime_error("GUI branch commit failed");
    undo();
    if (serialize(history.project) != original)
        throw std::runtime_error("GUI branch undo failed");
    canvas->selected = {a};
    inspect();
    properties->SetCellValue(P_VALUE, 1, "1");
    apply();
    if (simulator->read({sum, "A"}).value != 1)
        throw std::runtime_error("GUI input apply failed");
    undo();
    auto table = truthTable(history.project, current);
    if (table.rows.size() != 4)
        throw std::runtime_error("GUI truth table failed");
    savedPath = (folder / "数字逻辑.logic.json").u8string();
    if (!save())
        throw std::runtime_error("GUI save failed");
    if (!load(U(savedPath)))
        throw std::runtime_error("GUI load failed");
    canvas->selected.clear();
    canvas->tool = "Select";
    rebuild(false);
    canvas->exportPng(wxString((folder / "logic-half-adder.png").wstring()));
    writeAtomic(folder / "logic-truth-table.txt", table.report());
    canvas->placing = "Clock";
    canvas->tool = "Place";
    canvas->place({200, 550});
    if (circuit().parts.back().kind != "Clock")
        throw std::runtime_error("GUI library placement failed");
    undo();
    for (const std::string name : {"bus-register", "hierarchical-adder"}) {
        auto sample = fsPath(wxStandardPaths::Get().GetExecutablePath()).parent_path() / "examples" /
                      (name + ".logic.json");
        if (!std::filesystem::exists(sample))
            sample = std::filesystem::path(__FILE__).parent_path().parent_path() / "examples" /
                     (name + ".logic.json");
        history.reset(deserialize(readFile(sample)));
        current = history.project.main;
        canvas->selected.clear();
        canvas->tool = "Select";
        rebuild(true, true);
        simulator->tick();
        rebuild(false);
        canvas->fit();
        canvas->exportPng(wxString((folder / ("logic-" + name + ".png")).wstring()));
    }
    // Exercise the actual custom-component dialog, then the same history/library/canvas paths as users.
    for (int id : {L_NEW_COMPONENT, L_SAVE_COMPONENT, L_LOAD_COMPONENT, L_NETLIST})
        if (!GetMenuBar()->FindItem(id))
            throw std::runtime_error("feature menu item missing");
    history.reset(Project{});
    current = "main";
    canvas->selected.clear();
    rebuild(true, true);
    wxTheApp->CallAfter([] {
        auto dialog = dynamic_cast<wxDialog *>(wxWindow::FindWindowByName("custom-component-dialog"));
        if (!dialog)
            throw std::runtime_error("custom component dialog missing");
        auto name = dynamic_cast<wxTextCtrl *>(wxWindow::FindWindowByName("custom-component-name", dialog));
        auto inputs =
            dynamic_cast<wxTextCtrl *>(wxWindow::FindWindowByName("custom-component-inputs", dialog));
        auto outputs =
            dynamic_cast<wxTextCtrl *>(wxWindow::FindWindowByName("custom-component-outputs", dialog));
        if (!name || !inputs || !outputs)
            throw std::runtime_error("custom component fields missing");
        name->SetValue(U("测试元件"));
        inputs->SetValue("DATA:8\nCLK:1");
        outputs->SetValue("Q:8");
        dialog->EndModal(wxID_OK);
    });
    newComponent();
    if (current != "测试元件" || circuit().parts.size() != 3 || circuit().parts.front().width != 8)
        throw std::runtime_error("custom component dialog did not create interfaces");
    undo();
    if (history.project.circuits.size() != 1)
        throw std::runtime_error("custom creation undo failed");
    undo(true);
    if (history.project.circuits.size() != 2)
        throw std::runtime_error("custom creation redo failed");
    auto packagePath = folder / std::filesystem::u8path("半加器.component.json");
    writeAtomic(packagePath, exportComponent(halfAdder(), "main"));
    current = "main";
    std::string definition;
    change([&] { definition = importComponent(history.project, readFile(packagePath)); });
    canvas->placing = "Subcircuit";
    canvas->subcircuit = definition;
    canvas->place({360, 180});
    const auto customInstanceId = circuit().parts.back().id;
    change([&] {
        // Mirror the imported interface to make a complete, testable top-level half adder.
        auto customPorts = history.project.circuit(definition).parts;
        for (const auto &port : customPorts) {
            if (port.kind != "Input" && port.kind != "Output")
                continue;
            auto external = port;
            external.id = history.project.id("p");
            external.at = {port.kind == "Input" ? 40.0 : 700.0, port.at.y};
            circuit().parts.push_back(external);
            circuit().wires.push_back({history.project.id("w"),
                                       {external.id, port.kind == "Input" ? "Y" : "A"},
                                       {customInstanceId, port.id},
                                       {}});
        }
    });
    if (truthTable(history.project, current).rows != truthTable(halfAdder(), "main").rows)
        throw std::runtime_error("placed custom component simulation failed");
    auto timeBeforeExport = simulator->time();
    auto queueBeforeExport = simulator->pending();
    writeAtomic(folder / "custom-half-adder.net", simulator->netlist(true));
    writeAtomic(folder / "custom-half-adder.net.json", simulator->netlist());
    if (simulator->time() != timeBeforeExport || simulator->pending() != queueBeforeExport)
        throw std::runtime_error("netlist export modified GUI simulation");
    canvas->fit();
    canvas->selected.clear();
    canvas->exportPng(wxString((folder / "custom-half-adder.png").wstring()));
    history.markSaved();
    // Import through the same load/save paths used by the file menu and command line.
    auto circExample =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "examples" / "circ-limited.circ";
    auto circSource = folder / std::filesystem::u8path("受限导入.CIRC");
    auto circBytes = readFile(circExample);
    writeAtomic(circSource, circBytes);
    if (!load(wxString(circSource.wstring())) || !savedPath.empty() || !history.dirty())
        throw std::runtime_error("外部电路 import must be unsaved and have no native saved path");
    auto importedNotes = circDiagnostics(circuit());
    if (importedNotes.size() != 2 || issues.size() < importedNotes.size())
        throw std::runtime_error("外部电路 diagnostics missing");
    rebuild(true, true);
    if (issues.front().object != importedNotes.front().object)
        throw std::runtime_error("外部电路 diagnostics lost after simulator rebuild");
    wxCommandEvent diagnosticClick(wxEVT_LISTBOX_DCLICK, diagnostics->GetId());
    diagnosticClick.SetInt(0);
    diagnostics->GetEventHandler()->ProcessEvent(diagnosticClick);
    if (!canvas->selected.count(importedNotes.front().object))
        throw std::runtime_error("外部电路 diagnostic navigation failed");
    savedPath = circSource.u8string();
    if (!save() || readFile(circSource) != circBytes || savedPath == circSource.u8string())
        throw std::runtime_error("外部电路 source overwritten or native save failed");
    if (!load(U(savedPath)) || circDiagnostics(circuit()).size() != 2)
        throw std::runtime_error("外部电路 diagnostics did not survive native reload");
    canvas->selected.clear();
    canvas->fit();
    canvas->exportPng(wxString((folder / "circ-import.png").wstring()));
    history.markSaved();
    // Exercise actual canvas handlers as well as the persisted artwork model.
    canvas->cancel();
    history.reset(Project{});
    current = "main";
    createComponent(history.project, "Artwork", {{"A", false, 1}, {"Y", true, 1}});
    current = "Artwork";
    rebuild(true, true);
    setAppearance(true);
    canvas->view.zoom = 2;
    canvas->view.offset = {300, 240};
    auto shapeEvent = [&](wxEventType type, Point world, bool held = false) {
        wxMouseEvent e(type);
        auto point = canvas->view.screen(world);
        e.SetPosition({int(point.x), int(point.y)});
        e.SetLeftDown(held);
        if (type == wxEVT_LEFT_DOWN)
            canvas->leftDown(e);
        else if (type == wxEVT_LEFT_UP)
            canvas->leftUp(e);
        else
            canvas->motion(e);
    };
    auto draw = [&](const std::string &kind, Point a, Point b) {
        canvas->cancel();
        canvas->tool = kind;
        shapeEvent(wxEVT_LEFT_DOWN, a);
        shapeEvent(wxEVT_MOTION, b, true);
        shapeEvent(wxEVT_LEFT_UP, b);
    };
    draw("Line", {-50, -30}, {40, -30});
    draw("Rectangle", {-50, -20}, {40, 30});
    draw("Ellipse", {-30, -10}, {20, 20});
    draw("Circle", {80, -30}, {110, 10});
    draw("Curve", {-50, 40}, {40, 40});
    if (!canvas->draft || !canvas->curveControl)
        throw std::runtime_error("curve phase missing");
    shapeEvent(wxEVT_MOTION, {0, 90});
    shapeEvent(wxEVT_LEFT_DOWN, {0, 90});
    shapeEvent(wxEVT_LEFT_UP, {0, 90});
    if (circuit().shapes.size() != 5 || !circuit().shapes.back().appearance)
        throw std::runtime_error("shape drawing failed");
    auto curveId = circuit().shapes.back().id;
    auto drawn = serialize(history.project);
    draw("Rectangle", {0, 0}, {0, 0});
    if (serialize(history.project) != drawn)
        throw std::runtime_error("degenerate gesture committed");
    canvas->tool = "Line";
    shapeEvent(wxEVT_LEFT_DOWN, {0, 0});
    shapeEvent(wxEVT_MOTION, {100, 100}, true);
    canvas->cancel();
    if (serialize(history.project) != drawn)
        throw std::runtime_error("cancel committed artwork");
    canvas->tool = "Select";
    canvas->selected = {curveId};
    shapeEvent(wxEVT_LEFT_DOWN, {0, 90});
    shapeEvent(wxEVT_MOTION, {20, 100}, true);
    shapeEvent(wxEVT_LEFT_UP, {20, 100});
    if (distance(circuit().shapes.back().control, {20, 100}) > .01)
        throw std::runtime_error("curve control edit failed");
    undo();
    if (serialize(history.project) != drawn)
        throw std::runtime_error("artwork undo failed");
    undo(true);
    canvas->selected = {curveId};
    copySelection();
    pasteSelection();
    if (circuit().shapes.size() != 6)
        throw std::runtime_error("artwork clipboard failed");
    removeSelection();
    if (circuit().shapes.size() != 5)
        throw std::runtime_error("artwork delete failed");
    canvas->selected.clear();
    canvas->tool = "Select";
    canvas->exportPng(wxString((folder / "component-appearance.png").wstring()));
    writeAtomic(folder / "drawn.component.json", exportComponent(history.project, current));
    setAppearance(false);
    current = "main";
    rebuild(true, true);
    canvas->placing = "Subcircuit";
    canvas->subcircuit = "Artwork";
    canvas->place({200, 150});
    canvas->selected.clear();
    canvas->tool = "Select";
    canvas->exportPng(wxString((folder / "component-instance.png").wstring()));
    draw("Line", {0, 0}, {100, 0});
    if (circuit().shapes.size() != 1 || circuit().shapes[0].appearance)
        throw std::runtime_error("schematic drawing layer failed");
    writeAtomic(folder / "drawn.logic.json", serialize(history.project));
    history.markSaved();
    if (!load(wxString((folder / "drawn.logic.json").wstring())))
        throw std::runtime_error("artwork reload failed");
    if (history.project.circuit("Artwork").shapes.size() != 5)
        throw std::runtime_error("artwork reload lost shapes");
    writeAtomic(
        folder / "SHAPES-PASS.txt",
        "Drawing, curve phases, handles, cancellation, layers, clipboard, undo and persistence passed.\n");
    canvas->cancel();
    canvas->appearance = false;
    canvas->tool = "Select";
    history.reset(halfAdder());
    current = "main";
    canvas->selected.clear();
    Part clockPart;
    clockPart.id = history.project.id("p");
    clockPart.kind = "Clock";
    clockPart.at = {150, 550};
    Part regPart;
    regPart.id = history.project.id("p");
    regPart.kind = "Register";
    regPart.at = {450, 550};
    auto sourceId = circuit().parts[0].id;
    circuit().parts[0].value = 1;
    circuit().parts.push_back(clockPart);
    circuit().parts.push_back(regPart);
    circuit().wires.push_back({history.project.id("w"), {sourceId, "Y"}, {regPart.id, "D"}, {}});
    circuit().wires.push_back({history.project.id("w"), {clockPart.id, "Y"}, {regPart.id, "CLK"}, {}});
    rebuild(true, true);
    simulator->tick();
    rebuild(false);
    if (simulator->read({regPart.id, "Q"}).value != 1)
        throw std::runtime_error("register setup failed");
    canvas->selected = {sourceId};
    inspect();
    auto editCell = [&](int row, const wxString &value) {
        auto old = properties->GetCellValue(row, 1);
        properties->SetCellValue(row, 1, value);
        wxGridEvent edit(properties->GetId(), wxEVT_GRID_CELL_CHANGED, properties, row, 1);
        edit.SetString(old);
        properties->GetEventHandler()->ProcessEvent(edit);
    };
    editCell(P_VALUE, "0");
    if (circuit().part(sourceId)->value != 0 || simulator->read({regPart.id, "Q"}).value != 1)
        throw std::runtime_error("table source edit reset live register state");
    auto beforeInvalid = serialize(history.project);
    editCell(P_WIDTH, "33");
    if (serialize(history.project) != beforeInvalid || properties->GetCellValue(P_WIDTH, 1) != "1")
        throw std::runtime_error("invalid table width was not rolled back");
    editCell(P_LABEL, U(circuit().parts[1].label));
    if (serialize(history.project) != beforeInvalid)
        throw std::runtime_error("duplicate table name committed");
    editCell(P_LABEL, U("输入测试"));
    if (circuit().part(sourceId)->label != "输入测试")
        throw std::runtime_error("table label edit failed");
    undo();
    if (serialize(history.project) != beforeInvalid)
        throw std::runtime_error("table edit undo failed");
    canvas->selected = {sourceId};
    inspect();
    editCell(P_WIDTH, "8");
    auto mismatch = std::find_if(issues.begin(), issues.end(), [](const Issue &i) {
        return i.severity == "error" && i.message.find("位宽") != std::string::npos;
    });
    if (mismatch == issues.end() || diagnosticSummary->GetForegroundColour() != wxColour("#dc2626"))
        throw std::runtime_error("connection width error was not presented");
    auto problemId = mismatch->object;
    showDiagnostic(int(mismatch - issues.begin()), true);
    if (!canvas->selected.count(problemId))
        throw std::runtime_error("connection error navigation failed");
    undo();
    Part dangling;
    dangling.id = history.project.id("p");
    dangling.kind = "Output";
    dangling.label = "Floating";
    dangling.at = {800, 550};
    change([&] { circuit().parts.push_back(dangling); });
    auto warning = std::find_if(issues.begin(), issues.end(), [&](const Issue &i) {
        return i.object == dangling.id && i.severity == "warning";
    });
    if (warning == issues.end())
        throw std::runtime_error("floating input warning was not presented");
    auto warningIndex = int(warning - issues.begin());
    setAppearance(true);
    showDiagnostic(warningIndex, true);
    if (canvas->appearance || !canvas->selected.count(dangling.id))
        throw std::runtime_error("warning navigation did not restore schematic mode");
    undo();
    if (!issues.empty())
        throw std::runtime_error("resolved connection warning remained visible");
    Part ram;
    ram.id = history.project.id("p");
    ram.kind = "RAM";
    ram.width = 8;
    change([&] { circuit().parts.push_back(ram); });
    canvas->selected = {ram.id};
    inspect();
    editCell(P_MEMORY, "0x10 20 255");
    if (circuit().part(ram.id)->data != std::vector<uint32_t>{16, 20, 255})
        throw std::runtime_error("table memory edit failed");
    auto beforeMemory = serialize(history.project);
    std::string tooMany;
    for (int n = 0; n < 257; ++n)
        tooMany += "1 ";
    editCell(P_MEMORY, U(tooMany));
    if (serialize(history.project) != beforeMemory)
        throw std::runtime_error("oversized memory edit committed");
    wxBitmap propertySheet(420, 330, 32);
    {
        wxMemoryDC dc(propertySheet);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        properties->Render(dc);
    }
    propertySheet.ConvertToImage().SaveFile(wxString((folder / "property-table.png").wstring()),
                                            wxBITMAP_TYPE_PNG);
    writeAtomic(folder / "EDITOR-UI-PASS.txt",
                "Compact I/O, five state colours, table edits/validation/undo/live state, connection "
                "warnings/errors/navigation passed.\n");
    writeAtomic(
        folder / "LOGIC-PASS.txt",
        "Digital-only GUI: default startup, complete icon catalog at 1x/2x, tree mappings, "
        "component toolbar bindings, all canvas symbols/previews/state colours, stable terminal hit targets, "
        "inverter, animated pin hover/zoom/nearest endpoint/leave/export isolation, rendering, drag, branch "
        "split/cancel/undo, clipboard "
        "copy/paste/undo/redo, property input, save/load with Unicode path, simulation, truth table "
        "and full PNG export passed.\n"
        "Custom component dialog/interfaces, undo/redo, library import/placement, nested simulation, "
        "Unicode component file, KiCad/JSON netlists and export state preservation passed.\n"
        "外部电路 import, persistent diagnostics/navigation, unsaved state and native save/source "
        "preservation passed.\n");
}
void LogicEditor::closeSmoke() {
    canvas->cancel();
    history.markSaved();
    Close();
}
} // namespace eda::logic
