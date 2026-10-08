#pragma once
#include "Digital.h"

namespace eda::logic {
// 不关心项只存在于设计真值表（输出为 -1），不等同于运行仿真的未知 X。
struct LogicCube {
    uint32_t bits = 0, dontCare = 0;
};
struct Minimization {
    std::vector<LogicCube> terms;
    std::string expression;
    bool exact = true; // 大规模覆盖搜索达到预算时仍返回等价表达式，但不声称最少项。
};
void validateTable(const Table &);
Minimization minimizeOutput(const Table &, size_t output);
std::vector<std::string> minimizedExpressions(const Table &);
std::string karnaughReport(const Table &, size_t output);
std::string analysisReport(const Table &);
Table parseTruthCsv(const std::string &);
// 生成独立工程；调用方可把生成电路作为自定义元件导入，避免覆盖用户原电路。
Project synthesize(const Table &, const std::string &circuitName = "generated", bool nandOnly = false);
} // namespace eda::logic
