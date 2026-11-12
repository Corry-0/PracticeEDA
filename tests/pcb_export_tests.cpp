#include "PcbExport.h"
#include "json.hpp"
#include <iostream>
#include <regex>
#include <sstream>
using namespace eda;using namespace eda::logic;

namespace {
void verifyOrCadPads(const std::string &text,const nlohmann::json &manifest) {
    const std::regex part(R"(\s*\(\s*([[:xdigit:]]{8})\s+(\S+)\s+(\S+)\s+(\S+))");
    const std::regex pin(R"(\s*\(\s*(\d+)\s+(\S+)\s+\))");
    const std::map<std::string,std::string> orcadNames{
        {"Package_DIP:DIP-14_W7.62mm","DIP-14_W7.62mm"},
        {"Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm","C_Disc_D5.0mm_W2.5mm_P5.00mm"},
        {"Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical","PinHeader_1x02_P2.54mm_Vertical"},
    };
    std::map<std::string,std::string> footprints;
    std::map<std::string,std::map<int,std::string>> expected;
    for(const auto &c:manifest["components"]) {
        const auto ref=c["ref"].get<std::string>();
        footprints[ref]=orcadNames.at(c["footprint"].get<std::string>());
        for(int pad=1;pad<=(ref.front()=='U'?14:2);++pad)expected[ref][pad]="?";
        for(const auto &p:c["pads"])
            expected[ref].at(p["pad"].get<int>())=p["name"].get<std::string>();
    }
    std::istringstream input(text);
    std::string line,ref;
    std::smatch match;
    std::set<std::string> stamps;
    std::map<std::string,std::map<int,std::string>> actual;
    if(!std::getline(input,line)||line.rfind("( { OrCAD/PCB II Netlist Format",0)!=0||line.back()!='}')
        throw std::runtime_error("OrCAD header missing");
    while(std::getline(input,line)&&line!=")") {
        if(ref.empty()&&std::regex_match(line,match,part)) {
            ref=match[3].str();
            if(actual.count(ref)||!stamps.insert(match[1].str()).second||footprints.at(ref)!=match[2].str())
                throw std::runtime_error("OrCAD component reference, ID or footprint differs");
            actual[ref]={};
        } else if(!ref.empty()&&line==" )") {
            ref.clear();
        } else if(!ref.empty()&&std::regex_match(line,match,pin)) {
            if(!actual.at(ref).emplace(std::stoi(match[1].str()),match[2].str()).second)
                throw std::runtime_error("Duplicate OrCAD pin");
        } else {
            throw std::runtime_error("Invalid OrCAD record: "+line);
        }
    }
    if(!ref.empty()||line!=")"||!std::getline(input,line)||line!="*"||std::getline(input,line))
        throw std::runtime_error("Invalid OrCAD end of file");
    if(actual!=expected)throw std::runtime_error("OrCAD physical pin connections differ from KiCad mapping");
}

void saveExchange(const std::filesystem::path &folder,const std::string &name,const PcbExchange &exchange) {
    writeAtomic(folder/(name+".net"),exchange.netlist);
    writeAtomic(folder/(name+".kicad_pcb"),exchange.board);
    writeAtomic(folder/(name+".mapping.json"),exchange.manifest);
}

// Exercise both package capacities and interleaved gate types, with a distinct net for every output.
Project packedGates() {
    Project p;
    p.name="Packed gates";
    auto &c=p.circuits[0];
    auto add=[&](std::string id,const char *kind,Point at) {
        Part part;
        part.id=id;part.label=id;part.kind=kind;part.at=at;
        c.parts.push_back(std::move(part));
        return id;
    };
    auto wire=[&](Endpoint from,Endpoint to) {c.wires.push_back({p.id("w"),std::move(from),std::move(to),{}});};
    add("input_a","Input",{100,100});add("input_b","Input",{100,200});
    for(int i=1;i<=7;++i) {
        const auto addGate=[&](const char *kind) {
            const auto gate=add(kind+std::to_string(i),kind,{300,80.0*c.parts.size()});
            const auto out=add("out_"+gate,"Output",{500,80.0*(c.parts.size()-1)});
            wire({"input_a","Y"},{gate,"A"});
            if(gate.rfind("And",0)==0)wire({"input_b","Y"},{gate,"B"});
            wire({gate,"Y"},{out,"A"});
        };
        if(i<=5)addGate("And");
        addGate("Not");
    }
    return p;
}

void verifyPacking(const nlohmann::json &manifest) {
    using Json=nlohmann::json;
    std::map<std::string,std::map<int,std::string>> pads;
    std::map<std::string,Json> chips;
    std::map<std::string,std::string> signals;
    size_t caps=0;
    for(const auto &c:manifest["components"]) {
        const auto ref=c["ref"].get<std::string>();
        for(const auto &p:c["pads"])pads[ref][p["pad"].get<int>()]=p["name"].get<std::string>();
        if(ref.front()=='U')chips[ref]=c;
        if(ref.front()=='C') {
            ++caps;
            if(pads.at(ref)!=std::map<int,std::string>{{1,"VCC"},{2,"GND"}})
                throw std::runtime_error("Packed chip decoupling is not on the supply rails");
        }
        if(ref.front()=='J')signals[c["path"].get<std::string>()]=pads.at(ref).at(1);
    }
    if(chips.size()!=4||caps!=4)throw std::runtime_error("5 AND and 7 NOT gates must use two packages each");

    const std::vector<Json> quad{{{"A",1},{"B",2},{"Y",3}},{{"A",4},{"B",5},{"Y",6}},
                                 {{"A",9},{"B",10},{"Y",8}},{{"A",12},{"B",13},{"Y",11}}};
    const std::vector<Json> hex{{{"A",1},{"Y",2}},{{"A",3},{"Y",4}},{{"A",5},{"Y",6}},
                                {{"A",9},{"Y",8}},{{"A",11},{"Y",10}},{{"A",13},{"Y",12}}};
    auto verify=[&](const std::string &ref,const std::string &kind,const char *value,const char *symbol,
                    const std::vector<Json> &pinout,size_t used,int first) {
        const auto &chip=chips.at(ref);
        if(chip["value"]!=value||chip["symbol"]!=symbol||chip["footprint"]!="Package_DIP:DIP-14_W7.62mm"||
           chip["units"].size()!=used||pads.at(ref).at(7)!="GND"||pads.at(ref).at(14)!="VCC")
            throw std::runtime_error("Incorrect packed chip metadata or supply wiring: "+ref);
        for(size_t i=0;i<pinout.size();++i) {
            const auto path=kind+std::to_string(first+i);
            if(i<used) {
                const auto &unit=chip["units"][i];
                if(unit["path"]!=path||unit["unit"]!=std::string(1,static_cast<char>('A'+i))||unit["pins"]!=pinout[i])
                    throw std::runtime_error("Incorrect KiCad unit assignment: "+path);
            }
            for(const auto &[port,number]:pinout[i].items()) {
                const auto actual=pads.at(ref).find(number.get<int>());
                if(i>=used&&port=="Y") {
                    if(actual!=pads.at(ref).end())throw std::runtime_error("Unused gate output wired");
                } else {
                    const auto expected=i>=used?"GND":signals.at(port=="A"?"input_a":port=="B"?"input_b":"out_"+path);
                    if(actual==pads.at(ref).end()||actual->second!=expected)
                        throw std::runtime_error("Incorrect packed gate connection: "+path+"."+port);
                }
            }
        }
    };
    verify("U1","And","74HC08","74xx:74LS08",quad,4,1);
    verify("U2","Not","74HC04","74xx:74HC04",hex,6,1);
    verify("U3","And","74HC08","74xx:74LS08",quad,1,5);
    verify("U4","Not","74HC04","74xx:74HC04",hex,1,7);
}
}

int main(int argc,char **argv){try{
    auto p=halfAdder();auto before=serialize(p);auto exchange=exportPcb(p,"main");
    if(serialize(p)!=before)throw std::runtime_error("PCB export modified design");
    auto map=nlohmann::json::parse(exchange.manifest);
    if(map["components"].size()!=9||map["nets"].size()!=6)throw std::runtime_error("Physical component/net counts differ");
    int chips=0,caps=0;
    for(const auto &c:map["components"]){auto ref=c["ref"].get<std::string>();if(ref[0]=='U'){
        ++chips;bool power=false,ground=false;for(const auto &pad:c["pads"]){power|=pad["pad"]==14&&pad["name"]=="VCC";ground|=pad["pad"]==7&&pad["name"]=="GND";}
        if(!power||!ground)throw std::runtime_error("IC power pins missing");
    }else if(ref[0]=='C')++caps;}
    if(chips!=2||caps!=2)throw std::runtime_error("Half adder needs separate AND and XOR packages");
    if(exportPcbNetlist(p,"main",PcbNetlistFormat::KiCad)!=exchange.netlist)
        throw std::runtime_error("Standalone KiCad netlist differs from companion file");
    const auto orcad=exportPcbNetlist(p,"main",PcbNetlistFormat::OrCadPcb2);
    verifyOrCadPads(orcad,map);
    auto labelled=p;
    for(auto &part:labelled.circuits[0].parts)if(part.kind=="Input"){part.label="输入 A(test)\t";break;}
    const auto labelledOrCad=exportPcbNetlist(labelled,"main",PcbNetlistFormat::OrCadPcb2);
    verifyOrCadPads(labelledOrCad,map);
    if(labelledOrCad.find("输入_A_test__")==std::string::npos||serialize(p)!=before)
        throw std::runtime_error("Netlist did not preserve syntax or design state");
    auto invalid=p;invalid.circuits[0].parts[0].width=8;bool refused=false;
    try{exportPcb(invalid,"main");}catch(const std::exception &){refused=true;}
    if(!refused)throw std::runtime_error("Unmapped bus was not rejected");
    const auto packed=packedGates();
    const auto packedExchange=exportPcb(packed,"main");
    const auto packedMap=nlohmann::json::parse(packedExchange.manifest);
    const auto packedOrCad=exportPcbNetlist(packed,"main",PcbNetlistFormat::OrCadPcb2);
    verifyPacking(packedMap);
    verifyOrCadPads(packedOrCad,packedMap);
    if(argc>1){auto folder=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(folder);
        writeAtomic(folder/"half-adder-orcad.net",orcad);
        saveExchange(folder,"half-adder-physical",exchange);
        saveExchange(folder,"packed-gates",packedExchange);
        writeAtomic(folder/"packed-gates-orcad.net",packedOrCad);
        writeAtomic(folder/"packed-gates.logic.json",serialize(packed));}
    std::cout<<"PCB mapping: standard pinouts, gate packing, spillover and both netlist formats passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
