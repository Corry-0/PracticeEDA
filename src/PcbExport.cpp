#include "PcbExport.h"
#include "KiCadFootprints.h"
#include "json.hpp"
#include <iomanip>
#include <sstream>
#include <string_view>
namespace eda::logic {
namespace {
using Json=nlohmann::json;
std::string quoted(const std::string &s) { std::string v="\"";for(char c:s) {if(c=='"'||c=='\\')v+='\\';v+=c;}return v+'"'; }
constexpr auto dipFootprint = "Package_DIP:DIP-14_W7.62mm";
constexpr auto capFootprint = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm";
constexpr auto headerFootprint = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical";
constexpr int vcc = 1, gnd = 2;

struct GatePins {int a, b, y;}; // b=0 表示反相器没有第二个输入。
const std::vector<GatePins> quadGates{{1,2,3},{4,5,6},{9,10,8},{12,13,11}};
const std::vector<GatePins> hexInverters{{1,0,2},{3,0,4},{5,0,6},{9,0,8},{11,0,10},{13,0,12}};
struct ChipSpec {const char *value, *symbol; const std::vector<GatePins> &units;};
// KiCad 10 74xx 标准符号单元顺序。08/32 沿用同引脚的 LS 库符号，实际料号仍为 HC。
const std::map<std::string,ChipSpec> gateChips{
    {"And",{"74HC08","74xx:74LS08",quadGates}},
    {"Nand",{"74HC00","74xx:74HC00",quadGates}},
    {"Or",{"74HC32","74xx:74LS32",quadGates}},
    {"Xor",{"74HC86","74xx:74HC86",quadGates}},
    {"Not",{"74HC04","74xx:74HC04",hexInverters}},
};
struct AssignedGate {std::string path; GatePins pins;};
struct Physical {
    std::string ref,value,footprint,path;
    int pads=2;
    std::map<int,int> nets;
    std::string symbol{};
    std::vector<AssignedGate> gates{};
};
struct PhysicalNetlist {
    std::vector<Physical> components;
    std::map<int,std::string> netNames;
};

PhysicalNetlist buildPhysicalNetlist(const Project &project,const std::string &circuit) {
    Simulator compiled(project,circuit);compiled.settle();
    for(const auto &issue:compiled.issues())if(issue.severity=="error")throw std::runtime_error("PCB映射前请先修复连接错误："+issue.message);
    auto logical=Json::parse(compiled.netlist());
    std::map<int,int> netIds;std::map<int,std::string> netNames{{vcc,"VCC"},{gnd,"GND"}};int next=3;
    // 常量网直接接电源轨；先识别全部常量，防止处理顺序影响网络编号。
    for(const auto &c:logical["components"]) {
        auto kind=c["kind"].get<std::string>();
        if(kind=="Constant"||kind=="Power"||kind=="Ground") {
            if(c["width"].get<int>()!=1)throw std::runtime_error("实体映射仅支持单比特常量");
            int number=c["pins"][0]["net"].get<int>();
            int supply=kind=="Ground"?gnd:kind=="Power"?vcc:c["value"].get<unsigned>()?vcc:gnd;
            if(netIds.count(number)&&netIds[number]!=supply)throw std::runtime_error("电源轨冲突");netIds[number]=supply;
        }
    }
    for(const auto &n:logical["nets"]) {int id=n["id"].get<int>();if(!netIds.count(id)){netIds[id]=next;netNames[next++]=n["name"].get<std::string>();}}
    std::vector<Physical> physical;int chip=0,connector=1;
    physical.push_back({"J1","POWER_5V",headerFootprint,"power",2,{{1,vcc},{2,gnd}}});
    std::map<std::string,size_t> lastPackage;
    for(const auto &c:logical["components"]) {
        auto kind=c["kind"].get<std::string>(),source=c["path"].get<std::string>();
        if(kind=="Text"||kind=="Probe"||kind=="Constant"||kind=="Ground"||kind=="Power")continue;
        if(c["width"].get<int>()!=1)throw std::runtime_error("PCB实体映射不支持总线器件："+source+" / "+kind);
        auto params=c["params"];auto pin=[&](const std::string &id) {for(const auto &p:c["pins"])if(p["id"]==id)return netIds.at(p["net"].get<int>());throw std::runtime_error("缺少实体映射端口："+id);};
        if(kind=="Input"||kind=="Output") {
            physical.push_back({"J"+std::to_string(++connector),c["label"].get<std::string>(),headerFootprint,source,2,{{1,pin(kind=="Input"?"Y":"A")},{2,gnd}}});continue;
        }
        const auto spec=gateChips.find(kind);
        if(spec==gateChips.end()||params.value("inputs",2)!=2||params.value("invertMask",0)!=0)
            throw std::runtime_error("该器件或属性尚无已核对的实体引脚映射："+source+" / "+kind+"；请使用单比特二输入AND/OR/NAND/XOR或NOT演示");
        const auto &package=spec->second;
        const auto last=lastPackage.find(kind);
        if(last==lastPackage.end()||physical[last->second].gates.size()==package.units.size()) {
            const auto number=std::to_string(++chip);
            Physical ic{"U"+number,package.value,dipFootprint,source,14,{{14,vcc},{7,gnd}},package.symbol};
            // 未使用的 CMOS 输入接地；输出保持无网。被分配的单元再写入实际连接。
            for(const auto &unit:package.units) {
                ic.nets[unit.a]=gnd;
                if(unit.b)ic.nets[unit.b]=gnd;
            }
            lastPackage[kind]=physical.size();
            physical.push_back(std::move(ic));
            physical.push_back({"C"+number,"100nF",capFootprint,source+"/decoupling",2,{{1,vcc},{2,gnd}}});
        }
        auto &ic=physical[lastPackage.at(kind)];
        const auto pins=package.units.at(ic.gates.size());
        ic.nets[pins.a]=pin("A");
        if(pins.b)ic.nets[pins.b]=pin("B");
        ic.nets[pins.y]=pin("Y");
        ic.gates.push_back({source,pins});
    }
    if(!chip)throw std::runtime_error("实体映射示例至少需要一个逻辑门");
    if(physical.size()>500)throw std::runtime_error("教学PCB实体映射最多500个物理元件");
    return {std::move(physical),std::move(netNames)};
}

std::string componentStamp(size_t index) {
    std::ostringstream stamp;
    stamp<<std::hex<<std::setw(8)<<std::setfill('0')<<index+100;
    return stamp.str();
}

template<class Visit>
void visitNetMembers(const PhysicalNetlist &data,int netId,Visit visit) {
    for(const auto &p:data.components)
        for(const auto &[pad,id]:p.nets)
            if(id==netId)visit(p.ref,pad);
}

std::string writeKiCadNetlist(const PhysicalNetlist &data) {
    std::ostringstream net;
    net<<"(export (version E) (design (source \"PracticeEDA\") (tool \"PracticeEDA 0.5 physical mapping\"))\n (components\n";
    for(size_t index=0;index<data.components.size();++index) {
        const auto &p=data.components[index];
        net<<"  (comp (ref "<<quoted(p.ref)<<") (value "<<quoted(p.value)<<") (footprint "<<quoted(p.footprint)
           <<") (tstamp "<<quoted(componentStamp(index))<<"))\n";
    }
    net<<" )\n (nets\n";
    for(const auto &[id,name]:data.netNames) {
        net<<"  (net (code "<<id<<") (name "<<quoted(name)<<")";
        visitNetMembers(data,id,[&](const std::string &ref,int pad) {
            net<<" (node (ref "<<quoted(ref)<<") (pin "<<quoted(std::to_string(pad))<<"))";
        });
        net<<")\n";
    }
    net<<" ))\n";
    return net.str();
}

std::string writeOrCadPcb2Netlist(const PhysicalNetlist &data) {
    std::ostringstream net;
    net<<"( { OrCAD/PCB II Netlist Format - PracticeEDA }\n";
    for(size_t index=0;index<data.components.size();++index) {
        const auto &p=data.components[index];
        // 连接器的显示值来自用户标签。保留 UTF-8，将空白和分隔符替换为下划线。
        auto value=p.value.empty()?p.ref:p.value;
        for(char &c:value) {
            if(static_cast<unsigned char>(c)<=0x20||c==0x7f||std::string_view("(){}*\"\\").find(c)!=std::string_view::npos)
                c='_';
        }
        // OrCAD PCB II uses a footprint name without the KiCad library nickname.
        std::string_view footprint=p.footprint;
        if(const auto colon=footprint.find(':');colon!=std::string_view::npos)
            footprint.remove_prefix(colon+1);
        net<<" ( "<<componentStamp(index)<<' '<<footprint<<' '<<p.ref<<' '<<value<<'\n';
        for(int pad=1;pad<=p.pads;++pad) {
            const auto connected=p.nets.find(pad);
            net<<"  ( "<<std::setw(4)<<pad<<' '
               <<(connected==p.nets.end()?"?":data.netNames.at(connected->second))<<" )\n";
        }
        net<<" )\n";
    }
    net<<")\n*\n";
    return net.str();
}

// Read the top-level fields of the embedded library footprint; nested geometry stays verbatim.
std::vector<std::string_view> footprintFields(std::string_view libraryFootprint) {
    std::vector<std::string_view> fields;
    size_t start=0;
    int depth=0;
    bool inString=false;
    for(size_t i=0;i<libraryFootprint.size();++i) {
        const char c=libraryFootprint[i];
        if(inString&&c=='\\') { ++i;continue; }
        if(c=='"')inString=!inString;
        if(inString)continue;
        if(c=='('&&depth++==1)start=i;
        if(c==')'&&--depth==1)fields.push_back(libraryFootprint.substr(start,i-start+1));
    }
    if(depth!=0||inString||fields.empty())throw std::logic_error("KiCad 标准封装数据无效");
    return fields;
}

void writeBoardFootprint(std::ostream &out,const Physical &p,const std::map<int,std::string> &netNames,double x,double y) {
    static const std::map<std::string,std::vector<std::string_view>> footprints{
        {dipFootprint,footprintFields(stock::dip)},
        {capFootprint,footprintFields(stock::capacitor)},
        {headerFootprint,footprintFields(stock::header)},
    };
    out<<" (footprint "<<quoted(p.footprint)<<" (layer \"F.Cu\") (at "<<x<<' '<<y<<")\n";
    std::set<int> pads;
    for(const auto field:footprints.at(p.footprint)) {
        const auto starts=[&](std::string_view prefix) {return field.substr(0,prefix.size())==prefix;};
        if(starts("(version ")||starts("(generator ")||starts("(generator_version ")||starts("(layer "))
            continue;
        out<<"  ";
        constexpr std::string_view refPrefix="(property \"Reference\" ";
        constexpr std::string_view valuePrefix="(property \"Value\" ";
        if(starts(refPrefix)||starts(valuePrefix)) {
            const bool reference=starts(refPrefix);
            const auto prefix=reference?refPrefix:valuePrefix;
            out<<prefix<<quoted(reference?p.ref:p.value)<<field.substr(field.find('"',prefix.size()+1)+1);
        } else if(starts("(pad \"")) {
            const auto number=std::stoi(std::string(field.substr(6,field.find('"',6)-6)));
            if(number<1||number>p.pads||!pads.insert(number).second)
                throw std::logic_error("KiCad 标准封装的焊盘编号不匹配："+p.footprint);
            const auto net=p.nets.find(number);
            out<<field.substr(0,field.size()-1);
            if(net!=p.nets.end())out<<" (net "<<quoted(netNames.at(net->second))<<")";
            out<<')';
        } else {
            out<<field;
        }
        out<<'\n';
    }
    if(pads.size()!=static_cast<size_t>(p.pads))
        throw std::logic_error("KiCad 标准封装的焊盘数量不匹配："+p.footprint);
    out<<" )\n";
}
}

std::string exportPcbNetlist(const Project &project,const std::string &circuit,PcbNetlistFormat format) {
    const auto data=buildPhysicalNetlist(project,circuit);
    switch(format) {
    case PcbNetlistFormat::KiCad: return writeKiCadNetlist(data);
    case PcbNetlistFormat::OrCadPcb2: return writeOrCadPcb2Netlist(data);
    }
    throw std::invalid_argument("未知的 PCB 网表格式");
}

PcbExchange exportPcb(const Project &project,const std::string &circuit) {
    const auto data=buildPhysicalNetlist(project,circuit);
    const auto &[physical,netNames]=data;
    std::ostringstream board;
    board<<"(kicad_pcb (version 20260206) (generator \"PracticeEDA\")\n (general (thickness 1.6)) (paper \"A4\")\n"
         <<" (layers (0 \"F.Cu\" signal) (2 \"B.Cu\" signal) (5 \"F.SilkS\" user \"F.Silkscreen\") (7 \"B.SilkS\" user \"B.Silkscreen\")"
         <<" (1 \"F.Mask\" user) (3 \"B.Mask\" user) (25 \"Edge.Cuts\" user) (31 \"F.CrtYd\" user \"F.Courtyard\") (35 \"F.Fab\" user))\n"
         <<" (setup (pad_to_mask_clearance 0))\n";
    Json manifest={{"format","PracticeEDA.physical-mapping"},{"version",1},{"sourceCircuit",circuit},{"routed",false},{"components",Json::array()},{"nets",Json::array()}};
    for(size_t index=0;index<physical.size();++index) {
        const auto &p=physical[index];double x=30+30*double(index%6),y=30+30*double(index/6);
        writeBoardFootprint(board,p,netNames,x,y);
        Json pads=Json::array();
        for(const auto &[pad,id]:p.nets)pads.push_back({{"pad",pad},{"net",id},{"name",netNames.at(id)}});
        Json component={{"ref",p.ref},{"value",p.value},{"footprint",p.footprint},{"path",p.path},{"pads",pads}};
        if(!p.gates.empty()) {
            component["symbol"]=p.symbol;
            component["units"]=Json::array();
            for(size_t unit=0;unit<p.gates.size();++unit) {
                const auto &gate=p.gates[unit];
                Json pins={{"A",gate.pins.a},{"Y",gate.pins.y}};
                if(gate.pins.b)pins["B"]=gate.pins.b;
                component["units"].push_back({{"unit",std::string(1,static_cast<char>('A'+unit))},{"path",gate.path},{"pins",pins}});
            }
        }
        manifest["components"].push_back(std::move(component));
    }
    for(const auto &[id,name]:netNames) {
        Json members=Json::array();
        visitNetMembers(data,id,[&](const std::string &ref,int pad) { members.push_back({{"ref",ref},{"pad",pad}}); });
        manifest["nets"].push_back({{"id",id},{"name",name},{"members",members}});
    }
    double height=45+30*double((physical.size()+5)/6);board<<" (gr_rect (start 20 20) (end 200 "<<height<<") (stroke (width 0.1) (type default)) (fill no) (layer \"Edge.Cuts\"))\n)\n";
    return {writeKiCadNetlist(data),board.str(),manifest.dump(2)};
}
}
