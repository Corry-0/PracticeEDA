"""同步当前源码导读、四人分工和现场演示；不虚构历史作者、测试或开发时间线。"""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/defense'
PPTX = 'PracticeEDA_四人答辩_2026-10-08_正式版.pptx'
slides = json.loads((ROOT/'docs/defense/ppt/slides.json').read_text(encoding='utf-8'))

def write(name, text):
    (OUT/name).write_text(text.strip()+'\n', encoding='utf-8')

write('00_阅读与演示说明.md', f'''
# PracticeEDA 0.5 答辩材料入口

更新日期：2026-10-08。以当前工作区源码及本日 Debug / Release 实际回归为依据。共25页，讲解18分钟、机动2分钟，教师提问另计。

## 先打开哪些文件

|文件|用途|
|---|---|
|[网页演示](ppt/index.html)|离线播放、演讲者备注、计时、观众页同步|
|[可编辑PowerPoint](ppt/{PPTX})|25页，原生文字和表格可修改，每页有讲者备注|
|[源码详细讲解](01_源码详细讲解.md)|由操作现象找到数据、算法和当前源码片段|
|[四人分工与排练](02_四人分工与排练.md)|常人元、闫浩楠、杨曙启、黄歆贻的板块与交接|
|[逐页讲稿](03_答辩逐页讲稿.md)|与两种演示文件同源的讲解词及问答准备|
|[方案对比](04_方案对比与迭代证据.md)|选型理由、代价、可核对证据|
|[现场演示](05_当前进度与现场演示.md)|半加器、寄存器/计数器、RAM、多层实例、综合和交换步骤|
|[证据说明](evidence/说明.md)|本日测试记录、图像、外部软件结果与验证边界|

先运行项目根目录 `dist/PracticeEDA/PracticeEDA.exe`。分发目录内含 examples 和本套材料；源码构建入口为 `scripts/build.ps1`。答辩 ZIP 含材料、精选样例、证据和当前源码快照，保留相对路径以便跳转；程序分发包单独提供。构建源码仍需准备Visual Studio和wxWidgets，启动目标电脑需要 Microsoft Visual C++ x64 运行库。

## 演示操作

网页直接用 Edge / Chrome 打开，无需服务器或联网。左右方向键、PageUp/PageDown、空格翻页；Home/End 到首尾页；Esc 显示索引；B 切换低功耗模式。点击演讲者按钮进入备注、计时和预览界面，再打开观众页。若使用双屏，将观众窗口移动到投影屏、演讲者界面留在电脑屏；软件显示“已连接”只表示页面同步，不证明 HDMI 连接成功。

PowerPoint 使用16:9比例和 Microsoft YaHei 字体。备注包含讲者、计划时长、讲稿、证据入口；检查记录说明了已执行的结构、字体、渲染检查和未执行的原生PowerPoint人工放映。请在答辩电脑提前打开一次，确认投影比例和字体。

四人按架构 → 编辑/综合 → 仿真/实例 → 交换/验证依次讲解，第7、13、19页交接，第25页共同收束。这是本次主讲责任分配，历史代码贡献以真实记录为准。

## 本轮范围和演示重点

约定的 A01—A18 已纳入当前实现，逐项入口和范围见 [验收清单](../OPTIMIZATION_ACCEPTANCE_CN.md)。重点为真实子电路实例：进入 `main/left/inner` 看寄存器和 RAM 为 `0x12`，进入右实例看 `0x34`，导航不换仿真器、不复位。内部按钮只影响当前实例；父层驱动的Input只读；运行RAM内容只读；“编辑定义（复位）”暂停并重新建立根仿真。

保存原生工程记录结构与初值。运行中的寄存器、RAM、键盘队列和当前时刻不是隐式断点快照。化简输入最多10位，卡诺图最多4变量；覆盖搜索耗尽预算时仍返回等价结果，但注明未证明最少项。

`.circ` 对照本地经典 Logisim 2.7.1；33个实际加载与输出样例通过，不表示兼容全部器件/属性。PCB实体映射覆盖单比特二输入 AND/OR/NAND/XOR 和 NOT；半加器配套PCB已由 KiCad 10.0.7 加载，36个已分配焊盘与标准网表一致。尚未自动测试GUI直接导入 `.net`，板文件未布线、未完成制造规则验收。

## 内容维护

`scripts/defense-content.json` 是网页、25页讲稿和PPTX的内容源。`scripts/build_defense.mjs --html-only` 可同步网页与讲稿；生成PPTX需要配置脚本列明的 artifact-tool 运行环境，新的正式文件需使用新的 `DEFENSE_PPTX_NAME`，避免覆盖已经验证的文件。`scripts/update_defense_docs.py` 更新其余说明及问答，`scripts/verify_defense_deck.mjs` 执行离线网页检查。

修改代码后，应先更新截图和测试记录，再更新演示材料。`evidence/2026-09-*` 仅为历史记录，不能作为0.5新增功能的通过证明。
''')

# 每节放真实源码片段，行号按本次文件内容计算，避免沿用旧版本行号。
topics = [
('构建与启动：一个窗口，共用独立核心', 'CMakeLists.txt', 'add_library(logic_core',
 '''CMake把模型、网络、仿真、组合分析与格式转换编入logic_core，把wxWidgets窗口、图标和辅助对话框编入PracticeEDA。测试链接同一个核心库，避免为测试另写一套逻辑。EDA_BUILD_GUI=OFF可配置核心构建；正式桌面交付由Visual Studio 2022 x64生成。

App.cpp创建LogicEditor、注册PNG处理器，按参数加载工程；--smoke-test触发窗口集成检查并把异常转成非零退出码。build.ps1先构建和CTest，再检查本次生成的四种GUI标志，移除旧标志防止旧结果掩盖失败。'''),
('Project、Circuit和Part：设计数据与运行态分开', 'include/Digital.h', 'struct Part {',
 '''Project拥有多个电路定义；Circuit保存Part、Junction、Wire、Shape及interfaceLayout。Part的value/data是复位初值，params是具名整数参数，rotation用0/1/2/3表示四方向。旧version 1工程缺省新字段时保留默认行为。

Part.id和Port.id是连接身份，label只是显示名。同一定义的两个实例共用结构，但Simulator展开出两个Node及各自内存。运行时左实例的RAM写入不会写回共享Part.data，因此右实例和工程初值保持独立。'''),
('端点、端口与旋转：外观变化不能弄断连接', 'src/Digital.cpp', 'std::vector<Port> ports(',
 '''Wire两端引用Endpoint(object,pin)，pin为空表示Junction。导线折点仅决定画法，纯几何交叉不自动连通；建立分支时编辑器显式分割原导线并加入Junction。

ports()按类型、位宽和参数生成端口。多输入门使用A、B、I2等稳定ID；总线分组内第一个位映射到支路最低位。旋转只变换相对坐标，命中、布线和符号绘制读取同一端口结果。子电路端口ID绑定内部Input/Output对象ID，改显示名称和移动接口不会改变身份。'''),
('校验与保存：候选文件先满足模型契约', 'src/Digital.cpp', 'void Project::validate() const',
 '''校验覆盖电路数量、对象唯一ID、有限坐标、元件种类、位宽、参数、端口引用、外观图形和递归依赖等。加载先完成解析与候选校验，再替换编辑器工程，错误文件不能部分覆盖当前工作。

serialize()/deserialize()保存结构、参数、位映射、初值与外观布局。writeAtomic()先写同目录临时文件、检查写入再替换目标。此策略降低部分写入风险，不承诺磁盘损坏或断电情况下绝无数据损失；运行状态不在工程JSON中偷偷持久化。'''),
('一次编辑如何变成可恢复事务', 'src/DigitalEditor.cpp', 'void LogicEditor::change(',
 '''change()是结构编辑公共入口，运行实例scope非空时拒绝结构修改。它备份整个History、当前电路、仿真根及旧Simulator，暂停后执行候选修改，并先验证、编译、收敛候选模型。

成功后才提交撤销记录并替换仿真器。失败时恢复旧History和旧Simulator，保留先前运行状态。History保存全工程快照，因此导入自定义元件涉及多个共享定义时仍能整体撤销。根层普通Input拨动通过运行输入接口更新，不需要重建寄存器/RAM。'''),
('坐标、布线和临时手势', 'include/LogicSupport.h', 'struct View {',
 '''View提供world/screen变换，zoomAt保持鼠标对应的世界点。画布命中基于统一端口位置和屏幕容差。选择、拖动、框选、布线折点、悬停和曲线阶段是临时交互态，不应提前进入工程持久化数据。

用户完成有效手势后才提交事务；Esc取消、零长度绘图和取消布线不生成半成品。图形Shape独立于电气Part，外观层与电路注释层可保存和撤销，但不能提供虚构的电气连接。'''),
('网络编译：展开实例、合并连接、建立依赖', 'src/Digital.cpp', 'void flatten(const Circuit',
 '''flatten()递归展开每个Subcircuit，把父接口与内部Input/Output端点关联。每个普通元件以完整实例路径成为运行Node；left/inner/reg与right/inner/reg是不同节点。同名Junction标签仅在相同实例作用域内合并。

并查集合并显式Wire端点和接口别名，随后生成实际Net以及驱动者和依赖者列表。编译工作只在定义变化或显式复位时进行；实例导航只改变显示电路和路径，不重新编译根电路。这既减少工作，也维持状态连续。'''),
('逐位信号：0、1、X、Z、E', 'include/Digital.h', 'struct Signal {',
 '''Signal用width和value/known/z/error位掩码表达总线，而不是把未知值塞进bool。known确定时value给出0/1；z是高阻；error记录驱动冲突，其余未知显示X。8位总线可以部分已知、部分未知。

解析网络时忽略高阻驱动；相同确定驱动可以一致，不同强驱动产生冲突。弱上拉/下拉在有强驱动的位上让位；未知控制保持保守。画布颜色只展示信号，不能改变求值语义。'''),
('事件批次：先共同求值，再统一发布', 'src/Digital.cpp', 'bool step() {',
 '''事件队列按逻辑时间组织同批次节点。step()先让本时刻所有节点读取旧网络并求值，再统一解析网络变化，调度受影响的后续节点。若求一个节点就立即发布，两个同沿寄存器可能读到彼此刚更新的值，结果会依赖遍历顺序。

settle()推进直到事件队列为空或达到保护预算，超限报告振荡或传播过长。这里是单线程离散事件仿真，不是多线程仿真，也没有电压、电流、纳秒或建立保持时间模型。trace显示缓存最多2000条，持续文件接收器不受该缓存限制。'''),
('双向数字开关与弱驱动', 'src/Digital.cpp', 'std::vector<std::pair<int, int>> uncertain;',
 '''Transistor和TransmissionGate根据控制端临时连接A/B两侧网络，因此允许从任一侧驱动。它们不是只从A向B复制值的普通逻辑门；断开后两侧恢复各自驱动。

控制明确时选择导通或关断；未知控制用保守网络解析，不虚构可靠0/1。弱上/下拉参与逐位强弱解析。此模型描述数字连通关系，不计算MOS管阈值、电阻或模拟电流。advanced_core_tests覆盖反向导通、关断和强弱覆盖。'''),
('独立时钟与边沿采样', 'src/Digital.cpp', 'bool edge(Node &n, Signal clk)',
 '''每个Clock有highTicks/lowTicks/phase；tick()推进一个基础节拍，由各自配置决定输出。UI毫秒设置仅控制播放速度，不改变逻辑频率关系；cycles()保留旧接口含义，显示以ticks为准。

时序元件只在旧CLK和新CLK都确定且满足指定上升/下降沿时采样。X/Z到1不算可靠上升沿。Counter支持上限、清零和可选装载；原生LOAD优先规则与导入Logisim Counter的CT/LD组合规则分别明确，避免名字相同而行为错误。'''),
('RAM初值和真实实例内存', 'src/Digital.cpp', 'std::vector<Signal> Simulator::memory(',
 '''RAM/ROM地址位数1—16、数据位宽1—32，最多65536字；展开后全工程内存还有总量限制。ROM读取初值；RAM按地址异步读、在明确的写边沿与使能下更新。未知地址或控制按核心规则传播不确定性。

memory(fullPath)读取展开实例的实际内容。memoryEditor(false)编辑工程初值；memoryEditor(true)只读运行内存并定时刷新。HEX文件对应初值或显示导出，不能把运行值误写回共享定义。复位从Part.value/Part.data重新初始化。'''),
('重点：进入指定实例，导航不复位', 'src/DigitalEditor.cpp', 'void LogicEditor::enterInstance(',
 '''界面分开保存current（显示定义）、simulationRoot（编译仿真根）、scope（当前完整实例路径）。enterInstance记录父级视口，拼出子scope并调用rebuild(false,true)。read()将局部对象ID加上scope再读取同一个Simulator。

navigateTo恢复父级的定义、scope和视口。它们不重置时间、tick、事件队列、寄存器或RAM。GUI回归在运行计时器开启时进入两层，确认Simulator地址、time和ticks相同，再切到另一实例检查独立值。'''),
('观察、操作与编辑定义的边界', 'src/DigitalEditor.cpp', 'void LogicEditor::editDefinition()',
 '''观察模式允许看真实信号、运行RAM和逐层返回；内部Button/Keyboard/Joystick修改当前展开实例的运行输入。由父级驱动的Input接口没有独立运行Node，不允许在子层覆盖父信号。

修改线路、元件参数、外观接口或初值需“编辑定义（复位）”。该操作暂停、离开scope并重新编译/复位仿真。保存工程可以保留设计初值，不隐含保存调试断点。界面通过属性只读和统一change入口同时守住这条边界。'''),
('组合分析表与CSV契约', 'src/LogicAnalysis.cpp', 'void validateTable(',
 '''组合真值表最多10位输入、128位输出，必须完整覆盖且不重复每种0/1输入组合。输出允许0、1或设计不关心项-1，后者与仿真未知X不同。名称必须唯一、无换行及首尾空白。

导出的CSV用IN:/OUT:明确列角色，并对逗号和双引号正确引用转义。界面可新建、导入、编辑输出；对已有组合电路提取真值表使用独立Simulator，含时序元件或不确定输出会拒绝确定真值表分析。'''),
('Quine–McCluskey化简和卡诺图', 'src/LogicAnalysis.cpp', 'Minimization minimizeOutput(',
 '''先从输出1和不关心行生成最小项，反复合并掩码相同且仅一位确定值不同的项，得到主蕴含项。只要求覆盖输出1，不要求覆盖不关心项。选必要主蕴含项，再建立贪心有效覆盖作为上界，做有预算的覆盖搜索。

搜索最多200000次访问，中间项也受限。预算耗尽返回已经有效的等价表达式并将exact设为false，不能宣称全局最少。卡诺图对最多4变量以格雷码排列，颜色表示当前覆盖项，可单独选某个圈组；更多变量仍可化简但不展示二维卡诺图。'''),
('电路综合后必须重新仿真', 'src/LogicAnalysis.cpp', 'Project synthesize(',
 '''synthesize()根据化简项生成独立工程，可使用AND/OR/NOT或仅NAND，常量函数有显式常量源。平衡连接树避免大量输入串成不必要的深链，并限制最大生成规模。

生成完成后建立新的Simulator，对每个确定输出行重新设置输入、收敛并比较结果。不关心输出不构成失败条件。全部验证成功才由界面导入为新电路，失败保留原工程。测试覆盖全部16种二输入函数及随机四输入函数两种综合形式。'''),
('自定义元件与接口布局的ID重映射', 'src/CustomComponents.cpp', 'auto remapEndpoint =',
 '''exportComponent收集根电路可达的依赖闭包，独立文件携带嵌套定义。importComponent先校验，在候选工程中为电路名和对象ID分配新值，同名定义加后缀，不能覆盖已有定义。

端点object需要重映射；当它指向子电路时，pin也是内部接口对象ID，必须同时重映射。interfaceLayout的键也使用同一映射表。全部验证成功才提交，避免导入后外观与接线分别指向不同身份。'''),
('Logisim导入：几何还原与明确诊断', 'src/CircImport.cpp', 'Project importCirc(',
 '''TinyXML2读取经典2.7项目；先建立库编号与库身份映射，再转换支持的器件、参数、朝向与端口。按水平/垂直导线、端点、引脚位置建立显式节点，处理T形连接和重叠；不凭纯交叉制造短路。

Splitter/Joiner方向根据实际驱动连通关系迭代判断，无法可靠表达的模式给出诊断。未知器件用带原属性/坐标/原因的Text提示保留，缺少电气行为不会被冒充实现；坏XML、规模或整体模型错误拒绝加载。拒绝DTD、实体与外部JAR执行。'''),
('Logisim导出：公开器件和真实网络', 'src/CircExport.cpp', 'std::string exportCirc(',
 '''导出器写标准XML、公开元件、Tunnel网络和层次appear接口。因两套符号尺寸不同，重新排布布局以保证电气连接。非零RAM初值使用ROM、运行RAM、已写标记RAM和MUX封装，逆序位段和其他必要转换也使用公开器件。

不能等价表示的配置明确拒绝，例如非零寄存器初态、保持式Button。输入交互值、时钟相位和装饰外观差异给出提示。此前Constant辅助源写裸0触发经典HexInteger解析错误，已统一十六进制0x0写法并由33个实际Logisim样例验证，验证进程采用headless避免弹窗反复阻塞。'''),
('网表：逻辑报告与实体映射', 'src/PcbExport.cpp', 'PcbExchange exportPcb(',
 '''原生Simulator::netlist复用已经编译的网络，JSON保留完整实例路径和逻辑参数，S-expression逐位展开总线；导出不推进运行中的仿真器。逻辑引脚不自动成为真实PCB芯片引脚。

exportPcb针对单比特二输入AND/OR/NAND/XOR及NOT选择74HC系列DIP14器件，每门独立芯片，补齐电源、未用输入处理、100nF去耦和信号连接器，同时输出标准.net、配套.kicad_pcb和mapping.json。KiCad加载板文件后导出IPC-D-356，逐个已分配焊盘核对标准网表。当前半加器9个元件、6个网络、36个已分配焊盘一致；未布线，未自动验证GUI直接导入.net。'''),
('持续日志、打印与批量输出', 'src/LogicEditorTools.cpp', 'void LogicEditor::monitorSignals()',
 '''信号选择器展开全层次输出，以完整路径和引脚作为过滤键，setTraceSink在每次变化时直接写入CSV并flush，不依赖最近2000条显示缓存。导航保持原仿真器，所以日志继续记录相同实例身份。

批量图片临时为各定义编译用于输出的Simulator，完成或异常都恢复原显示和运行器；单图支持比例，超大图按像素预算缩小以避免分配失败。打印与预览复用正式clean渲染，排除选择/悬停效果。原生打印机和文件选择器的人工点击不在本次自动化范围。'''),
('验证：不同层证明不同事实', 'tests/circ_exchange_tests.cpp', 'int main(',
 '''本日Debug和Release均11/11 CTest通过。原逻辑209项、自定义/网表167项、导入1066项、图形28项、分析3688项、新核心103项、交换211项；PCB映射按属性报告，无同口径断言总数。独立Python解析65组逻辑网表，实际Logisim运行33组样例。

GUI测试真实窗口和事件处理路径，确认菜单、符号、布线、剪贴板、属性、保存和层次调试；图片来自正式画布导出。测试目标数、运行检查数、文件样例数不是同一口径，不能当作覆盖率。外部PCB验证明确记录直接GUI网表导入未测，不能扩大结论。'''),
]
guide = ['# PracticeEDA 0.5 源码详细讲解\n\n2026-10-08，根据当前源码自动定位片段。建议按“模型 → 编辑 → 网络 → 实例 → 分析 → 交换 → 验证”阅读。代码关键处已有中文注释；下文解释为什么这样设计、怎样复核，以及限制。\n']
for i,(title,file,anchor,description) in enumerate(topics,1):
    lines=(ROOT/file).read_text(encoding='utf-8-sig').splitlines()
    start=next(n for n,line in enumerate(lines) if anchor in line)
    language='cmake' if file.endswith('.txt') else 'cpp'
    guide.append(f'## {i}. {title}\n\n{description.strip()}\n\n源码：[{file}](../../{file})，本次定位第{start+1}行起。\n\n```{language}\n'+'\n'.join(lines[start:start+8])+'\n```\n')
guide.append('''## 24. 两条现场调用链

半加器：拨动根Input → setInput调度源 → 同时刻节点求值 → 网络解析 → 下游XOR/AND求值 → Output显示 → 真值表独立穷举确认。A=B=1时Sum=0、Carry=1。保存的是设计和输入初值，不是事件轨迹。

层次：打开defense-hierarchy → 推进一个基础节拍 → 根Clock上升沿 → 两个inner实例分别采样父输入0x12/0x34 → 进入left/inner → 以完整路径读取寄存器及RAM → 返回并进入right/inner → 同一Simulator内读另一份状态。切换不产生复位；点“编辑定义（复位）”才暂停、重建。

## 25. 答辩中需要准确说出的边界

- 数字离散逻辑模型，不计算电压电流和器件时序签核。
- 布尔等价逐行反验；化简最小性在搜索预算内给出，耗尽时明确注明。
- .circ支持常用内置与多层子电路，受限属性见交换说明，不是全格式无损复刻。
- 实体PCB仅支持列出的门映射，当前未布线，配套PCB加载与网络一致性已经验证，直接GUI导入.net未自动验证。
- 快照撤销包含工程定义，工程保存不包含隐式运行断点。
- 关键测试记录在本日evidence目录，历史日志不证明本轮新增功能。
''')
write('01_源码详细讲解.md','\n'.join(guide))

write('02_四人分工与排练.md', '''
# 四人分工与排练（2026-10-08）

讲解18分钟，机动2分钟，教师问答另计。姓名按用户确认顺序：常人元、闫浩楠、杨曙启、黄歆贻。此表分配本次主讲、演示和问答责任，不认定历史代码作者。

|成员|页面|板块|必须讲清|准备成果|
|---|---|---|---|---|
|常人元|1—6，7交接，25收束|项目目标、架构、验收|稳定ID、几何/电气、定义/实例的三条约定|画模型关系，定位Digital.h和CMake|
|闫浩楠|8—12，13交接，25收束|编辑、参数、组合综合|事务回滚、位映射、等价反验与最小性区别|演示参数、撤销、表格综合，定位LogicAnalysis.cpp|
|杨曙启|14—18，19交接，25收束|网络、时序、重点实例调试|同批事件发布、独立时钟、左右RAM、导航连续|完整跑两层实例与内部按钮，定位enterInstance和flatten|
|黄歆贻|20—24，25收束|组件、.circ、PCB、日志、验证|交换边界、真实引脚、外部软件验证口径|打开33组结果与PCB映射，解释未布线和未测项目|

四个板块各4分21秒（包括交接页），第25页每人约9秒，合计每人约4分30秒；交接双方各一句，具体个人耗时以排练修正。各页秒数见页面计划和讲稿。

## 交接

第7页，常人元：“模型确定之后，每次操作都必须遵守这些约定。下面由闫浩楠介绍编辑与综合。”闫浩楠：“我用参数、接口和真值表生成，说明结果怎样恢复和验证。”

第13页，闫浩楠：“编辑结果已经可以保存、恢复并验证。接下来由杨曙启解释随时间变化的行为。”杨曙启：“我从事件批次讲到寄存器、RAM和真实实例导航。”

第19页，杨曙启：“实例状态连续之后，还需要把设计可靠地带到其他软件。下面由黄歆贻介绍交换与证据。”黄歆贻：“我展示.circ、实体网表、日志，以及验证能证明的范围。”

第25页四人依顺序各说一句收束，然后请老师提问。不要临时新增功能承诺。

## 排练三轮

第一轮理解：每人脱稿解释本板块三个机制，至少定位一个实际函数；其他人用“为什么”和“异常时怎样”追问。重点问双实例是否共享RAM、导航是否重建Simulator、最小性是否总有保证、PCB是否已布线。

第二轮操作：闫浩楠负责翻页和综合演示，杨曙启负责实例演示，黄歆贻负责外部交换结果，常人元负责总计时和异常接话。全部操作用同一Release分发包与sample，先设置A=B=1，再确认左右0x12/0x34及内部按钮隔离。每次开始前复位到已知起点。

第三轮限时：从第一页计时，记录每板块用时。超时先缩短功能枚举和备用外观演示，保留实例连续性、综合反验和交换边界。禁止为赶时间连续快速点击导致导航动画未完成。

## 故障恢复与问答

若软件窗口异常，重新打开原生演示文件并推进已知节拍；若外部软件不可用，明确展示本日已执行的日志和画布导出图。备份不是现场运行，不能混称。

若误点“编辑定义”，说明这会按约定暂停复位，再从根推进一个节拍恢复演示状态。实例内父驱动Input只读和RAM只读是设计边界，不是程序没有响应。

提问按板块首答，涉及共同模型由常人元补充，涉及实例由杨曙启确认，涉及测试结论由黄歆贻引用证据。每人都应能回答其他板块的基础边界。推荐问答见逐页讲稿末尾。
''')

write('04_方案对比与迭代证据.md', '''
# 方案对比与可复核证据（2026-10-08）

本文件说明当前方案与代价，不把能力阶段虚构成逐周开发时间线。用户需求、课程任务书、已有代码与实际验证共同约束本轮实现。

|问题|可选方案|当前选择与理由|代价/限制|证据|
|---|---|---|---|---|
|界面框架|重写Web/Qt；延续wxWidgets|沿用课程要求和既有wxWidgets，保留三栏并改进层次、分隔栏、属性|需维护原生控件和平台构建|CMake；DigitalEditor|
|连接模型|纯几何交叉；显式端点|稳定ID端点，分支显式Junction，移动和旋转不改电气身份|布线时要维护节点和端口|ports/endpoint；布线GUI回归|
|编辑恢复|每操作命令；全工程快照|快照包含全部共享定义，候选验证后提交|大工程复制成本更高|History/change；失败回滚测试|
|仿真|每帧全图；离散事件|网络编译后事件批次传播，先求值再统一发布|维护队列、依赖和振荡预算|Simulator::Impl；两级寄存器回归|
|层次调试|单独运行定义；读取真实实例|完整路径读取同一个根Simulator，左右运行态隔离|结构编辑必须明确暂停复位|HIERARCHY-UI-PASS；left/inner与right/inner图|
|布尔化简|仅标准式；有限搜索|主蕴含项+覆盖搜索，贪心给有效上界，有限预算|耗尽时不保证最少项|LogicAnalysis；3688项分析检查|
|综合验收|只检查生成结构；独立求值|新Simulator逐行比较确定输出，成功才导入|最多10位输入，生成规模受限|synthesize；AND/OR/NOT和NAND测试|
|.circ交换|私有嵌入；公开元件|真实XML、Tunnel、appear，必要时生成公开包装子电路|重新排布；不能表达的配置拒绝|211项交换检查；33组实际Logisim结果|
|PCB输出|抽象逻辑网表；实体映射|保留逻辑报告，另提供受限74HC芯片/封装映射和配套板|未布线；只覆盖列出的组合门|KiCad10.0.7加载、36焊盘与.net比对|

## 本轮已完成的能力

A01—A09完善编辑、参数、时序、存储及真实实例，A10—A13补充组合综合和交互/双向数字器件，A14—A18补充交换、持续日志、最近文件、模板、图片打印和右键操作。逐项入口、测试与限制见[验收清单](../OPTIMIZATION_ACCEPTANCE_CN.md)，不能把“18项完成”解释为Logisim全部功能复刻。

## 证据链

本日Debug / Release各11项CTest及四组GUI标志通过。核心测试验证语义和回归；GUI检查验证窗口/事件处理路径；独立解析验证文件结构；实际Logisim和KiCad验证外部读取及输出/连接。每层记录可直接打开，本日证据优先于历史版本。

文件错误修复具有具体因果：辅助Constant的裸0导致经典Logisim整数解析失败，统一写0x0后33组样例通过；外部验证加headless，避免出错弹窗占用桌面和测试挂起。此结论来自当前生成文件与实际Java运行，不是只查看XML文本。

不填写没有测量的性能倍率、CPU占用、开发人日、覆盖率或历史贡献比例。不把配套PCB加载等同于已经测试GUI直接导入.net，不把未布线板称为可制造成品。
''')

write('05_当前进度与现场演示.md', '''
# 当前进度与现场演示（2026-10-08）

程序和材料已同步到0.5，本日Debug/Release各11/11 CTest与GUI集成检查通过。重点实例调试、组合综合和外部交换证据都在材料包中。演示使用Release程序，编辑前保留一份原生样例。

## 准备

启动dist/PracticeEDA/PracticeEDA.exe，确认帮助菜单可打开“操作指南”，原左侧常驻七行提示已移除。准备examples/half-adder.logic.json、bus-register.logic.json、defense-hierarchy.logic.json；外部样例在sample/logisim和sample/pcb。打开网页演讲者视图或可编辑PPTX，保持主讲页号一致。

## 1. 组合逻辑与编辑

打开半加器，A=B=1时Sum=0、Carry=1；依次拨动输入并查看真值表。演示旋转、参数或外观时先说明这是定义编辑，修改结构按约定复位。可选演示一次非法位宽或撤销，确认工程恢复；不要在主线里临时创建大型电路。

分析菜单打开真值表，编辑一个输出或不关心项，查看化简和卡诺图圈组，生成AND/OR/NOT或仅NAND新电路。说明每行确定输出已经用新Simulator反验；超过4变量不显示二维卡诺图，覆盖预算耗尽时界面注明最小性未证明。

## 2. 寄存器、计数器与RAM

bus-register用于8位总线采样；Clock属性展示高/低持续基础节拍，与界面播放毫秒区分。计数器上限、边沿和装载可由属性说明，时序序列的Logisim对照见sample/logisim/counter-sequence.circ及sequence-Register.circ。

打开defense-hierarchy，推进一个基础节拍，左右寄存器及RAM地址0分别写入0x12、0x34。回到根明确时钟起点，再开始自动播放或保持暂停。静态常量使写入值稳定，方便清楚展示状态隔离。

## 3. 多层真实实例（重点）

在根层双击left进入“嵌套模块”，再双击inner进入“存储单元”，路径应指向left/inner。查看reg、q和RAM输出为0x12；选择mem，打开运行内容窗口，地址0为12，内容只读。按返回上级或路径逐层返回，切换right/inner看到0x34。

在运行和暂停两种状态分别导航，状态不复位。操作左实例内部button，只有左侧buttonOut变化，右实例保持原值。data/clk是父层接口Input，在子层只读；要改输入回父层操作源。可在信号监测中选择left/inner/reg:Q和right/inner/reg:Q记录CSV，导航保持记录。

最后点击“编辑定义（复位）”说明它会暂停并复位整个相关仿真。若现场误点此按钮，回根重新推进一个基础节拍，不把预期复位当故障。

## 4. .circ与PCB

在Logisim打开sample/logisim/three-level-custom-interface.circ和一个sequence-Register或sequence-RAM样例。回到PracticeEDA打开同一.circ，查看层次和端口。序列期望写在对应.expected.txt，33组实际外部运行记录在logisim-validation.json。双方输入交互值和初始相位需按交换说明设为同一起点。

用半加器选择“文件 → 导出网表 → KiCad实体映射网表”，产生.net、.kicad_pcb和.mapping.json。已有样例在sample/pcb，可用KiCad PCB编辑器打开板文件，查看DIP芯片、连接器和焊盘。KiCad10.0.7实际加载后导出IPC-D-356，与标准.net逐焊盘对照：9元件、6网络、36已分配焊盘一致。板未布线，需要后续布局、布线和规则检查；本次未自动测试GUI直接导入.net。

## 备份与现场异常

evidence保留本日原始日志；ppt/images/hierarchy-left-instance.png和right图来自正式画布导出，展示独立状态，不能称为完整编辑器截图。外部软件不可用时明确切换到已验证结果备份。别使用早期隐藏窗口生成的黑色截图。

若再出现Logisim文件错误，先记下具体样例名并查看日志，不连续确认大量弹窗。当前正式33组样例已修复常量数值写法，后台测试启用headless和超时，不会以弹窗无限阻塞。
''')

qa = [
('常人元','为何保留wxWidgets？','课程要求及现有窗口、事件和构建环境可复用。核心仍独立，框架替换不能直接改善仿真正确性。'),
('常人元','改名为何不断线？','连接引用Part/Port稳定ID，label仅显示。子电路接口绑定内部Input/Output的ID。'),
('常人元','为何工程保存不保存当前RAM？','工程是共享定义与复位初值，运行RAM属于指定实例。混存会把左实例运行值错误传播到所有实例；当前没有隐式断点快照。'),
('闫浩楠','修改失败会破坏旧工程吗？','候选模型先验证、编译和收敛，再提交History。异常恢复旧History和Simulator；集成测试覆盖无效参数回退。'),
('闫浩楠','卡诺图与化简的变量限制相同吗？','真值表/化简最多10位输入，二维卡诺图最多4变量；设计不关心项可参与扩圈，仿真未知X不是设计不关心项。'),
('闫浩楠','生成门电路为何可信？','生成后用新Simulator遍历所有确定输出行反向比较，失败不覆盖原工程。AND/OR/NOT与NAND两种生成形式均有回归。'),
('闫浩楠','化简一定最少吗？','覆盖搜索有预算；预算耗尽已有等价覆盖，但exact=false并提示未证明最少项。正确性和最小性分别判断。'),
('杨曙启','为何同批次一起发布？','让所有节点读取同一旧网络，避免两个同沿寄存器因遍历顺序不同而采样刚更新的数据。'),
('杨曙启','事件驱动就是多线程吗？','不是，当前为单线程离散事件队列；减少不必要求值与增加并行线程是不同问题。'),
('杨曙启','两个实例的RAM如何隔离？','flatten用left/inner/mem和right/inner/mem构造两个运行节点，各有内存。共享的Circuit定义只保留初值。'),
('杨曙启','进入子层会重新仿真吗？','导航只改变current、scope和视口；rebuild(false)保留同一个根Simulator，时间、队列、寄存器和RAM连续。'),
('杨曙启','为何实例内Input不能改？','它是父级驱动接口，子层覆盖会破坏真实连通语义。内部Button/Keyboard可作用于当前展开节点，运行RAM仍只读。'),
('杨曙启','F8等于一个完整时钟周期吗？','F8推进一个基础节拍，各Clock按自己的高低持续配置决定是否翻转；不是一条CPU指令，也不一定是完整周期。'),
('杨曙启','晶体管模型是模拟电路吗？','是数字双向连通开关，保守处理未知控制和驱动强弱，不计算电压、电流或阈值。'),
('黄歆贻','.circ兼容是否无损？','常用支持器件和多层子电路能交换；布局重排，初态与部分配置有明确限制，不支持项拒绝或诊断。33组外部样例不代表全部Logisim属性。'),
('黄歆贻','之前文件错误是什么？','辅助Constant的裸0不被经典数值解析接受，已统一0x0；实际Logisim33组运行通过。验证进程设headless和超时，避免模态弹窗阻塞。'),
('黄歆贻','PCB可以直接制造吗？','不能。当前提供受限74HC组合门实体映射和未布线板，电源/去耦/连接器已补齐，仍要布局布线和规则验证。'),
('黄歆贻','PCB实际验证了什么？','KiCad10.0.7加载配套板并导出IPC-D-356，与标准.net及映射表逐个已分配焊盘比较。直接GUI导入.net未自动验证。'),
('黄歆贻','为什么不把测试数字相加？','CTest目标数、运行检查项数、文件对比样例数口径不同。它们都不是覆盖率，也不能保证所有未来电路绝无问题。'),
('全体','如何证明分工？','本材料安排本次主讲、演示与问答责任。真实历史贡献以小组记录和Git历史为准，不用演讲分工冒充历史作者。'),
]
notes=(OUT/'03_答辩逐页讲稿.md').read_text(encoding='utf-8').split('\n## 备用问答')[0]
notes+='\n## 备用问答（教师提问另计）\n\n'
for i,(name,q,a) in enumerate(qa,1):
    notes+=f'### {i}. {q}\n\n建议首答：{name}。{a}\n\n'
write('03_答辩逐页讲稿.md',notes)
print(f'Updated six defense guides, {len(topics)} source excerpts and {len(qa)} Q&A items')

write('../TEST_REPORT_CN.md', '''
# PracticeEDA 0.5 功能与回归测试报告

日期：2026-10-08。环境：Windows、Visual Studio 2022 x64、C++17、wxWidgets静态库；外部验证使用本地经典Logisim 2.7.1和KiCad CLI 10.0.7。所有下列结果均来自本日实际执行，原始日志保存在docs/defense/evidence。

|构建配置|CTest|窗口集成|结果|
|---|---|---|---|
|Debug桌面版|11/11通过|四组本次GUI标志通过|通过|
|Release桌面版|11/11通过|四组本次GUI标志通过|通过|

## 核心与格式测试

|CTest目标|本次输出|覆盖重点|
|---|---|---|
|digital_logic_tests|209项检查|原有门、总线、事件、时序、存储、层次、文件及旧示例回归|
|component_netlist_tests|167项检查|组件依赖、重名、ID、失败回滚、只读导出、逐位连接|
|circ_import_tests|1066项检查|经典元件映射、方向、拓扑、控制、ROM/RAM、诊断与往返|
|shape_tests|28项检查|图形几何、校验、复制、外观与持久化|
|analysis_tests|3688项检查|16种二输入函数、48组随机四输入函数、两种综合、不关心项、常量、CSV引用/往返|
|advanced_core_tests|103项检查|参数门、选择器、位映射、时钟、边沿、计数、实例RAM、弱驱动/双向开关、交互、持续日志|
|circ_exchange_tests|211项检查|导出再导入、算术、总线、多层接口、寄存器/触发器/移位/RAM序列、导出限制|
|pcb_export_tests|属性检查通过|芯片映射、真实引脚、电源/去耦、只读导出、不支持总线拒绝|
|netlist_format_tests|65组文件比对|独立Python解析S-expression，逐网络/逐位与JSON比对|
|logisim_exchange_validation|33/33样例|实际Java加载导出.circ，核对组合和时序输出|
|kicad_pcb_validation|9元件、6网络、36已分配焊盘|KiCad加载配套PCB导出IPC-D-356，与标准.net及映射逐焊盘核对|

测试目标数、运行检查数、文件样例数为不同口径，不相加声称覆盖率。CMake检测到Python后注册独立文件测试；检测到本地Logisim JAR/Java后注册外部交换测试；EDA_KICAD_CLI显式指定后注册实际PCB验证。缺少外部工具时这些检查不会运行，不能照抄本机11/11结论。

## GUI集成

LOGIC-PASS覆盖启动、图标/符号、拖拽、布线、分支、取消、悬停隔离、剪贴板、撤销、属性、保存/加载、Unicode路径、真值表、图片、组件导入与源文件保护。

SHAPES-PASS覆盖绘图阶段、控制点、取消、图层、剪贴板、撤销和持久化。EDITOR-UI-PASS覆盖紧凑I/O、状态色、表格校验/回退、保持运行寄存器、连接错误/警告与定位。

HIERARCHY-UI-PASS在真实编辑器中推进时钟，为left/inner与right/inner写入不同寄存器及RAM值；运行计时器开启时进入多层，断言同一个Simulator、time、ticks保持，暂停后切换实例仍为0x12/0x34；父驱动Input和结构只读；内部Button只改变本实例，serialize工程定义不变。正式画布图保存在ppt/images，两张图分别来自左右实例，不称为完整窗口截图。

## 外部结果与已修复问题

Logisim样例包含四向多输入门、MUX/编解码、逆序拆合、算术/位运算、ROM/RAM初值、计数器序列、Register/D/T/JK/SR/ShiftRegister/RAM序列和三层自定义接口。所有33组由实际Logisim输出与期望比较；验证设置headless及每文件20秒超时，不让模态错误窗口无限阻塞。

辅助Constant曾写裸0，经典数值解析报“属性值(0)对value无效”。已统一使用0x0及带前缀十六进制，重新生成和验证正式样例。BitSelector选择端位宽按分组数量推导；Splitter→Joiner链用迭代方向传播，均已回归。

PCB结果记录standardNetlistMatchesLoadedBoard=true、directNetlistGuiImportTested=false、routed=false。实际验证是加载配套.kicad_pcb，并从IPC-D-356确认标准.net的逐焊盘连接；未自动执行GUI直接导入.net，不宣称布线、DRC或制造验收通过。

## 复现

```powershell
.\\scripts\\build.ps1 -Configuration Debug -GuiTest
.\\scripts\\build.ps1 -Configuration Release -GuiTest -Package
```

可用cmake -D参数指定EDA_LOGISIM_JAR、EDA_LOGISIM_JAVA、EDA_KICAD_CLI。外部工具为验证依赖，不随程序分发。核心构建可使用EDA_BUILD_GUI=OFF；在Visual Studio多配置生成器下，构建与CTest都应指定同一配置。

原始产物：build/qa-features/{Debug,Release}、qa-circ-exchange/{Debug,Release}、qa-pcb/{Debug,Release}以及qa-debug/qa-release。对应精选日志和交换样例在答辩材料evidence与sample中。

## 验证边界

本次未自动点击系统原生文件选择器、实体打印机或全部参数对话框；GUI测试直接调用真实窗口处理路径，并非所有人工场景已穷举。演讲文件通过25页渲染/边界/字体/备注与离线网页检查，未在原生PowerPoint人工放映，也未连接实体投影仪。上述检查证明列明范围，不保证所有未来电路或全部Logisim配置均无问题。
''')

write('evidence/说明.md', '''
# 验证证据说明

本日基线：2026-10-08当前源码与Release程序。2026-09-*文件仅为历史资料，不代表本轮新增能力。

|文件|证据范围|
|---|---|
|2026-10-08-debug-ctest.log / release-ctest.log|两种配置11/11目标与每程序原始输出|
|2026-10-08-{debug,release}-LOGIC/SHAPES/EDITOR-UI/HIERARCHY-UI-PASS.txt|本次GUI成功标志，各文件写明断言范围|
|2026-10-08-deck-layout.json|25页离线浏览器边界、图片、备注、键盘及演讲者导航，0异常、0联网请求|
|2026-10-08-pptx-validation.json|正式PPTX结构、字体、几何及artifact-tool导入检查，含SHA256|
|2026-10-08-material-audit.json|正式演示文件、源码索引、样例和材料包一致性检查|
|../sample/logisim/logisim-validation.json|33个实际Logisim样例输出，与对应.expected.txt核对|
|../sample/pcb/kicad-validation.json|实际KiCad版本、板加载及36焊盘与标准网表一致|

ppt/images/hierarchy-left-instance.png与right图由正式画布导出，显示左0x12、右0x34。它们不是整个编辑器窗口截图。PPTX采用原生文字/表格，图片仅用于真实电路图。

KiCad验证方法为：加载对应.kicad_pcb → 导出IPC-D-356 → 与.net和mapping.json逐焊盘比较；直接GUI导入.net未自动验证，板未布线。原生逻辑网表的虚拟端口不等于实体封装。

材料记录中的目标数、检查项数、样例数为不同口径，不相加作覆盖率；外部加载和内部往返也不能互相替代。详见上级测试报告与交换说明。
''')

write('ppt/展示检查记录.md', f'''
# 展示文件检查记录

日期：2026-10-08。对象：当前index.html及正式可编辑PPTX `{PPTX}`，内容源为scripts/defense-content.json。

|检查|本轮结果|
|---|---|
|内容与时间|25页、25条讲者备注、18分钟；保留2分钟机动、7/13/19交接和25共同收束|
|姓名与责任|常人元、闫浩楠、杨曙启、黄歆贻；明确为本次主讲职责|
|PPTX结构|25张16:9幻灯片，结构校验0发现、0警告，artifact-tool重新导入通过|
|字体与几何|Microsoft YaHei；字体/标题边界/几何检查0发现、0警告|
|实际渲染|artifact-tool生成25张1280×720图，逐页查看完整图；源码文字、表格、图注均可见|
|真实图片|本日正式画布导出左右实例图；排除早期隐藏窗口黑色截图|
|网页离线|实际Edge headless通过file打开；HTTP(S)请求0，JavaScript异常0|
|网页排版|1280×720下25页逐一定位并检查内容边界、文字滚动溢出和图片加载，通过|
|网页交互|方向键翻页、演讲者外壳、当前/下页预览、25条备注、18分钟合计、下一页按钮通过|
|源码摘录|演示代码按当前文件锚点提取，具体索引见源码摘录索引.json；详细导读另有23组当前源码片段|

PPTX检查回执和网页结果保存在../evidence，本地逐页PNG位于build/defense-ppt/renders及build/defense-web。结构检查本身不读取全部文字或执行原生PowerPoint；视觉检查与文本/备注比对单独执行。

本轮没有重新执行全部旧版观众同步、弹窗拦截、自动翻页、标注和计时扩展场景；保留既有运行时，不把2026-09记录冒充本轮结果。也未进行原生PowerPoint人工放映、实体投影、打印、不同DPI/浏览器的全面验证。正式使用前在现场电脑检查字体与显示比例。
''')
