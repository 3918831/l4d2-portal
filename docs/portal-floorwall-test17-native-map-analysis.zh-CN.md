# 第 17 轮：条目正常参与，但地板→墙面空中 origin 插值没有产生变化

日期：2026-10-10。输入：`TestLog/高速穿越连续性修复/17/portal_l4d2_traversal.log`。本轮为分析和下一步计划，未修改产品代码、构建、部署、提交或推送。

## 1. 结论

测试有效，新探针运行态通过。上一轮把 recv 当作 native-local 的观测错误已纠正：普通下落、有效墙门空中样本都能看到真正本地位置在 Part1 内变化，且变化与外层 abs 一致。

地板→墙面在首次落地前的 184 条中后段空中事件样本中，origin 绑定存在、处于参与范围、needs 前后均为 1，map 自然调用且时基推进，但 native-local origin 没有发生插值变化。外层 abs 同样没有插值变化。当前首个已观测失真边界进一步收窄到具体 origin 插值变量及其选样/历史路径；不支持把问题归到“local 已变但 Part2 未传播”。

仍未直接观测具体 watcher 的执行，也未读到实际 interpolationAmount 和历史。因此不能宣称最终根因已经证明。下一轮应观察实际 origin watcher 调用及 native GetInterpolationInfo 的输入和输出，用实际选择结果判断历史不足、时间落在区间外、历史值异常或写入目标不符。

## 2. 阅读方式与覆盖

日志为 7,376,576 bytes、14,878 行。流式解析器 `TestLog/高速穿越连续性修复/17/analyze-map.cjs` 按 capture/frame/prelude 关联有限事件行；窗口 Summary 提供所有接受帧计数。没有把整份日志读入模型上下文，也没有把有预算的事件样本当成全部渲染帧。

可复核命令（Node 位于本机已有路径）：

```powershell
& 'C:/Program Files/nodejs/node.exe' 'TestLog/高速穿越连续性修复/17/analyze-map.cjs' --assert
```

输出 `verdict=diagnostic-valid`、`floorEligibleLocalNoChangeReproduced=true`，退出码 0。该检查是捕获日志的确定性重放分析，不是自动运行游戏的回归测试。

- build `pe-6AC7BAD4`，与本轮部署身份一致（第 3 行）。
- Map Hook 的 installed、loopValidated、localIdentity、bindingValidated 全为 true（第 76 行）；outer/Part1 Hook 均安装通过（第 77–78 行）。
- singleeyehistory=0（第 613 行）；普通下落前关闭 Phase1，完成后开启（第 616、1833 行）。
- 17 个完整窗口：普通下落 2、墙门相关 10、地板→墙面 5；没有窗口缺少结尾 Summary。
- 共 5,509 接受帧：outer、Part1、map 各自然调用 5,509 次；callerMatched=5,509、CONTINUE=5,509；STOP/invalid/mapInvalid/mapUnavailable 均为 0。
- 调用前本地字段与引擎 oldOutputs 对照成功 5,509 次，mismatch=0。native origin 全窗口变化 4,203 次，而 recv 在 Part1 内变化仍为 0；后者是接收字段语义，不能代替本地插值变化。
- selected time 全程前进，仍比 Part1 输入晚约 66.7 ms；没有 selectedStalled/backwards 或 rewind 分支。这个偏移在所有场景共有，不足以单独认定错误；实际有效时间还需减去 watcher 的 interpolationAmount。
- 无 origin/angles 绑定缺失、参与范围外或无效分支；无命令行丢弃、阶段别名缺失。

测试覆盖有两个限制，但不影响主要判断：capture 12 在首次落地后出现 19 条 MoveType=8 的空中事件，已从有效空中对照排除；capture 16 在首次落地后又出现 12 条正常移动模式的空中事件，已从首次自由飞行样本排除。不能将这些后续样本与传送后的首次落地前轨迹混合。有效墙门跳跃样本仅来自 capture 10–12，速度约 220–320，未达到地板门 700–870 的完整速度匹配；普通下落则已有约 680–690 的速度参考。无需重复同一 DLL，下一轮顺带补对照即可。

## 3. 新增区分证据

筛选：非 prelude、事务开始两 tick 后、第一次观察到 onground 前、MoveType=2，并关联有效 MapFrame、InterpolationFrame 与 main-view 阶段。以下均为事件样本计数：

| 场景 | 有效空中样本 | origin 可参与 | native-local origin 有变化 | local/outer 变化状态不一致 |
| --- | ---: | ---: | ---: | ---: |
| 普通下落两次 | 48 | 48 | 48 | 0 |
| 有效墙门跳跃三次 | 41 | 41 | 41 | 0 |
| 地板→墙面五次 | 184 | 184 | 0 | 0 |

这使“绑定缺失/排除/needs 跳过”和“local 有写、abs 不传播”都不再是本轮异常空中样本的解释。有效条目 total=7、participatingCount=7，origin index=6、angles index=5；origin needsBefore=needsAfter=1，branch=4。branch=4 是经验证的分支条件，不是运行态直接 watcher 调用证据。

五个窗口全接受帧的 nativeOriginChanged 并非 0，是因为窗口也覆盖落地和后续运动；不能用全窗口总计掩盖异常空中区间。首次落地前分组如下：

| capture / txn | 空中可参与且 origin 不变样本 | 首次 onground tick | 首次落地后观察到 native origin 变化 tick | 最终视图累计冻结 / 最长单段 ms |
| --- | ---: | ---: | ---: | ---: |
| 13 / 11 | 42 | 21 | 22 | 336.930 / 31.922 |
| 14 / 12 | 35 | 18 | 19 | 285.706 / 31.058 |
| 15 / 13 | 37 | 19 | 20 | 309.649 / 31.171 |
| 16 / 14 | 23 | 13 | 14 | 112.217 / 30.949 |
| 17 / 15 | 47 | 21 | 22 | 342.443 / 31.877 |

tick 为相对窗口开始的值，恢复时刻是已输出事件中首次观察到的值。累计冻结来自全接受帧 Summary，不是一次暂停 0.1–0.34 秒。capture 16 飞行更短、随后又进入空中，累计冻结更小不能解释成只读版本修复有效。

回正归零且 State/shadow 均结束后，仍有 100 条首次落地前事件样本；100 条 origin 可参与但没有插值变化，其中 54 条最终视图冻结，其他样本有约 25–29 units 的 tick 跳变。这里的“origin 不变”指原生插值调用前后差为 0，不是玩家整段飞行静止：预测/模拟仍按 tick 推进，缺少帧间位置插值产生了阶梯。

具体证据：

- 第 793、802 行：普通下落 prelude 已可见 origin index=6、needs 1→1、nativeOriginChanged=1，验证观测能读取真实变化。
- capture 13，第 9440–9472 行，frame 157760/157761/157763：roll=0、State/shadow 已结束，同一 tick 内最终位移为 0，下一 tick 跳约 26.59 units；MapFrame 有效、origin needs 1→1、nativeOriginChanged=0。
- capture 14，第 10634–10667 行，同类状态下出现 0→26.05→0 的最终位移，origin 均不变。
- capture 16 第 13540–13542 行提供完整窗口计数；首次落地前 23 条与其他四次一致。后续额外空中样本从这次诊断分组剔除。
- 五组完整 Summary 位于第 10071–10073、11292–11294、12491–12493、13540–13542、14641–14643 行。

`nativeOutcome=3` 同时包含 origin 或 angles 变化。有些异常位置样本因 angles 在动而显示 3；判断位置必须使用 `nativeOriginChanged`。下一轮应把 origin outcome 与 angles outcome 分开，避免误读。

## 4. 对照官方与本机可继续验证的入口

官方调用链：

`BaseInterpolatePart1` → `Interp_Interpolate` → `IInterpolatedVar::Interpolate(currentTime)` → `Interpolate(currentTime, interpolationAmount)` → `GetInterpolationInfo` → Hermite/linear/extrapolate/早退 → Part2。

已重新完整核对相关正文：

- `Reference Code/portal-sdk-2013/src/game/client/c_baseentity.cpp:853`：needs 成立时自然调用 watcher；watcher 返回非零才清 needs。
- 同文件 `:2795`、`:2836`：Part1 保存 local，调用 map；Part2 比较 local 是否变化后失效 abs 缓存。
- `Reference Code/portal-sdk-2013/src/game/client/interpolatedvar.h:799`：有效取样时间是 currentTime−interpolationAmount，选 newer/older/oldest 和 frac。
- 同文件 `:968`：取样信息不可用时可以返回而不写位置；样本选择退化时也可能保持相同值；函数返回值表示 noMoreChanges，不表示“插值位置有变化”。
- `Reference Code/portal-sdk-2013/src/game/client/portal/c_prop_portal.cpp:53`：官方 EntityPortalled 变换真实 position/rotation interpolator 历史，并以玩家 origin/center 偏移处理位置。它说明正确历史和时间区间也是官方连续性的组成部分，不能仅变换相机后假定该链完整。

本机 client.dll SHA-256 仍为 `D2553CCA3A63BDD844864F5FEE2C20A233AB19DEE816463A90857B95309982A2`。只读反汇编已找到更具体的候选：

| 静态链条 | 本机证据 | 下一轮尚需完成 |
| --- | --- | --- |
| origin watcher 构造与类型 | 构造路径将 entity+0x2F8 的 watcher 注册到 origin；对应最终 vtable 磁盘 VA 0x10544D80 | 完整构造/类型指纹、ASLR、运行态实际 entry watcher/vtable 交叉验证 |
| origin 虚函数 Interpolate | slot6（vtable+0x18）指向磁盘 VA 0x1004C540，ECX watcher + float，ret4 | 验证完整包装体，关联真实 map caller 后原样转发自然调用 |
| 实际 amount 和具体实现 | 包装体读取 watcher+0x24，并以两个 float 调用 0x1004AA10；实现 ret8 | 由真实指令提取并校验 amount，不套用未验证 SDK 对象布局 |
| 实际选样 | 内部 rel32 调用 0x10047FC0；ECX watcher、info*/currentTime/amount/noMore* 四参数，ret16；写入 bool、三个索引及 frac | 验证完整函数体、输出结构/分支有效性、同一 origin 作用域的输入输出 |
| 样本容器 | 本机选样路径有 ring buffer 的 count/head/capacity 与 16-byte 元素访问 | 独立校验边界与所有索引；只读取少量实际选择样本，失败则 unknown |

这些磁盘地址用于说明静态证据，不是运行态硬编码地址，也不证明本次游戏已经命中了具体 watcher。下一轮必须从实际条目及验证过的 rel32/vtable 链解析，并保留严格失效关闭条件。

## 5. 假设与下一轮实施计划

按当前证据排列：

1. **origin 选样信息不可用，或有效时间没有可用区间。** 若成立，异常空中 watcher 会实际调用，但 GetInterpolationInfo 返回 false，或 newer/older 退化且 effectiveTime 与样本区间不符；普通下落能形成有效区间。先记录实际 return、amount、effectiveTime、count 与选样结果。
2. **穿越后 origin 历史更新/重建不完整。** 若成立，实际样本时间或位置持续不推进、重复/空白，与 current origin/simulation time 不一致，落地附近重新具备有效历史。区间正常但样本值相同也应归到这项继续追 latch/恢复，不直接改 amount。
3. **具体 watcher 的写入目标或写回路径异常。** 若前两项不成立，校验 watcher 实际 value 绑定是否等于 entry.data；对照 watcher 前后、Part1 返回及 outer abs。若内部确实生成变化却随后丢失，再跟覆盖来源。当前没有 local→abs 丢失证据，此项优先级较低。

下一轮仍建议做有明确新判别信号的只读诊断：

- origin watcher 的真实调用、caller/对象身份、return 和真实本地位置前后值；angles 仅作为已有对照，避免扩大 Hook 面。
- 直接观察原生 GetInterpolationInfo 的自然调用，记录 actual currentTime/amount/effectiveTime、return、noMore、Hermite、newer/older/oldest、frac。输出只有对应字段已证明初始化且有效时才读取/解释；不能把 false 当作所有输出都未写，也不能把部分默认索引当作有效样本。
- 基于完整指令和独立容器路径校验实际 ring buffer，读取 count 与选中样本的时间/位置；最多有限几个样本，不扫描整个 history，不写入或主动额外调用插值。
- 严格关联 capture/txn/frame/map scope；正常下落先验证观测，再对比地板门。安装/身份失败应明确报告原因，不能重回只有 eligibility 的同类日志。
- 清理观测歧义：拆分位置/角度 outcome，删除剩余误标 recv 为 local 的字段；保留本轮已验证的必要父链及汇总，压缩冗余证明行，日志按状态变化/异常输出，全接受帧计数继续独立于明细预算。
- 仍保留普通下落两次、墙门步行/跳跃回归、地板→墙面五次；补高速度墙门对照仅在方便安全时进行。未来给用户的控制台指令仍以英文 `;` 结尾，无需新增主观标记或更多 A/B 开关。

这一轮值得继续：新增数据已明确排除之前的两类分支，且下一层存在具体原生入口，能产生不同于当前 map 条件的实际选样证据。它仍不构成主观改善承诺。

继续／停止门槛：

| 下一轮结果 | 动作 |
| --- | --- |
| 正常下落都无法验证 watcher/输出身份 | 修正诊断或报告能力缺口，不让用户重复无效测试 |
| 明确空 history、无有效取样、时间错位或样本值失真 | 沿其实际 latch/预测恢复链定位，再提出单因子修复；不先变更时间常数 |
| 具体 watcher 写入正确，但 Part1/outer 丢失 | 转查已证明的覆盖/传播边界 |
| watcher、选样、样本和写入全正确，却仍无法解释本地位置阶梯 | 暂停当前局部插值补丁路线，讨论完整预测/历史生命周期的复刻范围 |
| 无法安全验证目标 ABI/容器，或没有新的区分信号 | 在编码阶段止步并报告，不做一轮只有相同日志和相同体验的实测 |

本轮不建议调整 shadow 尾巴、回正曲线、MoveType、ground setter 或 brush。需要补齐的是更靠近官方的实体位置插值链证据；从证据指向的缺口再决定行为修复。
