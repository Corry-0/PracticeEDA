#include "LogicAnalysis.h"
#include <limits>
#include <sstream>
#include <unordered_set>

namespace eda::logic {
namespace {
unsigned ones(uint32_t n) {
    unsigned result = 0;
    for (; n; n &= n - 1) ++result;
    return result;
}
uint32_t inputCode(const Table &t, const std::vector<int> &row) {
    uint32_t code = 0;
    for (size_t i = 0; i < t.inputs.size(); ++i) code |= uint32_t(row[i]) << i;
    return code;
}
bool covers(const LogicCube &c, uint32_t m) { return ((c.bits ^ m) & ~c.dontCare) == 0; }
uint64_t cubeKey(const LogicCube &c) { return uint64_t(c.dontCare) << 32 | c.bits; }
std::string formatTerms(const Table &t, const std::vector<LogicCube> &terms) {
    if (terms.empty()) return "0";
    std::ostringstream result;
    for (size_t j = 0; j < terms.size(); ++j) {
        if (j) result << " | ";
        std::string product;
        for (size_t i = 0; i < t.inputs.size(); ++i) {
            if (terms[j].dontCare & (uint32_t(1) << i)) continue;
            if (!product.empty()) product += " & ";
            if (!(terms[j].bits & (uint32_t(1) << i))) product += '!';
            product += t.inputs[i];
        }
        if (product.empty()) return "1";
        result << '(' << product << ')';
    }
    return result.str();
}
std::string trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
std::vector<std::string> csvFields(const std::string &line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
            else quoted = !quoted;
        } else if (line[i] == ',' && !quoted) { fields.push_back(trim(field)); field.clear(); }
        else field += line[i];
    }
    if (quoted) throw std::runtime_error("CSV 引号没有闭合");
    fields.push_back(trim(field));
    return fields;
}
} // namespace

void validateTable(const Table &t) {
    if (t.inputs.size() > 10 || t.outputs.empty() || t.outputs.size() > 128)
        throw std::runtime_error("真值表需 0–10 位输入及 1–128 位输出");
    std::set<std::string> labels;
    for (const auto &name : t.inputs)
        if (name.empty() || name != trim(name) || name.find_first_of("\r\n") != std::string::npos || !labels.insert(name).second)
            throw std::runtime_error("输入名称为空、重复，或包含换行及首尾空白");
    for (const auto &name : t.outputs)
        if (name.empty() || name != trim(name) || name.find_first_of("\r\n") != std::string::npos || !labels.insert(name).second)
            throw std::runtime_error("输出名称为空、重名，或包含换行及首尾空白");
    if (t.rows.size() != (size_t(1) << t.inputs.size()))
        throw std::runtime_error("真值表必须完整列出每一种输入组合");
    std::set<uint32_t> codes;
    for (const auto &row : t.rows) {
        if (row.size() != t.inputs.size() + t.outputs.size()) throw std::runtime_error("真值表列数不一致");
        for (size_t i = 0; i < row.size(); ++i)
            if (row[i] != 0 && row[i] != 1 && !(i >= t.inputs.size() && row[i] == -1))
                throw std::runtime_error("输入只能为 0/1；输出可以为 0/1/-（不关心项）");
        if (!codes.insert(inputCode(t, row)).second) throw std::runtime_error("真值表含重复的输入组合");
    }
}

Minimization minimizeOutput(const Table &t, size_t output) {
    validateTable(t);
    if (output >= t.outputs.size()) throw std::runtime_error("输出列不存在");
    std::set<uint32_t> required;
    std::vector<LogicCube> layer, primes;
    for (const auto &row : t.rows) {
        const int v = row[t.inputs.size() + output];
        if (v == 1) required.insert(inputCode(t, row));
        if (v != 0) layer.push_back({inputCode(t, row), 0});
    }
    Minimization result;
    if (required.empty()) { result.expression = "0"; return result; }
    // Quine–McCluskey：仅合并掩码相同、且恰有一个确定比特不同的项。
    // 每轮去重，避免不同合并路径产生指数级重复；不关心项可扩圈，但不要求覆盖。
    while (!layer.empty()) {
        std::vector<bool> used(layer.size(), false);
        std::vector<LogicCube> next;
        std::unordered_set<uint64_t> unique;
        for (size_t i = 0; i < layer.size(); ++i)
            for (size_t j = i + 1; j < layer.size(); ++j) {
                if (layer[i].dontCare != layer[j].dontCare) continue;
                const uint32_t difference = layer[i].bits ^ layer[j].bits;
                if (!difference || (difference & (difference - 1))) continue;
                LogicCube merged{layer[i].bits & ~difference, layer[i].dontCare | difference};
                used[i] = used[j] = true;
                if (unique.insert(cubeKey(merged)).second) next.push_back(merged);
            }
        for (size_t i = 0; i < layer.size(); ++i)
            if (!used[i] && std::any_of(required.begin(), required.end(), [&](uint32_t m) { return covers(layer[i], m); }))
                primes.push_back(layer[i]);
        if (next.size() > 32768) throw std::runtime_error("化简中间项过多，请减少输入变量或拆分输出");
        layer = std::move(next);
    }
    std::sort(primes.begin(), primes.end(), [](const LogicCube &a, const LogicCube &b) { return cubeKey(a) < cubeKey(b); });
    primes.erase(std::unique(primes.begin(), primes.end(), [](const LogicCube &a, const LogicCube &b) { return cubeKey(a) == cubeKey(b); }), primes.end());
    std::vector<size_t> selected;
    std::set<uint32_t> remaining = required;
    for (uint32_t minterm : required) {
        size_t count = 0, only = 0;
        for (size_t p = 0; p < primes.size(); ++p) if (covers(primes[p], minterm)) { ++count; only = p; }
        if (count == 1 && std::find(selected.begin(), selected.end(), only) == selected.end()) selected.push_back(only);
    }
    auto removeCovered = [&](std::set<uint32_t> &left, size_t index) {
        for (auto it = left.begin(); it != left.end();) {
            if (covers(primes[index], *it)) it = left.erase(it); else ++it;
        }
    };
    for (auto index : selected) removeCovered(remaining, index);
    std::vector<size_t> best;
    auto left = remaining;
    // 先建立有效的贪心上界；即使搜索预算耗尽，也已有完整且等价的覆盖结果。
    while (!left.empty()) {
        size_t choice = 0, most = 0;
        for (size_t p = 0; p < primes.size(); ++p) {
            size_t count = size_t(std::count_if(left.begin(), left.end(), [&](uint32_t m) { return covers(primes[p], m); }));
            if (count > most) { most = count; choice = p; }
        }
        if (!most) throw std::runtime_error("化简无法覆盖目标项");
        best.push_back(choice); removeCovered(left, choice);
    }
    auto literals = [&](const std::vector<size_t> &items) {
        unsigned count = 0;
        for (auto p : items) count += unsigned(t.inputs.size()) - ones(primes[p].dontCare);
        return count;
    };
    size_t visits = 0;
    std::vector<size_t> working;
    std::function<void(const std::set<uint32_t> &)> search = [&](const std::set<uint32_t> &uncovered) {
        if (++visits > 200000) { result.exact = false; return; }
        if (uncovered.empty()) {
            if (working.size() < best.size() || (working.size() == best.size() && literals(working) < literals(best))) best = working;
            return;
        }
        if (working.size() >= best.size()) return;
        std::vector<size_t> choices;
        size_t least = std::numeric_limits<size_t>::max();
        for (auto minterm : uncovered) {
            std::vector<size_t> possible;
            for (size_t p = 0; p < primes.size(); ++p) if (covers(primes[p], minterm)) possible.push_back(p);
            if (possible.size() < least) { least = possible.size(); choices = std::move(possible); }
        }
        std::sort(choices.begin(), choices.end(), [&](size_t a, size_t b) { return ones(primes[a].dontCare) > ones(primes[b].dontCare); });
        for (auto p : choices) {
            auto next = uncovered; removeCovered(next, p);
            working.push_back(p); search(next); working.pop_back();
            if (!result.exact) break;
        }
    };
    if (!remaining.empty()) search(remaining);
    selected.insert(selected.end(), best.begin(), best.end());
    for (auto p : selected) result.terms.push_back(primes[p]);
    result.expression = formatTerms(t, result.terms);
    return result;
}

std::vector<std::string> minimizedExpressions(const Table &t) {
    std::vector<std::string> result;
    for (size_t o = 0; o < t.outputs.size(); ++o) result.push_back(minimizeOutput(t, o).expression);
    return result;
}
std::string karnaughReport(const Table &t, size_t output) {
    const auto minimized = minimizeOutput(t, output);
    std::ostringstream report;
    report << t.outputs[output] << " = " << minimized.expression << "\n";
    if (t.inputs.size() > 4) {
        report << "卡诺图可视化支持最多 4 个变量；当前表达式仍完成化简。\n";
        return report.str();
    }
    const size_t rbits = t.inputs.size() / 2, cbits = t.inputs.size() - rbits;
    auto gray = [](size_t n) { return n ^ (n >> 1); };
    auto binary = [](size_t n, size_t width) {
        std::string s;
        for (size_t i = 0; i < width; ++i) s += ((n >> (width - i - 1)) & 1) ? '1' : '0';
        return s.empty() ? "-" : s;
    };
    report << "行变量：";
    for (size_t i = 0; i < rbits; ++i) report << t.inputs[i] << ' ';
    report << "\n列变量：";
    for (size_t i = rbits; i < t.inputs.size(); ++i) report << t.inputs[i] << ' ';
    report << "\n行/列按格雷码排列；- 表示设计不关心项。\n\t";
    for (size_t c = 0; c < (size_t(1) << cbits); ++c) report << binary(gray(c), cbits) << '\t';
    report << '\n';
    for (size_t r = 0; r < (size_t(1) << rbits); ++r) {
        report << binary(gray(r), rbits) << '\t';
        for (size_t c = 0; c < (size_t(1) << cbits); ++c) {
            uint32_t code = 0;
            for (size_t i = 0; i < rbits; ++i) code |= uint32_t((gray(r) >> (rbits - i - 1)) & 1) << i;
            for (size_t i = 0; i < cbits; ++i) code |= uint32_t((gray(c) >> (cbits - i - 1)) & 1) << (rbits + i);
            auto found = std::find_if(t.rows.begin(), t.rows.end(), [&](const auto &row) { return inputCode(t, row) == code; });
            const int value = (*found)[t.inputs.size() + output];
            report << (value < 0 ? "-" : std::to_string(value)) << '\t';
        }
        report << '\n';
    }
    report << "\n圈组（每组是可跨边界相邻的 2^k 个格）：\n";
    for (size_t i = 0; i < minimized.terms.size(); ++i) {
        report << "组 " << i + 1 << "：" << formatTerms(t, {minimized.terms[i]}) << "，格点 ";
        for (uint32_t n = 0; n < (uint32_t(1) << t.inputs.size()); ++n) if (covers(minimized.terms[i], n)) report << n << ' ';
        report << '\n';
    }
    return report.str();
}
std::string analysisReport(const Table &t) {
    validateTable(t);
    std::ostringstream report;
    report << "化简表达式（! 非，& 与，| 或；- 是设计不关心项）\n";
    for (size_t o = 0; o < t.outputs.size(); ++o) {
        auto result = minimizeOutput(t, o);
        report << t.outputs[o] << " = " << result.expression << '\n';
        if (!result.exact) report << "覆盖搜索达到预算：结果等价，未证明项数最少。\n";
    }
    return report.str();
}

Table parseTruthCsv(const std::string &bytes) {
    std::istringstream stream(bytes.size() >= 3 && bytes.substr(0,3) == "\xEF\xBB\xBF" ? bytes.substr(3) : bytes);
    std::string line;
    Table t;
    bool header = false;
    while (std::getline(stream, line)) {
        if (trim(line).empty()) continue;
        auto fields = csvFields(line);
        if (!header) {
            bool outputs = false;
            for (auto field : fields) {
                if (field == "|" || field == "OUTPUTS") { outputs = true; continue; }
                if (field.rfind("OUT:",0) == 0) { outputs = true; field = field.substr(4); }
                else if (field.rfind("IN:",0) == 0) field = field.substr(3);
                (outputs ? t.outputs : t.inputs).push_back(field);
            }
            if (!outputs) throw std::runtime_error("CSV 表头需以 | 分隔输入与输出，或使用 IN:/OUT: 前缀");
            header = true;
        } else {
            std::vector<int> row;
            for (auto field : fields) {
                if (field == "|") continue;
                if (field == "0" || field == "1") row.push_back(field[0] - '0');
                else if (field == "-" || field == "-1" || field == "x" || field == "X") row.push_back(-1);
                else throw std::runtime_error("CSV 数值只能为 0、1 或 -");
            }
            t.rows.push_back(std::move(row));
        }
    }
    validateTable(t);
    return t;
}

Project synthesize(const Table &t, const std::string &circuitName, bool nandOnly) {
    validateTable(t);
    if (circuitName.empty()) throw std::runtime_error("生成电路名称不能为空");
    Project p;
    p.main = circuitName; p.circuits[0].name = circuitName;
    auto &c = p.circuits[0];
    std::map<std::string, int> depths;
    std::map<int, int> slots;
    std::map<std::string, Endpoint> cache;
    auto add = [&](const std::string &kind, int depth, const std::string &label = "") {
        if (c.parts.size() >= 8000) throw std::runtime_error("生成电路规模过大，请拆分真值表");
        Part part; part.id = p.id("p"); part.kind = kind; part.label = label;
        part.at = {100.0 + depth * 210.0, 100.0 + slots[depth]++ * 120.0};
        c.parts.push_back(part); depths[part.id] = depth;
        return part.id;
    };
    auto connect = [&](const Endpoint &source, const std::string &dest, const std::string &pin) {
        Wire w; w.id = p.id("w"); w.a = source; w.b = {dest, pin}; c.wires.push_back(w);
    };
    std::function<Endpoint(const std::string &, Endpoint, Endpoint)> gate = [&](const std::string &kind, Endpoint a, Endpoint b) {
        if (a.key() > b.key()) std::swap(a,b);
        const std::string key = kind + ":" + a.key() + ":" + b.key();
        auto found = cache.find(key); if (found != cache.end()) return found->second;
        auto id = add(kind, std::max(depths[a.object], depths[b.object]) + 1);
        connect(a,id,"A"); connect(b,id,"B");
        return cache[key] = Endpoint{id,"Y"};
    };
    auto invert = [&](Endpoint a) { return gate("Nand", a, a); };
    auto operation = [&](const std::string &kind, Endpoint a, Endpoint b) {
        if (!nandOnly) return gate(kind,a,b);
        if (kind == "And") return invert(gate("Nand",a,b));
        return gate("Nand",invert(a),invert(b));
    };
    auto combine = [&](std::vector<Endpoint> values, const std::string &kind) {
        while (values.size() > 1) {
            std::vector<Endpoint> next;
            for (size_t i = 0; i < values.size(); i += 2)
                next.push_back(i + 1 < values.size() ? operation(kind,values[i],values[i+1]) : values[i]);
            values = std::move(next);
        }
        return values.front();
    };
    std::vector<Endpoint> inputs;
    for (const auto &name : t.inputs) inputs.push_back({add("Input",0,name),"Y"});
    auto constant = [&](uint32_t value) {
        const auto key = "constant" + std::to_string(value);
        auto found = cache.find(key); if (found != cache.end()) return found->second;
        auto id = add("Constant",0); c.part(id)->value = value; return cache[key] = Endpoint{id,"Y"};
    };
    for (size_t output = 0; output < t.outputs.size(); ++output) {
        const auto minimized = minimizeOutput(t,output);
        std::vector<Endpoint> sum;
        for (const auto &term : minimized.terms) {
            std::vector<Endpoint> factors;
            for (size_t i = 0; i < inputs.size(); ++i) {
                if (term.dontCare & (uint32_t(1) << i)) continue;
                factors.push_back(term.bits & (uint32_t(1) << i) ? inputs[i] : invert(inputs[i]));
            }
            sum.push_back(factors.empty() ? constant(1) : combine(factors,"And"));
        }
        Endpoint source = sum.empty() ? constant(0) : combine(sum,"Or");
        auto id = add("Output",depths[source.object] + 1,t.outputs[output]); connect(source,id,"A");
    }
    p.validate();
    // 从合成网表重新仿真，不复用化简器的计算路径；所有非不关心项都必须逐行匹配。
    const auto verified = truthTable(p,p.main);
    std::map<uint32_t,std::vector<int>> byInput;
    for (const auto &row : verified.rows) byInput[inputCode(verified,row)] = row;
    for (const auto &row : t.rows)
        for (size_t o = 0; o < t.outputs.size(); ++o)
            if (row[t.inputs.size()+o] >= 0 && row[t.inputs.size()+o] != byInput.at(inputCode(t,row))[t.inputs.size()+o])
                throw std::runtime_error("合成电路验证失败，未提交生成结果");
    return p;
}
} // namespace eda::logic
