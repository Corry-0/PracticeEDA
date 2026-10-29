// 编辑器命令共享事务式的模型修改；只读导出绝不重建仿真器。
#include "DigitalEditor.h"
#include "LogicIcons.h"
#include "LogicSymbols.h"
#include "LogicAnalysis.h"
#include "PcbExport.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/imaglist.h>
#include <wx/stdpaths.h>
#include <wx/textdlg.h>
#include <wx/toolbar.h>
#include <wx/splitter.h>
#include <wx/dirdlg.h>
#include <wx/choicdlg.h>
#include <wx/config.h>
#include <wx/bmpbuttn.h>

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
    L_RUN_CYCLES,
    L_TABLE,
    L_EXPRESSION,
    L_TRACE,
    L_NEW_CIRCUIT,
    L_WRAP,
    L_RENAME,
    L_HELP,
    L_CSV,
    L_NETLIST_JSON,
    L_PCB,
    L_NEW_COMPONENT,
    L_SAVE_COMPONENT,
    L_LOAD_COMPONENT,
    L_DEL_CIRCUIT,
    L_APPEARANCE,
    L_LINE,
    L_CURVE,
    L_RECTANGLE,
    L_ELLIPSE,
    L_CIRCLE,
    L_GUIDE, L_UP, L_EDIT_DEFINITION, L_MEMORY_INITIAL, L_MEMORY_RUNTIME, L_PIN_LAYOUT,
    L_MONITOR, L_RECENT, L_TEMPLATE, L_BATCH_PNG, L_IMPORT_CIRC, L_IMPORT_MENU, L_EXPORT_MENU,
    L_ROTATE, L_TEXT_DEVICE, L_NEW_TABLE, L_IMPORT_TABLE, L_DIAGNOSTICS, L_REROUTE, L_EXPERIMENTS,
    L_RECENT_FIRST = wxID_HIGHEST + 900
};
enum PropertyRow { P_CIRCUIT, P_ROTATION, P_WIDTH, P_INPUTS, P_SELECT, P_INPUTWIDTH, P_OUTPUTWIDTH,
    P_SIGN, P_HIGH, P_LOW, P_PHASE,
    P_FALLING, P_MAX, P_LOAD, P_ADDR, P_MOMENTARY, P_ACTIVELOW, P_GROUPS,
    P_MODE, P_ROWS, P_COLS, P_XOR, P_LABEL, P_VALUE, P_MEMORY, P_STATE, P_STROKE, P_FILL,
    P_TYPE, P_ID, P_INVERT, P_PARTCOUNT, P_WIRECOUNT, P_PORTCOUNT,
    P_INPUT_INVERT_FIRST, P_COUNT = P_INPUT_INVERT_FIRST + 32 };
const std::map<int, std::pair<std::string, int64_t>> parameterRows = {
    {P_INPUTS,{"inputs",2}}, {P_INVERT,{"invertMask",0}}, {P_SELECT,{"selectBits",1}},
    {P_INPUTWIDTH,{"inputWidth",1}}, {P_SIGN,{"signExtend",0}}, {P_HIGH,{"highTicks",1}},
    {P_LOW,{"lowTicks",1}}, {P_PHASE,{"phase",0}}, {P_FALLING,{"falling",0}},
    {P_MAX,{"counterMax",-1}}, {P_LOAD,{"loadable",0}}, {P_ADDR,{"addressBits",8}},
    {P_MOMENTARY,{"momentary",0}}, {P_ACTIVELOW,{"activeLow",0}},
    {P_OUTPUTWIDTH,{"outputWidth",1}}, {P_MODE,{"mode",0}}, {P_ROWS,{"rows",8}}, {P_COLS,{"cols",8}},
    {P_XOR,{"xorOne",0}}
};
const std::map<int, const char *> propertyCaptions = {
    {P_CIRCUIT,"电路名称"}, {P_ROTATION,"方向"}, {P_WIDTH,"数据位宽"}, {P_INPUTS,"输入引脚数量"},
    {P_SELECT,"选择位数"}, {P_INPUTWIDTH,"输入位宽"}, {P_OUTPUTWIDTH,"输出位宽"}, {P_SIGN,"符号扩展"},
    {P_HIGH,"高电平节拍"}, {P_LOW,"低电平节拍"}, {P_PHASE,"时钟相位"}, {P_FALLING,"下降沿触发"},
    {P_MAX,"计数上限"}, {P_LOAD,"启用装载"}, {P_ADDR,"地址位数"}, {P_MOMENTARY,"瞬时按钮"},
    {P_ACTIVELOW,"低电平有效"}, {P_GROUPS,"总线分组"}, {P_MODE,"查找模式"}, {P_ROWS,"矩阵行数"},
    {P_COLS,"矩阵列数"}, {P_XOR,"多输入行为"}, {P_LABEL,"标签"}, {P_VALUE,"初始值"},
    {P_MEMORY,"存储器初值"}, {P_STATE,"当前信号"}, {P_STROKE,"线宽"}, {P_FILL,"填充"},
    {P_TYPE,"类型"}, {P_ID,"对象 ID"}, {P_INVERT,"反相位掩码"}, {P_PARTCOUNT,"元件数量"},
    {P_WIRECOUNT,"导线数量"}, {P_PORTCOUNT,"输入 / 输出接口"}
};
const std::set<int> booleanRows = {P_FILL,P_SIGN,P_FALLING,P_LOAD,P_MOMENTARY,P_ACTIVELOW};
const std::vector<std::string> directionNames = {"右", "下", "左", "上"};
const std::vector<std::string> finderNames = {"最低位 1", "最高位 1", "最低位 0", "最高位 0"};
const std::vector<std::string> xorNames = {"奇数个为 1", "仅一个为 1"};
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
const wxString openFilter = U("可打开的工程 (*.logic.json;*.circ)|*.logic.json;*.circ|Logisim 2.7 兼容导入 "
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
    // 菜单和工具栏各自拥有菜单对象，共用入口定义及同一组命令处理器。
    auto importMenu = [] {
        auto m = new wxMenu();
        m->Append(L_IMPORT_CIRC, U("Logisim 2.7 电路 (.circ)…"));
        m->Append(L_LOAD_COMPONENT, U("自定义元件 (.component.json)…"));
        m->Append(L_IMPORT_TABLE, U("真值表 CSV…"));
        return m;
    };
    auto exportMenu = [] {
        auto m = new wxMenu();
        m->Append(L_PNG, U("完整电路 PNG…"));
        m->Append(L_BATCH_PNG, U("批量子电路 PNG…"));
        m->AppendSeparator();
        m->Append(L_CSV, U("最近分析 CSV…"));
        m->Append(L_NETLIST_JSON, U("逻辑网表 JSON…"));
        m->Append(L_PCB, U("KiCad PCB…"));
        m->Append(L_SAVE_COMPONENT, U("当前电路为自定义元件…"));
        return m;
    };
    auto file = new wxMenu();
    file->Append(wxID_NEW, U("新建数字工程\tCtrl+N"));
    file->Append(L_TEMPLATE, U("从模板新建…"));
    file->Append(L_DEMO, U("打开半加器示例"));
    file->AppendSeparator();
    file->Append(wxID_OPEN, U("打开…\tCtrl+O"));
    file->Append(L_RECENT, U("最近打开"));
    file->AppendSeparator();
    file->Append(wxID_SAVE, U("保存\tCtrl+S"));
    file->Append(L_SAVE_AS, U("另存为…\tCtrl+Shift+S"));
    file->AppendSeparator();
    file->Append(L_IMPORT_MENU, U("导入"), importMenu());
    file->Append(L_EXPORT_MENU, U("导出"), exportMenu());
    file->AppendSeparator();
    file->Append(wxID_EXIT, U("关闭"));
    bar->Append(file, U("文件"));
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
                  {L_ROTATE, "顺时针旋转元件\tCtrl+R"},
                  {L_REROUTE, "重排选中导线\tCtrl+Shift+R"},
                  {L_FIT, "适合画面 (Home)"}, {L_DIAGNOSTICS,"查看 ERC 问题…\tCtrl+Shift+E"}});
    menu("项目", {{L_NEW_CIRCUIT, "新建子电路…\tCtrl+Shift+N"},
                  {L_NEW_COMPONENT, "新建自定义元件…"},
                  {L_WRAP, "封装当前电路为子电路…"},
                  {L_RENAME, "重命名当前电路…"},
                  {L_DEL_CIRCUIT, "删除当前子电路\tCtrl+Shift+Delete"}});
    bar->GetMenu(3)->AppendSeparator();
    bar->GetMenu(3)->Append(L_UP, U("返回上级实例\tAlt+Up"));
    bar->GetMenu(3)->Append(L_EDIT_DEFINITION, U("编辑当前电路定义（暂停并复位）"));
    bar->GetMenu(3)->Append(L_PIN_LAYOUT, U("自定义符号引脚布局…"));
    menu("仿真", {{L_STEP, "单步传播\tF7"},
                  {L_TICK, "时钟基础节拍\tF8"},
                  {L_RUN, "运行 / 暂停\tF5"},
                  {L_RUN_CYCLES, "运行指定节拍…"},
                  {L_RESET, "复位"},
                  {L_TRACE, "事件追踪"}});
    bar->GetMenu(4)->Append(L_MEMORY_RUNTIME, U("查看实例运行内存…"));
    bar->GetMenu(4)->Append(L_MEMORY_INITIAL, U("编辑存储器初始内容…"));
    bar->GetMenu(4)->Append(L_MONITOR, U("信号监测 / 连续 CSV 日志…"));
    bar->GetMenu(4)->Append(L_TEXT_DEVICE, U("键盘输入 / TTY 内容…"));
    menu("分析", {{L_TABLE, "当前电路真值表与表达式"}, {L_EXPRESSION, "表达式 → 真值表…"},
                  {L_NEW_TABLE,"新建可编辑真值表…"}});
    menu("帮助", {{L_GUIDE, "操作指南"}, {L_HELP, "数字逻辑使用说明"}});
    menu("案例", {{L_EXPERIMENTS, "实验案例…"}});
    SetMenuBar(bar);
    auto root = new wxBoxSizer(wxVERTICAL);
    auto shortcuts = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxTB_HORIZONTAL | wxTB_TEXT | wxTB_NODIVIDER);
    shortcuts->SetName("logic-components");
    shortcuts->SetToolBitmapSize({28, 28});
    auto button = [&](int id, const char *name, const char *asset) {
        shortcuts->AddTool(id, U(name), icon(asset), U(name));
        shortcuts->SetToolLongHelp(id, U(asset));
        shortcuts->Bind(
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
    shortcuts->AddSeparator();
    auto dropdown = [shortcuts](int id, const char *name, const char *asset, wxMenu *m) {
        shortcuts->AddTool(id, U(name), icon(asset), U(name), wxITEM_DROPDOWN);
        shortcuts->SetDropdownMenu(id, m);
        shortcuts->Bind(wxEVT_TOOL, [shortcuts, m](wxCommandEvent &) { shortcuts->PopupMenu(m); }, id);
    };
    dropdown(L_IMPORT_MENU, "导入", "Import", importMenu());
    dropdown(L_EXPORT_MENU, "导出", "Export", exportMenu());
    shortcuts->AddSeparator();
    button(L_SELECT, "选择 S", "Select");
    button(L_WIRE, "导线 W", "Wire");
    shortcuts->AddSeparator();
    for (const auto &entry : std::vector<std::pair<std::string, std::string>>{{"Input", "输入"},
                                                                              {"Output", "输出"},
                                                                              {"Not", "非门"},
                                                                              {"Buffer", "缓冲"},
                                                                              {"And", "与门"},
                                                                              {"Or", "或门"},
                                                                              {"Xor", "异或"},
                                                                              {"Nand", "与非"},
                                                                              {"Nor", "或非"},
                                                                              {"Xnor", "同或"}}) {
        if (entry.first == "Not")
            shortcuts->AddSeparator();
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
                canvas->selected.clear();
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
    auto body = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    body->SetName("workspace-splitter");
    body->SetMinimumPaneSize(FromDIP(250));
    auto left = new wxPanel(body);
    left->SetName("library-sidebar");
    left->SetMinSize(FromDIP(wxSize(280, -1)));
    auto ls = new wxBoxSizer(wxVERTICAL);
    ls->Add(new wxStaticText(left, wxID_ANY, U("项目 / 数字元件库")), 0, wxLEFT | wxTOP | wxBOTTOM, FromDIP(7));
    auto circuitRow = new wxBoxSizer(wxHORIZONTAL);
    circuits = new wxChoice(left, wxID_ANY);
    circuits->SetName("circuit-selector");
    circuitRow->Add(circuits, 1, wxEXPAND | wxRIGHT, FromDIP(4));
    newCircuitButton = new wxButton(left, L_NEW_CIRCUIT, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(28, 27)));
    newCircuitButton->SetName("new-subcircuit");
    newCircuitButton->SetBitmap(icon("AddCircuit").GetBitmap(FromDIP(wxSize(18, 18))));
    newCircuitButton->SetToolTip(U("新建子电路 (Ctrl+Shift+N)"));
    deleteCircuitButton = new wxButton(left, L_DEL_CIRCUIT, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(28, 27)));
    deleteCircuitButton->SetName("delete-subcircuit");
    deleteCircuitButton->SetBitmap(icon("DeleteCircuit").GetBitmap(FromDIP(wxSize(18, 18))));
    deleteCircuitButton->SetToolTip(U("删除当前子电路 (Ctrl+Shift+Delete)"));
    circuitRow->Add(newCircuitButton, 0, wxRIGHT, FromDIP(3));
    circuitRow->Add(deleteCircuitButton, 0);
    ls->Add(circuitRow, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
    ls->Add(new wxStaticText(left, wxID_ANY, U("实例层次 · 双击进入")), 0, wxLEFT | wxTOP, FromDIP(6));
    hierarchy = new wxTreeCtrl(left, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 28)), wxTR_DEFAULT_STYLE);
    hierarchy->SetName("simulation-hierarchy");
    ls->Add(hierarchy, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
    search = new wxTextCtrl(left, wxID_ANY);
    search->SetHint(U("搜索元件名称或类型"));
    ls->Add(search, 0, wxEXPAND | wxALL, FromDIP(6));
    auto sidebarSplit = new wxSplitterWindow(left, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    sidebarSplit->SetName("library-properties-splitter");
    sidebarSplit->SetMinimumPaneSize(FromDIP(130));
    auto libraryPanel = new wxPanel(sidebarSplit);
    auto librarySizer = new wxBoxSizer(wxVERTICAL);
    tree = new wxTreeCtrl(libraryPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT);
    tree->SetName("component-library");
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
    librarySizer->Add(tree, 1, wxEXPAND);
    libraryPanel->SetSizer(librarySizer);
    auto propertyPanel = new wxPanel(sidebarSplit);
    propertyPanel->SetName("property-inspector");
    auto propertySizer = new wxBoxSizer(wxVERTICAL);
    selection = new wxStaticText(propertyPanel, wxID_ANY, U("电路：main"));
    selection->SetFont(selection->GetFont().Bold());
    propertySizer->Add(selection, 0, wxEXPAND | wxALL, FromDIP(5));
    properties = new wxGrid(propertyPanel, wxID_ANY);
    properties->SetName("component-properties");
    properties->CreateGrid(P_COUNT, 2);
    properties->SetColLabelValue(0, U("属性"));
    properties->SetColLabelValue(1, U("值"));
    properties->SetRowLabelSize(0);
    properties->SetColLabelSize(FromDIP(24));
    properties->SetDefaultRowSize(FromDIP(24));
    properties->SetColSize(0, FromDIP(126));
    properties->SetColSize(1, FromDIP(155));
    properties->SetDefaultCellAlignment(wxALIGN_LEFT, wxALIGN_CENTER);
    properties->SetGridLineColour(wxColour("#cbd5e1"));
    properties->SetLabelBackgroundColour(wxColour("#eef2f7"));
    properties->DisableDragRowSize();
    properties->DisableDragColMove();
    properties->SetMinSize(FromDIP(wxSize(265, 140)));
    for (int row = 0; row < P_COUNT; ++row) {
        auto caption = propertyCaptions.find(row);
        properties->SetCellValue(row, 0, caption == propertyCaptions.end() ? wxString{} : U(caption->second));
        properties->SetReadOnly(row, 0);
        properties->SetCellBackgroundColour(row, 0, wxColour("#f7f9fc"));
        if (booleanRows.count(row) || row >= P_INPUT_INVERT_FIRST) {
            properties->SetCellEditor(row, 1, new wxGridCellBoolEditor());
            properties->SetCellRenderer(row, 1, new wxGridCellBoolRenderer());
        }
    }
    auto choices = [&](int row, const std::vector<std::string> &values) {
        wxArrayString items;
        for (const auto &value : values)
            items.Add(U(value));
        properties->SetCellEditor(row, 1, new wxGridCellChoiceEditor(items));
    };
    choices(P_ROTATION, directionNames);
    choices(P_MODE, finderNames);
    choices(P_XOR, xorNames);
    properties->SetToolTip(U("双击值或按 F2 编辑，回车确认。方向使用下拉选项；勾选输入反相。数值支持十进制和 0x。"));
    properties->Bind(wxEVT_GRID_CELL_CHANGED, [this](wxGridEvent &e) {
        if (syncingProperties)
            return;
        try {
            apply();
            propertyHint->SetLabel(wxEmptyString);
            CallAfter([this] { inspect(); });
        } catch (const std::exception &error) {
            properties->SetCellValue(e.GetRow(), e.GetCol(), e.GetString());
            propertyHint->SetLabel(U("未修改：" + std::string(error.what())));
            propertyHint->Wrap(std::max(100, properties->GetClientSize().x));
            propertyHint->GetParent()->Layout();
        }
    });
    properties->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
        const int available = properties->GetClientSize().x - FromDIP(4);
        const int first = std::min(FromDIP(132), available / 2);
        properties->SetColSize(0, std::max(FromDIP(100), first));
        properties->SetColSize(1, std::max(FromDIP(90), available - first));
        e.Skip();
    });
    propertySizer->Add(properties, 1, wxEXPAND);
    propertyHint = new wxStaticText(propertyPanel, wxID_ANY, wxEmptyString);
    propertyHint->SetForegroundColour(wxColour("#c53242"));
    propertySizer->Add(propertyHint, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));
    propertyPanel->SetSizer(propertySizer);
    sidebarSplit->SetSashGravity(.52);
    sidebarSplit->SplitHorizontally(libraryPanel, propertyPanel, FromDIP(310));
    ls->Add(sidebarSplit, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(5));
    left->SetSizer(ls);
    auto canvasColumn = new wxPanel(body);
    auto canvasSizer = new wxBoxSizer(wxVERTICAL);
    auto navigationBar = new wxBoxSizer(wxHORIZONTAL);
    navigateUpButton = new wxButton(canvasColumn, L_UP, U("↑ 返回上级"));
    breadcrumbs = new wxChoice(canvasColumn, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(290, -1)));
    breadcrumbs->SetName("instance-breadcrumbs");
    breadcrumbs->SetMaxSize(FromDIP(wxSize(490, -1)));
    editDefinitionButton = new wxButton(canvasColumn, L_EDIT_DEFINITION, U("编辑定义（复位）"));
    navigationBar->Add(navigateUpButton, 0, wxRIGHT, FromDIP(5));
    navigationBar->Add(breadcrumbs, 0);
    navigationBar->Add(editDefinitionButton, 0, wxLEFT, FromDIP(5));
    canvasSizer->Add(navigationBar, 0, wxEXPAND | wxALL, FromDIP(5));
    canvas = new LogicCanvas(this, canvasColumn);
    canvas->SetName("logic-canvas");
    canvasSizer->Add(canvas, 1, wxEXPAND);
    canvasColumn->SetSizer(canvasSizer);
    body->SetSashGravity(0);
    body->SplitVertically(left, canvasColumn, FromDIP(310));
    root->Add(body, 1, wxEXPAND);
    status = new wxStaticText(this, wxID_ANY, "");
    root->Add(status, 0, wxEXPAND | wxALL, FromDIP(5));
    SetSizer(root);
    CallAfter([sidebarSplit] {
        sidebarSplit->SetSashPosition(int(sidebarSplit->GetClientSize().y * .56));
    });

    // 连接诊断只在需要时打开，画布不再为固定 ERC 面板预留空间。
    diagnosticsWindow = new wxDialog(this, wxID_ANY, U("ERC 问题"), wxDefaultPosition, FromDIP(wxSize(540, 410)),
                                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    diagnosticsWindow->SetName("erc-problems-window");
    auto diagnosticSizer = new wxBoxSizer(wxVERTICAL);
    diagnosticSummary = new wxStaticText(diagnosticsWindow, wxID_ANY, U("ERC 问题"));
    diagnosticSizer->Add(diagnosticSummary, 0, wxEXPAND | wxALL, FromDIP(8));
    diagnostics = new wxListBox(diagnosticsWindow, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_HSCROLL);
    diagnostics->SetName("connection-diagnostics");
    diagnostics->SetToolTip(U("单击问题即可缩放定位并高亮相关对象"));
    diagnosticSizer->Add(diagnostics, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
    diagnosticDetail = new wxTextCtrl(diagnosticsWindow, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    FromDIP(wxSize(-1, 140)), wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
    diagnosticDetail->SetName("connection-diagnostic-detail");
    diagnosticSizer->Add(diagnosticDetail, 0, wxEXPAND | wxALL, FromDIP(8));
    diagnosticsWindow->SetSizer(diagnosticSizer);
    diagnosticsWindow->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &e) {
        if (e.CanVeto()) { diagnosticsWindow->Hide(); e.Veto(); }
        else e.Skip();
    });
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
        simulator->reset();
        rebuild(false);
    });
    bind(L_RUN_CYCLES, [this] {
        wxTextEntryDialog d(this, U("推进多少个基础节拍？（1–100000）\n各时钟根据自身高/低持续节拍与相位独立变化。"), U("运行指定节拍"), "10");
        if (d.ShowModal() != wxID_OK)
            return;
        long n;
        if (!d.GetValue().ToLong(&n) || n < 1 || n > 100000)
            throw std::runtime_error("节拍数应为 1–100000");
        stop();
        if (simulator->pending())
            simulator->settle();
        for (long i = 0; i < n && !simulator->oscillating(); ++i)
            simulator->tick();          // 全局基础节拍，不把多个独立时钟误称为同一周期。
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
    bind(L_IMPORT_TABLE,[this] {
        wxFileDialog file(this,U("导入真值表 CSV"),"","","CSV (*.csv)|*.csv",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
        if(file.ShowModal()==wxID_OK) { stop();showTable(parseTruthCsv(readFile(fsPath(file.GetPath())))); }
    });
    bind(L_IMPORT_CIRC, [this] {
        wxFileDialog file(this, U("导入 Logisim 2.7 电路"), "", "", "Logisim (*.circ)|*.circ",
                          wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (file.ShowModal() == wxID_OK && discard()) load(file.GetPath());
    });
    bind(L_NEW_TABLE,[this] {
        wxTextEntryDialog inputs(this,U("输入变量名，以英文逗号分隔（1–10 个）"),U("新建真值表"),"A,B");
        if(inputs.ShowModal()!=wxID_OK)return;
        wxTextEntryDialog outputs(this,U("输出变量名，以英文逗号分隔"),U("新建真值表"),"Y");
        if(outputs.ShowModal()!=wxID_OK)return;
        Table table;auto names=[](const wxString &text){std::vector<std::string> values;std::istringstream input(utf8(text));std::string name;
            while(std::getline(input,name,',')){auto value=U(name);value.Trim().Trim(false);values.push_back(utf8(value));}return values;};
        table.inputs=names(inputs.GetValue());table.outputs=names(outputs.GetValue());
        if(table.inputs.empty()||table.inputs.size()>10||table.outputs.empty())throw std::runtime_error("需要 1–10 个输入和至少一个输出");
        for(size_t code=0;code<(size_t(1)<<table.inputs.size());++code){std::vector<int> row(table.inputs.size()+table.outputs.size(),0);
            for(size_t bit=0;bit<table.inputs.size();++bit)row[bit]=int((code>>bit)&1);table.rows.push_back(std::move(row));}
        stop();showTable(std::move(table));
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
        if (d.ShowModal() == wxID_OK) {
            wxTextEntryDialog scale(this, U("导出比例（0.25–4 倍）"), U("PNG 导出比例"), "1.5");
            if (scale.ShowModal() != wxID_OK) return;
            double value;
            if (!scale.GetValue().ToDouble(&value) || value < .25 || value > 4)
                throw std::runtime_error("比例应为 0.25–4");
            canvas->exportPng(d.GetPath(), value);
        }
    });
    bind(L_GUIDE, [this] {
        showReport("操作指南",
            "单击库元件，再点画布放置\n也可从库直接拖入画布\nShift 多选 · 空白处框选\n"
            "滚轮缩放 · 中键平移\n双击输入切换 · 双击实例进入\n连线时点导线创建分支\n"
            "双击空白结束导线 · Esc 取消\n\n"
            "运行实例调试\n双击子电路查看该实例的真实信号，导航不暂停、不复位。\n"
            "使用顶部路径或“返回上级”导航；接口 Input 由父层驱动。\n"
            "实例内可操作按钮；改线或属性请点击“编辑定义（复位）”。\n"
            "F5 运行/暂停；F7 单步传播；F8 时钟节拍。\n"
            "Ctrl+R 顺时针旋转；右键显示当前对象的快捷操作。");
    });
    bind(L_UP, [this] { if (!navigation.empty()) navigateTo(navigation.size() - 1); });
    bind(L_EDIT_DEFINITION, [this] { editDefinition(); });
    for (int id : {L_UP, L_EDIT_DEFINITION})
        Bind(wxEVT_BUTTON, [this,id](wxCommandEvent &) {
            wxCommandEvent command(wxEVT_MENU,id); GetEventHandler()->ProcessEvent(command);
        }, id);
    bind(L_MEMORY_INITIAL, [this] { memoryEditor(false); });
    bind(L_MEMORY_RUNTIME, [this] { memoryEditor(true); });
    bind(L_PIN_LAYOUT, [this] { pinLayout(); });
    bind(L_MONITOR, [this] { monitorSignals(); });
    bind(L_BATCH_PNG, [this] { exportBatch(); });
    bind(L_TEXT_DEVICE, [this] { textDevice(); });
    bind(L_ROTATE, [this] {
        change([this] { for(const auto &id : canvas->selected)
            if(auto p=circuit().part(id)) p->rotation=(p->rotation+1)%4; });
    });
    bind(L_RECENT, [this] { recentMenu(); });
    bind(L_EXPERIMENTS, [this] { showExperimentCases(); });
    bind(L_TEMPLATE, [this] {
        wxArrayString choices; choices.Add(U("空白数字工程")); choices.Add(U("半加器（组合逻辑）"));
        choices.Add(U("答辩演示：两层实例 / 独立寄存器与 RAM"));
        wxSingleChoiceDialog dialog(this,U("选择新建工程模板"),U("工程模板"),choices);
        if(dialog.ShowModal()!=wxID_OK || !discard()) return;
        stop(); canvas->cancel(); canvas->appearance=false;
        if(dialog.GetSelection()==2) {
            // 分发版优先从程序旁读取模板；源码路径仅作为开发构建的后备。
            auto path=fsPath(wxStandardPaths::Get().GetExecutablePath()).parent_path()/"examples"/"defense-hierarchy.logic.json";
            if(!std::filesystem::exists(path))
                path=std::filesystem::path(__FILE__).parent_path().parent_path()/"examples"/"defense-hierarchy.logic.json";
            history.reset(deserialize(readFile(path)),false);
        } else history.reset(dialog.GetSelection()==1 ? halfAdder() : Project{});
        current=history.project.main; savedPath.clear(); scope.clear(); navigation.clear();
        rebuild(true,true); canvas->fit();
    });
    bind(L_NEW_CIRCUIT, [this] { newCircuit(); });
    bind(L_DIAGNOSTICS, [this] { openDiagnostics(); });
    bind(L_REROUTE, [this] { rerouteSelected(); });
    for (int command : {L_NEW_CIRCUIT, L_DEL_CIRCUIT})
        Bind(wxEVT_BUTTON, [this, command](wxCommandEvent &) {
            wxCommandEvent event(wxEVT_MENU, command);
            GetEventHandler()->ProcessEvent(event);
        }, command);
    bind(L_NEW_COMPONENT, [this] { newComponent(); });
    bind(L_SAVE_COMPONENT, [this] { saveComponent(); });
    bind(L_LOAD_COMPONENT, [this] { loadComponent(); });
    bind(L_NETLIST_JSON, [this] { exportLogicNetlist(); });
    bind(L_PCB, [this] { exportKiCadPcb(); });
    bind(L_WRAP, [this] { encapsulate(); });
    bind(L_RENAME, [this] { renameCircuit(); });
    bind(L_DEL_CIRCUIT, [this] { deleteCircuit(); });
    bind(L_HELP, [this] {
        showReport(
            "数字逻辑使用说明",
             "1. 选择左侧元件，在画布单击放置，或从库拖入。选择工具下拖动元件，导线跟随引脚。\n"
            "2. W 连线：点击引脚开始，空白处设置拐点，点击目标引脚结束。双击空白可结束到节点。\n"
            "3. 点击已有导线可创建分支；交叉不自动连接。J 创建节点；选中节点可设置网络标签。\n"
            "4. 双击输入/按钮切换；多位值在左侧属性表格编辑。F7 推进一个事件时刻，F8 翻转时钟"
             "（基础节拍），F5 运行/暂停；仿真菜单可一次推进指定数量的基础节拍。\n"
            "5. 项目菜单新建子电路，用输入/输出引脚定义接口；左侧子电路库放置实例，双击进入。\n"
            "   封装功能将当前电路的副本保存为子电路定义；每个实例的寄存器、RAM 状态独立。\n"
            "6. Ctrl+S 保存整个工程（包括全部子电路）；Ctrl+Z/Y 撤销重做；Ctrl+C/V 使用系统剪贴板。\n"
            "7. 分析菜单生成组合电路真值表和标准与或表达式，可导出 CSV；输入最多 10 位。\n"
            "8. 文件 → 导出集中提供完整/批量 PNG、分析 CSV、网表和自定义元件；PNG 不限于屏幕区域。\n"
            "9. 项目 → 新建自定义元件：设置接口后在画布实现逻辑，通过文件 → 导出保存 .component.json。\n"
            "10. 文件 → 导出 → 逻辑网表 JSON：保存当前电路及嵌套实例的连接、位宽和器件参数。\n"
            "    文件 → 导出 → KiCad PCB：生成 .kicad_pcb、配套 .net 和 .mapping.json，在 KiCad PCB 编辑器打开板文件。\n"
            "11. 点击画布 ERC 图标或按 Ctrl+Shift+E 查看问题，单击列表项定位；事件追踪显示传播时刻与实例路径。\n"
            "12. 文件 → 导入集中提供 .circ、自定义元件和真值表 CSV；导入 .circ 后保存为 .logic.json。\n"
             "位宽 1–32；多输入逻辑门、可配置选择器、位分组总线、零/符号扩展；RAM/ROM 容量由地址位数设置。\n"
             "时序元件可配置触发沿；CLR 引脚为异步清零（高电平有效，与时钟无关）；"
            "计数器 CARRY 在计到位宽最大值时输出 1，可用它级联多级计数器。复位操作恢复属性中的初值。\n"
            "整数运算无符号、结果截断到位宽；加减法 C 表示进位/借位；移位数量由 B 给出。\n"
             "组合分析支持表达式化简、彩色圈组卡诺图、编辑真值表和自动生成门电路。Text 是注释。\n"
             "按钮可配置保持切换或按下有效；时钟高/低节拍和相位可独立配置。\n"
            "11. 工具菜单：L 直线、B 曲线、R 矩形、E 椭圆、C 圆。拖动绘制，曲线再单击确定弯曲。\n"
             "12. 编辑元件外观：围绕绿色接口绘制符号，可直接拖动接口，或通过项目菜单精确输入坐标。\n"
             "14. 双击实例进入真实运行态，返回上级不复位。运行态属性/接口输入只读，内部按钮只影响当前实例。\n"
             "15. 运行内存只读查看；初值编辑支持 raw HEX 文件。信号监测可选择输出并连续记录 CSV。\n"
            "13. S 选择后拖动图形或控制点；左侧属性面板修改线宽、填充。Shift 约束，Esc 取消。");
    });
    search->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { populate(); });
    breadcrumbs->Bind(wxEVT_CHOICE, [this](wxCommandEvent &e) {
        if(e.GetSelection()>=0 && size_t(e.GetSelection())<navigation.size())
            navigateTo(size_t(e.GetSelection()));
    });
    hierarchy->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent &e) {
        auto item=dynamic_cast<Item*>(hierarchy->GetItemData(e.GetItem()));
        if(!item) return;
        auto instancePath=item->circuit;
        if(instancePath.empty()) { if(!navigation.empty()) navigateTo(0); return; }
        if(!navigation.empty()) navigateTo(0);
        std::istringstream path(instancePath); std::string id;
        while(std::getline(path,id,'/')) if(!id.empty()) enterInstance(id);
    });
    circuits->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
        canvas->cancel();
        stop();
        setAppearance(false);
        current = utf8(circuits->GetStringSelection());
        scope.clear(); navigation.clear();
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
        canvas->selected.clear();
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
        canvas->selected.clear();
        inspect();
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
    diagnostics->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &e) {
        const auto row = e.GetSelection();
        if (row >= 0 && size_t(row) < diagnosticOrder.size()) showDiagnostic(diagnosticOrder[size_t(row)], true);
    });
    diagnostics->Bind(wxEVT_LISTBOX_DCLICK,
                      [this](wxCommandEvent &e) {
                          const auto row = e.GetSelection();
                          if (row >= 0 && size_t(row) < diagnosticOrder.size()) showDiagnostic(diagnosticOrder[size_t(row)], true);
                      });
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
        simulator->setTraceSink({}); traceFile.reset();
        e.Skip();
    });
    rebuild(true, true);
    auto &config = *wxConfigBase::Get();
    for(int i=0;i<8;++i) { wxString path; if(config.Read("recent/"+std::to_string(i),&path) && !path.empty())
        recentFiles.push_back(utf8(path)); }
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
// 刷新界面状态：reset 时重建并稳定仿真器；lib 时重建电路下拉框与元件树。
std::string LogicEditor::scoped(const std::string &id) const {
    return scope.empty() ? id : scope + "/" + id;
}
Signal LogicEditor::read(const Endpoint &endpoint) const {
    return simulator->read({scoped(endpoint.object),endpoint.pin});
}
void LogicEditor::enterInstance(const std::string &id) {
    auto part=circuit().part(id);
    if(!part || part->kind!="Subcircuit") return;
    std::string child=part->circuit, childScope=scoped(id);
    canvas->cancel();
    navigation.push_back({current,scope,canvas->view});
    current=child; scope=childScope;
    canvas->appearance=false; canvas->tool="Select"; canvas->selected.clear();
    rebuild(false,true); canvas->fit();
}
void LogicEditor::navigateTo(size_t level) {
    if(level>=navigation.size()) return;
    auto destination=navigation[level];
    canvas->cancel(); current=destination.circuit; scope=destination.scope;
    navigation.resize(level); canvas->view=destination.view;
    canvas->selected.clear(); canvas->tool="Select"; canvas->appearance=false;
    rebuild(false,true);
}
void LogicEditor::editDefinition() {
    // 同一定义可能被多个实例引用。明确离开观察态后，结构修改才允许全局复位。
    stop(); canvas->cancel(); scope.clear(); navigation.clear(); canvas->selected.clear();
    rebuild(true,true);
}
void LogicEditor::refreshHierarchy() {
    hierarchy->Freeze(); hierarchy->DeleteAllItems();
    auto root=hierarchy->AddRoot(U(simulationRoot),-1,-1,new Item("Instance",""));
    size_t count=0;
    std::function<void(wxTreeItemId,const std::string&,const std::string&,int)> add;
    add=[&](wxTreeItemId parent,const std::string &name,const std::string &prefix,int depth) {
        if(depth>20 || count>2000) return;
        for(const auto &part:history.project.circuit(name).parts) if(part.kind=="Subcircuit") {
            auto path=prefix.empty()?part.id:prefix+"/"+part.id;
            auto child=hierarchy->AppendItem(parent,U((part.label.empty()?part.id:part.label)+" : "+part.circuit),
                                              -1,-1,new Item("Instance",path));
            ++count; add(child,part.circuit,path,depth+1);
        }
    };
    add(root,simulationRoot,"",0); hierarchy->Expand(root); hierarchy->Thaw();
    const int height = FromDIP(int(std::clamp<size_t>((count + 1) * 23 + 4, 28, 92)));
    hierarchy->SetMinSize({-1, height});
    hierarchy->SetMaxSize({-1, height});
    hierarchy->GetParent()->Layout();
}
void LogicEditor::rebuild(bool reset, bool lib) {
    canvas->wireDisplayDirty = true;
    canvas->previewDirty = true;
    if(!scope.empty() && !reset) { canvas->tool="Select"; canvas->appearance=false; }
    if (reset) {
        stop();
        scope.clear(); navigation.clear(); simulationRoot=current;
        simulator = std::make_unique<Simulator>(history.project, current);
        if(traceFile) simulator->setTraceSink([this](const Trace &event) { writeTrace(event); });
        simulator->settle();
        hasTable = false;
    }
    if (lib) {
        circuits->Clear();
        for (auto &c : history.project.circuits)
            circuits->Append(U(c.name));
        circuits->SetStringSelection(U(current));
        populate();
        refreshHierarchy();
    }
    breadcrumbs->Clear();
    for(const auto &level:navigation)
        breadcrumbs->Append(U(level.scope.empty() ? level.circuit : level.scope + " / " + level.circuit));
    breadcrumbs->Append(U(scope.empty() ? current + " · 定义编辑" : scope + " / " + current + " · 运行实例"));
    breadcrumbs->SetSelection(int(navigation.size()));
    navigateUpButton->Enable(!navigation.empty());
    editDefinitionButton->Show(!scope.empty());
    newCircuitButton->Enable(scope.empty());
    deleteCircuitButton->Enable(canDeleteCircuit());
    GetMenuBar()->FindItem(L_NEW_CIRCUIT)->Enable(scope.empty());
    GetMenuBar()->FindItem(L_DEL_CIRCUIT)->Enable(canDeleteCircuit());
    GetMenuBar()->FindItem(L_REROUTE)->Enable(scope.empty());
    breadcrumbs->GetParent()->Layout();
    SetTitle(U(std::string(history.dirty() ? "* " : "") + history.project.name + " — " + current +
                (scope.empty()?" · 定义编辑":" · 实例 "+scope) + " · PracticeEDA 数字逻辑"));
    refreshDiagnostics();
    status->SetLabel(
        U("工具：" + canvas->tool + (canvas->tool == "Place" ? " / " + pretty(canvas->placing) : "") +
          "   |   " + (timer.IsRunning() ? "运行中" : "已暂停") + "   基础节拍=" +
          std::to_string(simulator->ticks()) + "   |   t=" +
          std::to_string(simulator->time()) + "   待处理事件=" + std::to_string(simulator->pending()) +
          "   |   元件=" + std::to_string(circuit().parts.size()) +
          "   导线=" + std::to_string(circuit().wires.size())));
    if(!routingNotice.empty())status->SetLabel(status->GetLabel()+U("   |   "+routingNotice));
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
    if (oldIndex >= 0 && size_t(oldIndex) < diagnosticOrder.size() &&
        size_t(diagnosticOrder[size_t(oldIndex)]) < issues.size())
        picked = issues[size_t(diagnosticOrder[size_t(oldIndex)])];
    issues = circDiagnostics(circuit());
    auto simulationIssues = simulator->issues();
    issues.insert(issues.end(), simulationIssues.begin(), simulationIssues.end());
    diagnostics->Freeze();
    diagnostics->Clear();
    diagnosticOrder.clear();
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
        diagnosticOrder.push_back(int(index));
        if (picked && picked->object == issue.object && picked->message == issue.message &&
            picked->severity == issue.severity)
            selectedIndex = int(index);
    }
    if (issues.empty()) {
        diagnosticSummary->SetLabel(U("ERC · 当前检查范围内未发现问题"));
        diagnosticDetail->ChangeValue(U("目前检查位宽和仿真连接状态；此列表不表示完整静态 ERC 已通过。"));
    } else {
        diagnosticSummary->SetLabel(U("ERC · " + std::to_string(errors) + " 错误 / " +
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
    diagnosticDetail->ChangeValue(U(diagnosticText(issue)));
    if (!locate)
        return;
    auto id = issue.object;
    // 诊断使用完整实例路径；逐层导航只改变观察上下文，不修改任何运行状态。
    if(id.find('/')!=std::string::npos) {
        if(!navigation.empty()) navigateTo(0);
        while(id.find('/')!=std::string::npos) {
            auto split=id.find('/'); auto instance=id.substr(0,split);
            auto part=circuit().part(instance);
            if(!part || part->kind!="Subcircuit") break;
            enterInstance(instance); id=id.substr(split+1);
        }
    } else if(!scope.empty()) {
        if(!navigation.empty()) navigateTo(0);
    }
    std::optional<Point> at;
    Point extent{110, 90};
    auto find = [&] {
        if (auto part = circuit().part(id)) {
            at = part->at;
            const auto body = partSize(history.project, *part);
            extent = part->rotation % 2 ? Point{body.y, body.x} : body;
        }
        for (const auto &node : circuit().nodes)
            if (node.id == id)
                at = node.at;
        for (const auto &wire : circuit().wires)
            if (wire.id == id) {
                const auto points = wirePoints(history.project, circuit(), wire);
                Point lo = points.front(), hi = lo;
                for (auto p : points) { lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
                                       hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); }
                at = (lo + hi) * .5;
                extent = hi - lo;
            }
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
    canvas->view.zoom = std::clamp(std::min(size.x * .65 / (extent.x + 120), size.y * .6 / (extent.y + 100)), .3, 2.0);
    canvas->view.offset = Point{size.x / 2.0, size.y / 2.0} - *at * canvas->view.zoom;
    inspect();
    canvas->Refresh();
}
Part LogicEditor::placementPart() const {
    const auto key = canvas->placing + ":" + canvas->subcircuit;
    auto found = toolDefaults.find(key);
    Part part = found == toolDefaults.end() ? Part{} : found->second;
    part.kind = canvas->placing;
    part.circuit = canvas->subcircuit;
    if (found == toolDefaults.end() &&
        (part.kind == "Splitter" || part.kind == "Joiner" || part.kind == "Extender" ||
         part.kind == "Hex" || part.kind == "RAM" || part.kind == "ROM"))
        part.width = 8;
    return part;
}
void LogicEditor::inspect() {
    const bool templateMode = canvas->tool == "Place" && scope.empty();
    const auto context = current + "|" + scope + "|" + canvas->tool +
                         (templateMode ? "|" + canvas->placing + ":" + canvas->subcircuit : "");
    if (properties->IsCellEditControlEnabled() && inspectedSelection == canvas->selected &&
        inspectedContext == context)
        return;
    if (properties->IsCellEditControlEnabled()) {
        syncingProperties = true;
        properties->HideCellEditControl();
        properties->DisableCellEditControl();
        syncingProperties = false;
    }
    inspectedSelection = canvas->selected;
    inspectedContext = context;
    properties->BeginBatch();
    for (int row = 0; row < P_COUNT; ++row) {
        properties->SetCellValue(row, 1, "");
        properties->SetReadOnly(row, 1);
        properties->SetCellBackgroundColour(row, 1, wxColour("#f1f5f9"));
        properties->HideRow(row);
        auto caption = propertyCaptions.find(row);
        if (caption != propertyCaptions.end())
            properties->SetCellValue(row, 0, U(caption->second));
    }
    auto set = [&](int row, const std::string &value, bool editable = false) {
        properties->ShowRow(row);
        properties->SetCellValue(row, 1, U(value));
        properties->SetReadOnly(row, 1, !editable);
        properties->SetCellBackgroundColour(row, 1, wxColour(editable ? "#ffffff" : "#f1f5f9"));
    };
    auto templatePart = templateMode ? placementPart() : Part{};
    const Part *part = templateMode ? &templatePart :
        canvas->selected.size() == 1 ? circuit().part(*canvas->selected.begin()) : nullptr;
    if (part) {
        const auto &p = *part;
        const auto &kind = p.kind;
        selection->SetLabel(U((templateMode ? "工具：" : "元件：") + pretty(kind)));
        selection->SetToolTip(U(templateMode ? "先设置属性，再在画布放置一次。" : "对象 ID：" + p.id));
        set(P_LABEL, p.label, true);
        if (kind != "Text") {
            set(P_ROTATION, directionNames[size_t(p.rotation)], true);
            const bool configurable = kind != "Subcircuit" && kind != "Clock" &&
                                      kind != "Decoder" && kind != "Encoder";
            set(P_WIDTH, std::to_string(p.width), configurable);
        }
        auto param = [&](int row) {
            const auto &spec = parameterRows.at(row);
            const auto value = parameter(p, spec.first, spec.second);
            if (row == P_MODE)
                set(row, finderNames[size_t(value)], true);
            else if (row == P_XOR)
                set(row, xorNames[size_t(value)], true);
            else
                set(row, booleanRows.count(row) ? (value ? "1" : "") : std::to_string(value), true);
        };
        if (kind == "And" || kind == "Or" || kind == "Xor" || kind == "Nand" || kind == "Nor" || kind == "Xnor") {
            param(P_INPUTS);
            const int count = int(parameter(p, "inputs", 2));
            const auto mask = uint32_t(parameter(p, "invertMask", 0));
            for (int index = 0; index < count; ++index) {
                const int row = P_INPUT_INVERT_FIRST + index;
                const auto pin = index == 0 ? "A" : index == 1 ? "B" : "I" + std::to_string(index);
                properties->SetCellValue(row, 0, U("反相输入 " + std::to_string(index + 1) + " (" + pin + ")"));
                set(row, mask & (uint32_t(1) << index) ? "1" : "", true);
            }
            if (kind == "Xor" || kind == "Xnor")
                param(P_XOR);
        }
        if (kind == "Mux" || kind == "Demux" || kind == "Decoder" || kind == "Encoder" || kind == "BitSelector") param(P_SELECT);
        if (kind == "BitSelector") param(P_OUTPUTWIDTH);
        if (kind == "BitFinder") param(P_MODE);
        if (kind == "LEDMatrix") { param(P_ROWS); param(P_COLS); }
        if (kind == "LEDMatrix" || kind == "SevenSegment") param(P_ACTIVELOW);
        if (kind == "Extender") { param(P_INPUTWIDTH); param(P_SIGN); }
        if (kind == "Clock") { param(P_HIGH); param(P_LOW); param(P_PHASE); }
        if (kind == "DFF" || kind == "TFF" || kind == "JKFF" || kind == "SRFF" || kind == "Register" ||
            kind == "Counter" || kind == "ShiftRegister" || kind == "RAM" || kind == "Random") param(P_FALLING);
        if (kind == "Counter") { param(P_MAX); param(P_LOAD); }
        if (kind == "RAM" || kind == "ROM") param(P_ADDR);
        if (kind == "Button") { param(P_MOMENTARY); param(P_ACTIVELOW); }
        if (kind == "Transistor") param(P_ACTIVELOW);
        if (kind == "Splitter" || kind == "Joiner") {
            std::ostringstream groups;
            for (size_t group = 0; group < p.bitGroups.size(); ++group) {
                if (group) groups << ';';
                for (size_t bit = 0; bit < p.bitGroups[group].size(); ++bit) {
                    if (bit) groups << ',';
                    groups << p.bitGroups[group][bit];
                }
            }
            set(P_GROUPS, groups.str(), true);
        }
        if (kind == "Input" || kind == "Button" || kind == "Constant" || kind == "Clock" || kind == "DFF" ||
            kind == "TFF" || kind == "JKFF" || kind == "SRFF" || kind == "Register" ||
            kind == "Counter" || kind == "ShiftRegister") {
            properties->SetCellValue(P_VALUE, 0, U(kind == "Input" ? "输入值" : kind == "Constant" ? "常量值" : "初始值"));
            set(P_VALUE, std::to_string(p.value), true);
        }
        if (kind == "RAM" || kind == "ROM") {
            std::ostringstream words;
            for (auto value : p.data) words << value << ' ';
            set(P_MEMORY, words.str(), true);
        }
        if (!templateMode) {
            const auto pins = ports(history.project, p);
            if (!pins.empty()) {
                auto pin = std::find_if(pins.begin(), pins.end(), [](const Port &port) { return port.output; });
                if (pin == pins.end()) pin = pins.begin();
                set(P_STATE, read({p.id, pin->id}).text());
            }
        }
    } else if (canvas->selected.empty() && !canvas->appearance && !shapeKind(canvas->tool)) {
        selection->SetLabel(U("电路：" + current));
        selection->SetToolTip(wxEmptyString);
        set(P_CIRCUIT, current, true);
        set(P_PARTCOUNT, std::to_string(circuit().parts.size()));
        set(P_WIRECOUNT, std::to_string(circuit().wires.size()));
        size_t inputs = 0, outputs = 0;
        for (const auto &p : circuit().parts) {
            if (p.kind == "Input") ++inputs;
            if (p.kind == "Output") ++outputs;
        }
        set(P_PORTCOUNT, std::to_string(inputs) + " / " + std::to_string(outputs));
    } else if (canvas->selected.size() == 1) {
        const auto id = *canvas->selected.begin();
        for (const auto &node : circuit().nodes)
            if (node.id == id) {
                selection->SetLabel(U("网络节点"));
                properties->SetCellValue(P_LABEL, 0, U("网络标签"));
                set(P_LABEL, node.label, true);
                set(P_STATE, read({id, ""}).text());
            }
        for (const auto &wire : circuit().wires)
            if (wire.id == id) {
                selection->SetLabel(U("导线 / 总线"));
                set(P_STATE, read({id, ""}).text());
            }
        for (const auto &shape : circuit().shapes)
            if (shape.id == id) {
                selection->SetLabel(U("图形：" + shape.kind));
                set(P_STROKE, std::to_string(int(shape.stroke)), true);
                set(P_FILL, shape.filled ? "1" : "", true);
                drawingStroke = int(shape.stroke);
                drawingFilled = shape.filled;
            }
    } else {
        selection->SetLabel(U(canvas->selected.empty() ? "绘图属性" :
                              "已选择 " + std::to_string(canvas->selected.size()) + " 个对象"));
        set(P_STROKE, std::to_string(drawingStroke), true);
        set(P_FILL, drawingFilled ? "1" : "", true);
    }
    if (!scope.empty()) {
        for (int row = 0; row < P_COUNT; ++row) properties->SetReadOnly(row, 1);
        selection->SetLabel(selection->GetLabel() + U(" · 实例只读"));
    }
    properties->EndBatch();
}
void LogicEditor::change(const std::function<void()> &action) {
    if(!scope.empty()) throw std::runtime_error("当前正在观察运行实例；请先点击顶部“编辑定义（复位）”再修改结构或初值。");
    // 编辑失败时必须同时恢复完整模型与当前打开的电路定义。
    canvas->cancel();
    auto before = history.project;
    auto beforeHistory=history;
    auto oldCurrent = current;
    auto oldRoot=simulationRoot;
    bool wasRunning=timer.IsRunning();stop();
    auto previousSimulator=std::move(simulator);
    try {
        action();
        history.project.validate();
        const auto routing = rerouteChangedGeometry(history.project, before);
        // 先编译成功，再提交撤销记录。无效参数/断线不能破坏旧运行态。
        auto candidate=std::make_unique<Simulator>(history.project,current);
        if(traceFile)candidate->setTraceSink([this](const Trace &event){ writeTrace(event); });
        candidate->settle();
        history.commit(before);
        simulator=std::move(candidate);simulationRoot=current;navigation.clear();scope.clear();hasTable=false;
        routingResult(routing);
        rebuild(false, true);
    } catch (...) {
        history=std::move(beforeHistory);
        current = oldCurrent;
        simulationRoot=oldRoot;simulator=std::move(previousSimulator);
        rebuild(false, true);if(wasRunning)timer.Start(500);
        throw;
    }
}
// 把左侧属性表格的编辑写回模型；校验失败时单元格回退并给出提示。
void LogicEditor::apply() {
    if (!scope.empty())
        throw std::runtime_error("实例属性只读。请先切换到“编辑定义（复位）”。");
    if (!properties->IsReadOnly(P_CIRCUIT, 1)) {
        const auto name = utf8(properties->GetCellValue(P_CIRCUIT, 1));
        if (name != current) renameCircuitTo(name);
        return;
    }
    if (!properties->IsReadOnly(P_STROKE, 1)) {
        long value;
        if (!properties->GetCellValue(P_STROKE, 1).ToLong(&value) || value < 1 || value > 20)
            throw std::runtime_error("图形线宽必须为 1–20");
        const bool filled = properties->GetCellValue(P_FILL, 1) == "1";
        const bool hasShapes = std::any_of(circuit().shapes.begin(), circuit().shapes.end(),
            [&](const Shape &shape) { return canvas->selected.count(shape.id) != 0; });
        if (hasShapes)
            change([&] {
                for (auto &shape : circuit().shapes)
                    if (canvas->selected.count(shape.id)) { shape.stroke = double(value); shape.filled = filled; }
            });
        drawingStroke = int(value);
        drawingFilled = filled;
        return;
    }
    auto choiceValue = [&](int row, const std::vector<std::string> &choices) {
        const auto value = utf8(properties->GetCellValue(row, 1));
        auto found = std::find(choices.begin(), choices.end(), value);
        if (found != choices.end()) return int64_t(found - choices.begin());
        long long number;
        if (properties->GetCellValue(row, 1).ToLongLong(&number) && number >= 0 &&
            size_t(number) < choices.size()) return int64_t(number);
        throw std::runtime_error("请从下拉列表选择有效属性值");
    };
    auto editedPart = [&](Part p) {
        p.label = utf8(properties->GetCellValue(P_LABEL, 1));
        if (!properties->IsReadOnly(P_WIDTH, 1)) {
            long width;
            if (!properties->GetCellValue(P_WIDTH, 1).ToLong(&width) || width < 1 || width > 32)
                throw std::runtime_error("位宽必须为 1–32");
            p.width = int(width);
        }
        if (!properties->IsReadOnly(P_VALUE, 1))
            p.value = unsignedValue(properties->GetCellValue(P_VALUE, 1));
        if (!properties->IsReadOnly(P_ROTATION, 1))
            p.rotation = int(choiceValue(P_ROTATION, directionNames));
        for (const auto &entry : parameterRows) {
            const auto row = entry.first;
            if (properties->IsReadOnly(row, 1)) continue;
            int64_t value;
            if (row == P_MODE) value = choiceValue(row, finderNames);
            else if (row == P_XOR) value = choiceValue(row, xorNames);
            else if (booleanRows.count(row)) value = properties->GetCellValue(row, 1) == "1" ? 1 : 0;
            else {
                long long number;
                if (!properties->GetCellValue(row, 1).ToLongLong(&number))
                    throw std::runtime_error("参数请输入整数");
                value = number;
            }
            p.params[entry.second.first] = value;
        }
        if (!properties->IsReadOnly(P_INPUTS, 1)) {
            uint32_t mask = 0;
            for (int index = 0; index < 32; ++index)
                if (!properties->IsReadOnly(P_INPUT_INVERT_FIRST + index, 1) &&
                    properties->GetCellValue(P_INPUT_INVERT_FIRST + index, 1) == "1")
                    mask |= uint32_t(1) << index;
            p.params["invertMask"] = mask;
        }
        if (!properties->IsReadOnly(P_GROUPS, 1)) {
            p.bitGroups.clear();
            std::istringstream input(utf8(properties->GetCellValue(P_GROUPS, 1)));
            std::string group;
            while (std::getline(input, group, ';')) {
                std::vector<int> bits;
                std::istringstream stream(group);
                std::string bit;
                while (std::getline(stream, bit, ',')) {
                    size_t used = 0;
                    int number = std::stoi(bit, &used);
                    if (used != bit.size() || number < 0 || number >= p.width)
                        throw std::runtime_error("分组格式为 0,1,2,3;4,5,6,7，位编号不能超出位宽");
                    bits.push_back(number);
                }
                if (!bits.empty()) p.bitGroups.push_back(std::move(bits));
            }
        }
        if (!properties->IsReadOnly(P_MEMORY, 1)) {
            p.data.clear();
            std::istringstream input(utf8(properties->GetCellValue(P_MEMORY, 1)));
            std::string word;
            while (input >> word) p.data.push_back(unsignedValue(U(word)));
        }
        return p;
    };
    if (canvas->tool == "Place") {
        auto part = editedPart(placementPart());
        auto candidate = history.project;
        part.id = candidate.id("p");
        candidate.circuit(current).parts.push_back(part);
        candidate.validate();
        part.id.clear();
        toolDefaults[canvas->placing + ":" + canvas->subcircuit] = std::move(part);
        rebuild(false);
        return;
    }
    if (canvas->selected.size() != 1) return;
    const auto id = *canvas->selected.begin();
    if (auto original = circuit().part(id)) {
        auto changed = editedPart(*original);
        if ((original->kind == "Input" || original->kind == "Button") &&
            changed.width == original->width && changed.label == original->label &&
            changed.rotation == original->rotation && changed.params == original->params &&
            changed.bitGroups == original->bitGroups) {
            const auto before = history.project;
            original->value = changed.value;
            history.commit(before);
            simulator->setInput(id, changed.value);
            simulator->settle();
            rebuild(false);
            return;
        }
        change([&] { *circuit().part(id) = changed; });
    } else {
        const auto name = utf8(properties->GetCellValue(P_LABEL, 1));
        change([&] { for (auto &node : circuit().nodes) if (node.id == id) node.label = name; });
    }
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
        rememberFile(savedPath);
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
        rememberFile(utf8(path));
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
    // Windows OLE 默认延迟提供数据；立即固化文本，关闭窗口后仍可在另一个工程粘贴。
    if (ok)
        wxTheClipboard->Flush();
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
    if(enabled && !scope.empty()) throw std::runtime_error("请先进入定义编辑再修改元件外观");
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
    // 序列化已提交的定义前，先结束/取消进行中的手势。
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
    // 导入只增加定义；选择放置不会改变当前电路。
    setAppearance(false);
    canvas->tool = "Place";
    canvas->placing = "Subcircuit";
    canvas->subcircuit = name;
    canvas->SetFocus();
    rebuild(false);
}

void LogicEditor::exportLogicNetlist() {
    canvas->cancel();
    // 单独编译已提交的工程：实时输入变化、RAM 与时钟状态保持不变。
    Simulator compiled(history.project, current);
    const auto content = compiled.netlist();
    wxFileDialog dialog(this, U("导出逻辑网表 JSON（包含嵌套元件）"), "", "circuit.json",
                        U("逻辑网表 JSON (*.json)|*.json"),
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    dialog.SetName("export-logic-netlist-dialog");
    if (dialog.ShowModal() != wxID_OK)
        return;
    if (!dialog.GetPath().Lower().EndsWith(".json"))
        throw std::runtime_error("逻辑网表请使用 .json 后缀保存");
    writeAtomic(fsPath(dialog.GetPath()), content);
}

void LogicEditor::exportKiCadPcb() {
    canvas->cancel();
    // 先检查实体映射；不支持的器件在选择保存位置前提示，不生成半成品。
    const auto result = exportPcb(history.project, current);
    wxFileDialog dialog(this, U("导出 KiCad PCB（包含配套网表和引脚映射）"), "", "circuit.kicad_pcb",
                        U("KiCad PCB (*.kicad_pcb)|*.kicad_pcb"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    dialog.SetName("export-kicad-pcb-dialog");
    if (dialog.ShowModal() != wxID_OK)
        return;
    if (!dialog.GetPath().Lower().EndsWith(".kicad_pcb"))
        throw std::runtime_error("KiCad PCB 请使用 .kicad_pcb 后缀保存");
    const auto board = fsPath(dialog.GetPath());
    auto netlist = board; netlist.replace_extension(".net");
    auto mapping = board; mapping.replace_extension(".mapping.json");
    std::string existing;
    for (const auto &path : {netlist, mapping})
        if (std::filesystem::exists(path)) existing += path.u8string() + "\n";
    if (!existing.empty() &&
        wxMessageBox(U("以下配套文件已存在，将同时覆盖：\n" + existing + "\n是否继续？"),
                     U("覆盖配套文件"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) {
        return;
    }
    writeAtomic(netlist, result.netlist);
    writeAtomic(mapping, result.manifest);
    writeAtomic(board, result.board);
    showReport("KiCad PCB 已导出",
               "请在 KiCad PCB 编辑器中打开：\n" + board.u8string() +
               "\n\n配套网表：\n" + netlist.u8string() + "\n引脚映射：\n" + mapping.u8string() +
               "\n\n支持单比特二输入 AND/OR/NAND/XOR 及 NOT，包含电源、去耦和连接器。\n"
               "板文件未布线，请继续布局、布线和设计规则检查。");
}

void LogicEditor::newCircuit() {
    wxTextEntryDialog d(this, U("子电路名称（在其中放置输入/输出引脚定义接口）"), U("新建子电路"),
                        U("circuit" + std::to_string(history.project.circuits.size())));
    d.SetName("new-circuit-dialog");
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
bool LogicEditor::canDeleteCircuit() const {
    if (!scope.empty() || current == history.project.main) return false;
    for (const auto &c : history.project.circuits)
        for (const auto &part : c.parts)
            if (part.kind == "Subcircuit" && part.circuit == current) return false;
    return true;
}
void LogicEditor::deleteCircuit() {
    if (!canDeleteCircuit())
        throw std::runtime_error("主电路、运行实例或仍被引用的子电路不能删除");
    change([&] {
        const auto name = current;
        auto &definitions = history.project.circuits;
        definitions.erase(std::remove_if(definitions.begin(), definitions.end(),
            [&](const Circuit &c) { return c.name == name; }), definitions.end());
        current = history.project.main;
        canvas->selected.clear();
        canvas->tool = "Select";
    });
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
        std::map<std::string,Point> copiedLayout;
        for(const auto &pin:c.interfaceLayout) copiedLayout[ids.at(pin.first)]=pin.second;
        c.interfaceLayout=std::move(copiedLayout);
        history.project.circuits.push_back(c);
    });
}
void LogicEditor::renameCircuit() {
    wxTextEntryDialog d(this, U("新电路名称"), U("重命名"), U(current));
    if (d.ShowModal() != wxID_OK)
        return;
    renameCircuitTo(utf8(d.GetValue()));
}
void LogicEditor::renameCircuitTo(const std::string &name) {
    if (name.empty() || name.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::runtime_error("电路名称不能为空");
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
    validateTable(table);
    wxDialog dialog(this,wxID_ANY,U("组合分析 · 真值表 / 化简 / 卡诺图 / 自动生成"),wxDefaultPosition,
                    {1060,760},wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
    auto sizer=new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(&dialog,wxID_ANY,U("输出列可编辑：0、1 或 -（设计不关心）。输入列固定；分析与生成使用当前表格。")),0,wxALL,10);
    auto grid=new wxGrid(&dialog,wxID_ANY); grid->SetName("editable-truth-table"); grid->CreateGrid(0,0);
    auto fill=[&] {
        if(grid->GetNumberRows()) grid->DeleteRows(0,grid->GetNumberRows());
        if(grid->GetNumberCols()) grid->DeleteCols(0,grid->GetNumberCols());
        grid->AppendCols(int(table.inputs.size()+table.outputs.size())); grid->AppendRows(int(table.rows.size()));
        for(size_t column=0;column<table.inputs.size()+table.outputs.size();++column) {
            bool output=column>=table.inputs.size();
            grid->SetColLabelValue(int(column),U((output?"输出 · ":"输入 · ")+
                (output?table.outputs[column-table.inputs.size()]:table.inputs[column])));
            grid->SetColSize(int(column),110);
            for(size_t row=0;row<table.rows.size();++row) {
                grid->SetCellValue(int(row),int(column),table.rows[row][column]<0?"-":std::to_string(table.rows[row][column]));
                grid->SetReadOnly(int(row),int(column),!output);
                if(output) grid->SetCellBackgroundColour(int(row),int(column),wxColour("#eefbf3"));
            }
        }
    };
    auto collect=[&] {
        if(grid->IsCellEditControlEnabled()) grid->SaveEditControlValue();
        for(size_t row=0;row<table.rows.size();++row) for(size_t column=table.inputs.size();column<table.rows[row].size();++column) {
            auto value=grid->GetCellValue(int(row),int(column)); value.Trim().Trim(false);
            if(value!="0"&&value!="1"&&value!="-") throw std::runtime_error("输出仅允许 0、1、-，请检查第 "+std::to_string(row+1)+" 行");
            table.rows[row][column]=value=="-"?-1:value=="1"?1:0;
        }
        validateTable(table); lastTable=table; hasTable=true;
    };
    fill(); sizer->Add(grid,1,wxEXPAND|wxLEFT|wxRIGHT,10);
    auto actions=new wxBoxSizer(wxHORIZONTAL);
    auto add=[&](const char *caption,std::function<void()> action) {
        auto button=new wxButton(&dialog,wxID_ANY,U(caption)); actions->Add(button,0,wxRIGHT,8);
        button->Bind(wxEVT_BUTTON,[&,action](wxCommandEvent &) { try { collect();action(); }
            catch(const std::exception &error){wxMessageBox(U(error.what()),U("分析失败"),wxOK|wxICON_ERROR,&dialog);} });
    };
    add("化简表达式",[&] { showReport("化简表达式",analysisReport(table)); });
    add("卡诺图 / 圈组",[&] {
        wxArrayString outputs;for(const auto &name:table.outputs) outputs.Add(U(name));
        wxSingleChoiceDialog choice(&dialog,U("选择输出函数"),U("卡诺图"),outputs);
        if(choice.ShowModal()!=wxID_OK)return; size_t output=size_t(choice.GetSelection());
        if(table.inputs.size()>4) { showReport("化简 / 卡诺图说明",karnaughReport(table,output));return; }
        auto minimized=minimizeOutput(table,output);size_t rbits=table.inputs.size()/2,cbits=table.inputs.size()-rbits;
        wxDialog map(&dialog,wxID_ANY,U("卡诺图 · "+table.outputs[output]),wxDefaultPosition,{800,650},wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
        auto box=new wxBoxSizer(wxVERTICAL);auto groups=new wxChoice(&map,wxID_ANY);
        groups->Append(U("显示所有圈组"));for(size_t i=0;i<minimized.terms.size();++i)groups->Append(U("圈组 "+std::to_string(i+1)));
        groups->SetSelection(0);box->Add(groups,0,wxEXPAND|wxALL,10);
        auto cells=new wxGrid(&map,wxID_ANY);cells->CreateGrid(1<<rbits,1<<cbits);cells->EnableEditing(false);
        cells->SetDefaultCellAlignment(wxALIGN_CENTER,wxALIGN_CENTER);cells->SetDefaultRowSize(66);cells->SetDefaultColSize(100);
        auto binary=[](size_t value,size_t bits){std::string result;for(size_t bit=0;bit<bits;++bit)result+=((value>>(bits-bit-1))&1)?'1':'0';return result.empty()?"-":result;};
        for(int row=0;row<cells->GetNumberRows();++row)cells->SetRowLabelValue(row,U(binary(size_t(row)^(size_t(row)>>1),rbits)));
        for(int col=0;col<cells->GetNumberCols();++col)cells->SetColLabelValue(col,U(binary(size_t(col)^(size_t(col)>>1),cbits)));
        auto colour=[&] {
            const char *palette[]={"#bbf7d0","#bfdbfe","#fde68a","#ddd6fe","#fecdd3","#a5f3fc"};
            for(int row=0;row<cells->GetNumberRows();++row)for(int col=0;col<cells->GetNumberCols();++col){
                size_t rg=size_t(row)^(size_t(row)>>1),cg=size_t(col)^(size_t(col)>>1);uint32_t code=0;
                for(size_t bit=0;bit<rbits;++bit)code|=uint32_t((rg>>(rbits-bit-1))&1)<<bit;
                for(size_t bit=0;bit<cbits;++bit)code|=uint32_t((cg>>(cbits-bit-1))&1)<<(rbits+bit);
                auto record=std::find_if(table.rows.begin(),table.rows.end(),[&](const auto &values){uint32_t value=0;for(size_t i=0;i<table.inputs.size();++i)value|=uint32_t(values[i])<<i;return value==code;});
                int value=(*record)[table.inputs.size()+output];std::string caption=value<0?"-":std::to_string(value),membership;
                wxColour background("#ffffff");int chosen=groups->GetSelection()-1;
                for(size_t group=0;group<minimized.terms.size();++group){const auto &term=minimized.terms[group];
                    if((code&~term.dontCare)==(term.bits&~term.dontCare)){
                        if(!membership.empty())membership+=",";membership+=std::to_string(group+1);
                        if(chosen<0||chosen==int(group))background=wxColour(palette[group%6]);
                    }
                }
                cells->SetCellValue(row,col,U(caption+(membership.empty()?"":"  ["+membership+"]")));
                cells->SetCellBackgroundColour(row,col,background);
            }cells->ForceRefresh();
        };
        colour();groups->Bind(wxEVT_CHOICE,[&](wxCommandEvent &){colour();});
        box->Add(cells,1,wxEXPAND|wxLEFT|wxRIGHT,10);
        auto explanation=new wxTextCtrl(&map,wxID_ANY,U(karnaughReport(table,output)),wxDefaultPosition,{750,170},wxTE_MULTILINE|wxTE_READONLY);
        explanation->SetFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));box->Add(explanation,0,wxEXPAND|wxALL,10);
        box->Add(map.CreateButtonSizer(wxOK),0,wxEXPAND|wxALL,10);map.SetSizer(box);map.ShowModal();
    });
    add("自动生成门电路…",[&] {
        if(!scope.empty())throw std::runtime_error("请先返回定义编辑再导入生成电路");
        wxTextEntryDialog name(&dialog,U("生成的新子电路名称"),U("生成门电路"),U("generated"));if(name.ShowModal()!=wxID_OK)return;
        wxArrayString modes;modes.Add(U("与 / 或 / 非门"));modes.Add(U("仅 NAND 门"));
        wxSingleChoiceDialog mode(&dialog,U("选择实现形式；生成后逐行验证真值表"),U("逻辑综合"),modes);if(mode.ShowModal()!=wxID_OK)return;
        auto generated=synthesize(table,utf8(name.GetValue()),mode.GetSelection()==1);
        auto package=exportComponent(generated,generated.main);std::string imported;
        change([&]{imported=importComponent(history.project,package);current=imported;canvas->selected.clear();});
        canvas->fit();hasTable=true;lastTable=table;
        wxMessageBox(U("已生成并验证子电路："+imported+"。原有工程电路已保留。"),U("逻辑综合"),wxOK|wxICON_INFORMATION,&dialog);
    });
    add("导入 CSV…",[&] {
        wxFileDialog file(&dialog,U("导入真值表 CSV"),"","","CSV (*.csv)|*.csv",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
        if(file.ShowModal()==wxID_OK){table=parseTruthCsv(readFile(fsPath(file.GetPath())));fill();}
    });
    add("导出 CSV…",[&] {
        wxFileDialog file(&dialog,U("导出真值表 CSV"),"","truth-table.csv","CSV (*.csv)|*.csv",wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
        if(file.ShowModal()==wxID_OK)writeAtomic(fsPath(file.GetPath()),"\xEF\xBB\xBF"+table.csv());
    });
    sizer->Add(actions,0,wxALL,10);sizer->Add(dialog.CreateButtonSizer(wxOK),0,wxEXPAND|wxALL,10);
    dialog.SetSizer(sizer);dialog.ShowModal();collect();
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
        diagnosticTooltip.clear();
        SetToolTip(wxEmptyString);
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
    Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent &event) {
        cancel();
        tool = "Select";
        auto id=hit(view.world({double(event.GetX()),double(event.GetY())}));
        if(!id.empty()) selected={id};
        owner->inspect(); Refresh();
        wxMenu menu;
        auto add=[&](int command,const char *caption) { menu.Append(command,U(caption)); };
        auto part=owner->circuit().part(id);
        int enterCommand=wxID_NONE;
        if(part && part->kind=="Subcircuit") {
            enterCommand=wxWindow::NewControlId(); menu.Append(enterCommand,U("进入该运行实例"));
        }
        if(!owner->scope.empty()) { add(L_UP,"返回上级实例"); add(L_EDIT_DEFINITION,"编辑定义（暂停并复位）"); }
        else { add(wxID_COPY,"复制"); add(wxID_CUT,"剪切"); add(wxID_PASTE,"粘贴");
               add(wxID_DELETE,"删除"); if(part) add(L_ROTATE,"顺时针旋转");
               add(L_REROUTE,"重排选中导线"); }
        if(part && (part->kind=="RAM"||part->kind=="ROM")) {
            add(L_MEMORY_RUNTIME,"查看运行内存"); if(owner->scope.empty()) add(L_MEMORY_INITIAL,"编辑初始内容");
        }
        if(part && (part->kind=="Keyboard"||part->kind=="TTY"||part->kind=="Joystick")) add(L_TEXT_DEVICE,"输入 / 查看内容");
        add(L_MONITOR,"监测信号 / CSV日志"); add(L_FIT,"适合画面");
        menu.Bind(wxEVT_MENU,[this,id,enterCommand](wxCommandEvent &command) {
            if(command.GetId()==enterCommand) { owner->enterInstance(id); return; }
            wxCommandEvent forwarded(wxEVT_MENU,command.GetId()); owner->GetEventHandler()->ProcessEvent(forwarded);
        });
        PopupMenu(&menu);
    });
}
void LogicCanvas::cancel() {
    wireSegment = -1;
    draggedWire.clear();
    wireDragPoints.clear();
    previewDirty = wireDisplayDirty = true;
    if(!pressedButton.empty()) {
        auto path=pressedButton; pressedButton.clear();auto split=path.find_last_of('/');
        auto id=split==std::string::npos?path:path.substr(split+1);auto part=owner->circuit().part(id);
        owner->simulator->setInput(path,part && parameter(*part,"activeLow",0)?1:0);owner->simulator->settle();
    }
    draggedInterface.clear();
    draft.reset();
    curveControl = false;
    shapeHandle = -1;
    mouseInside = false;
    clearPinHover();
    diagnosticTooltip.clear();
    SetToolTip(wxEmptyString);
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
    // 用屏幕坐标绘制：缩放既不改变提示外观，也不改变命中半径。
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
        auto local = at - i->at;
        for(int rotation=0;rotation<i->rotation;++rotation) local={local.y,-local.x};
        if (i->kind == "Input" || i->kind == "Output") {
            if (std::abs(local.x) <= 18 && std::abs(local.y) <= 18)
                return i->id;
            continue;
        }
        if (i->kind == "Subcircuit")
            for (const auto &s : p.circuit(i->circuit).shapes)
                if (s.appearance && shapeHit(s, local, 6 / view.zoom))
                    return i->id;
        auto size = partSize(p, *i);
        if (std::abs(local.x) <= size.x / 2 + 4 && std::abs(local.y) <= size.y / 2 + 4)
            return i->id;
    }
    for (auto &n : c.nodes)
        if (distance(at, n.at) < 9 / view.zoom)
            return n.id;
    const auto candidates=wireCandidates(at);
    if(!candidates.empty())return candidates.front();
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
    auto candidates=wireCandidates(at);
    std::stable_sort(candidates.begin(),candidates.end(),[&](const std::string &a,const std::string &b){return selected.count(a)>selected.count(b);});
    for (const auto &candidate : candidates) {
        const size_t wi=size_t(std::find_if(c.wires.begin(),c.wires.end(),[&](const Wire &w){return w.id==candidate;})-c.wires.begin());
        auto w = c.wires[wi];
        auto ps = wirePoints(p, c, w);
        for (size_t i = 1; i < ps.size(); ++i)
            if (segmentDistance(at, ps[i - 1], ps[i]) < FromDIP(7) / view.zoom) {
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
                wireDisplayDirty = previewDirty = true;
                return {id, ""};
            }
    }
    auto id = p.id("j");
    c.nodes.push_back({id, "", snap(at, 10)});
    return {id, ""};
}
void LogicCanvas::route(Point at, bool freeEnd) {
    if(!owner->scope.empty()) throw std::runtime_error("运行实例仅供观察，请切换到定义编辑后布线");
    auto &p = owner->history.project;
    if (!start) {
        before = p;
        start = endpointAt(at);
        bends.clear();
        previewDirty = true;
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
        Wire wire{p.id("w"), *start, finish, {}};
        const auto routed=routeWire(p,owner->circuit(),wire,bends);
        if(!routed.ok())throw std::runtime_error(routed.failure);
        wire.bends.assign(routed.points.begin()+1,routed.points.end()-1);
        owner->circuit().wires.push_back(wire);
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
        previewDirty = true;
        Refresh();
    }
}
void LogicCanvas::place(Point at) {
    auto preset = owner->placementPart();
    owner->change([&] {
        Part p = preset;
        p.id = owner->history.project.id("p");
        p.at = snap(at, 10);
        if ((p.kind == "Input" || p.kind == "Output") && p.label.empty())
            p.label = p.id;
        if (p.kind == "Text" && p.label.empty())
            p.label = "文字标签";
        owner->circuit().parts.push_back(p);
        selected = {p.id};
    });
    // 放置是单次操作；仅在提交成功后退出预览，保留新元件供直接选择/拖动。
    tool = "Select";
    subcircuit.clear();
    owner->rebuild(false);
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
    if (handleDiagnosticClick(e.GetPosition())) return;
    Point at = view.world({double(e.GetX()), double(e.GetY())});
    mouse = at;
    if(tool=="Select"&&!start&&e.AltDown()&&cycleWire(at,e.ShiftDown()?-1:1))return;
    auto pressed=owner->circuit().part(hit(at));
    const auto nearbyPin = pinAt(at);
    // 缩小时端子命中范围可能覆盖整个小元件，中心应仍能选择，边缘端子优先连线。
    const bool onPin = nearbyPin && (!pressed ||
        distance(at, endpoint(owner->history.project, owner->circuit(), *nearbyPin)) <= distance(at, pressed->at));
    if(tool=="Select" && !start && !onPin && pressed && pressed->kind=="Button" && parameter(*pressed,"momentary",0)) {
        pressedButton=owner->scoped(pressed->id);
        owner->simulator->setInput(pressedButton,parameter(*pressed,"activeLow",0)?0:1);
        owner->simulator->settle(); owner->rebuild(false);
        if(!HasCapture()) CaptureMouse(); return;
    }
    if(!owner->scope.empty()) {
        auto id=hit(at); selected.clear(); if(!id.empty()) selected.insert(id);
        owner->inspect(); Refresh(); return;
    }
    if(appearance && tool=="Select") {
        Part instance; instance.kind="Subcircuit"; instance.circuit=owner->current;
        for(const auto &pin:ports(owner->history.project,instance))
            if(distance(at,pin.at)<10/view.zoom) {
                before=owner->history.project; draggedInterface=pin.id; dragging=true;
                if(!HasCapture()) CaptureMouse(); return;
            }
    }
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
    if (tool == "Select" && !start && selected.size() == 1) {
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
    if(tool=="Select"&&!e.ShiftDown()&&!onPin&&owner->scope.empty()&&beginWireDrag(at))return;
    if (tool == "Wire" || (tool == "Select" && !appearance && (start || (onPin && !e.ShiftDown())))) {
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
    if(tool=="Select"&&!e.ShiftDown()&&std::any_of(owner->circuit().wires.begin(),owner->circuit().wires.end(),
        [&](const Wire &w){return w.id==id;})) {
        selected={id};owner->inspect();Refresh();return;
    }
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
    wireDisplayDirty = previewDirty = true;
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
    updateDiagnosticTooltip(screen);
    if (panning) {
        view.offset = view.offset + (screen - lastScreen);
        lastScreen = screen;
        Refresh();
        return;
    }
    if(dragging&&wireSegment>=0&&e.LeftIsDown()){updateWireDrag(mouse);return;}
    if (draft) {
        updateDraft(mouse, e.ShiftDown());
        Refresh();
        return;
    }
    if(dragging && !draggedInterface.empty() && e.LeftIsDown()) {
        owner->circuit().interfaceLayout[draggedInterface]=snap(mouse,10); Refresh(); return;
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
    if(!pressedButton.empty()) {
        auto path=pressedButton; pressedButton.clear();
        auto split=path.find_last_of('/');
        auto id=split==std::string::npos?path:path.substr(split+1);
        auto part=owner->circuit().part(id);
        owner->simulator->setInput(path,part && parameter(*part,"activeLow",0)?1:0);
        owner->simulator->settle(); if(HasCapture()) ReleaseMouse(); owner->rebuild(false); return;
    }
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
        const bool segmentEdit = wireSegment >= 0;
        if(!segmentEdit)owner->routingResult(rerouteChangedGeometry(owner->history.project,saved));
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
        wireSegment = -1;
        draggedWire.clear();
        wireDragPoints.clear();
        draggedInterface.clear();
        before.reset();
        dragging = false;
        if (HasCapture())
            ReleaseMouse();
        owner->rebuild(modified&&!segmentEdit, modified);
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
    if (tool == "Wire" || (tool == "Select" && start)) {
        route(at, true);
        return;
    }
    if (tool != "Select")
        return;
    auto id = hit(at);
    auto p = owner->circuit().part(id);
    if (!p)
        return;
    if(p->kind=="Button" && parameter(*p,"momentary",0)) return;
    if ((p->kind == "Input" && owner->scope.empty()) || p->kind == "Button") {
        cancel();
        p = owner->circuit().part(id);
        if(!owner->scope.empty()) {
            auto live=owner->read({id,"Y"});
            owner->simulator->setInput(owner->scoped(id),live.defined() && live.value ? 0 : 1);
            owner->simulator->settle(); owner->rebuild(false); return;
        }
        auto saved = owner->history.project;
        p->value = p->value ? 0 : 1;
        owner->history.commit(saved);
        owner->simulator->setInput(id, p->value);
        owner->simulator->settle();
        owner->rebuild(false);
    } else if (p->kind == "Subcircuit") {
        owner->enterInstance(id);
    } else if(p->kind=="RAM" || p->kind=="ROM") {
        selected={id}; owner->memoryEditor(!owner->scope.empty());
    } else if(p->kind=="Keyboard" || p->kind=="TTY" || p->kind=="Joystick") {
        selected={id}; owner->textDevice();
    } else {
        selected = {id};
        owner->inspect();
        if(owner->scope.empty()) {
            owner->properties->SetFocus();
            owner->properties->SetGridCursor(P_LABEL, 1);
            owner->properties->EnableCellEditControl();
        }
    }
}
void LogicCanvas::key(wxKeyEvent &e) {
    try {
        int code = e.GetKeyCode();
        if(code==WXK_TAB&&!e.ControlDown()&&!e.AltDown()&&tool=="Select"&&cycleWire(mouse,e.ShiftDown()?-1:1))return;
        if(e.AltDown() && code==WXK_UP) {
            if(!owner->navigation.empty()) owner->navigateTo(owner->navigation.size()-1);
            return;
        }
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
    if (mouseInside) updateDiagnosticTooltip(hoverScreen);
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour("#f9fbfd")));
    dc.Clear();
    std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
    if (g)
        render(*g, GetClientSize());
}
// 画布渲染主入口，按图层顺序绘制。clean=true 用于 PNG 导出（去掉网格、选中框、涟漪）。
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
        // 显示与实例暴露相同的稳定端子，而不仿真外观图形。
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
            auto body = partSize(project, instance);
            g.DrawRectangle(-body.x / 2, -body.y / 2, body.x, body.y);
        }
    }
    if (!appearance) {
        std::set<std::string> bad, warned;
        for (auto &i : owner->issues)
            if (i.severity == "error" || i.severity == "warning")
                {
                    auto object=i.object;
                    if(!owner->scope.empty()) {
                        auto prefix=owner->scope+"/";
                        if(object.rfind(prefix,0)!=0) continue;
                        object=object.substr(prefix.size());
                    }
                    (i.severity == "error" ? bad : warned).insert(object.substr(0,object.find('/')));
                }
        for (auto &w : c.wires) {
            auto signal = owner->read({w.id, ""});
            auto colour = !clean && bad.count(w.id) ? wxColour("#dc2626") :
                          !clean && warned.count(w.id) ? wxColour("#bd790c") :
                          !clean && selected.count(w.id) ? wxColour("#6366f1") : signalColour(signal);
            g.SetPen(wxPen(colour,
                           signal.width > 1 ? 4 : 2));
            drawWire(g, w, clean);
        }
        for (auto &n : c.nodes) {
            auto s = owner->read({n.id, ""});
            auto colour = !clean && bad.count(n.id) ? wxColour("#dc2626") :
                          !clean && warned.count(n.id) ? wxColour("#bd790c") :
                          !clean && selected.count(n.id) ? wxColour("#6366f1") : signalColour(s);
            g.SetPen(wxPen(colour));
            g.SetBrush(wxBrush(colour));
            g.DrawEllipse(n.at.x - 4, n.at.y - 4, 8, 8);
            if (!n.label.empty())
                text(g, n.label, {n.at.x + 6, n.at.y - 23});
        }
        for (auto &p : c.parts)
            drawSymbol(
                g, project, p, [&](const Port &pin) { return owner->read({p.id, pin.id}); },
                !clean && selected.count(p.id), bad.count(p.id) != 0, false, true,
                !clean && warned.count(p.id) != 0);
        if(!clean)drawWireHandles(g);
        if (!clean && start) {
            const auto &preview=wirePreview();
            g.SetPen(wxPen(wxColour(preview.ok()?"#6366f1":"#dc2626"), 2, wxPENSTYLE_SHORT_DASH));
            std::vector<Point> ps{endpoint(project, c, *start)};
            ps.insert(ps.end(), bends.begin(), bends.end());
            ps.push_back(snap(mouse, 10));
            line(g, preview.ok()?preview.points:orthogonal(ps));
        }
    } // 原理图层
    if (!clean && draft)
        drawShape(g, *draft, true, true, view.zoom);
    if (!clean && box) {
        g.SetPen(wxPen(wxColour("#6366f1"), 1, wxPENSTYLE_SHORT_DASH));
        g.SetBrush(*wxTRANSPARENT_BRUSH);
        g.DrawRectangle(std::min(mouse.x, boxStart.x), std::min(mouse.y, boxStart.y),
                        std::abs(mouse.x - boxStart.x), std::abs(mouse.y - boxStart.y));
    }
    if (!appearance && !clean && tool == "Place" && !start) {
        Part p = owner->placementPart();
        p.at = snap(mouse, 10);
        drawSymbol(
            g, project, p, [](const Port &pin) { return Signal::unknown(pin.width); }, false, false, true);
    }
    g.PopState();
    if (!clean)
        drawPinHover(g);
    if (!clean)
        drawDiagnostics(g);
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
        for(const auto &pin:ports(p,instance)) { add(pin.at-Point{80,30});add(pin.at+Point{80,30}); }
        return {lo, hi};
    }
    for (auto &q : c.parts) {
        auto addLocal=[&](Point point) { for(int turn=0;turn<q.rotation;++turn)point={-point.y,point.x};add(q.at+point); };
        if (q.kind == "Subcircuit")
            for (const auto &s : p.circuit(q.circuit).shapes)
                if (s.appearance) {
                    auto b = shapeBounds(s);
                    double margin = s.stroke / 2 + 6;
                    addLocal(b.first - Point{std::max(105.0, margin), margin + 35});
                    addLocal(b.second + Point{std::max(105.0, margin), margin + 40});
                }
        double h = partHeight(p, q);
        double half = std::max(105.0, double(q.label.size()) * 7);
        addLocal({-half,-h/2-35});addLocal({half,h/2+40});
        for(const auto &pin:ports(p,q)) { add(q.at+pin.at-Point{20,20});add(q.at+pin.at+Point{20,20}); }
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
void LogicCanvas::exportPng(const wxString &path, double requestedScale) {
    auto b = bounds(owner->history.project, owner->circuit(), appearance);
    auto extent = b.second - b.first;
    if(!std::isfinite(requestedScale) || requestedScale<=0)
        throw std::runtime_error("导出比例必须为正数");
    // 同时限制最长边和总像素；大型导入电路应自动降采样，不能在默认比例下无法导出。
    double scale = std::min({requestedScale, 8000 / std::max(extent.x + 60, extent.y + 60),
                             std::sqrt(32000000.0/((extent.x+60)*(extent.y+60)))});
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
void LogicEditor::runSmoke(const std::filesystem::path &folder, bool experimentsOnly) {
    std::filesystem::create_directories(folder);
    std::error_code ignored;
    std::filesystem::remove(folder / "LOGIC-PASS.txt", ignored);
    std::filesystem::remove(folder / "FAILED.txt", ignored);
    std::filesystem::remove(folder / "EXPERIMENTS-PASS.txt", ignored);
    if (experimentsOnly) {
        runExperimentSmoke(folder);
        return;
    }
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
    const std::vector<std::string> toolbarKinds = {
        "Select", "Wire", "Input", "Output", "Not", "Buffer", "And", "Or", "Xor", "Nand", "Nor", "Xnor"};
    if (!shortcuts || shortcuts->GetToolsCount() != toolbarKinds.size() + 9 ||
        wxWindow::FindWindowByName("logic-actions", this))
        throw std::runtime_error("digital component toolbar missing");
    const std::vector<int> fileTools = {wxID_NEW, wxID_OPEN, wxID_SAVE, wxID_SEPARATOR,
                                       L_IMPORT_MENU, L_EXPORT_MENU, wxID_SEPARATOR};
    for (size_t i = 0; i < fileTools.size(); ++i) {
        auto tool = shortcuts->GetToolByPos(int(i));
        if (tool->GetId() != fileTools[i])
            throw std::runtime_error("file tools are not grouped before editing tools");
    }
    for (int group : {L_IMPORT_MENU, L_EXPORT_MENU}) {
        auto item = GetMenuBar()->FindItem(group);
        auto tool = shortcuts->FindById(group);
        if (!item || !item->GetSubMenu() || !tool || tool->GetKind() != wxITEM_DROPDOWN ||
            !tool->GetDropdownMenu())
            throw std::runtime_error("file import/export menu or dropdown missing");
        const std::vector<int> expected = group == L_IMPORT_MENU
            ? std::vector<int>{L_IMPORT_CIRC, L_LOAD_COMPONENT, L_IMPORT_TABLE}
            : std::vector<int>{L_PNG, L_BATCH_PNG, L_CSV, L_NETLIST_JSON, L_PCB, L_SAVE_COMPONENT};
        for (auto m : {item->GetSubMenu(), tool->GetDropdownMenu()}) {
            std::vector<int> actual;
            for (auto entry : m->GetMenuItems())
                if (!entry->IsSeparator()) actual.push_back(entry->GetId());
            if (actual != expected)
                throw std::runtime_error("file menu and toolbar exchange commands differ");
        }
        for (int id : expected) {
            wxMenu *parent = nullptr;
            if (!GetMenuBar()->FindItem(id, &parent) || parent != item->GetSubMenu())
                throw std::runtime_error("exchange command remains outside its file group");
        }
    }
    std::function<void(wxMenu *)> checkRemovedMenus = [&](wxMenu *m) {
        for (auto item : m->GetMenuItems()) {
            if (item->GetItemLabelText().Contains(U("打印")) ||
                item->GetItemLabelText().Contains(U("导出 Logisim")))
                throw std::runtime_error("removed print or Logisim export command remains");
            if (item->GetSubMenu()) checkRemovedMenus(item->GetSubMenu());
        }
    };
    for (size_t i = 0; i < GetMenuBar()->GetMenuCount(); ++i)
        checkRemovedMenus(GetMenuBar()->GetMenu(i));
    size_t shortcutIndex = 0;
    for (size_t i = fileTools.size(); i < shortcuts->GetToolsCount(); ++i) {
        auto tool = shortcuts->GetToolByPos(int(i));
        if (tool->IsSeparator())
            continue;
        if (shortcutIndex >= toolbarKinds.size() || U(toolbarKinds[shortcutIndex++]) != tool->GetLongHelp())
            throw std::runtime_error("compact toolbar contains unexpected tools");
        if (!tool->GetNormalBitmapBundle().IsOk())
            throw std::runtime_error("component toolbar bitmap missing");
        wxCommandEvent event(wxEVT_TOOL, tool->GetId());
        shortcuts->GetEventHandler()->ProcessEvent(event);
        const bool action = tool->GetId() == L_SELECT || tool->GetId() == L_WIRE;
        if (action ? U(canvas->tool) != tool->GetLongHelp()
                   : canvas->tool != "Place" || U(canvas->placing) != tool->GetLongHelp())
            throw std::runtime_error("component shortcut chose wrong kind");
    }
    if (shortcutIndex != toolbarKinds.size())
        throw std::runtime_error("compact toolbar is incomplete");
    std::vector<std::string> iconKinds;
    for (auto &part : library())
        iconKinds.push_back(part.kind);
    for (auto name : {"New", "Open", "Save", "Import", "Export", "Copy", "Cut", "Paste", "Undo", "Redo", "Help", "Folder",
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
    // 复用画布、预览与 PNG 导出使用的同一套矢量渲染器。
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
    // 无说明文字时，主体包围盒之外只允许边框连接圆点，不能残留外伸引线。
    for (auto p : specimens) {
        if (p.kind == "Text")
            continue;
        const auto body = partSize(symbols, p);
        for (int rotation = 0; rotation < 4; ++rotation) {
            p.rotation = rotation;
            const double halfWidth = (rotation % 2 ? body.y : body.x) / 2;
            const double halfHeight = (rotation % 2 ? body.x : body.y) / 2;
            const int w = int(std::ceil(2 * halfWidth)) + 60;
            const int h = int(std::ceil(2 * halfHeight)) + 60;
            p.at = {w / 2.0, h / 2.0};
            wxBitmap bitmap(w, h, 32);
            {
                wxMemoryDC dc(bitmap);
                dc.SetBackground(*wxWHITE_BRUSH);
                dc.Clear();
                std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
                if (!g)
                    throw std::runtime_error("compact symbol graphics context failed");
                drawSymbol(*g, symbols, p, [](const Port &pin) { return Signal::number(0, pin.width); },
                           false, false, false, false);
            }
            auto image = bitmap.ConvertToImage();
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if ((std::abs(x - p.at.x) > halfWidth + 5 || std::abs(y - p.at.y) > halfHeight + 5) &&
                        (image.GetRed(x, y) != 255 || image.GetGreen(x, y) != 255 || image.GetBlue(x, y) != 255))
                        throw std::runtime_error("symbol draws outside border terminal envelope: " + p.kind);
        }
    }
    // 输出五种信号状态在紧凑 I/O 主体上的栅格证据。
    wxBitmap ioSheet(760, 640, 32);
    std::vector<Signal> ioStates{Signal::number(0, 1), Signal::number(1, 1), Signal::unknown(1),
                                 Signal::floating(1), Signal::failure(1)};
    {
        wxMemoryDC dc(ioSheet);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        std::unique_ptr<wxGraphicsContext> g(wxGraphicsContext::Create(dc));
        for (size_t row = 0; row < ioStates.size(); ++row) {
            const auto state = ioStates[row];
            text(*g, state.text(), {30, 45.0 + row * 120}, 14, signalColour(state));
            for (int col = 0; col < 3; ++col) {
                Part p;
                p.kind = col == 0 ? "Input" : col == 1 ? "Output" : "And";
                p.at = {180.0 + col * 220, 60.0 + row * 120};
                drawSymbol(*g, symbols, p, [&](const Port &) { return state; }, row == 1);
            }
        }
    }
    auto ioImage = ioSheet.ConvertToImage();
    std::set<unsigned> bodyColours;
    for (int row = 0; row < 5; ++row) {
        int x = 172, y = 68 + row * 120;
        unsigned rgb = (unsigned(ioImage.GetRed(x, y)) << 16) | (unsigned(ioImage.GetGreen(x, y)) << 8) |
                       ioImage.GetBlue(x, y);
        bodyColours.insert(rgb);
        if (ioImage.GetRed(172, y) != ioImage.GetRed(392, y) ||
            ioImage.GetGreen(172, y) != ioImage.GetGreen(392, y) ||
            ioImage.GetBlue(172, y) != ioImage.GetBlue(392, y))
            throw std::runtime_error("I/O state fills disagree");
        if (ioImage.GetRed(180, 80 + row * 120) != 255 || ioImage.GetBlue(180, 80 + row * 120) != 255)
            throw std::runtime_error("I/O body did not shrink");
    }
    if (bodyColours.size() != 5)
        throw std::runtime_error("I/O states need distinct body colours");
    ioImage.SaveFile(wxString((folder / "io-signal-states.png").wstring()), wxBITMAP_TYPE_PNG);
    // 所有元件、四种方向均从可见边框圆点开始连线，不保留旧的长引线命中区域。
    current = "main";
    canvas->cancel();
    canvas->appearance = false;
    canvas->view.zoom = 1;
    for (auto p : specimens) {
        Project single = symbols;
        p.at = {200, 200};
        single.circuits.push_back(Circuit{});
        single.circuits.back().name = "symbol-hit-check";
        for (int rotation = 0; rotation < 4; ++rotation) {
            p.rotation = rotation;
            single.circuits.back().parts = {p};
            history.reset(single);
            current = "symbol-hit-check";
            for (const auto &pin : ports(history.project, p)) {
                const auto target = canvas->pinAt(p.at + pin.at);
                if (!target || !(*target == Endpoint{p.id, pin.id}))
                    throw std::runtime_error("border terminal hit failed: " + p.kind);
            }
            if (p.kind == "Input" || p.kind == "Output") {
                Point tail{p.kind == "Input" ? 45.0 : -45.0, 0};
                for (int turn = 0; turn < rotation; ++turn)
                    tail = {-tail.y, tail.x};
                if (!canvas->hit(p.at + tail).empty() || canvas->pinAt(p.at + tail))
                    throw std::runtime_error("I/O retains an invisible lead hit target");
            }
        }
    }
    writeAtomic(folder / "SYMBOLS-PASS.txt",
                "All library bodies and border terminals: no external leads in 4 rotated raster renders; "
                "all border pin hit targets passed; no invisible I/O tail hit regions.\n");
    // 复现用户“输入 → 非门 →输出”电路，保持端口 ID 与电气连接。
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
    // 悬停必须使用与布线相同的最近端点判定，且不产生时钟 tick 或编辑。
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
        // 向空白的竖直方向移出；缩小时水平移动可能靠近另一侧端子。
        mouseMove({300, 160.0 + canvas->FromDIP(12)});
        if (canvas->hoveredPin || canvas->rippleTimer.IsRunning())
            throw std::runtime_error("pin hover remained outside connection radius");
    }
    // 低缩放时，近处的节点必须胜过列表中更靠前但更远的元件引脚。
    circuit().nodes.push_back({"hoverNode", "", hoverAnchor + Point{0, 2}});
    canvas->view.zoom = 0.25;
    auto nearNode = canvas->pinAt(hoverAnchor + Point{0, 2});
    if (!nearNode || !(*nearNode == Endpoint{"hoverNode", ""}))
        throw std::runtime_error("hover did not choose nearest overlapping endpoint");
    circuit().nodes.pop_back();
    canvas->view.zoom = 1;
    canvas->view.offset = {40, 20};
    const auto hoverScreen = canvas->view.screen(hoverAnchor) + Point{5, 0};
    mouseMove(hoverScreen);
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
    mouseMove(hoverScreen);
    if (canvas->hoveredPin || canvas->rippleTimer.IsRunning())
        throw std::runtime_error("hover remained active during component placement");
    canvas->tool = "Wire";
    mouseMove(hoverScreen);
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
    // 用实际鼠标事件验证“单次放置 → 选择/拖动 → 引脚连线”的完整工作流。
    history.reset(Project{});
    current = "main";
    canvas->selected.clear();
    canvas->view.zoom = 1;
    canvas->view.offset = {20, 20};
    rebuild(true, true);
    auto clickCanvas = [&](Point at) {
        const auto screen = canvas->view.screen(at);
        wxMouseEvent motion(wxEVT_MOTION);
        motion.SetPosition({int(std::lround(screen.x)), int(std::lround(screen.y))});
        canvas->motion(motion);
        wxMouseEvent down(wxEVT_LEFT_DOWN);
        down.SetPosition(motion.GetPosition());
        canvas->leftDown(down);
        wxMouseEvent up(wxEVT_LEFT_UP);
        up.SetPosition(motion.GetPosition());
        canvas->leftUp(up);
    };
    auto chooseComponent = [&](const std::string &kind) {
        for (size_t i = 0; i < shortcuts->GetToolsCount(); ++i) {
            auto tool = shortcuts->GetToolByPos(int(i));
            if (tool->IsSeparator() || tool->GetLongHelp() != U(kind))
                continue;
            wxCommandEvent event(wxEVT_TOOL, tool->GetId());
            shortcuts->GetEventHandler()->ProcessEvent(event);
            return;
        }
        throw std::runtime_error("placement shortcut missing: " + kind);
    };
    chooseComponent("Input");
    clickCanvas({120, 120});
    const auto placedInput = circuit().parts.back().id;
    if (circuit().parts.size() != 1 || canvas->tool != "Select" || !canvas->selected.count(placedInput) ||
        !status->GetLabel().Contains(U("工具：Select")))
        throw std::runtime_error("single placement did not return to selection");
    clickCanvas({280, 280});
    if (circuit().parts.size() != 1 || !canvas->selected.empty())
        throw std::runtime_error("blank click repeated the placed component");
    chooseComponent("Output");
    clickCanvas({420, 120});
    const auto placedOutput = circuit().parts.back().id;
    if (circuit().parts.size() != 2 || canvas->tool != "Select")
        throw std::runtime_error("second placement did not return to selection");
    canvas->view.zoom = .25;
    canvas->view.offset = {100, 100};
    clickCanvas(circuit().part(placedInput)->at);
    if (canvas->start || !canvas->selected.count(placedInput))
        throw std::runtime_error("zoomed-out component center starts a wire instead of selecting");
    canvas->view.zoom = 1;
    canvas->view.offset = {20, 20};
    const Endpoint inputPin{placedInput, "Y"}, outputPin{placedOutput, "A"};
    const auto inputAnchor = endpoint(history.project, circuit(), inputPin);
    clickCanvas(inputAnchor + Point{-3, 0});
    if (!canvas->start || !(*canvas->start == inputPin) || canvas->dragging || canvas->tool != "Select")
        throw std::runtime_error("selection-mode pin click did not start a wire");
    clickCanvas({280, 200});
    if (canvas->bends.size() != 1 || !circuit().wires.empty() || circuit().parts.size() != 2)
        throw std::runtime_error("selection-mode wire bend changed placement or selection");
    clickCanvas(endpoint(history.project, circuit(), outputPin));
    if (circuit().wires.size() != 1 || canvas->start || canvas->tool != "Select" ||
        !(circuit().wires.front().a == inputPin) || !(circuit().wires.front().b == outputPin) ||
        simulator->read(outputPin) != Signal::number(0, 1))
        throw std::runtime_error("selection-mode wire did not finish on the correct terminals");
    const auto connectedProject = serialize(history.project);
    clickCanvas(inputAnchor);
    clickCanvas({280, 240});
    wxKeyEvent escape(wxEVT_KEY_DOWN);
    escape.m_keyCode = WXK_ESCAPE;
    canvas->key(escape);
    if (canvas->start || canvas->tool != "Select" || serialize(history.project) != connectedProject)
        throw std::runtime_error("selection-mode wire cancellation changed the project");
    undo();
    if (!circuit().wires.empty() || circuit().parts.size() != 2)
        throw std::runtime_error("selection-mode wire undo did not preserve placed components");
    undo(true);
    if (serialize(history.project) != connectedProject)
        throw std::runtime_error("selection-mode wire redo failed");
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
    if (circuit().parts.back().kind != "Clock" || canvas->tool != "Select")
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
    // 验证真实的自定义元件对话框，以及用户会走的历史/库/画布路径。
    for (int id : {L_NEW_COMPONENT, L_SAVE_COMPONENT, L_LOAD_COMPONENT, L_NETLIST_JSON, L_PCB})
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
    if (canvas->tool != "Select" || !canvas->subcircuit.empty())
        throw std::runtime_error("custom component placement did not return to selection");
    const auto customInstanceId = circuit().parts.back().id;
    change([&] {
        // 复制导入的接口，组成一个完整、可测试的顶层半加器。
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
    writeAtomic(folder / "custom-half-adder.json", simulator->netlist());
    const auto pcb = exportPcb(history.project, current);
    writeAtomic(folder / "custom-half-adder.net", pcb.netlist);
    writeAtomic(folder / "custom-half-adder.kicad_pcb", pcb.board);
    writeAtomic(folder / "custom-half-adder.mapping.json", pcb.manifest);
    if (simulator->time() != timeBeforeExport || simulator->pending() != queueBeforeExport)
        throw std::runtime_error("netlist export modified GUI simulation");
    canvas->fit();
    canvas->selected.clear();
    canvas->exportPng(wxString((folder / "custom-half-adder.png").wstring()));
    history.markSaved();
    // 通过文件菜单与命令行相同的加载/保存路径进行导入。
    auto circExample =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "examples" / "circ-limited.circ";
    auto circSource = folder / std::filesystem::u8path("受限导入.CIRC");
    auto circBytes = readFile(circExample);
    // 加法器现已支持；在烟测副本中插入真正未知器件和斜线，继续覆盖持久诊断。
    auto unsupported=circBytes.find("name=\"Adder\"");
    if(unsupported!=std::string::npos)circBytes.replace(unsupported,std::string("name=\"Adder\"").size(),"name=\"UnsupportedSmokeDevice\"");
    auto closingCircuit=circBytes.find("</circuit>");
    circBytes.insert(closingCircuit,"<wire from=\"(900,900)\" to=\"(910,910)\"/>\n");
    writeAtomic(circSource, circBytes);
    if (!load(wxString(circSource.wstring())) || !savedPath.empty() || !history.dirty())
        throw std::runtime_error("外部电路 import must be unsaved and have no native saved path");
    auto importedNotes = circDiagnostics(circuit());
    if (importedNotes.size() < 2 || issues.size() < importedNotes.size())
        throw std::runtime_error("外部电路 diagnostics missing");
    if(std::none_of(importedNotes.begin(),importedNotes.end(),[](const Issue &note){return note.message.find("UnsupportedSmokeDevice")!=std::string::npos;}) ||
       std::none_of(importedNotes.begin(),importedNotes.end(),[](const Issue &note){return note.message.find("斜向")!=std::string::npos;}))
        throw std::runtime_error("unsupported device or diagonal wire diagnostic was not retained");
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
    if (!load(U(savedPath)) || circDiagnostics(circuit()).size() != importedNotes.size())
        throw std::runtime_error("外部电路 diagnostics did not survive native reload");
    canvas->selected.clear();
    canvas->fit();
    canvas->exportPng(wxString((folder / "circ-import.png").wstring()));
    history.markSaved();
    // 同时验证真实的画布事件处理器与持久化的外观模型。
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
    // 答辩重点回归：同一定义的两个嵌套实例拥有独立寄存器/RAM，导航不复位。
    Project debugProject; debugProject.name="层次实例实时调试验收";debugProject.nextId=1000;
    Circuit leaf; leaf.name="存储单元";
    auto addPart=[](Circuit &target,const std::string &id,const std::string &kind,int width,Point at,uint32_t value=0) {
        Part part;part.id=id;part.kind=kind;part.width=width;part.at=at;part.value=value;part.label=id;
        target.parts.push_back(part);
    };
    auto connect=[](Circuit &target,const std::string &id,Endpoint a,Endpoint b) {target.wires.push_back({id,a,b,{}});};
    addPart(leaf,"data","Input",8,{-330,-150});addPart(leaf,"clk","Input",1,{-330,-20});
    addPart(leaf,"reg","Register",8,{0,-150});addPart(leaf,"q","Output",8,{330,-150});
    addPart(leaf,"button","Button",1,{-200,250});addPart(leaf,"buttonOut","Output",1,{330,250});
    addPart(leaf,"mem","RAM",8,{0,60});addPart(leaf,"addr","Constant",8,{-330,60});
    addPart(leaf,"we","Constant",1,{-330,160},1);addPart(leaf,"ramq","Output",8,{330,60});
    connect(leaf,"w1",{"data","Y"},{"reg","D"});connect(leaf,"w2",{"clk","Y"},{"reg","CLK"});
    connect(leaf,"w3",{"reg","Q"},{"q","A"});connect(leaf,"w4",{"button","Y"},{"buttonOut","A"});
    connect(leaf,"w5",{"data","Y"},{"mem","D"});connect(leaf,"w6",{"clk","Y"},{"mem","CLK"});
    connect(leaf,"w7",{"addr","Y"},{"mem","ADDR"});connect(leaf,"w8",{"we","Y"},{"mem","WE"});
    connect(leaf,"w9",{"mem","Q"},{"ramq","A"});
    Circuit wrapper;wrapper.name="嵌套模块";
    addPart(wrapper,"d","Input",8,{-240,-80});addPart(wrapper,"c","Input",1,{-240,80});
    addPart(wrapper,"o","Output",8,{240,0});Part inner;inner.id="inner";inner.kind="Subcircuit";inner.circuit=leaf.name;
    wrapper.parts.push_back(inner);connect(wrapper,"w10",{"d","Y"},{"inner","data"});
    connect(wrapper,"w11",{"c","Y"},{"inner","clk"});connect(wrapper,"w12",{"inner","q"},{"o","A"});
    auto &top=debugProject.circuits.front();
    addPart(top,"leftData","Constant",8,{-320,-150},0x12);addPart(top,"rightData","Constant",8,{-320,150},0x34);
    addPart(top,"clock","Clock",1,{-320,0});
    for(auto entry:std::vector<std::pair<std::string,int>>{{"left",-150},{"right",150}}) {
        Part debugInstance;debugInstance.id=entry.first;debugInstance.kind="Subcircuit";debugInstance.circuit=wrapper.name;debugInstance.at={0,double(entry.second)};
        top.parts.push_back(debugInstance);connect(top,entry.first+"D",{entry.first+"Data","Y"},{entry.first,"d"});
        connect(top,entry.first+"C",{"clock","Y"},{entry.first,"c"});
    }
    debugProject.circuits.push_back(wrapper);debugProject.circuits.push_back(leaf);debugProject.validate();
    writeAtomic(std::filesystem::path(__FILE__).parent_path().parent_path()/"examples"/"defense-hierarchy.logic.json",serialize(debugProject));
    history.reset(debugProject);current="main";canvas->selected.clear();rebuild(true,true);simulator->tick();rebuild(false);
    auto originalSimulator=simulator.get();auto observedTime=simulator->time();auto observedTicks=simulator->ticks();
    auto definitionBytes=serialize(history.project);timer.Start(60000);
    enterInstance("left");enterInstance("inner");
    if(simulator.get()!=originalSimulator||!timer.IsRunning()||simulator->time()!=observedTime||simulator->ticks()!=observedTicks||
       read({"reg","Q"})!=Signal::number(0x12,8)||simulator->memory("left/inner/mem")[0]!=Signal::number(0x12,8))
        throw std::runtime_error("live hierarchy navigation reset or lost the left instance state");
    canvas->selected={"reg"};inspect();
    stop();canvas->selected={"data"};inspect();
    if(!properties->IsReadOnly(P_VALUE,1))throw std::runtime_error("nested interface Input is editable");
    bool rejected=false;try{change([]{});}catch(const std::exception &){rejected=true;}
    if(!rejected||serialize(history.project)!=definitionBytes)throw std::runtime_error("runtime structure edit was not rejected");
    canvas->tool="Select";auto buttonAt=canvas->view.screen(circuit().part("button")->at);
    wxMouseEvent buttonClick(wxEVT_LEFT_DCLICK);buttonClick.SetPosition({int(buttonAt.x),int(buttonAt.y)});canvas->doubleClick(buttonClick);
    if(read({"button","Y"}).value!=1||simulator->read({"right/inner/button","Y"}).value!=0||serialize(history.project)!=definitionBytes)
        throw std::runtime_error("instance button leaked into another instance or definition");
    canvas->exportPng(wxString((folder/"hierarchy-left-instance.png").wstring()));
    navigateTo(0);enterInstance("right");enterInstance("inner");
    if(read({"reg","Q"})!=Signal::number(0x34,8)||simulator->memory("right/inner/mem")[0]!=Signal::number(0x34,8)||
       simulator->ticks()!=observedTicks)throw std::runtime_error("right nested instance state was not isolated");
    canvas->exportPng(wxString((folder/"hierarchy-right-instance.png").wstring()));
    canvas->selected={"mem"};inspect();
    navigateTo(0);canvas->fit();
    // 真实窗口布局、子电路命令以及工具属性的回归。
    Layout();
    if (properties->ClientToScreen({0,0}).y <= tree->ClientToScreen({0,0}).y ||
        properties->ClientToScreen({0,0}).x >= canvas->ClientToScreen({0,0}).x ||
        hierarchy->GetSize().y > FromDIP(96) || diagnosticsWindow->IsShown())
        throw std::runtime_error("library/property layout or compact hierarchy failed");
    history.reset(Project{}); current="main"; canvas->selected.clear(); canvas->tool="Select";
    rebuild(true,true);
    if (deleteCircuitButton->IsEnabled() || canDeleteCircuit() ||
        !GetMenuBar()->FindItem(L_NEW_CIRCUIT)->GetItemLabel().Contains("Ctrl+Shift+N") ||
        !GetMenuBar()->FindItem(L_DEL_CIRCUIT)->GetItemLabel().Contains("Ctrl+Shift+Delete"))
        throw std::runtime_error("subcircuit shortcut/protection missing");
    wxTheApp->CallAfter([] {
        auto dialog=dynamic_cast<wxTextEntryDialog*>(wxWindow::FindWindowByName("new-circuit-dialog"));
        if(!dialog)throw std::runtime_error("new subcircuit shortcut did not open its dialog");
        dialog->SetValue(U("layout_child"));dialog->EndModal(wxID_OK);
    });
    wxCommandEvent addCircuit(wxEVT_BUTTON,L_NEW_CIRCUIT);
    newCircuitButton->GetEventHandler()->ProcessEvent(addCircuit);
    if(current!="layout_child" || !deleteCircuitButton->IsEnabled() || history.project.circuits.size()!=2)
        throw std::runtime_error("new subcircuit button failed");
    wxCommandEvent removeCircuit(wxEVT_BUTTON,L_DEL_CIRCUIT);
    deleteCircuitButton->GetEventHandler()->ProcessEvent(removeCircuit);
    if(current!="main" || history.project.circuits.size()!=1)
        throw std::runtime_error("delete subcircuit button failed");
    undo();
    if(history.project.circuits.size()!=2)throw std::runtime_error("subcircuit deletion undo failed");
    undo(true);
    properties->SetCellValue(P_CIRCUIT,1,"renamed_main");apply();
    if(current!="renamed_main" || history.project.main!=current)
        throw std::runtime_error("circuit property rename failed");
    undo();
    const auto templateBefore=serialize(history.project);
    const auto templateTime=simulator->time();
    canvas->tool="Place";canvas->placing="And";canvas->subcircuit.clear();canvas->selected.clear();inspect();
    if(properties->GetCellValue(P_ROTATION,1)!=U("右") || properties->GetRowSize(P_INVERT)!=0 ||
       properties->GetRowSize(P_ID)!=0 || properties->GetRowSize(P_INPUT_INVERT_FIRST)==0)
        throw std::runtime_error("Logisim-style property rows missing");
    properties->SetCellValue(P_ROTATION,1,U("下"));properties->SetCellValue(P_WIDTH,1,"8");
    properties->SetCellValue(P_INPUTS,1,"3");properties->SetCellValue(P_INPUT_INVERT_FIRST,1,"1");apply();
    if(serialize(history.project)!=templateBefore || simulator->time()!=templateTime)
        throw std::runtime_error("tool defaults modified the circuit or simulation");
    canvas->place({200,200});
    const auto &configured=circuit().parts.back();
    if(configured.width!=8 || configured.rotation!=1 || parameter(configured,"inputs",2)!=3 ||
       parameter(configured,"invertMask",0)!=1 || canvas->tool!="Select")
        throw std::runtime_error("placement did not use inspected tool properties");
    properties->SetCellValue(P_INPUT_INVERT_FIRST+2,1,"1");apply();
    if(parameter(circuit().parts.back(),"invertMask",0)!=5)
        throw std::runtime_error("individual input inversion edit failed");

    // ERC 表现层：颜色、外围警示图标、Tooltip、列表聚焦和干净导出。
    Project inlineProject;inlineProject.name="画布内联 ERC 验证";
    auto inlinePart=[&](const std::string &kind,Point at,int width) {
        Part p;p.id=inlineProject.id("p");p.kind=kind;p.at=at;p.width=width;
        inlineProject.circuit("main").parts.push_back(p);return p.id;
    };
    auto driver=inlinePart("Constant",{120,160},1),mismatchLoad=inlinePart("Output",{420,160},8);
    const auto warnedGate=inlinePart("And",{435,175},1);
    const auto floatingOutput=inlinePart("Output",{760,420},1);
    inlineProject.circuit("main").wires.push_back({inlineProject.id("w"),{driver,"Y"},{mismatchLoad,"A"},{}});
    inlineProject.validate();
    writeAtomic(folder/"inline-erc-preview.logic.json",serialize(inlineProject));
    history.reset(inlineProject);current="main";canvas->cancel();canvas->selected.clear();canvas->tool="Select";
    rebuild(true,true);canvas->view.zoom=.9;canvas->view.offset={80,90};Layout();
    const auto inlineBytes=serialize(history.project);
    const auto inlineTime=simulator->time(),inlinePending=simulator->pending();
    const auto inlineView=canvas->view;
    const auto warnedPin=endpoint(history.project,circuit(),{mismatchLoad,"A"});
    const auto warnedScreen=canvas->view.screen(warnedPin);
    if(canvas->handleDiagnosticClick({int(std::lround(warnedScreen.x)),int(std::lround(warnedScreen.y))}))
        throw std::runtime_error("ERC badge intercepted a connection terminal");
    clickCanvas(warnedPin);
    if(!canvas->start)throw std::runtime_error("ERC markers prevented selection-mode pin wiring");
    for(const auto &group:canvas->diagnosticMarkers())
        if(canvas->handleDiagnosticClick({group.bounds.x+group.bounds.width/2,group.bounds.y+group.bounds.height/2}))
            throw std::runtime_error("ERC icon interrupted an in-progress wire");
    canvas->cancel();
    const auto markers=canvas->diagnosticMarkers();
    auto marker=std::find_if(markers.begin(),markers.end(),[&](const auto &m){return m.object==warnedGate;});
    const auto single=std::find_if(markers.begin(),markers.end(),[&](const auto &m){return m.object==floatingOutput;});
    const auto adjacent=std::find_if(markers.begin(),markers.end(),[&](const auto &m){return m.object==mismatchLoad;});
    if(marker==markers.end() || marker->issues.size()<2 || single==markers.end() || single->issues.size()!=1 ||
       single->error || adjacent==markers.end() || !adjacent->error)
        throw std::runtime_error("ERC object icons or severity missing");
    const wxPoint center{marker->bounds.x+marker->bounds.width/2,marker->bounds.y+marker->bounds.height/2};
    canvas->updateDiagnosticTooltip({double(center.x),double(center.y)});
    for(const auto *field:{"规则：","严重级别：","涉及对象：","修复建议："})
        if(!canvas->diagnosticTooltip.Contains(U(field)))throw std::runtime_error("ERC tooltip field missing");
    auto inlineImage=[&](bool clean) {
        auto size=canvas->GetClientSize();wxBitmap bitmap(size.x,size.y,32);
        { wxMemoryDC dc(bitmap);dc.SetBackground(*wxWHITE_BRUSH);dc.Clear();
          std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::Create(dc));
          if(!graphics)throw std::runtime_error("inline ERC graphics context failed");
          canvas->render(*graphics,size,clean); }
        return bitmap.ConvertToImage();
    };
    const auto cleanBefore=inlineImage(true);
    auto overlayImage=inlineImage(false);
    if(!overlayImage.SaveFile(wxString((folder/"inline-erc-canvas.png").wstring()),wxBITMAP_TYPE_PNG) ||
       !samePixels(cleanBefore,inlineImage(true)))
        throw std::runtime_error("inline ERC changed clean export");
    if(!canvas->handleDiagnosticClick(center) || !diagnosticsWindow->IsShown() ||
       diagnosticOrder.empty() || diagnosticOrder.front()!=marker->issues.front())
        throw std::runtime_error("ERC icon click did not expand related problems");
    wxCommandEvent focusProblem(wxEVT_LISTBOX,diagnostics->GetId());focusProblem.SetInt(0);
    diagnostics->GetEventHandler()->ProcessEvent(focusProblem);
    if(canvas->selected.empty() || std::abs(canvas->view.zoom-inlineView.zoom)<.01)
        throw std::runtime_error("problem list did not zoom and highlight");
    diagnosticsWindow->Hide();canvas->view=inlineView;canvas->selected.clear();
    canvas->updateDiagnosticTooltip(canvas->view.screen(circuit().part(mismatchLoad)->at));
    if(!canvas->diagnosticTooltip.Contains(U("规则：")))
        throw std::runtime_error("object colour warning lost its tooltip");
    if(serialize(history.project)!=inlineBytes || simulator->time()!=inlineTime || simulator->pending()!=inlinePending)
        throw std::runtime_error("ERC interaction changed project or simulation state");

    // 单个 NOR、单条警告以及密集摆放、缩放、旋转、视口边缘均使用不遮挡的图标。
    Project collisionProject;collisionProject.name="ERC 图标避让验证";
    Part nor;nor.id=collisionProject.id("p");nor.kind="Nor";nor.label="NOR 警示避让";
    collisionProject.circuit("main").parts.push_back(nor);
    history.reset(collisionProject);current="main";canvas->selected.clear();rebuild(true,true);
    auto verifyMarkers=[&] {
        const auto markers=canvas->diagnosticMarkers();
        std::set<std::string> markedObjects;
        for(size_t i=0;i<markers.size();++i) {
            const auto &bounds=markers[i].bounds;
            if(!markedObjects.insert(markers[i].object).second)
                throw std::runtime_error("multiple ERC icons for the same object");
            if(markers[i].issues.empty() || bounds.width!=canvas->FromDIP(18) || bounds.height!=canvas->FromDIP(18) || !canvas->GetClientRect().Contains(bounds))
                throw std::runtime_error("ERC icon missing issues, oversized, or offscreen");
            for(size_t j=0;j<i;++j)
                if(bounds.Intersects(markers[j].bounds))throw std::runtime_error("ERC icons overlap");
            for(const auto &part:circuit().parts) {
                auto size=partSize(history.project,part);
                if(part.rotation%2)std::swap(size.x,size.y);
                const auto a=canvas->view.screen(part.at-size*.5),b=canvas->view.screen(part.at+size*.5);
                wxRect body(int(a.x)-2,int(a.y)-2,int(std::ceil(b.x-a.x))+4,int(std::ceil(b.y-a.y))+4);
                if(bounds.Intersects(body))throw std::runtime_error("ERC icon obscures a component body");
                for(const auto &pin:ports(history.project,part)) {
                    const auto at=canvas->view.screen(part.at+pin.at);
                    const int radius=canvas->FromDIP(9);
                    wxRect terminal(int(at.x)-radius,int(at.y)-radius,2*radius+1,2*radius+1);
                    if(bounds.Intersects(terminal))throw std::runtime_error("ERC icon obscures a connection target");
                }
            }
        }
    };
    for(int rotation=0;rotation<4;++rotation) {
        circuit().parts.front().rotation=rotation;rebuild(true,true);
        for(double zoom:{.15,.5,1.0,2.0,4.0}) {
            canvas->view.zoom=zoom;
            canvas->view.offset={canvas->GetClientSize().x*.5,canvas->GetClientSize().y*.5};
            verifyMarkers();
            if(canvas->diagnosticMarkers().empty())
                throw std::runtime_error("NOR warning icon disappeared despite available space");
        }
    }
    canvas->view.zoom=1;canvas->view.offset={canvas->GetClientSize().x*.5,canvas->GetClientSize().y*.5};
    circuit().parts.front().rotation=0;rebuild(true,true);
    inlineImage(false).SaveFile(wxString((folder/"erc-nor-no-overlap.png").wstring()),wxBITMAP_TYPE_PNG);
    writeAtomic(folder/"erc-nor-no-overlap.logic.json",serialize(history.project));
    for(int row=-1;row<=1;++row)for(int col=-1;col<=1;++col) {
        if(!row && !col)continue;
        Part nearby=nor;nearby.id=history.project.id("p");nearby.at={col*95.0,row*115.0};
        nearby.rotation=(col-row+4)%4;circuit().parts.push_back(nearby);
    }
    rebuild(true,true);
    for(Point offset:std::vector<Point>{{30,30},{double(canvas->GetClientSize().x-30),double(canvas->GetClientSize().y-30)},
                                         {canvas->GetClientSize().x*.5,canvas->GetClientSize().y*.5}}) {
        canvas->view.offset=offset;verifyMarkers();
    }
    inlineImage(false).SaveFile(wxString((folder/"erc-dense-no-overlap.png").wstring()),wxBITMAP_TYPE_PNG);
    history.reset(halfAdder());current="main";canvas->selected.clear();rebuild(true,true);
    if(!issues.empty() || !canvas->diagnosticMarkers().empty())
        throw std::runtime_error("resolved ERC markers remained on canvas");
    writeAtomic(folder/"LAYOUT-ERC-PASS.txt",
        "Library above property inspector; compact hierarchy; add/delete subcircuit commands and undo; "
        "direction/individual inversion/tool defaults; inline ERC colours, compact per-object warning/error icons, rule tooltips, "
        "modeless list zoom/highlight; icon avoidance at five zooms/four rotations/dense layout/viewport edges; "
        "no aggregate bubbles or minimap; clean export and model/simulation isolation passed.\n");
    runRoutingSmoke(folder);
    writeAtomic(folder/"HIERARCHY-UI-PASS.txt","Live and paused navigation preserved simulator identity/time/ticks; nested instances kept independent registers/RAM; interface Input and structural edits read-only; internal button changed only its instance, without changing project definitions.\n");
    writeAtomic(folder / "EDITOR-UI-PASS.txt",
                "Compact I/O, five state colours, table edits/validation/undo/live state, connection "
                "warnings/errors/navigation passed.\n");
    writeAtomic(
        folder / "LOGIC-PASS.txt",
        "Digital-only GUI: default startup, complete icon catalog at 1x/2x, tree mappings, "
        "grouped file menus and toolbar dropdowns, no print/Logisim export commands, "
        "component toolbar bindings, single placement returns to Select, blank click does not duplicate, "
        "selection-mode pin wiring/bends/cancel/undo/redo, zoomed-out center selection, "
        "all canvas symbols/previews/state colours, stable terminal hit targets, "
        "inverter, animated pin hover/zoom/nearest endpoint/leave/export isolation, rendering, drag, branch "
        "split/cancel/undo, clipboard "
        "copy/paste/undo/redo, property input, save/load with Unicode path, simulation, truth table "
        "and full PNG export passed.\n"
        "Custom component dialog/interfaces, undo/redo, library import/placement, nested simulation, "
        "Unicode component file, separate JSON/KiCad PCB export menus, JSON logical netlist, "
        "physical netlist/board/mapping bundle and export state preservation passed.\n"
        "外部电路 import, persistent diagnostics/navigation, unsaved state and native save/source "
        "preservation passed.\n");
    runExperimentSmoke(folder);
    runModuleSmoke(folder);
}
void LogicEditor::closeSmoke() {
    canvas->cancel();
    history.markSaved();
    Close();
}
} // namespace eda::logic
