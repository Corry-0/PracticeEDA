#include "LogicAnalysis.h"
#include <iostream>
#include <random>

using namespace eda::logic;
namespace {
unsigned checks = 0;
void check(bool value, const std::string &message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
Table functionTable(unsigned inputs, uint32_t function) {
    Table t;
    for (unsigned i = 0; i < inputs; ++i) t.inputs.push_back("I" + std::to_string(i));
    t.outputs = {"F"};
    for (uint32_t value = 0; value < (uint32_t(1) << inputs); ++value) {
        std::vector<int> row;
        for (unsigned i = 0; i < inputs; ++i) row.push_back((value >> i) & 1);
        row.push_back((function >> value) & 1); t.rows.push_back(row);
    }
    return t;
}
void equivalent(const Table &t, bool nandOnly) {
    auto p = synthesize(t,"generated",nandOnly);
    Simulator s(p,p.main);
    for (const auto &row : t.rows) {
        s.reset();
        const auto &c = p.circuit(p.main);
        for (size_t i = 0; i < t.inputs.size(); ++i) {
            auto found = std::find_if(c.parts.begin(),c.parts.end(),[&](const Part &part) {return part.kind == "Input" && part.label == t.inputs[i];});
            s.setInput(found->id,uint32_t(row[i]));
        }
        check(s.settle(),"generated circuit converges");
        for (size_t i = 0; i < t.outputs.size(); ++i) {
            auto found = std::find_if(c.parts.begin(),c.parts.end(),[&](const Part &part) {return part.kind == "Output" && part.label == t.outputs[i];});
            auto signal = s.read({found->id,"A"});
            check(signal.defined(),"generated output defined");
            if (row[t.inputs.size()+i] >= 0) check(int(signal.value) == row[t.inputs.size()+i],"independent generated truth verification");
        }
    }
    if (nandOnly)
        for (const auto &part : p.circuit(p.main).parts)
            check(part.kind == "Nand" || part.kind == "Input" || part.kind == "Output" || part.kind == "Constant","NAND-only synthesis uses NAND primitives");
    check(serialize(deserialize(serialize(p))) == serialize(p),"generated circuit serialization");
}
}
int main() {
    try {
        for (uint32_t f = 0; f < 16; ++f) {
            auto t = functionTable(2,f);
            check(minimizeOutput(t,0).exact,"all two-input functions exactly minimized");
            equivalent(t,false); equivalent(t,true);
        }
        std::mt19937 random(20261008);
        for (int i = 0; i < 48; ++i) equivalent(functionTable(4,random() & 0xffff),i % 2 != 0);
        auto t = functionTable(3,0xa0);
        t.rows[0].back() = -1; t.rows[2].back() = -1;
        equivalent(t,false);
        check(karnaughReport(t,0).find("圈组") != std::string::npos,"Karnaugh group report");
        check(analysisReport(t).find("F =") != std::string::npos,"minimized report");
        auto csv = parseTruthCsv("IN:A,IN:B,OUT:F\n0,0,0\n0,1,1\n1,0,1\n1,1,-\n");
        equivalent(csv,false);
        auto roundTrip = parseTruthCsv(csv.csv());
        check(roundTrip.inputs == csv.inputs && roundTrip.outputs == csv.outputs && roundTrip.rows == csv.rows,
              "exported CSV can be imported without guessing input/output columns");
        csv.inputs[0] = "A,bit"; csv.outputs[0] = "F\"quoted";
        roundTrip = parseTruthCsv("\xEF\xBB\xBF" + csv.csv());
        check(roundTrip.inputs == csv.inputs && roundTrip.outputs == csv.outputs && roundTrip.rows == csv.rows,
              "CSV names with comma/quote and UTF-8 BOM round trip");
        bool refused = false;
        try { auto invalid = csv; invalid.rows[1] = invalid.rows[0]; validateTable(invalid); }
        catch (const std::exception &) { refused = true; }
        check(refused,"duplicate truth row rejected");
        refused = false;
        try { auto invalid = csv; invalid.rows[0][0] = -1; validateTable(invalid); }
        catch (const std::exception &) { refused = true; }
        check(refused,"don't-care input rejected");
        equivalent(functionTable(0,0),false); equivalent(functionTable(0,1),true);
        std::cout << checks << " analysis checks passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
