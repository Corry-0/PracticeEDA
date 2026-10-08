#include "PcbExport.h"
#include "json.hpp"
#include <iostream>
using namespace eda;using namespace eda::logic;
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
    if(chips!=2||caps!=2)throw std::runtime_error("Each gate needs one DIP and decoupling");
    auto invalid=p;invalid.circuits[0].parts[0].width=8;bool refused=false;
    try{exportPcb(invalid,"main");}catch(const std::exception &){refused=true;}
    if(!refused)throw std::runtime_error("Unmapped bus was not rejected");
    if(argc>1){auto folder=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(folder);
        writeAtomic(folder/"half-adder-physical.net",exchange.netlist);writeAtomic(folder/"half-adder-physical.kicad_pcb",exchange.board);writeAtomic(folder/"half-adder-physical.mapping.json",exchange.manifest);}
    std::cout<<"PCB mapping: counts, power pins, decoupling, read-only export and unsupported bus checks passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
