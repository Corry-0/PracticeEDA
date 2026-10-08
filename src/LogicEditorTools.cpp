// 编辑器的辅助窗口。初值属于工程定义，运行值属于展开实例，两者绝不混写。
#include "DigitalEditor.h"
#include <wx/config.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/choicdlg.h>
#include <wx/textdlg.h>
#include <wx/print.h>
#include <wx/printdlg.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace eda::logic {
namespace {
std::string hexadecimal(uint32_t value, int digits=2) {
    std::ostringstream out; out << std::uppercase << std::hex << std::setw(digits)
                                << std::setfill('0') << value; return out.str();
}
uint32_t hexValue(const wxString &value) {
    auto text=utf8(value); size_t used=0;
    auto result=std::stoull(text,&used,16);
    if(used!=text.size() || result>UINT32_MAX) throw std::runtime_error("请输入有效的十六进制数（不超过 FFFFFFFF）");
    return uint32_t(result);
}
class CircuitPrintout : public wxPrintout {
    std::function<bool(wxDC*)> draw;
  public:
    CircuitPrintout(const wxString &title,std::function<bool(wxDC*)> fn):wxPrintout(title),draw(std::move(fn)) {}
    bool OnPrintPage(int page) override { return page==1 && GetDC() && draw(GetDC()); }
    bool HasPage(int page) override { return page==1; }
    void GetPageInfo(int *min,int *max,int *from,int *to) override { *min=*max=*from=*to=1; }
};
}

void LogicEditor::memoryEditor(bool runtime) {
    if(canvas->selected.size()!=1) throw std::runtime_error("请先选择一个 RAM 或 ROM 元件");
    auto id=*canvas->selected.begin(); auto part=circuit().part(id);
    if(!part || (part->kind!="RAM" && part->kind!="ROM")) throw std::runtime_error("选中的元件不是存储器");
    if(!runtime && !scope.empty()) throw std::runtime_error("运行实例不能修改定义初值，请先点击“编辑定义（复位）”");
    auto path=scoped(id); int width=part->width;
    size_t capacity=size_t(1) << parameter(*part,"addressBits",8);
    auto initial=part->data; initial.resize(capacity,0);
    wxDialog dialog(this,wxID_ANY,U(runtime?"实例运行内存（只读） · "+path:"存储器初始内容 · "+id),
                    wxDefaultPosition,{1050,670},wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
    auto sizer=new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(&dialog,wxID_ANY,U(runtime?
        "此窗口显示该实例的实时内容。复位恢复工程初值；导航和查看不会修改内存。":
        "单元格使用十六进制；每行 16 个地址。确定后更新工程初值并复位仿真。")),0,wxALL,10);
    auto grid=new wxGrid(&dialog,wxID_ANY); grid->SetName(runtime?"runtime-memory-grid":"initial-memory-grid");
    grid->CreateGrid(int((capacity+15)/16),16); grid->SetRowLabelSize(82);
    for(int column=0;column<16;++column) { grid->SetColLabelValue(column,U(hexadecimal(column,1))); grid->SetColSize(column,56); }
    for(int row=0;row<grid->GetNumberRows();++row) grid->SetRowLabelValue(row,U(hexadecimal(uint32_t(row*16),4)));
    grid->EnableEditing(!runtime); grid->SetDefaultCellFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE));
    auto refresh=[&] {
        auto values=runtime?simulator->memory(path):std::vector<Signal>{};
        grid->BeginBatch();
        for(size_t address=0;address<capacity;++address) {
            std::string value;
            if(runtime) value=address<values.size()?(values[address].defined()?hexadecimal(values[address].value,(width+3)/4):values[address].text()):"?";
            else value=hexadecimal(initial[address],(width+3)/4);
            grid->SetCellValue(int(address/16),int(address%16),U(value));
        }
        grid->EndBatch();
    };
    auto collect=[&] {
        if(grid->IsCellEditControlEnabled()) grid->SaveEditControlValue();
        std::vector<uint32_t> values(capacity); uint32_t mask=width==32?UINT32_MAX:((uint32_t(1)<<width)-1);
        for(size_t address=0;address<capacity;++address) {
            auto value=hexValue(grid->GetCellValue(int(address/16),int(address%16)));
            if(value>mask) throw std::runtime_error("地址 0x"+hexadecimal(uint32_t(address),4)+" 的值超出数据位宽");
            values[address]=value;
        }
        return values;
    };
    refresh(); sizer->Add(grid,1,wxEXPAND|wxLEFT|wxRIGHT,10);
    auto tools=new wxBoxSizer(wxHORIZONTAL);
    auto load=new wxButton(&dialog,wxID_ANY,U("载入 HEX…")); load->Enable(!runtime);
    auto save=new wxButton(&dialog,wxID_ANY,U("导出 HEX…"));
    tools->Add(load,0,wxRIGHT,8); tools->Add(save,0);
    sizer->Add(tools,0,wxALL,10); sizer->Add(dialog.CreateButtonSizer(runtime?wxOK:wxOK|wxCANCEL),0,wxEXPAND|wxALL,10);
    dialog.SetSizer(sizer);
    load->Bind(wxEVT_BUTTON,[&](wxCommandEvent &) {
        wxFileDialog file(&dialog,U("载入 Logisim raw HEX"),"","","HEX (*.hex;*.txt)|*.hex;*.txt",wxFD_OPEN|wxFD_FILE_MUST_EXIST);
        if(file.ShowModal()!=wxID_OK) return;
        try {
            std::istringstream input(readFile(fsPath(file.GetPath()))); std::string line,all;
            while(std::getline(input,line)) {
                if(line.rfind("v2.0",0)==0 || line.rfind("v3.0",0)==0) continue;
                all += line.substr(0,line.find('#'))+" ";
            }
            std::istringstream words(all); std::string word; std::vector<uint32_t> values(capacity,0); size_t address=0;
            while(words>>word) {
                size_t count=1; auto star=word.find('*');
                if(star!=std::string::npos) { count=std::stoull(word.substr(0,star)); word=word.substr(star+1); }
                auto value=hexValue(U(word));
                if(count>capacity-address) throw std::runtime_error("HEX 内容超过当前存储器容量");
                for(size_t repeat=0;repeat<count;++repeat) values[address++]=value;
            }
            initial=std::move(values); refresh();
        } catch(const std::exception &error) { wxMessageBox(U(error.what()),U("载入失败"),wxOK|wxICON_ERROR,&dialog); }
    });
    save->Bind(wxEVT_BUTTON,[&](wxCommandEvent &) {
        wxFileDialog file(&dialog,U("导出 HEX"),"","memory.hex","HEX (*.hex)|*.hex",wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
        if(file.ShowModal()!=wxID_OK) return;
        try {
            auto values=runtime?simulator->memory(path):std::vector<Signal>{};
            auto initialValues=runtime?std::vector<uint32_t>{}:collect(); std::ostringstream output; output<<"v2.0 raw\n";
            for(size_t address=0;address<capacity;++address) {
                if(runtime && (address>=values.size()||!values[address].defined()))
                    throw std::runtime_error("运行内存包含 X/Z/E，不能无损导出为二态 HEX");
                output<<hexadecimal(runtime?values[address].value:initialValues[address],(width+3)/4)
                      <<(address%16==15?'\n':' ');
            }
            writeAtomic(fsPath(file.GetPath()),output.str());
        } catch(const std::exception &error) { wxMessageBox(U(error.what()),U("导出失败"),wxOK|wxICON_ERROR,&dialog); }
    });
    wxTimer refreshTimer(&dialog);
    dialog.Bind(wxEVT_TIMER,[&](wxTimerEvent &) { if(runtime) refresh(); },refreshTimer.GetId());
    if(runtime && capacity<=4096) refreshTimer.Start(350);
    if(!runtime) dialog.Bind(wxEVT_BUTTON,[&](wxCommandEvent &) {
        try { initial=collect();dialog.EndModal(wxID_OK); }
        catch(const std::exception &error) { wxMessageBox(U(error.what()),U("初值未保存"),wxOK|wxICON_ERROR,&dialog); }
    },wxID_OK);
    auto result=dialog.ShowModal(); refreshTimer.Stop();
    if(!runtime && result==wxID_OK) change([&] { circuit().part(id)->data=initial; });
}

void LogicEditor::pinLayout() {
    if(!scope.empty()) throw std::runtime_error("请先切换到定义编辑，再安排符号接口");
    Part instance; instance.kind="Subcircuit"; instance.circuit=current;
    auto pins=ports(history.project,instance);
    if(pins.empty()) throw std::runtime_error("当前电路没有 Input/Output 接口");
    wxDialog dialog(this,wxID_ANY,U("自定义符号引脚布局 · "+current),wxDefaultPosition,{680,520},wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
    auto sizer=new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(&dialog,wxID_ANY,U("坐标相对元件中心，单位为画布像素。推荐按 10 的倍数安排。\n稳定接口 ID 不变；已有连线自动跟随端点。")),0,wxALL,10);
    auto grid=new wxGrid(&dialog,wxID_ANY); grid->CreateGrid(int(pins.size()),4); grid->SetRowLabelSize(0);
    for(int column=0;column<4;++column) grid->SetColLabelValue(column,U(std::vector<std::string>{"接口 ID","方向","X","Y"}[column]));
    for(size_t row=0;row<pins.size();++row) {
        grid->SetCellValue(int(row),0,U(pins[row].id)); grid->SetReadOnly(int(row),0);
        grid->SetCellValue(int(row),1,U(pins[row].output?"输出":"输入")); grid->SetReadOnly(int(row),1);
        grid->SetCellValue(int(row),2,std::to_string(int(pins[row].at.x)));
        grid->SetCellValue(int(row),3,std::to_string(int(pins[row].at.y)));
    }
    sizer->Add(grid,1,wxEXPAND|wxALL,10); sizer->Add(dialog.CreateButtonSizer(wxOK|wxCANCEL),0,wxALL|wxEXPAND,10);
    dialog.SetSizer(sizer); if(dialog.ShowModal()!=wxID_OK) return;
    std::map<std::string,Point> layout;
    for(size_t row=0;row<pins.size();++row) {
        double x=0,y=0;
        if(!grid->GetCellValue(int(row),2).ToDouble(&x)||!grid->GetCellValue(int(row),3).ToDouble(&y)||
           !std::isfinite(x)||!std::isfinite(y)||std::abs(x)>5000||std::abs(y)>5000)
            throw std::runtime_error("引脚坐标应为 -5000–5000 的有效数字");
        layout[pins[row].id]={x,y};
    }
    change([&] { circuit().interfaceLayout=layout; });
}

void LogicEditor::writeTrace(const Trace &event) {
    if(!traceFile || !monitoredSignals.count(event.object+":"+event.pin)) return;
    // CSV字段中的双引号必须成对转义，合法的自定义对象ID也能包含逗号和引号。
    auto quoted=[](const std::string &value) {
        std::string result="\"";
        for(char c:value) { if(c=='\"') result+='\"'; result+=c; }
        return result+'\"';
    };
    *traceFile<<event.time<<','<<quoted(event.object)<<','<<quoted(event.pin)<<','
              <<quoted(event.value)<<'\n'<<std::flush;
}
void LogicEditor::monitorSignals() {
    if(traceFile) {
        wxArrayString actions; actions.Add(U("重新选择信号并保存新日志")); actions.Add(U("停止记录并关闭文件"));
        wxSingleChoiceDialog action(this,U("连续日志正在记录，当前实例导航不影响记录"),U("信号监测"),actions);
        if(action.ShowModal()!=wxID_OK) return;
        if(action.GetSelection()==1) { simulator->setTraceSink({}); traceFile.reset(); monitoredSignals.clear(); return; }
    }
    wxArrayString names; std::vector<std::string> keys;
    std::function<void(const std::string&,const std::string&,bool)> add;
    add=[&](const std::string &name,const std::string &prefix,bool nested) {
        for(const auto &part:history.project.circuit(name).parts) {
            auto path=prefix+part.id;
            if(part.kind=="Subcircuit") { add(part.circuit,path+"/",true); continue; }
            if(nested && (part.kind=="Input"||part.kind=="Output")) continue;
            for(const auto &port:ports(history.project,part)) if(port.output) {
                keys.push_back(path+":"+port.id); names.Add(U(path+" : "+port.id+" ("+std::to_string(port.width)+" 位)"));
            }
        }
    };
    add(simulationRoot,"",false);
    wxMultiChoiceDialog select(this,U("选择持续记录的输出信号；跨越所有运行实例，CSV 使用完整实例路径。"),U("信号监测"),names);
    wxArrayInt selectedSignals; for(size_t i=0;i<keys.size();++i) if(monitoredSignals.count(keys[i])) selectedSignals.Add(int(i));
    select.SetSelections(selectedSignals);
    if(select.ShowModal()!=wxID_OK) return;
    if(select.GetSelections().empty()) throw std::runtime_error("请至少选择一个信号");
    wxFileDialog file(this,U("保存连续信号日志"),"","signal-log.csv","CSV (*.csv)|*.csv",wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
    if(file.ShowModal()!=wxID_OK) return;
    auto stream=std::make_shared<std::ofstream>(fsPath(file.GetPath()),std::ios::binary|std::ios::trunc);
    if(!*stream) throw std::runtime_error("无法创建 CSV 日志文件");
    *stream<<"\xEF\xBB\xBF时间,实例路径,引脚,值\n"; traceFile=stream; monitoredSignals.clear();
    for(auto index:select.GetSelections()) monitoredSignals.insert(keys[size_t(index)]);
    simulator->setTraceSink([this](const Trace &event) { writeTrace(event); });
    wxMessageBox(U("日志已开始。信号每次变化都会立即写入 CSV；再次打开此菜单可停止。"),U("信号监测"),wxOK|wxICON_INFORMATION,this);
}

void LogicEditor::rememberFile(const std::string &path) {
    recentFiles.erase(std::remove(recentFiles.begin(),recentFiles.end(),path),recentFiles.end());
    recentFiles.insert(recentFiles.begin(),path); if(recentFiles.size()>8) recentFiles.resize(8);
    wxConfig config("PracticeEDA");
    for(size_t i=0;i<8;++i) config.Write(U("recent/"+std::to_string(i)),i<recentFiles.size()?U(recentFiles[i]):wxString{});
    config.Flush();
}
void LogicEditor::recentMenu() {
    if(recentFiles.empty()) { wxMessageBox(U("尚未打开过工程"),U("最近打开"),wxOK,this); return; }
    wxArrayString paths; for(const auto &path:recentFiles) paths.Add(U(path));
    wxSingleChoiceDialog dialog(this,U("选择最近打开的工程"),U("最近打开"),paths);
    if(dialog.ShowModal()==wxID_OK && discard()) load(U(recentFiles[size_t(dialog.GetSelection())]));
}

void LogicEditor::exportBatch() {
    wxDirDialog folder(this,U("选择批量 PNG 导出目录")); if(folder.ShowModal()!=wxID_OK) return;
    wxTextEntryDialog scaleDialog(this,U("导出比例（0.25–4 倍）"),U("批量 PNG"),"1.5");
    if(scaleDialog.ShowModal()!=wxID_OK) return; double scale;
    if(!scaleDialog.GetValue().ToDouble(&scale)||scale<.25||scale>4) throw std::runtime_error("比例应为 0.25–4");
    // 导出临时定义视图使用独立仿真器；原运行器完整移回，时间、内存与日志均不受影响。
    bool wasRunning=timer.IsRunning(); stop(); canvas->cancel(); auto savedCurrent=current,savedScope=scope,savedRoot=simulationRoot;
    auto savedNavigation=navigation; auto savedView=canvas->view; bool savedAppearance=canvas->appearance;
    auto savedSimulator=std::move(simulator);
    auto restore=[&] { simulator=std::move(savedSimulator); current=savedCurrent; scope=savedScope;
        simulationRoot=savedRoot; navigation=savedNavigation; canvas->view=savedView;
        canvas->appearance=savedAppearance; rebuild(false,true); if(wasRunning)timer.Start(500); };
    try {
        for(const auto &definition:history.project.circuits) {
            current=definition.name; scope.clear(); canvas->appearance=false;
            simulator=std::make_unique<Simulator>(history.project,current); simulator->settle();
            refreshDiagnostics();
            std::string name=definition.name;
            for(auto &c:name) if(std::string("<>:\"/\\|?*").find(c)!=std::string::npos) c='_';
            auto path=fsPath(folder.GetPath())/std::filesystem::u8path(name+".png");
            canvas->exportPng(wxString(path.wstring()),scale);
        }
        restore();
    } catch(...) { restore(); throw; }
}

void LogicEditor::printCircuit(bool preview) {
    canvas->cancel();
    auto draw=[this](wxDC *dc) {
        Point lo{1e9,1e9},hi{-1e9,-1e9};
        auto add=[&](Point point) { lo.x=std::min(lo.x,point.x);lo.y=std::min(lo.y,point.y);
            hi.x=std::max(hi.x,point.x);hi.y=std::max(hi.y,point.y); };
        for(const auto &part:circuit().parts) {
            auto local=[&](Point point){for(int turn=0;turn<part.rotation;++turn)point={-point.y,point.x};add(part.at+point);};
            local({-100,-partHeight(history.project,part)/2-40});
            local({100,partHeight(history.project,part)/2+40});
            for(const auto &pin:ports(history.project,part)) add(part.at+pin.at);
            if(part.kind=="Subcircuit")for(const auto &shape:history.project.circuit(part.circuit).shapes)if(shape.appearance){
                auto box=shapeBounds(shape);local(box.first-Point{30,30});local(box.second+Point{30,30});
            }
        }
        for(const auto &wire:circuit().wires) for(const auto &point:wirePoints(history.project,circuit(),wire)) add(point);
        for(const auto &shape:circuit().shapes) if(shape.appearance==canvas->appearance) {
            auto bounds=shapeBounds(shape); add(bounds.first);add(bounds.second);
        }
        if(lo.x>hi.x) { lo={0,0};hi={500,300}; }
        auto size=dc->GetSize(); auto extent=hi-lo; auto saved=canvas->view;
        canvas->view.zoom=std::min((size.x-120)/std::max(1.0,extent.x),(size.y-160)/std::max(1.0,extent.y));
        canvas->view.offset=Point{60,80}-lo*canvas->view.zoom;
        std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::CreateFromUnknownDC(*dc));
        if(!graphics) { canvas->view=saved; return false; }
        canvas->render(*graphics,size,true); canvas->view=saved; return true;
    };
    wxPrintDialogData data; data.GetPrintData().SetOrientation(wxLANDSCAPE);
    if(preview) {
        auto result=new wxPrintPreview(new CircuitPrintout(U(current),draw),new CircuitPrintout(U(current),draw),&data);
        if(!result->IsOk()) { delete result; throw std::runtime_error("无法创建打印预览，请检查打印机设置"); }
        auto frame=new wxPreviewFrame(result,this,U("电路打印预览"),wxDefaultPosition,{1100,800});
        frame->Initialize(); frame->Show();
    } else {
        wxPrinter printer(&data); CircuitPrintout printout(U(current),draw);
        if(!printer.Print(this,&printout,true) && wxPrinter::GetLastError()==wxPRINTER_ERROR)
            throw std::runtime_error("打印失败，请检查打印机设置");
    }
}

void LogicEditor::textDevice() {
    if(canvas->selected.size()!=1) throw std::runtime_error("请先选择 Keyboard、TTY 或 Joystick 元件");
    auto id=*canvas->selected.begin(); auto part=circuit().part(id);
    if(!part) return; auto path=scoped(id);
    if(part->kind=="TTY") { showReport("TTY 运行输出 · "+path,simulator->text(path)); return; }
    if(part->kind=="Keyboard") {
        wxTextEntryDialog dialog(this,U("输入要送入键盘 FIFO 的文字（UTF-8 字节）"),U("键盘输入 · "+path),"",wxOK|wxCANCEL|wxTE_MULTILINE);
        if(dialog.ShowModal()==wxID_OK) { simulator->typeText(path,utf8(dialog.GetValue()));simulator->settle();rebuild(false); }
    } else if(part->kind=="Joystick") {
        wxTextEntryDialog dialog(this,U("输入 X,Y 坐标，范围 0–65535，以逗号分隔"),U("摇杆 · "+path),"0,0");
        if(dialog.ShowModal()!=wxID_OK) return; std::istringstream input(utf8(dialog.GetValue())); int x,y; char comma;
        if(!(input>>x>>comma>>y)||comma!=','||x<0||y<0||x>65535||y>65535) throw std::runtime_error("坐标格式应为 X,Y，且均为 0–65535");
        simulator->setInput(path,uint32_t(x)|(uint32_t(y)<<16));simulator->settle();rebuild(false);
    } else throw std::runtime_error("选中的元件不是 Keyboard、TTY 或 Joystick");
}
} // namespace eda::logic
