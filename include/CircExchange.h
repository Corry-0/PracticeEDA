#pragma once
#include "Digital.h"

namespace eda::logic {
// 导出真实 Logisim 2.7.1 XML：内置元件、层次定义和 Tunnel 网络均可由 Logisim 直接读取。
// 两套编辑器的符号尺寸不同，因此导出重排布局；无法保持运行语义的器件明确报错。
std::string exportCirc(const Project &);
// 经典文件仅保存设计，输入交互值/时钟相位会复位；调用方应在导出时展示这些说明。
std::vector<std::string> circExportWarnings(const Project &);

namespace detail {
// 导入与导出共用的 Logisim 器件几何描述，避免两套独立端口公式逐渐偏离。
struct CircDevice {
    Part part;
    std::vector<std::pair<Point, std::string>> pins;
    std::vector<Point> unsupportedPins;
};
CircDevice circDevice(const std::string &name, const std::string &library,
                      const std::map<std::string, std::string> &attributes);
} // namespace detail
} // namespace eda::logic
