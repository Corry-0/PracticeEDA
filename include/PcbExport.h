#pragma once
#include "Digital.h"
namespace eda::logic {
struct PcbExchange { std::string netlist, board, manifest; };
enum class PcbNetlistFormat { KiCad, OrCadPcb2 };
// 单比特二输入门/非门映射到标准 74HC DIP-14，同型号逻辑门共用芯片中的不同单元。
// 自动补电源、去耦和连接器；未布线。未有明确实体映射的器件必须拒绝导出。
PcbExchange exportPcb(const Project &, const std::string &circuit);
std::string exportPcbNetlist(const Project &, const std::string &circuit, PcbNetlistFormat);
}
