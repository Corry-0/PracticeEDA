#pragma once
#include "Digital.h"
namespace eda::logic {
struct PcbExchange { std::string netlist, board, manifest; };
enum class PcbNetlistFormat { KiCad, OrCadPcb2 };
// 同型号逻辑门共用 74HC DIP-14 芯片，顶层输入输出映射到连接器。
// 电源和常量通过网络表示。输出未布线，无实体映射的器件拒绝导出。
PcbExchange exportPcb(const Project &, const std::string &circuit);
std::string exportPcbNetlist(const Project &, const std::string &circuit, PcbNetlistFormat);
}
