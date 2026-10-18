# 高速穿越第 13 轮：单一眼位历史与原生位置节奏

日期：2026-09-20。性质：日志分析和下一轮建议，**未实施新修复，未构建或部署**。

## 1. 输入与结论边界

输入为 `TestLog/高速穿越连续性修复/13/portal_l4d2_traversal.log`，约 6.04 MB、12,608 行。采用标签计数、事务筛选和局部上下文读取，没有把整份日志输出到分析上下文。

用户反馈：墙面双门步行、跳跃正常；地板到墙面两组均粘滞，没有明显改善。

结论：本轮单一 State 所有者确实生效，但“解决双重所有者即可解决地板→墙面粘滞”的充分性假设被否定。日志进一步显示，部分异常发生在 State 和 shadow 均停止工作之后，且来自原始眼位/实体位置的 tick 级停顿与集中位移。不能继续仅凭终端 residual 较小判断视觉已连续。

尚未证明原生位置阶梯的内部原因，也未证明它解释全部主观症状；尤其不能把 render-camera 位移比率直接当成服务端物理速度。

## 2. 测试是否生效

| 范围 | 场景/路径 | 能够确认的内容 |
| --- | --- | --- |
| txn 1–20 | 墙面双门，singleeyehistory 关闭 | 用户报告恢复正常；当前日志没有覆盖开启实验因子后的墙面回归 |
| txn 21–24 | 地板→墙面，控制组 | 固定 shadow 控制路径 |
| txn 25–31 | 地板→墙面，实验组 | 7/7 seed candidate、valid、applied 均为 true；无 shadow 回退 |

实验开关在第 7971 行切为 1；实验事务的 `shadowFrames=0`，7/7 以 `state-converged` 结束，释放 residual 为 `0.371..0.485` units。因此不是实验未启用或悄悄运行了控制路径。

第 24–31 次共 8 个主观标记均为 `perceived_sticky`。没有逐次标记的事务不擅自补写主观标签。

两组渲染帧间隔不完全相同：控制组部分样本约 3.333 ms，实验组大部分约 1.333 ms；tick 均约 33.333 ms。不能直接比较“冻结了多少帧”，也不能把帧数差异视为实验劣化的因果证据。下一轮应固定 FPS/时间尺度并记录实际值。

txn 29–31 后段存在长时间物理位置不动或异常大步长；txn 30 尤其不同。它们保留为独立样本，不用于推导常规自由飞行结论，本轮不追修此前暂缓的空中停顿。

## 3. 关键证据：交还相机后位置仍不连续

### 3.1 txn 25 的直接链路

- 第 8109 行：成功消费 official-command-end seed，State 接管。
- 第 8234 行，frame 61786：State 以 residual `0.371` 收敛释放。
- 第 8238–8239 行，frame 61787：`eyeHistory=false`、`shadowApplied=false`；原始眼位、adapter 和最终主视图位移都为 0，按该帧速度和时长预期约 `0.754` units。角度仍变化约 `0.8°`，不是整帧停止渲染。
- 第 8254–8255 行，frame 61798：tick 推进一次，最终视图单帧移动 `18.967` units，预期帧位移约 `0.776`；simulationTime 增加 `0.033325s`。
- 第 8418 行：roll 已在约 `0.374664s` 完成转正。
- 第 8437–8438 行，frame 62043：roll 已为 0，State/shadow 仍关闭，原始眼位与最终视图又一次集中移动 `23.072` units，预期帧位移约 `0.934`；simulationTime 再次推进约一个 tick。

后一个样本的 X 位移约 `18.222`，与 `546.667 × 1/30` 相符。这是 tick 级位置推进的证据，不是人物瞬间具有 24 倍物理速度。此前的重复相同位置，加上集中推进，可以解释“角度仍连续、画面移动有粘滞/赶进度”的一部分感受。

### 3.2 不只存在于实验组

控制组 txn 22/24 的全捕获窗口最终视图最大连续冻结均为 9 帧；实验组 txn 25/26 为 24 帧。结合各自帧间隔，均接近一个 tick，而非仅实验组出现的新现象。

txn 25 汇总的最终位移比率平均值为 `0.989`，但范围为 `0..25.594`，最终视图冻结累计 364 帧。平均值接近 1 可以与强烈的停顿/跳动共存，不能作平滑判据。

墙面跳跃 txn 15 的后段（第 4162 行）则出现 abs/render 与 network origin 不同、原始眼位按约 `0.27` units 的帧级步长推进的样本。墙面诊断窗口只有约 6 tick，而地板→墙面为约 32 tick；不能直接拿两个完整窗口的总冻结率作同条件比较，但原生来源不同值得追踪。

## 4. 当前方案哪里还不等同官方

### 4.1 相同的追赶曲线，不保证相同的输入

本地参考 `Reference Code/portal-sdk-2013/src/game/client/portal/c_portal_player.cpp`：

- `UpdatePortalEyeInterpolation`（1299 起）携带 `EyeFootPosition` 的帧间增量，然后在同一份历史上以 100 units/s、最大 lag 150 追赶。
- `EyeFootPosition`（1376 起）还按 roll 调整眼位：`abs(roll) / 180 × 0.75 × viewOffset`。
- `CalcPortalView`（1511 起）在其适用分支重新读取 Portal 的 EyePosition/EyeAngles；并不简单把所有普通 CalcPlayerView 处理后的输出当作历史的原始来源。

当前适配器在 L4D2 原始 CalcPlayerView 返回后更新 State，carry 的是该输出的差分；没有等价的 Portal EyeFootPosition roll 眼高路径。因此“一个所有者”只是贴近了状态所有权，不能称为完整复刻。

EyeFootPosition 是可核实的差异，但它不能单独解释 roll 已为 0 后 X 方向仍存在的冻结/跳进。暂不把它作为下一轮混合修复；原生位置节奏查清后再单因子补齐。

### 4.2 官方还有每帧的原生实体插值时基

本地参考 `c_baseentity.cpp:2795`，并与 [Valve Source SDK 2013 的 c_baseentity.cpp](https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/game/client/c_baseentity.cpp) 中 `BaseInterpolatePart1`、`Interpolate`、`ProcessTeleportList` 核对：predictable 实体使用 final-predicted-time、tick interval 和 interpolation_amount 选择插值时刻；禁用插值路径会回到最后接收位置；teleport/no-interpolation 状态还可触发历史重置。

这些是需在 L4D2 运行态验证的检查点，**不是已经确认 L4D2 命中了其中某个分支**。不能直接复用参考源码的偏移或槽位，更不能未经验证清除 EF_NOINTERP、强制入插值列表、补写真实位置。

如果输入眼位在一个 tick 内基本不动，State 的“真实增量 carry + 100 units/s 追赶”并不会自动重建人物 550–700 units/s 的逐帧轨迹。继续调整尾巴只能改变遮蔽这种输入阶梯的时长，无法恢复缺失的原生数据流。

## 5. 诊断自身的缺口

1. `src/Util/Logger/PortalFileLog.h` 白名单只含 `PortalFloorWallBegin`，没有 `PortalFloorWallCommand`、`PortalFloorWallServerCommand`。相关源码存在，但本次文件无记录；现有测试也只断言 Begin。不能据此说 movement/server hook 没执行。
2. cadence 的 cmd 来自 `simulator.GetContext().lastCommandNumber`。`ClearPhase` 清空该 context 后可显示 cmd=0、负 commandDelta；这不是实际 usercmd 中断。服务端记录仍存在下一条命令。
3. 当前 serverRunCommand 样本的 server origin/velocity validity 为 false，moveType 为 true。不能用无效字段证明权威物理速度完整连续。
4. 只有穿越后窗口，缺少同位置普通下落及穿越前基线；不足以区分穿越诱发、原有插值行为和其他 mod/测试环境影响。
5. 低残差、平均速度比不是当前症状的充分验收指标。需要冻结持续毫秒数、集中位移与 tick 的关系，并分 State 活跃、交接、转正结束后、落地前后统计。

## 6. 下一轮建议：查原生位置来源，不再增加相机尾巴

下一轮先做只读诊断，保留已验证墙门/空中方案，single-owner 仍不提升为默认。没有新证据前，不同时修改 roll 曲线、EyeFootPosition、碰撞或 movement。

### 6.1 三个待区分假设

1. **原生插值历史/时基在该场景未正常消费。** 预期：movement command-end 位置按 tick 正常推进，但 render-start 后的 abs/render 仍停在 network 端点，或所选时间/历史不足；需要与正常墙门及普通下落比较。
2. **实际 movement/prediction 的结果已异常，镜头只是如实显示。** 预期：command-end、预测重跑或有效服务端状态先出现停顿/改写，之后才传到相机。若成立，不能再修纯 render-eye。
3. **位置来源正常，而角度/roll 眼高语义造成另一部分不连续。** 预期：在统一时基验证逐帧位置连续后，症状仍严格绑定 roll；届时才测试官方 EyeFootPosition。当前 roll=0 的反例说明它不是全部解释。

### 6.2 最小可判别的编码范围

- 修复精确日志白名单及测试；恢复有界的 command-end/server 诊断，不全局输出每帧日志。
- 诊断事务和 command identity 脱离短寿命 traversal phase；状态切换后继续跟踪同一事务，失效字段显式记录 unknown，不能用 0 冒充实值。
- 利用已有 FrameStageNotify、prediction/ProcessMovement、CalcPlayerView、主 RenderView 接口，记录 render-start 原函数前后、相机读取前的 origin 来源与同帧 identity。
- 补记录 `curtime/frametime/interpolation_amount/tickBase/simulationTime`、prediction/replay 状态、已验证的 effects 和相关 cvar；需要新的 native 插值内部字段时先做可读性、签名和 ABI 验证，缺失时直接报告，不能猜偏移。
- 使用小环形缓冲，按 tick 边界、冻结开始/结束、State 释放、roll 结束和落地边界输出；加入完整捕获窗口的 summary。默认不新增一批需要用户手动输入的开关。
- 对无传送普通下落提供一次性自动窗口，避免要求高速过程中手动标记；用户只需按测试预设完成正常墙门、地板→墙门和普通下落。

### 6.3 继续/停止条件

值得再做这一轮，因为目标从“尾端 residual 优化”变成了可区分的“movement → 原生插值 → CalcPlayerView → 最终主视图”首个失真边界。

若能定位明确的历史重置、错误时基或 overwrite，下一轮只修这个点。若同条件原生来源正常而差异只剩 roll/眼高，则转入 EyeFootPosition 单因子。

若无法安全获取关键 native 状态，或者样本证明正确插值依赖当前无法补齐的 Portal 客户端预测/碰撞生命周期，就停止宣称“相机层小修即可复刻”；明确列出缺失接口，与用户讨论是否转向更完整的客户端 Portal prediction。**不再用延长 shadow、增加所有者或调追赶常数来回避这一界限。**

## 7. 本轮变更

仅新增此分析文档并在技术总结合入索引。没有修改运行代码、默认参数、游戏文件，也没有提交或推送。离线日志证据可复核，但不能替代游戏内最终验证。
