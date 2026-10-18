# 第 16 轮分析：Part1 未提前退出、选时持续推进；修正本地位置的观测盲点

日期：2026-10-08。输入：`TestLog/高速穿越连续性修复/16/portal_l4d2_traversal.log`。

本轮仅分析、记录技术结论与下一步方案；不修改产品代码、不构建、部署、提交或推送。遵循系统化诊断与仓库 SDK 研究流程，区分运行证据、二进制静态证据与尚待验证的假设。

## 1. 结论与测试有效性

测试有效，不需要用同一 DLL 整轮重测。主观没有变化符合本轮只读诊断的设计，并不说明探针没有生效。原来的视觉阶梯仍在；这次排除了更内层的提前退出和选时停住，但还没有定位最终根因。

- 日志为 6,243,098 bytes、13,601 行。按行流式聚合，只抽查关键事务/帧，没有整份载入对话。
- 第 3 行身份 `pe-6AB40E57`，路径为游戏根目录 `L4D2_Portal.dll`，与第 16 轮部署记录相符。
- 第 77 行 `PortalNativePart1Hook`：installed、outerInstalled、signatureValidated、abiStaticValidated 均为 true；四个栈参数、ret16，targetRva=0x48EB0、callerRva=0x3454A。
- 15 个 begin/summary 配对：普通下落 2 次、墙门 8 次（txn 1–8）、地板→墙面 5 次（txn 9–13）。首次墙门穿越仍只作预热。
- 共 **5,463 个接受帧、5,463 次 Part1 调用、5,463 次 caller 匹配**，全部 CONTINUE。stop、unknownResult、invalid、notObserved、noCall、outerWithoutPart1 均为 0。
- 全程 singleeyehistory=0，不是行为 A/B。各窗口 cvar 快照一致：cl_predict=1、cl_interpolate=1、cl_interp=0、cl_interp_ratio=1、cl_smooth=1、cl_smoothtime=0.1、host_timescale=1、fps_max=300。
- 明细 suppressed、commandRowsDropped、floor rows dropped 为 0；sameAs 阶段引用全部可解析。明细仍是事件抽样，不是全部渲染帧；summary 才汇总全部接受帧。

墙门有 4 次空中事务 txn 5–8，但已输出中后段空中速度约 220–318；地板门入口约 707–735，尚不是同速度、同方向的完整对照。因此可以确认当前墙门回归窗口正常，不能据此宣称排除了所有高速水平运动条件。下一轮若安全方便，可补少量约 700 的墙门空中对照；不要求为此冒险掉出地图。

## 2. 新证据：不是 Part1 STOP，也不是选时停止

所有窗口的 Part1 输入时间都被原生重选，返回时间比输入时间晚 **0.066650–0.066673 秒**，约为本次 0.033333 秒 tick 的两倍。正常下落、墙门、地板门一致；selectedStalled=0、selectedBackwards=0。

可以关闭的解释：

1. 被观测帧的父类在 Part1 前提前返回。
2. 被观测帧的 Part1 走 STOP，完全跳过内部插值循环。
3. 选中时间在输入推进时停住或倒退，或者地板门独有一个明显不同的时间偏移。

**仍不能说“插值时间完全正确”**：是否位于 origin watcher 的有效历史区间，要同时知道历史时间戳和插值量；本轮没有这些数据。也不能直接减掉这 66.7 ms，正常对照采用同样的原生选时。

`noMore=false` 也不代表 origin watcher 正在工作。它是多个插值变量聚合后的结果，且可能被原生 ALWAYS_INTERPOLATE 条件覆盖。不要把日志中的网络 `effects` 位与本机 Part1 使用的内部客户端标志混为一谈。

## 3. 原来的阶梯仍然成立，且不依赖持续回正

剔除最初两 tick、落地阶段后的**已输出空中边界样本**：

| 场景 | 样本数 | 外层 Interpolate 中有实质位置变化 | 返回后 abs 等于 recv-field |
| --- | ---: | ---: | ---: |
| 普通下落两次 | 46 | 46 | 0 |
| 墙门空中 txn 5–8 | 47 | 47 | 0 |
| 地板→墙面 txn 9–13 | 201 | 0 | 201 |

这里的变化指外层 abs 位移超过 0.002 units，与 §4 中有语义缺陷的 Part1 `changed` 无关。

五次地板门的完整窗口统计：

| txn / capture | 帧数＝Part1 调用数 | 最终视图累计冻结 ms | 最长单段 ms | 最大单帧位移 units | summary 证据行 |
| --- | ---: | ---: | ---: | ---: | --- |
| 9 / 11 | 382 | 350.240 | 32.558 | 29.967 | 8438–8439 |
| 10 / 12 | 380 | 346.553 | 30.301 | 29.961 | 9612–9613 |
| 11 / 13 | 375 | 365.690 | 30.077 | 29.696 | 10921–10922 |
| 12 / 14 | 377 | 339.763 | 30.188 | 28.918 | 12255–12256 |
| 13 / 15 | 372 | 339.264 | 30.669 | 29.338 | 13503–13504 |

共 1,886 帧，外层与 Part1 均无缺失调用，外层返回后的 abs 与 render-start 结束值无差异。约 339–366 ms 是整个约 40 tick 窗口内多段冻结的累计，**不是一次停顿 0.35 秒**。不能把它比第 15 轮更小解释为修复改善：本轮只加诊断，而且飞行轨迹/接触时机不同，没有受控行为 A/B。

五次合计 121 条 roll=0、State inactive、shadow=false 的空中事件样本，其中 61 条最终视图冻结；原生位置在这些样本的调用中均未变化。事件输出偏向边界，不能把 61/121 当全帧冻结概率。

可复核例子，txn 9：

- 第 7532/7538/7539 行，frame 43929、tick 4285：roll=0，State/shadow 均已停止，速度约 771.94；最终位移为 0，外层 native 位移为 0。Part1 CONTINUE，selectedTime=142.915787，仍在推进。
- 第 7547/7553/7554 行，frame 43935、tick 4286：最终视图一次移动 25.8723 units，外层 native 调用前已在新位置、调用中仍无位移；selectedTime=142.935806。
- 同一边界客户端 movement 输出从 `(-4124.695,2096.549,390.352)` 推进到 `(-4149.140,2097.549,381.935)`；水平速度约 -733.333 保持，竖直速度从 -239.187 到 -265.853，符合一个 tick 的重力变化。位置在 tick 上推进，但 tick 内渲染缺少连续位置变化。

这支持视觉位置更新阶梯，不证明真实速度变量完全停顿。部分服务端记录 `stateValid(origin=false velocity=false)`，其中的零值是无效占位，不能据此诊断服务器速度归零。

首次观察到落地的 elapsedTick 分别为 21/21/22/21/21，首次观察到 grounded native 位置变化为 22/22/23/22/22，仍是落地后一个 tick。该相关性也伴随减速、接触和历史刷新，**不能据此再修改 SetGroundEntity**。txn 10 的 noMore=true 较多发生在整个窗口统计中，也不能用它替代空中 origin watcher 的参与证据。

## 4. 必须纠正的探针问题：recv 字段被命名成 localOrigin

本轮 `PortalNativePart1Summary changed=0` 不仅出现在地板门，也出现在两个正常下落和所有墙门窗口。这不是所有场景插值都坏了，而是上一轮内部位置探针的观测对象没有对齐真实本地插值字段。

代码证据：

- `src/Portal/PortalNativeDiagnostics.cpp:265–274`：Part1Begin/End 读 `player->m_vecOrigin()`、`player->m_angRotation()`。
- `src/SDK/L4D2/Entities/C_BaseEntity.h:219–220`：这两个访问器是 M_NETVAR。
- `src/Util/NetVarManager/NetVarManager.h:13`：按 recv 属性名解析偏移，而不是 GetLocalOrigin/GetLocalAngles。
- 本地 Portal SDK `c_baseentity.cpp:437–441` 将网络接收成员 `m_vecNetworkOrigin/m_angNetworkAngles` 暴露为 recv 名称 `m_vecOrigin/m_angRotation`，`:895–896` 却将真正本地成员绑定给 origin/rotation watcher。公开的 [Valve C_BaseEntity 源码](https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/game/client/c_baseentity.cpp) 也保留了这个区别。

运行反例（第 563–570 行，capture 1 / frame 25171）：外层 abs 在原生调用中移动 **4.9866 units**，但 Part1 的 `localOrigin` 前后相同。同时该探针的 rotation 是 `(32.736,-170.857,0)`，而引擎实际返回的 oldAngles 是 `(81.212,-85.783,0)`，显然不能把两者当作同一个本地旋转字段。

因此需要明确勘误：

1. 本轮 Part1 `localOrigin/rotation/changed` 只能按现有 recv/netvar 访问器的读数解读，**不能证明真正 local origin 没被插值器写入**。
2. Part1 的调用、caller、STOP/CONTINUE、时间引用、有效 oldOutputs，以及外层 abs 证据不受这个字段命名/来源问题影响。
3. 下一步既要考虑“watcher 没参与/没有有效历史”，也要保留“真正 local 已改变，但没有正确传播到 abs”的可能，不能预先排除后者。
4. 不全局改动现有 SDK netvar 访问器的语义，以免影响不相关代码；只为诊断建立明确区分 recv、native-local、outer-abs 的读取链。

这是诊断实现的缺陷，不是用户测试错误；也不是已经证明游戏粘滞由这个只读字段造成。

## 5. 本机二进制有新的可验证边界，不需要猜布局

本轮只读复核游戏实际 `left4dead2/bin/client.dll`，SHA-256：
`D2553CCA3A63BDD844864F5FEE2C20A233AB19DEE816463A90857B95309982A2`。

以下 VA/RVA 仅是当前二进制分析记录，不允许裸写为跨版本运行偏移：

- 已运行态验证完整函数体的 Part1 中，磁盘 VA `0x10048F8D` 起从 entity+0xA0/A4/A8 复制到 oldOrigin，`0x10048FB3` 起从 +0xC4/C8/CC 复制到 oldAngles；`0x10048F9F` 导出 entity+0x18 作为 map，`0x10048FD3` rel32 调用 `0x10045CD0`。
- 已验证父类的 `0x10034596` 调用 `0x100427C0`；这个 Part2 比较的正是 +0xA0 和 +0xC4 对应的三个分量，变化时置 POSITION/ANGLES 标志并调用失效更新。与上面的 oldOutputs 来源形成静态交叉验证。
- `0x10045CD0` 的原生循环根据条目的 needs 状态决定是否调用 watcher；循环中可见 map 数量/时基、12-byte 条目步长、needs word、watcher 指针以及虚表 +0x18 的调用点。循环及 watcher 自身仍需要独立运行态身份/ABI/绑定验证，不能照抄 SDK 类布局。

参考调用链：

`真实 Interpolate → Part1（选时、保存 local 旧值）→ Interp_Interpolate（按 entry 参与）→ watcher → Part2（比较 local、失效 abs）→ 外层 abs/CalcView`

SDK 分层来自本地 `c_baseentity.cpp:853,2795,2836`；L4D2 是否在每一层产生位置变化，应以本机真实调用为准。当前已具备 Part1 入口、完整指纹、四参数转发和外层观察；缺少的是可信 native-local 读数、origin entry 身份及 watcher 参与数据，不是再缺一个相机平滑公式。

## 6. 下一步建议：校正字段，同时追踪 origin watcher 的实际参与

建议再做一轮有明确区分能力的只读诊断，不直接修改行为，也不让用户再测同样的 Part1 问题。实施需下一轮授权。

### 6.1 先把观测链校准

- 将旧字段明确标成 recvField；新增 nativeLocalOrigin/nativeLocalAngles，记录其解析来源、地址关系和有效性。保留外层 abs。
- 从已验证函数的数据流解析本地字段，交叉核对 Part1 保存旧值与 Part2 比较值；读取前做范围、内存、有限值检查。CONTINUE 时比较 entry-local 与引擎 oldOutputs；STOP 仍不读未初始化输出。任何不匹配报 invalid，不将无效值计作不动。
- 正常下落必须成为验收对照：其真实 local 写入/外层 abs 关系应可解释，而不是再接受“所有场景 local.changed=0”的歧义。

### 6.2 只深入 origin/rotation 相关条目，不全量导出历史

- 从真实 Part1 call 导出的插值循环验证布局、调用约定；确认具体 entry 绑定的 data 地址就是上述 native-local，不能按条目顺序或名字猜。
- 记录有效条目数/参与范围、origin entry 是否存在、needs 前后值、map 时基及真实 watcher 调用次数；如需接入 watcher，必须先验证实际目标/ABI，只观察引擎自然调用，原样转发一次，不主动调用或修改返回值。
- 如果只能读到 needs，日志就明确标 `callObserved=false/unknown`，不能将 needs=true 自动写成“watcher 已调用”。
- 同一帧关联 selectedTime、entry 状态、native local 前后、oldOutputs、outer abs；覆盖穿越前、穿越后空中、首次接触/落地及恢复后的边界。
- 历史样本与 interpolationAmount 在布局未验证前继续标 unknown；这一轮不做猜测性 history 写入，也不承诺一次完成全部私有历史解析。

### 6.3 避免诊断堆积

- Part1 STOP/选时详细输出降为汇总和异常边界，保留低成本门禁；不继续重复打印已经证实的正常分支。
- 替换误导性的 localOrigin 字段，不再并列保留两套含糊的 changed 计数。只对活动本机窗口采样，沿用有界明细与全接受帧汇总，无新开关、无新主观标记。
- 不更改当前 singleeyehistory=0、shadow、回正曲线、Teleport、速度、ground、MoveType、brush 基线，不恢复任何已禁用旧路径。

### 6.4 每一步的继续／停止门槛

| 新证据 | 下一动作 | 不能做什么 |
| --- | --- | --- |
| origin entry 不在参与范围，或 needs/实际调用在空中特有地停止 | 沿注册、排除、latch/恢复生命周期追实际差异，再讨论一个针对性修复 | 不强制每帧调用 Interpolate、不直接置 needs=true |
| watcher 确实调用，但可信 native-local 没变化 | 才继续验证有效历史样本、插值量、选时是否落在历史区间 | 不因 +66.7 ms 直接回拨时间 |
| native-local 已变化，outer abs 却没变化 | 转查 Part2/失效与缓存传播边界 | 不继续指责 history 不参与 |
| 绑定/ABI/数据流不能安全验证 | 报告具体缺失能力，停止下发模糊实测 | 不猜偏移、不绕过验证 |
| 新增探针仍只能重复“调用了但不动” | 暂停当前补丁路线，讨论完整预测/历史生命周期的工程范围或用户的新思路 | 不无限追加相机尾巴或没有判据的下一轮 |

当前仍值得推进，原因是有一项明确的观测错误可校正，以及两个尚未区分的底层分支；不是认为再试一次就一定丝滑。下一轮若仍是只读版，不预期主观改善。行为修复必须在具体分支得到证实之后，按单因子验收，并回归墙门步行/跳跃和已完成的高速场景。

## 7. 可复核性与本轮变更范围

- 忽略的测试数据目录新增 `16/analyze-part1.cjs`，流式读取并按 frame 关联 Part1/outer/stages；汇总结果如上。复用 `15/analyze-native.cjs ... assert-evidence` 得到 `CAPTURED_STAIRCASE_PRESENT`、floorTransactions=5、observedFrames=1886、lateAirMeaningfulChanges=0。它故意返回非零表示日志中的阶梯仍存在，不是游戏自动复现，也不是产品单元测试失败。
- 修改范围仅为本文、技术总资产、第 16 轮测试说明的字段语义勘误及忽略目录分析脚本。既有工作区产品代码未动；没有新 DLL、构建、部署或提交推送。
