#include "PcbExport.h"
#include "json.hpp"
#include <iomanip>
#include <sstream>
namespace eda::logic {
namespace {
using Json=nlohmann::json;
std::string quoted(const std::string &s) { std::string v="\"";for(char c:s) {if(c=='"'||c=='\\')v+='\\';v+=c;}return v+'"'; }
struct Physical {std::string ref,value,footprint,path;int pads=2;std::map<int,int> nets;};
}
PcbExchange exportPcb(const Project &project,const std::string &circuit) {
    Simulator compiled(project,circuit);compiled.settle();
    for(const auto &issue:compiled.issues())if(issue.severity=="error")throw std::runtime_error("PCB映射前请先修复连接错误："+issue.message);
    auto logical=Json::parse(compiled.netlist());
    std::map<int,int> netIds;std::map<int,std::string> netNames{{1,"VCC"},{2,"GND"}};int next=3;
    // 常量网直接接电源轨；先识别全部常量，防止处理顺序影响网络编号。
    for(const auto &c:logical["components"]) {
        auto kind=c["kind"].get<std::string>();
        if(kind=="Constant"||kind=="Power"||kind=="Ground") {
            if(c["width"].get<int>()!=1)throw std::runtime_error("实体映射仅支持单比特常量");
            int number=c["pins"][0]["net"].get<int>();int supply=kind=="Ground"?2:kind=="Power"?1:c["value"].get<unsigned>()?1:2;
            if(netIds.count(number)&&netIds[number]!=supply)throw std::runtime_error("电源轨冲突");netIds[number]=supply;
        }
    }
    for(const auto &n:logical["nets"]) {int id=n["id"].get<int>();if(!netIds.count(id)){netIds[id]=next;netNames[next++]=n["name"].get<std::string>();}}
    std::vector<Physical> physical;int chip=0,connector=1;
    physical.push_back({"J1","POWER_5V","Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical","power",2,{{1,1},{2,2}}});
    const std::map<std::string,std::string> chips={{"And","74HC08"},{"Nand","74HC00"},{"Or","74HC32"},{"Nor","74HC02"},{"Xor","74HC86"},{"Xnor","74HC4077"},{"Not","74HC04"}};
    for(const auto &c:logical["components"]) {
        auto kind=c["kind"].get<std::string>(),source=c["path"].get<std::string>();
        if(kind=="Text"||kind=="Probe"||kind=="Constant"||kind=="Ground"||kind=="Power")continue;
        if(c["width"].get<int>()!=1)throw std::runtime_error("PCB实体映射不支持总线器件："+source+" / "+kind);
        auto params=c["params"];auto pin=[&](const std::string &id) {for(const auto &p:c["pins"])if(p["id"]==id)return netIds.at(p["net"].get<int>());throw std::runtime_error("缺少实体映射端口："+id);};
        if(kind=="Input"||kind=="Output") {
            physical.push_back({"J"+std::to_string(++connector),c["label"].get<std::string>(),"Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",source,2,{{1,pin(kind=="Input"?"Y":"A")},{2,2}}});continue;
        }
        // 74HC02与74HC4077的实际管脚/器件族不同，先限定已核对的常用TTL兼容DIP映射。
        if(!chips.count(kind)||kind=="Nor"||kind=="Xnor"||params.value("inputs",2)!=2||params.value("invertMask",0)!=0)
            throw std::runtime_error("该器件或属性尚无已核对的实体引脚映射："+source+" / "+kind+"；请使用单比特二输入AND/OR/NAND/XOR或NOT演示");
        Physical ic{"U"+std::to_string(++chip),chips.at(kind),"Package_DIP:DIP-14_W7.62mm",source,14,{{14,1},{7,2}}};
        if(kind=="Not") {ic.nets[1]=pin("A");ic.nets[2]=pin("Y");for(int n:{3,5,9,11,13})ic.nets[n]=2;}
        else {ic.nets[1]=pin("A");ic.nets[2]=pin("B");ic.nets[3]=pin("Y");for(int n:{4,5,9,10,12,13})ic.nets[n]=2;}
        physical.push_back(ic);
        physical.push_back({"C"+std::to_string(chip),"100nF","Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm",source+"/decoupling",2,{{1,1},{2,2}}});
    }
    if(!chip)throw std::runtime_error("实体映射示例至少需要一个逻辑门");
    if(physical.size()>500)throw std::runtime_error("教学PCB实体映射最多500个物理元件");
    std::ostringstream net,board;net<<"(export (version E) (design (source \"PracticeEDA\") (tool \"PracticeEDA 0.5 physical mapping\"))\n (components\n";
    board<<"(kicad_pcb (version 20240108) (generator \"PracticeEDA\")\n (general (thickness 1.6)) (paper \"A4\")\n"
         <<" (layers (0 \"F.Cu\" signal) (31 \"B.Cu\" signal) (36 \"B.SilkS\" user \"b.silkscreen\") (37 \"F.SilkS\" user \"f.silkscreen\") (44 \"Edge.Cuts\" user))\n (setup (pad_to_mask_clearance 0))\n";
    for(const auto &[id,name]:netNames)board<<" (net "<<id<<' '<<quoted(name)<<")\n";
    Json manifest={{"format","PracticeEDA.physical-mapping"},{"version",1},{"sourceCircuit",circuit},{"routed",false},{"components",Json::array()},{"nets",Json::array()}};
    for(size_t index=0;index<physical.size();++index) {
        const auto &p=physical[index];double x=30+30*double(index%6),y=30+30*double(index/6);
        std::ostringstream timestamp;timestamp<<std::hex<<std::setw(8)<<std::setfill('0')<<index+100;
        net<<"  (comp (ref "<<quoted(p.ref)<<") (value "<<quoted(p.value)<<") (footprint "<<quoted(p.footprint)<<") (tstamp "<<quoted(timestamp.str())<<"))\n";
        board<<" (footprint "<<quoted(p.footprint)<<" (layer \"F.Cu\") (at "<<x<<' '<<y<<")\n"
             <<"  (attr through_hole) (fp_text reference "<<quoted(p.ref)<<" (at 0 -3) (layer \"F.SilkS\") (effects (font (size 1 1) (thickness 0.15))))\n"
             <<"  (fp_text value "<<quoted(p.value)<<" (at 0 -5) (layer \"F.SilkS\") (effects (font (size 1 1) (thickness 0.15))))\n";
        Json pads=Json::array();
        for(int pad=1;pad<=p.pads;++pad){double px=0,py=0;
            if(p.pads==14){px=pad<=7?0:7.62;py=2.54*(pad<=7?pad-1:14-pad);}else if(p.ref[0]=='C')px=pad==1?0:5;else py=(pad-1)*2.54;
            board<<"  (pad "<<quoted(std::to_string(pad))<<" thru_hole "<<(pad==1?"rect":"oval")<<" (at "<<px<<' '<<py<<") (size 1.8 1.8) (drill 0.9) (layers \"*.Cu\" \"*.Mask\")";
            if(p.nets.count(pad)){auto id=p.nets.at(pad);board<<" (net "<<id<<' '<<quoted(netNames.at(id))<<")";pads.push_back({{"pad",pad},{"net",id},{"name",netNames.at(id)}});}board<<")\n";
        }board<<" )\n";manifest["components"].push_back({{"ref",p.ref},{"value",p.value},{"footprint",p.footprint},{"path",p.path},{"pads",pads}});
    }
    net<<" )\n (nets\n";
    for(const auto &[id,name]:netNames) {
        net<<"  (net (code "<<id<<") (name "<<quoted(name)<<")";Json members=Json::array();
        for(const auto &p:physical)for(const auto &[pad,n]:p.nets)if(n==id){net<<" (node (ref "<<quoted(p.ref)<<") (pin "<<quoted(std::to_string(pad))<<"))";members.push_back({{"ref",p.ref},{"pad",pad}});}
        net<<")\n";manifest["nets"].push_back({{"id",id},{"name",name},{"members",members}});
    }net<<" ))\n";
    double height=45+30*double((physical.size()+5)/6);board<<" (gr_rect (start 20 20) (end 200 "<<height<<") (stroke (width 0.1) (type default)) (fill none) (layer \"Edge.Cuts\"))\n)\n";
    return {net.str(),board.str(),manifest.dump(2)};
}
}
