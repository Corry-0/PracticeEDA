// 模块验收补充：经过真实菜单处理器和窗口控件，以 wxWidgets 标准钩子提供对话框响应。
// 钩子跳过系统文件选择器的鼠标交互，文件内容、撤销、仿真和副作用仍由真实实现执行。
#include "DigitalEditor.h"
#include "LogicAnalysis.h"
#include "PcbExport.h"
#include <wx/choicdlg.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/modalhook.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <sstream>

namespace eda::logic {
namespace {
class DialogResponses final : public wxModalDialogHook {
  public:
    explicit DialogResponses(std::function<int(wxDialog *)> fn) : respond(std::move(fn)) { Register(); }
  private:
    std::function<int(wxDialog *)> respond;
    int Enter(wxDialog *dialog) override { return respond(dialog); }
};
wxButton *buttonNamed(wxWindow *parent, const wxString &label) {
    for (auto child : parent->GetChildren()) {
        if (auto button = dynamic_cast<wxButton *>(child))
            if (button->GetLabel() == label) return button;
        if (auto result = buttonNamed(child, label)) return result;
    }
    return nullptr;
}
void press(wxButton *button) {
    if (!button) throw std::runtime_error("expected dialog button missing");
    wxCommandEvent event(wxEVT_BUTTON, button->GetId()); event.SetEventObject(button);
    button->GetEventHandler()->ProcessEvent(event);
}
}

void LogicEditor::runModuleSmoke(const std::filesystem::path &folder) {
    const auto outputs = folder / "module-exports";
    std::filesystem::create_directories(outputs);
    std::ostringstream rows;
    rows << "id\tscenario\tstatus\tassertions\tdetail\n";
    size_t assertions = 0, failures = 0, cases = 0;
    auto require = [&](bool value, const std::string &reason) {
        ++assertions; if (!value) throw std::runtime_error(reason);
    };
    auto cleanTrace = [&] {
        if (simulator) simulator->setTraceSink({});
        traceFile.reset(); monitoredSignals.clear(); stop();
    };
    auto activate = [&](Project project) {
        cleanTrace(); canvas->cancel(); history.reset(std::move(project)); current=history.project.main;
        simulationRoot=current; scope.clear(); navigation.clear(); savedPath.clear();
        canvas->selected.clear(); canvas->tool="Select"; canvas->appearance=false; rebuild(true,true);
    };
    auto scenario = [&](const char *id, const char *name, const std::function<void()> &action) {
        const auto before=assertions; ++cases;
        try { action(); rows<<id<<'\t'<<name<<"\tPASS\t"<<assertions-before<<"\tpassed\n"; }
        catch(const std::exception &error) {
            ++failures; std::string detail=error.what();
            for(auto &c:detail) if(c=='\n'||c=='\r'||c=='\t')c=' ';
            rows<<id<<'\t'<<name<<"\tFAIL\t"<<assertions-before<<'\t'<<detail<<'\n';
        }
        cleanTrace(); writeAtomic(folder/"MODULE-RESULTS.tsv",rows.str());
    };
    auto invoke = [&](const char *caption) {
        int id=wxID_NONE;
        std::function<void(wxMenu *)> find=[&](wxMenu *menu) {
            for(auto item:menu->GetMenuItems()) {
                if(item->GetItemLabelText()==U(caption))id=item->GetId();
                if(item->GetSubMenu())find(item->GetSubMenu());
            }
        };
        for(size_t i=0;i<GetMenuBar()->GetMenuCount();++i)find(GetMenuBar()->GetMenu(i));
        require(id!=wxID_NONE,std::string("menu missing: ")+caption);
        wxCommandEvent command(wxEVT_MENU,id);
        require(GetEventHandler()->ProcessEvent(command),"menu command was not handled");
    };
    auto fileResponse = [&](wxDialog *dialog,const std::filesystem::path &path) {
        auto file=dynamic_cast<wxFileDialog *>(dialog);
        if(!file)throw std::runtime_error("expected file dialog");
        file->SetPath(wxString(path.wstring())); return wxID_OK;
    };
    auto example = [&](const char *name) {
        return deserialize(readFile(std::filesystem::path(__FILE__).parent_path().parent_path()/"examples"/name));
    };
    auto noChange = [&](const std::string &bytes,Simulator *runtime,uint64_t time,size_t pending) {
        require(serialize(history.project)==bytes,"read-only action changed project");
        require(simulator.get()==runtime&&simulator->time()==time&&simulator->pending()==pending,
                "read-only action changed live simulator/time/queue");
    };

    scenario("M01","JSON export through menu / Unicode filename",[&] {
        activate(halfAdder()); const auto target=outputs/std::filesystem::u8path("逻辑连接.json");
        const auto bytes=serialize(history.project), expected=simulator->netlist();
        auto runtime=simulator.get();auto time=simulator->time();auto pending=simulator->pending();int dialogs=0;
        DialogResponses hook([&](wxDialog *dialog) {
            auto file=dynamic_cast<wxFileDialog *>(dialog); require(file!=nullptr,"unexpected JSON export dialog");
            require(file->GetWildcard()==U("逻辑网表 JSON (*.json)|*.json"),"JSON format selector is ambiguous");
            require(file->GetFilename()=="circuit.json","JSON default filename differs"); ++dialogs;
            return fileResponse(dialog,target);
        });
        invoke("逻辑网表 JSON…");require(dialogs==1&&readFile(target)==expected,"JSON export content differs");
        noChange(bytes,runtime,time,pending);
    });
    scenario("M02","JSON export cancel",[&] {
        activate(halfAdder());const auto target=outputs/"cancel.json";int dialogs=0;
        DialogResponses hook([&](wxDialog *dialog){++dialogs;fileResponse(dialog,target);return wxID_CANCEL;});
        invoke("逻辑网表 JSON…");require(dialogs==1&&!std::filesystem::exists(target),"cancel created a JSON file");
    });
    scenario("M03","JSON extension and write failure",[&] {
        activate(halfAdder());const auto target=outputs/"wrong.net";int errors=0;
        DialogResponses hook([&](wxDialog *dialog){
            if(dynamic_cast<wxFileDialog *>(dialog))return fileResponse(dialog,target);
            ++errors;return wxID_OK;
        });
        invoke("逻辑网表 JSON…");require(errors==1&&!std::filesystem::exists(target),"wrong JSON extension accepted");
        bool refused=false;try{writeAtomic(outputs/"absent-parent"/"cannot-write.json","x");}catch(const std::exception&){refused=true;}
        require(refused,"write to missing parent silently succeeded");
    });
    scenario("M04","KiCad PCB bundle through menu",[&] {
        activate(halfAdder());const auto board=outputs/std::filesystem::u8path("半加器.kicad_pcb");
        const auto bytes=serialize(history.project);auto runtime=simulator.get();auto time=simulator->time();auto pending=simulator->pending();
        int files=0,reports=0;DialogResponses hook([&](wxDialog *dialog){
            if(auto file=dynamic_cast<wxFileDialog *>(dialog)) {
                require(file->GetWildcard()==U("KiCad PCB (*.kicad_pcb)|*.kicad_pcb"),"PCB format selector differs");
                require(file->GetFilename()=="circuit.kicad_pcb","PCB default filename differs");++files;return fileResponse(dialog,board);
            }
            require(dialog->GetTitle()==U("KiCad PCB 已导出"),"unexpected PCB result dialog");++reports;return wxID_OK;
        });
        invoke("KiCad PCB…");auto net=board;net.replace_extension(".net");auto mapping=board;mapping.replace_extension(".mapping.json");
        require(files==1&&reports==1,"PCB export dialogs differ");
        require(readFile(board).find("(kicad_pcb ")==0&&readFile(net).find("(export ")==0&&readFile(mapping).find("PracticeEDA.physical-mapping")!=std::string::npos,"PCB bundle missing or corrupt");
        noChange(bytes,runtime,time,pending);
    });
    scenario("M05","KiCad PCB cancel",[&] {
        activate(halfAdder());const auto board=outputs/"cancel.kicad_pcb";
        DialogResponses hook([&](wxDialog *dialog){fileResponse(dialog,board);return wxID_CANCEL;});
        invoke("KiCad PCB…");require(!std::filesystem::exists(board)&&!std::filesystem::exists(outputs/"cancel.net"),"cancel wrote PCB files");
    });
    scenario("M06","KiCad companion overwrite refusal and approval",[&] {
        activate(halfAdder());const auto board=outputs/"overwrite.kicad_pcb",net=outputs/"overwrite.net",mapping=outputs/"overwrite.mapping.json";
        writeAtomic(board,"BOARD_SENTINEL");writeAtomic(net,"NET_SENTINEL");writeAtomic(mapping,"MAP_SENTINEL");
        bool approve=false;int confirmations=0;
        DialogResponses hook([&](wxDialog *dialog){
            if(dynamic_cast<wxFileDialog *>(dialog))return fileResponse(dialog,board);
            if(dialog->GetTitle()==U("覆盖配套文件")){++confirmations;return approve?wxID_YES:wxID_NO;}
            return wxID_OK;
        });
        invoke("KiCad PCB…");require(readFile(board)=="BOARD_SENTINEL"&&readFile(net)=="NET_SENTINEL"&&readFile(mapping)=="MAP_SENTINEL","refusal overwrote a companion");
        approve=true;invoke("KiCad PCB…");require(confirmations==2&&readFile(board).find("(kicad_pcb ")==0&&readFile(net).find("(export ")==0,"approved overwrite failed");
    });
    scenario("M07","Unsupported PCB fails before file chooser",[&] {
        activate(example("bus-register.logic.json"));const auto bytes=serialize(history.project);int files=0,errors=0;
        DialogResponses hook([&](wxDialog *dialog){if(dynamic_cast<wxFileDialog *>(dialog))++files;else ++errors;return wxID_CANCEL;});
        invoke("KiCad PCB…");require(files==0&&errors==1&&serialize(history.project)==bytes,"unsupported PCB opened chooser or changed design");
    });
    scenario("M08","PNG menu and scale validation",[&] {
        activate(halfAdder());const auto image=outputs/"menu.png";bool invalid=false;int errors=0;
        DialogResponses hook([&](wxDialog *dialog){
            if(dynamic_cast<wxFileDialog *>(dialog))return fileResponse(dialog,invalid?outputs/"invalid.png":image);
            if(auto scale=dynamic_cast<wxTextEntryDialog *>(dialog)){scale->SetValue(invalid?"0.1":"0.25");return wxID_OK;}
            ++errors;return wxID_OK;
        });
        invoke("完整电路 PNG…");wxImage loaded(wxString(image.wstring()),wxBITMAP_TYPE_PNG);
        require(loaded.IsOk()&&loaded.GetWidth()>0&&loaded.GetHeight()>0,"PNG export cannot be reopened");
        invalid=true;invoke("完整电路 PNG…");require(errors==1&&!std::filesystem::exists(outputs/"invalid.png"),"invalid PNG scale created output");
    });
    scenario("M09","Batch PNG restores running nested instance",[&] {
        activate(example("defense-hierarchy.logic.json"));enterInstance("left");enterInstance("inner");
        const auto destination=outputs/"batch";std::filesystem::create_directories(destination);
        const auto bytes=serialize(history.project),oldCurrent=current,oldScope=scope;auto runtime=simulator.get();auto time=simulator->time();auto pending=simulator->pending();
        auto oldView=canvas->view;timer.Start(500);
        DialogResponses hook([&](wxDialog *dialog){
            if(auto directory=dynamic_cast<wxDirDialog *>(dialog)){directory->SetPath(wxString(destination.wstring()));return wxID_OK;}
            if(auto scale=dynamic_cast<wxTextEntryDialog *>(dialog)){scale->SetValue("0.25");return wxID_OK;}
            return wxID_CANCEL;
        });
        invoke("批量子电路 PNG…");size_t count=0;
        for(const auto &entry:std::filesystem::directory_iterator(destination)){wxImage image(wxString(entry.path().wstring()),wxBITMAP_TYPE_PNG);require(image.IsOk(),"batch image unreadable");++count;}
        require(count==history.project.circuits.size(),"batch export lost a definition");
        require(timer.IsRunning()&&current==oldCurrent&&scope==oldScope&&canvas->view.zoom==oldView.zoom&&distance(canvas->view.offset,oldView.offset)<.001,"batch export lost navigation/view/timer");
        noChange(bytes,runtime,time,pending);
    });
    auto memoryProject=[] {
        Project p;Part ram;ram.id=p.id("p");ram.kind="RAM";ram.width=8;ram.params["addressBits"]=4;ram.data={1,2,3};p.circuit("main").parts.push_back(ram);return p;
    };
    scenario("M10","HEX import/export and memory undo",[&] {
        activate(memoryProject());const auto id=circuit().parts.front().id;canvas->selected={id};
        const auto input=outputs/"memory-input.hex",output=outputs/"memory-output.hex";writeAtomic(input,"v2.0 raw\n2*2a 7f # comment\n");
        const auto before=serialize(history.project);int files=0;
        DialogResponses hook([&](wxDialog *dialog){
            if(auto file=dynamic_cast<wxFileDialog *>(dialog)){++files;return fileResponse(file,file->GetWindowStyleFlag()&wxFD_SAVE?output:input);}
            auto grid=dynamic_cast<wxGrid *>(wxWindow::FindWindowByName("initial-memory-grid",dialog));require(grid!=nullptr,"initial memory grid missing");
            press(buttonNamed(dialog,U("载入 HEX…")));require(grid->GetCellValue(0,0)=="2A"&&grid->GetCellValue(0,1)=="2A"&&grid->GetCellValue(0,2)=="7F","HEX values were not loaded");
            press(buttonNamed(dialog,U("导出 HEX…")));return wxID_OK;
        });
        memoryEditor(false);require(files==2&&circuit().part(id)->data[0]==42&&circuit().part(id)->data[2]==127,"HEX import was not committed");
        require(readFile(output).find("2A 2A 7F")!=std::string::npos,"HEX roundtrip contents differ");
        undo();require(serialize(history.project)==before,"memory initial value undo failed");
    });
    scenario("M11","Runtime memory read-only and HEX export",[&] {
        activate(memoryProject());canvas->selected={circuit().parts.front().id};const auto bytes=serialize(history.project);
        auto runtime=simulator.get();auto time=simulator->time();auto pending=simulator->pending();const auto output=outputs/"runtime.hex";
        DialogResponses hook([&](wxDialog *dialog){
            if(dynamic_cast<wxFileDialog *>(dialog))return fileResponse(dialog,output);
            auto grid=dynamic_cast<wxGrid *>(wxWindow::FindWindowByName("runtime-memory-grid",dialog));require(grid&&!grid->IsEditable(),"runtime memory is editable");
            auto load=buttonNamed(dialog,U("载入 HEX…"));require(load&&!load->IsEnabled(),"runtime HEX load is enabled");press(buttonNamed(dialog,U("导出 HEX…")));return wxID_OK;
        });
        memoryEditor(true);require(readFile(output).find("01 02 03")!=std::string::npos,"runtime memory export differs");noChange(bytes,runtime,time,pending);
    });
    scenario("M12","Malformed HEX preserves initial memory",[&] {
        activate(memoryProject());canvas->selected={circuit().parts.front().id};const auto bytes=serialize(history.project);const auto input=outputs/"bad.hex";writeAtomic(input,"v2.0 raw\nzz\n");int errors=0;
        DialogResponses hook([&](wxDialog *dialog){
            if(dynamic_cast<wxFileDialog *>(dialog))return fileResponse(dialog,input);
            if(wxWindow::FindWindowByName("initial-memory-grid",dialog)){press(buttonNamed(dialog,U("载入 HEX…")));return wxID_CANCEL;}
            ++errors;return wxID_OK;
        });
        memoryEditor(false);require(errors==1&&serialize(history.project)==bytes,"malformed HEX changed initial memory");
    });
    scenario("M13","Custom pin layout and undo",[&] {
        activate(halfAdder());const auto bytes=serialize(history.project);const auto inputId=circuit().parts.front().id;
        DialogResponses hook([&](wxDialog *dialog){
            wxGrid *grid=nullptr;for(auto child:dialog->GetChildren())if(auto candidate=dynamic_cast<wxGrid *>(child))grid=candidate;
            require(grid!=nullptr,"pin layout grid missing");grid->SetCellValue(0,2,"-100");grid->SetCellValue(0,3,"40");return wxID_OK;
        });
        pinLayout();require(circuit().interfaceLayout.count(inputId)&&distance(circuit().interfaceLayout.at(inputId),{-100,40})<.001,"pin layout update missing");
        undo();require(serialize(history.project)==bytes,"pin layout undo failed");
    });
    scenario("M14","Continuous CSV logging start/change/stop",[&] {
        activate(halfAdder());const auto path=outputs/"signal.csv";int acknowledgements=0;
        DialogResponses hook([&](wxDialog *dialog){
            if(auto choice=dynamic_cast<wxMultiChoiceDialog *>(dialog)){wxArrayInt selected;selected.Add(0);choice->SetSelections(selected);choice->TransferDataFromWindow();return wxID_OK;}
            if(auto choice=dynamic_cast<wxSingleChoiceDialog *>(dialog)){choice->SetSelection(1);return wxID_OK;}
            if(dynamic_cast<wxFileDialog *>(dialog))return fileResponse(dialog,path);
            ++acknowledgements;return wxID_OK;
        });
        monitorSignals();require(bool(traceFile)&&monitoredSignals.size()==1,"signal logger did not start");
        const auto key=*monitoredSignals.begin();const auto id=key.substr(0,key.find(':'));simulator->setInput(id,1);simulator->settle();
        monitorSignals();require(!traceFile&&monitoredSignals.empty()&&acknowledgements==1,"signal logger did not stop");
        auto bytes=readFile(path);require(bytes.find("实例路径")!=std::string::npos&&bytes.find(id)!=std::string::npos,"signal log lacks actual changed signal");
    });
    scenario("M15","Templates blank/half-adder/nested",[&] {
        int selected=0;DialogResponses hook([&](wxDialog *dialog){
            if(auto choice=dynamic_cast<wxSingleChoiceDialog *>(dialog)){choice->SetSelection(selected);return wxID_OK;}return wxID_NO;
        });
        for(selected=0;selected<3;++selected){activate(Project{});invoke("从模板新建…");require(savedPath.empty()&&current==history.project.main,"template reused a saved file path");
            require(selected==0?circuit().parts.empty():!circuit().parts.empty(),"template content differs");history.project.validate();}
    });
    scenario("M16","Recent file open and missing-file recovery",[&] {
        activate(halfAdder());const auto file=outputs/"recent.logic.json";writeAtomic(file,serialize(history.project));rememberFile(file.u8string());
        DialogResponses hook([&](wxDialog *dialog){if(auto choice=dynamic_cast<wxSingleChoiceDialog *>(dialog)){choice->SetSelection(0);return wxID_OK;}return wxID_OK;});
        recentMenu();require(savedPath==file.u8string()&&!history.dirty(),"recent file did not open");
        const auto bytes=serialize(history.project);recentFiles={ (outputs/"missing.logic.json").u8string() };recentMenu();
        require(serialize(history.project)==bytes,"missing recent file replaced current project");
    });
    scenario("M17","Help windows preserve simulation",[&] {
        activate(halfAdder());const auto bytes=serialize(history.project);auto runtime=simulator.get();auto time=simulator->time();auto pending=simulator->pending();int dialogs=0;
        DialogResponses hook([&](wxDialog *){++dialogs;return wxID_OK;});invoke("操作指南");invoke("数字逻辑使用说明");
        require(dialogs==2,"help command did not construct its report window");noChange(bytes,runtime,time,pending);
    });
    scenario("M18","Joystick dialog updates runtime only",[&] {
        Project p;Part joy;joy.id=p.id("p");joy.kind="Joystick";joy.width=8;p.circuit("main").parts.push_back(joy);activate(p);canvas->selected={joy.id};const auto bytes=serialize(history.project);
        DialogResponses hook([&](wxDialog *dialog){auto input=dynamic_cast<wxTextEntryDialog *>(dialog);require(input!=nullptr,"joystick dialog missing");input->SetValue("11,22");return wxID_OK;});
        textDevice();require(simulator->read({joy.id,"X"}).value==11&&simulator->read({joy.id,"Y"}).value==22,"joystick values differ");require(serialize(history.project)==bytes,"joystick dialog changed design initial values");
    });
    scenario("M19","Truth-table CSV import and export menu",[&] {
        activate(halfAdder());const auto table=truthTable(history.project,current);const auto source=outputs/"table-in.csv",target=outputs/"table-out.csv";writeAtomic(source,table.csv());
        DialogResponses hook([&](wxDialog *dialog){
            if(auto file=dynamic_cast<wxFileDialog *>(dialog))return fileResponse(dialog,file->GetWindowStyleFlag()&wxFD_SAVE?target:source);
            return wxID_OK;
        });
        invoke("真值表 CSV…");require(hasTable&&lastTable.rows==table.rows,"table import was not remembered");
        invoke("最近分析 CSV…");require(parseTruthCsv(readFile(target)).rows==table.rows,"exported table cannot be reimported");
    });
    scenario("M20","New/open/save and cancellation protection",[&] {
        activate(halfAdder());const auto target=outputs/std::filesystem::u8path("原生工程.logic.json");
        DialogResponses hook([&](wxDialog *dialog){if(dynamic_cast<wxFileDialog *>(dialog))return fileResponse(dialog,target);return wxID_NO;});
        require(save(true)&&!history.dirty(),"save-as failed");require(serialize(deserialize(readFile(target)))==serialize(history.project),"saved project cannot roundtrip");
        const auto bytes=serialize(history.project);writeAtomic(outputs/"invalid.logic.json","{");require(!load(wxString((outputs/"invalid.logic.json").wstring()))&&serialize(history.project)==bytes,"bad project replaced current design");
    });
    // 边界用例明确给出期望，发现缺陷时保留失败记录，继续执行其他模块。
    scenario("M21","Batch PNG sanitized-name collision",[&] {
        // 对象 ID 为全工程唯一，使用组件导入为每个额外定义重分配 ID。
        auto p=halfAdder();auto first=importComponent(p,exportComponent(halfAdder(),"main"));p.circuit(first).name="a/b";
        auto second=importComponent(p,exportComponent(halfAdder(),"main"));p.circuit(second).name="a:b";
        activate(p);const auto destination=outputs/"batch-name-collision";std::filesystem::create_directories(destination);
        DialogResponses hook([&](wxDialog *dialog){if(auto directory=dynamic_cast<wxDirDialog *>(dialog)){directory->SetPath(wxString(destination.wstring()));return wxID_OK;}if(auto scale=dynamic_cast<wxTextEntryDialog *>(dialog)){scale->SetValue("0.25");return wxID_OK;}return wxID_CANCEL;});
        exportBatch();size_t count=0;for(const auto &entry:std::filesystem::directory_iterator(destination))if(entry.path().extension()==".png")++count;
        require(count==p.circuits.size(),"distinct circuit names overwrite the same sanitized PNG filename");
    });
    scenario("M22","Nonempty half-adder template is an unsaved design",[&] {
        DialogResponses hook([&](wxDialog *dialog){if(auto choice=dynamic_cast<wxSingleChoiceDialog *>(dialog)){choice->SetSelection(1);return wxID_OK;}return wxID_NO;});
        activate(Project{});invoke("从模板新建…");require(!circuit().parts.empty()&&savedPath.empty(),"half-adder template not created");
        require(history.dirty(),"nonempty half-adder template is marked saved and closes without an unsaved-design prompt");
    });
    scenario("M23","Analysis editing / Karnaugh / NAND synthesis controls",[&] {
        activate(halfAdder());auto table=truthTable(history.project,current);table.rows[0][table.inputs.size()]=1;
        int maps=0,reports=0;
        DialogResponses hook([&](wxDialog *dialog){
            if(auto name=dynamic_cast<wxTextEntryDialog *>(dialog)){name->SetValue("audit_generated");return wxID_OK;}
            if(auto mode=dynamic_cast<wxSingleChoiceDialog *>(dialog)){mode->SetSelection(1);return wxID_OK;}
            if(auto grid=dynamic_cast<wxGrid *>(wxWindow::FindWindowByName("editable-truth-table",dialog))){
                require(grid->IsReadOnly(0,0)&&!grid->IsReadOnly(0,2),"truth-table input/output editability differs");
                grid->SetCellValue(0,2,"1");press(buttonNamed(dialog,U("化简表达式")));press(buttonNamed(dialog,U("卡诺图 / 圈组")));press(buttonNamed(dialog,U("自动生成门电路…")));return wxID_OK;
            }
            if(dialog->GetTitle().StartsWith(U("卡诺图"))){
                wxGrid *cells=nullptr;for(auto child:dialog->GetChildren())if(auto grid=dynamic_cast<wxGrid *>(child))cells=grid;
                require(cells&&cells->GetNumberRows()==2&&cells->GetNumberCols()==2,"Karnaugh cell dimensions differ");++maps;
            }else if(dialog->GetTitle()==U("化简表达式"))++reports;
            return wxID_OK;
        });
        showTable(truthTable(history.project,current));
        require(maps==1&&reports==1&&current=="audit_generated","analysis actions did not execute");
        require(truthTable(history.project,current).rows==table.rows,"synthesized circuit differs from edited truth table");
        for(const auto &part:circuit().parts)require(part.kind=="Nand"||part.kind=="Input"||part.kind=="Output"||part.kind=="Constant","NAND synthesis contains other gates");
    });
    scenario("M24","Simulation run/pause/tick/step/fixed ticks/reset menus",[&] {
        activate(example("bus-register.logic.json"));
        DialogResponses hook([&](wxDialog *dialog){if(auto number=dynamic_cast<wxTextEntryDialog *>(dialog)){number->SetValue("3");return wxID_OK;}return wxID_CANCEL;});
        invoke("运行 / 暂停");require(timer.IsRunning(),"run command did not start timer");invoke("运行 / 暂停");require(!timer.IsRunning(),"pause command did not stop timer");
        const auto before=simulator->ticks();invoke("时钟基础节拍");require(simulator->ticks()==before+1,"clock tick was not advanced");
        invoke("单步传播");invoke("运行指定节拍…");require(simulator->ticks()==before+4,"fixed tick count differs");
        invoke("复位");require(simulator->ticks()==0&&!timer.IsRunning(),"reset did not restore tick count");
    });
    scenario("M25","New project cancel / save before replacing dirty design",[&] {
        activate(halfAdder());change([&]{circuit().parts.front().label="unsaved_label";});const auto bytes=serialize(history.project);const auto path=outputs/"before-new.logic.json";bool cancel=true;
        DialogResponses hook([&](wxDialog *dialog){if(dynamic_cast<wxFileDialog *>(dialog))return fileResponse(dialog,path);return cancel?wxID_CANCEL:wxID_YES;});
        invoke("新建数字工程");require(history.dirty()&&serialize(history.project)==bytes,"cancel discarded an edited design");
        cancel=false;invoke("新建数字工程");require(readFile(path)==bytes&&circuit().parts.empty()&&savedPath.empty(),"save-before-new lost edited design");
    });
    scenario("M26","Malformed HEX repeat count is rejected",[&] {
        activate(memoryProject());canvas->selected={circuit().parts.front().id};const auto input=outputs/"invalid-repeat.hex";writeAtomic(input,"v2.0 raw\n2x*2a\n");int errors=0;
        DialogResponses hook([&](wxDialog *dialog){if(dynamic_cast<wxFileDialog *>(dialog))return fileResponse(dialog,input);if(wxWindow::FindWindowByName("initial-memory-grid",dialog)){press(buttonNamed(dialog,U("载入 HEX…")));return wxID_CANCEL;}++errors;return wxID_OK;});
        memoryEditor(false);require(errors==1,"invalid repeat count 2x was silently parsed as 2");
    });
    activate(halfAdder());
    writeAtomic(folder/"MODULE-SUMMARY.txt","cases="+std::to_string(cases)+"\nassertions="+std::to_string(assertions)+"\nfailures="+std::to_string(failures)+"\n");
    if(failures)throw std::runtime_error("module acceptance scenarios failed: "+std::to_string(failures)+"; see MODULE-RESULTS.tsv");
    writeAtomic(folder/"MODULE-PASS.txt","All "+std::to_string(cases)+" module acceptance scenarios passed.\n");
}
} // namespace eda::logic
