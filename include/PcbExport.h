#pragma once
#include "Digital.h"
namespace eda::logic {
struct PcbExchange { std::string netlist, board, manifest; };
// 教学用实体映射：单比特二输入门/非门映射74HC DIP，每个逻辑门单独使用一片芯片。
// 自动补电源、去耦和连接器；未布线。未有明确实体映射的器件必须拒绝导出。
PcbExchange exportPcb(const Project &, const std::string &circuit);
}
