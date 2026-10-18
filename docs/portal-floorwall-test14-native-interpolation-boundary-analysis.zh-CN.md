# 高速穿越第 14 轮：定位到原生渲染插值边界

日期：2026-09-22。性质：日志诊断及下一轮建议，未修改运行逻辑，未构建、部署或提交。

## 1. 结论

本轮数据可用，**无需因测试顺序或次数偏差整轮重测**。第 13 轮的“先检查位置来源，不再延长相机尾巴”方向得到支持：

1. 普通下落有正常的 render-start 位置插值；地板门入口前也能看到同样的插值。
2. 地板→墙面后的有效自由飞行中，客户端 movement command-end 持续按 tick 推进，但 `FRAME_RENDER_START` 原函数返回后，abs/render 仍停在非插值端点，之后原始相机和最终主视图继承这个阶梯。
3. 该现象在 roll=0、State/shadow 均停止输出时仍存在。它不是“只有转正曲线太慢”或“终端 residual 太大”。

目前定位的是**最早已观测到的失真边界**，还没有识别 L4D2 内部的具体错误分支。render-start 前后相同不等于证明 `Interpolate()` 没调用：函数未调用、历史被重置、时间选择导致取端点、插值结果在原函数内部被覆盖，都可能产生这一结果。尚不能给出确定的 native 修复补丁。

## 2. 数据完整性和测试偏差

输入：`TestLog/高速穿越连续性修复/14/portal_l4d2_traversal.log`，12,233,682 bytes、25,273 行。按流式标签统计、capture/transaction 聚合、局部证据行读取，没有整份日志输出。

- 日志 build ID 为 `pe-6AAFD6AD`，与当前 Debug DLL 的 PE 时间戳一致；29 个 `PortalNativeCadenceBegin version=1` 均有 Summary。
- 29 个窗口：普通下落 2 次，穿越 27 次。采样阶段及 input identity 有效；没有 native frame/command budget 丢弃。
- 28 个窗口正常完成；txn 26 的窗口被 txn 27 替换，保留了 39 个命令，不是日志丢失。
- 29 个窗口的环境一致：`fps_max=300`、`host_timescale=1`、`cl_predict=1`、`cl_interpolate=1`、`cl_interp=0`、`cl_interp_ratio=1`、`cl_smooth=1`、`cl_smoothtime=0.1`。实际 frametime 仍有少量长帧，不能把 FPS 上限当成每帧耗时保证。
- `PortalFloorWallCommand` / `PortalFloorWallServerCommand` 各 396 条；上一轮的白名单缺口已补齐。服务端 origin/velocity 仍为无效字段，不能拿它们的零值或派生 exitDepth 推导权威速度、卡墙等结论。
- `finishmove-post-original` 的 `replayOrSameCmd=true` 是同一命令的 ProcessMovement → FinishMove 配对，不是重预测风暴。按 FinishMove 单独统计，窗口内 command number 都逐一递增。

| 分组 | 事务 | 本日志实际覆盖 |
| --- | --- | --- |
| 普通下落 | capture 1、2，txn=0 | 无传送，2 个完整窗口，最高记录速度约 693/697 units/s |
| 墙门控制组 | txn 1–7 | 步行 5 次（含首次预热），跳跃双向各 1 次 |
| 地板→墙面控制组 | txn 8–12 | 5 次，均 Blue→Orange，live/projected 均有 |
| 地板→墙面实验组 | txn 13–19 | 7 次，均 Blue→Orange；txn 18 不属于有效自由飞行样本 |
| 墙门实验组 | txn 20–27 | 步行双向各 2 次，跳跃双向各 2 次 |

实验开关在第 12520 行启用；第 13601 行重复设为 1，没有新的 mode-switch reset。实验 consume 全部成功，未回退 shadow。先做实验地板门、再做实验墙门的顺序不影响分组。

与原测试安排的偏差及处理：

- 控制组墙门跳跃只有双向各 1 次，而非各 2 次；其空中总速约 318 units/s，不能算对过去极高速墙门基线的完整压力验收。足够提供本轮原生插值对照，不足以宣布所有高速回归通过。
- txn 26→27 间隔不足完整捕获窗口，故 txn 26 后段不作为完整时长比较。
- txn 18 的主视图窗口全部 `velocity=0`、`FL_ONGROUND`，其零冻结统计不代表丝滑飞行；冻结判据要求有非零运动预期。本轮排除它，不展开追修该独立停顿/接触现象。
- 有落地/接触后降速至 0 的后段，只分析落地前。正常重力造成 3D 速度变化，不等于速度连续性错误。
- 首次 txn 1 的长帧/预热仍按既定优先级排除。

下一轮只需补足有针对性的回归覆盖，不需要现在用同一 DLL 重跑整套。

## 3. 全窗口量化结果

冻结时间来自全帧 tracker 的引擎 frametime 累计，不是已输出边界行的数量，也不是一次连续停顿的时长。当前 timescale=1；窗口最长 40 tick（约 1.33 秒），但包含不同长度的落地后片段，不能直接把组间总量差异当成因子效果。

| 场景 | 原始相机累计冻结 | 最终主视图累计冻结 | 最终最大连续冻结 |
| --- | --- | --- | --- |
| 普通下落 ×2 | 0 ms | 0 ms | 0 ms |
| 地板→墙面控制 txn 8–12 | 569.299–721.190 ms | 316.289–480.595 ms | 30.376–32.543 ms |
| 地板→墙面实验 txn 13–17、19 | 566.707–661.159 ms | 401.628–599.249 ms | 30.527–31.477 ms |

地板门两组的最终最大单帧位移约 28.730–30.072 units。这不是单帧物理速度暴涨，而是位置多帧不变后，集中显示一个 tick 的位移。

进一步筛选 `elapsedTick > 2` 且未落地的**已输出边界样本**，以 0.002 units 容差比较：

- 有效地板→墙面 11 次，共 435 条：435/435 的 render-start 前后 abs 相同，且 post abs 与 `recvField` 相同。
- 普通下落共 46 条：0/46 相同，能观察到插值。
- 墙门跳跃共 77 条：0/77 相同，能观察到插值。
- 上述 435 条的 `effects` 均不含 `EF_NOINTERP=0x8`；moveType 均为 WALK（2），而不是 noclip。故“no-interp 标志一直挂着”不足以解释后续冻结。

这些是事件采样计数，不是全渲染帧比例。`recvField` 仍仅表示本工程 `m_vecOrigin()` 访问器读到的字段，不能未经内部验证改称权威网络历史或 interpolator 本体。

## 4. 可复核的证据链

### 4.1 普通下落和过门前，插值确实在工作

普通下落 capture 1：

- 第 659–660 行：cmd 2011 FinishMove 的位置为 `(-4187.307, 2157.277, 363.142)`，实体位置与 move 一致。
- 第 662 行：render-start-pre 保持该端点。
- 第 663 行：render-start-post 变为 `(-4187.413, 2161.426, 364.019)`，`recvField` 仍为端点。
- 第 664 行：原始 CalcPlayerView 读取到了这个插值位置。

txn 8 的入口 prelude 第 6850–6854 行也有同样现象：在下降速度约 733 units/s 时，render-start 前的 `z=28.031`，后为 `z=51.533`。因此不能把当前现象简单归结为“原生引擎本来就只有 30 Hz 更新”或“只要速度高就绝不插值”。不过入口的高速竖直下降也不能替代同等高速水平飞行的完整对照。

### 4.2 控制组 txn 8：转正结束后仍然阶梯推进

- 第 7230 行：roll 在约 0.274 秒结束。
- 第 7290–7304 行：frame 27070→27071，tick 都为 4163，`interpolation_amount` 从 `0.021205` 增到 `0.309907`，curtime 正常推进；abs/render/recv-field 却完全不变，原始与最终眼位 delta 都为 0。
- 第 7300–7304 行：`effects=0x4`、`flags=0x80`、moveType=2，眼位历史和 shadow 已关闭，roll=0，仍在空中。
- 第 7311 行：下一命令的 FinishMove 正常推进位置；第 7315–7317 行最终眼位一次跳进约 26.03 units。

自由飞行阶段 X 速度维持约 `-733.333`；每个命令 X 位移约 `-24.444`，Z 速度每 tick 约减 `26.667`，与该段重力运动一致。之后落地/接触降速另行排除。本证据支持“物理按 tick 继续走，呈现没有帧内插值”，但不替代无效的服务端速度字段。

### 4.3 实验组 txn 13：不是实验没启用，也不是 State 还在输出

- 第 12650 行：single-owner seed `candidate/valid/applied=true`、`fallbackToShadow=false`。
- 第 12945 行：frame 38109 State 收敛结束，residual=0.388。
- 第 12956 行：roll 在约 0.224 秒结束。
- 第 13000 行：cmd 5471 FinishMove 为 `(-4124.555, 2011.151, 377.634)`，速度 `(-733.333, 29.681, -244.657)`。
- 第 13004–13017 行：frame 38124→38125，interp 从 `0.087468` 到 `0.189222`，实体和眼位没有推进；State/shadow 均 inactive，roll=0。
- 第 13025、13029 行：cmd 5472 正常给出下一端点 `(-4149.000, 2012.141, 369.034)`；最终相机集中推进 25.9318 units。

原始相机→适配后相机没有新增位置跳变；最终视图有约 0.793 unit 的 Z 偏移，但冻结/跳进节奏与原始相机一致。不能把本例的主失真定位到最后 RenderView 才产生。

## 5. 实验组墙门的旁证与边界

控制组墙门跳跃 txn 6、7 的最终冻结均为 0；实验组 txn 24–27 分别累计 36.949、30.202、49.465、24.238 ms，最大单次约 24.238–30.282 ms。

两组原始眼位早期都有约 55–62 ms 的累计冻结；实验组 State 在 consume 后约 1–11 帧就收敛释放，早于部分原生过渡结束，最终输出因此暴露了短暂阶梯。2 tick 之后，墙门样本的原生插值恢复，与地板门的持续异常不同。

这不是本轮只读诊断“修好了/破坏了”的新行为结论，也没有逐次主观标签可作严格相关分析；但它足以阻止将 `singleeyehistory=1` 升为默认。保留已验证的控制路径，下一轮先固定 `singleeyehistory=0` 查 native 层，避免把一个尚未通过回归的因子同时作为修复底座。

## 6. 官方差异与下一轮计划

本地 Portal SDK 2013 `src/game/client/c_baseentity.cpp`：

- `BaseInterpolatePart1`（2795 起）在 following/disabled 分支回到接收端点；predictable 实体还会改用 final-predicted-time、tick interval 和 interpolation_amount 选择时间，再调用 VarMap 插值。
- `ProcessTeleportList`（3013 起）在 teleport/no-interpolation 状态下调用 `MoveToLastReceivedPosition(true)` 和 `ResetLatched()`。

本轮证明外层 curtime/interpolation_amount 有推进、EF_NOINTERP 没持续保留，却没有记录内部实际采用时间、history 样本和分支。当前工程的虚函数声明不等于已验证运行态槽位/ABI，**不能据此直接调用或强制写私有字段**。

下一轮是更窄的 native 只读诊断，而不是再次调整相机算法。按优先级区分：

1. **插值调用/列表/启用状态异常。** 预期：正常下落/墙门调用原生插值，地板门没有，或走了 STOP/端点分支。首先验证本机实体 `Interpolate` 入口及实际派发，记录每帧调用次数、输入时间、返回值、调用前后 origin；保留外层前后快照区分“没调用”和“调用后又被覆盖”。
2. **历史或内部时基异常。** 预期：函数被调用，但 selected time 不在有效历史内，或历史持续被清空/同值重填。只在运行态验证通过后，读 final-predicted-time、predictable、origin watcher 的时间范围/数量/needs-interpolation，以及 ResetLatched 的发生时机。不恢复之前证伪的 entity-history mutation、固定 RVA 或绕过原函数。
3. **地板门特有的 native 分支/后续覆盖。** 预期：插值函数本身给出连续位置，而 render-start 返回时被覆盖；届时只追踪两者之间的实际写入者。若仅地板门异常但同速水平无传送数据不足，再要求小规模定向对照，不要求盲目全量重测。

实施边界：

- 继续保留现有物理、brush、速度变换、roll 曲线、已验证 shadow 控制路径。暂不混入 EyeFootPosition 眼高修改。
- 先验证签名、模块归属、可执行范围、对象/调用约定；未通过的字段输出 unknown 并报告缺口。禁止猜偏移、强制清 EF_NOINTERP、强制入插值列表、ResetLatched mutation 或延长 shadow 掩盖。
- 如果拿到明确错误时基/重复重置/覆盖证据，再做一个 native 因子的修复 A/B。若拿不到安全入口或确实需要完整 Portal prediction 生命周期，明确暂停相机层补丁路线并讨论实现范围，不承诺下一轮必然变顺。
- `PortalNativeCadenceStage` 本轮 10,345 行，约 5.07 MB 字符，约占日志 41%。下一轮合并同帧相同快照：保留 stage identity/validity/相对上一阶段差值及首个异常完整值；正常 command-end 聚合，异常和阶段切换展开，summary 保留完整计数。不要因扩展 native 诊断再次复制整条链的重复明细。
- 后续测试指令每行统一使用英文分号 `;`。无需主观标记；保留普通下落、同一地板门场景和墙门双向跳跃的最小对照，极高速压力测试单独列为回归而不混入普通跳跃。

## 7. 本轮文件与验证

仅新增此分析文档、补充技术总结索引，并给第 14 轮测试文档的控制台命令补英文分号。日志目录下的 `analyze-native.cjs` 是本次只读流式复核脚本，位于已忽略的 TestLog 中，不进入 DLL/提交。

已运行脚本的 summary、stages、commands 三种聚合，并用原始日志行交叉验证关键样本。没有声称主观 bug 已修复；没有修改运行代码、执行游戏、构建 DLL、部署、暂存、提交或推送。
