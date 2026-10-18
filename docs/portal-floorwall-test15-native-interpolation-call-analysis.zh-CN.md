# 第 15 轮分析：Interpolate 正常派发，但地板门后的空中位置未被插值

日期：2026-09-23。输入：`TestLog/高速穿越连续性修复/15/portal_l4d2_traversal.log`。

本轮只分析和制定下一步，不修改行为、构建、部署或提交。日志按行流式聚合，再按事务和行号抽查；没有把整份日志读入对话。复核脚本放在同一忽略的测试数据目录 `analyze-native.cjs`，不是产品代码。

## 1. 结论与测试有效性

本轮达到原定诊断目标，**不需要整轮重测**。已经可以排除已观测窗口内的“原生 Interpolate 没有调用”和“函数返回位置连续、随后到 render-start 结束又被改坏”两条解释；尚未确定原生内部为什么没有产生位置插值。

- 文件 5,424,555 bytes、12,446 行；14 个 begin 与 14 个 summary 配对，包含普通下落 1 次、墙门穿越 8 次、地板→墙面 5 次。
- 日志第 3 行的构建身份为 `pe-6AB29951`。现有构建源与游戏目录 DLL 的 SHA-256 均为 `CD23E74DEA6298D2E0178CB939FAFFDD68457E2B308E118FDE49F5A5524C0EC9`，与上轮部署记录一致，没有旧 DLL 证据。
- 第 76 行 Hook 为 installed=true，image、RTTI/dispatch、signature、static ABI 均通过；槽位 107、targetRva=0x222220、parentRva=0x344F0。
- 所有窗口共 4,776 个被 tracker 接受的帧、4,776 次调用。没有无调用帧、无效调用样本或返回后位置差异；已输出样本均确认在 render-start 原函数期间调用，返回后比较有效。此计数不代表整个游戏进程的所有帧。
- `singleeyehistory=0`，不是行为 A/B。cvar 快照一致：cl_predict=1、cl_interpolate=1、cl_interp=0、cl_interp_ratio=1、cl_smooth=1、cl_smoothtime=0.1、host_timescale=1、fps_max=300。
- suppressed、commandDropped、floorDropped 均为 0；压缩阶段的 sameAs 引用全部可解析，没有靠缺失数据得到零差。

覆盖有两项不足，但不影响此次地板门主结论：普通下落只录到一次，而非要求的两次；墙门空中事务仅 txn 6–8 三次，入口三维速度约 229/238/262，没有形成与地板门约 700 速度匹配的完整高速双向回归。后续若要排除“高速水平运动特有条件”，应补少量匹配样本，不能把此次墙门结果泛化到全部速度。

墙门 txn 4 的窗口被 txn 5 替换，跨事务边缘最大 step=104.053；不能把这项当作新的单帧闪屏。首次 txn 1 仍只用于预热，不用于稳定体验判断。

## 2. 调用了，但位置没有在调用中改变

以下是排除最初两 tick 和落地后的**已输出空中边界样本**，不是全部帧的百分比：

| 场景 | 样本数 | Interpolate 调用中位置有变化 | 调用后 abs 等于 recv-field |
| --- | ---: | ---: | ---: |
| 普通下落 | 23 | 23 | 0 |
| 墙门空中 txn 6–8 | 40 | 40 | 0 |
| 地板→墙面 txn 9–13 | 224 | 0 | 224 |

五次地板门分别为 44/43/46/46/45 条，全部 native position delta=0。其外层输入时间随帧推进、等于当时 global curtime；这**不能证明 Part1 内部最终选时正确**，目前 selectedTime/history 仍 unknown。

地板门全窗口 summary（包含落地阶段）如下。这里的 changed 是全部接受帧的调用位置变化计数，不能与上表的空中事件样本混用：

| txn / capture | 帧数＝调用数 | changed | 最终视图累计冻结 ms | 最长一次冻结 ms | 最终视图最大单帧位移 units |
| --- | ---: | ---: | ---: | ---: | ---: |
| 9 / 10 | 379 | 31 | 403.926 | 31.521 | 29.844 |
| 10 / 11 | 373 | 151 | 412.714 | 30.756 | 30.435 |
| 11 / 12 | 371 | 29 | 439.477 | 32.501 | 29.847 |
| 12 / 13 | 365 | 142 | 432.999 | 31.871 | 29.190 |
| 13 / 14 | 374 | 18 | 437.533 | 30.431 | 29.960 |

这五个窗口共 1,862 帧。约 404–439 ms 是一个约 1.33 秒窗口内多段冻结的累计，**不是一次停了 0.4 秒**；单段约一个 simulation tick，随后集中前进约 25–30 units。普通下落和本轮八个墙门窗口的最终视图累计冻结均为 0。

### 可以复核的单个边界

txn 9 的 roll 于第 7707 行结束；shadow 于第 7749 行 horizon-expired。随后：

- 第 7761/7767 行，frame 35281：roll=0，State inactive，shadow=false；速度非零，但原始/最终视图位移为 0，native 调用 1 次、位置变化 0。
- 第 7775/7781 行，frame 35285：新 tick 的原始/最终视图一次移动 25.0696 units；native 调用前已经在新位置，调用本身仍无变化。
- 第 7784/7790 行，frame 35286：同 tick 下一渲染帧，currentTime 从 236.102234 增至 236.105713，interpolationAmount 从 0.066912 增至 0.171183；abs 仍为 `(-4140.337,1876.216,361.170)`，native 调用前后相同，最终位移再次为 0。

五次合计 146 条 roll=0、State/shadow 均已停止的空中事件样本，native 位置变化全部为 0，其中 73 条最终视图冻结。由此可见，剩余阶梯不依赖正在回正的 roll，也不是只有相机交权那一帧。

穿越前 prelude 在约 627–733 的下落速度下仍能插值。例如 txn 9 第 7354–7375 行，native delta 依次为 18.5544、19.5074、20.3165、21.0723。因此不是全局关闭 cl_interpolate，也不能笼统归因于“速度高就不支持插值”；但垂直高速与水平高速仍不是完全匹配的对照。

## 3. 新线索：落地附近恢复，但不能直接归因于 ground

五个地板门事务首次观察到落地分别为 elapsedTick=23/23/24/24/24；首次观察到有实质位置变化的 grounded native 调用分别为 24/24/25/25/25，均在其后一个 tick：

| txn | 恢复证据行 | native position delta |
| --- | ---: | ---: |
| 9 | 8096 | 0.6841 |
| 10 | 9105 | 7.3097 |
| 11 | 10120 | 0.6155 |
| 12 | 11152 | 7.2450 |
| 13 | 12132 | 0.6187 |

因此 summary 中的非零 changed 不能理解为“空中偶尔已经修好”，它主要来自落地后的恢复阶段。这是五次一致的生命周期相关性，值得用作下一轮分支/时间探针的触发边界。

但落地同时发生接触、减速、状态与历史刷新，当前无法证明 `ground` 是根因。尤其这些事务后段还在 x≈-4399 接触附近几何；不能把实际碰撞导致的速度下降自动归为传送连续性缺陷，也不应据此再改 SetGroundEntity。

尚在自由飞行时 command-end 位置按 tick 推进、水平速度基本保持、竖直速度按重力变化，渲染帧之间却没有相应位置插值。这足以说明一个**视觉位置更新阶梯**，不等于证明游戏速度变量全程停顿，或已排除所有物理问题。

## 4. 官方分层与本机 ABI：下一层有明确可验证入口

本地 Portal SDK 参考：

- `Reference Code/portal-sdk-2013/src/game/client/c_baseanimating.cpp:4216`：父类 Interpolate 的提前返回、Part1 STOP 和正常路径都可能返回 true，不能把返回 true 当作“位置插值已执行”。
- `Reference Code/portal-sdk-2013/src/game/client/c_baseentity.cpp:2795`：Part1 区分提前停止、预测玩家时间选择、Interp_Interpolate 和后续处理。
- 同文件 `:853`：Interp_Interpolate 根据 entry 的 needs 状态调用 watcher，并维护插值时基；函数被外层调用不保证 origin watcher 每帧有可用历史。

Valve 当前公开的共同基类源码也支持这些分层语义：[C_BaseAnimating::Interpolate](https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/game/client/c_baseanimating.cpp)、[C_BaseEntity / BaseInterpolatePart1](https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/game/client/c_baseentity.cpp)。这是共同基类的参考，不是宣称当前公开 Source SDK 与本机 L4D2 的二进制 ABI 相同。

本轮进一步只读反汇编实际 `left4dead2/bin/client.dll`：已验证父类的 call 位于磁盘 VA 0x10034545，目标为 0x10048eb0；后者调用 0x10045cd0 的原生插值循环。这些只作为当前二进制定位证据，不能编成固定 RVA。

重要差异：本机 Part1 通过 ECX 传 this，四个栈参数为 currentTime 引用、oldOrigin 引用、oldAngles 引用和 noMore 引用，`ret 0x10`。本地 SDK 版本还带 oldVelocity，共五个参数。**下一轮不能直接照抄 SDK 的函数 typedef**。当前已取得静态调用关系和 ABI 证据，但尚未接入 Part1 的运行态只读探针，不能声称内部 STOP/选时已经确认。

## 5. 下一步：仅深入一个原生边界，不继续堆叠相机补偿

推荐下一轮为 Part1 分支与最终选时的只读诊断，行为基线保持不变：

1. 从已验证父类的 rel32 导出 Part1，验证模块范围、完整指纹及四参数调用约定；不猜偏移、不绕开不兼容 detour。只观察真实调用并恰好转发一次，不主动调用插值。
2. 在活动本机玩家事务中记录 Part1 是否进入、STOP/CONTINUE、noMore、入口时间与返回后的时间引用、调用前后 origin/angles。以帧身份关联现有 outer Interpolate 记录；对其他实体只转发，不输出。
3. 优先区分三个具体结果：父类未进入 Part1／Part1 STOP；Part1 CONTINUE 但选时异常；Part1 CONTINUE 且选时推进正常、origin 仍无变化。最后一种才进一步追 origin watcher 的参与条件和历史生命周期。
4. 若确实需要读 VarMap/历史，先独立验证真实布局及调用关系；没有验证则继续明确标 unknown，不能用刚看到的静态字段偏移直接写 history。当前首轮诊断不要求先实现完整私有历史读取。
5. 把 prelude、穿越后的空中阶段、首次接触/落地、恢复插值的 tick 放在同一时间线。日志用全帧汇总加分支/时间状态边缘，避免新增无界逐帧输出。

待印证假设和决策：

| 待印证问题 | 本轮支持/限制 | 下一步如何区分 | 继续或停止 |
| --- | --- | --- | --- |
| 入口没派发／返回后被覆盖 | 已观测帧均调用，无返回后差异 | 不再围绕这两条新增补丁 | 关闭这两条主分支，保留低成本计数 |
| 父类提前返回或 Part1 STOP | 外层返回 true 无法区分 | Part1 进入与返回分支 | 若命中，研究实际条件，不强制 CONTINUE |
| 原生选择了不适合历史的时间 | 外层时间推进正常，不代表内部正确 | 比较输入和 Part1 改写后的时间及正常对照 | 有差异才提出时基单因子修复 |
| origin watcher 不参与／历史失去有效区间 | 空中无变化、落地后恢复与其相容，尚未证实 | 若 CONTINUE 且选时正常，再验证 needs/history 生命周期 | 有可验证入口才继续；禁止猜偏移写入 |
| 当前路线是否仍值得继续 | 两条分支已排除，有五次一致的内部分歧 | 下一层可给出新的离散判据 | 值得再推进这一层，不保证下一轮体验改善 |

停止条件：入口身份/ABI 不能可靠验证则 fail closed；内部时间或历史没有安全获取方式则报告缺失能力并暂停；如果新增探针仍只能重复“调用了但不动”、不能提供可区分证据，不让用户无限重测。若最终证明必须完整移植 Portal 专用预测/历史生命周期，先讨论工程范围，不以新的相机补偿冒充官方实现。

后续修复的验收不能只看平均速度或 residual：需要地板门自由飞行的原生位置实际在帧间推进、最终冻结显著消失，同时墙门步行/跳跃保持基线。首次卡顿继续单独排除。

## 6. 本轮可复核性与变更范围

流式脚本的 `audit` 得到 14 个配对窗口、4,776 帧/调用、aliasMissing=0、lateFloorAir=224、lateFloorChanged=0。`assert-evidence` 输出 `CAPTURED_STAIRCASE_PRESENT` 并故意返回非零，表示捕获日志中的阶梯条件仍成立；它不是游戏自动重现或修复回归测试，也不把主观体验自动化。

本轮仅新增本分析、更新技术总资产及忽略目录内分析脚本，保留工作区已有源码/研究修改。没有变更 DLL、配置或游戏行为，没有构建部署、提交推送。
