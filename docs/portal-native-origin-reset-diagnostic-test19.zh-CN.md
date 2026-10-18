# 第 19 轮：origin Reset 调用者与历史重建诊断

日期：2026-10-13。承接 [第18轮分析 §6](portal-floorwall-test18-origin-selection-analysis.zh-CN.md)。本轮只加只读诊断，不修复粘滞行为，不应期待主观变化。

## 1. 实现与安全边界

从已验证的 live origin watcher 类型 vtable slot5 解析 Reset，验证完整120-byte本机函数、三处 writer rel32、三个一致且精确 ASLR 重定位的 GlobalVars 操作数。支持的 client.dll SHA-256 不变：`D2553CCA3A63BDD844864F5FEE2C20A233AB19DEE816463A90857B95309982A2`。原生 ABI 是 void thiscall、无栈参数、ret；不改参数、原样调用一次，不屏蔽 Reset，不主动增加 Reset/Interpolate，不写 player/map/history/GlobalVars。

- 不再限定在 map、Part1 或 render-start 作用域；只观察实际当前本地 origin watcher。每次验证对象偏移、vtable/slot 和 value binding，caller 必须属于可读、可执行的 PE32 模块。
- 直接读取原生 Reset 使用的 GlobalVars 指针与 curtime，和 SDK 时间/指针身份分别记录；不把 render curtime 假定为 Reset 时刻的实际时基。
- 调用前后各最多读取 first3 历史样本，包含此前未读取的 index1。容量、索引、有限值和元数据稳定性失败是 invalid，不能当成空历史或不动。
- ThreeClones 要求 count3、三个已读取条目的时间一致，三个位置均逐分量等于当前 value；不使用会下溢的距离平方当作相等。
- valueChangedByOriginal 也按逐分量不等计数，覆盖0→1e-30f等有限小变化；距离数值仍只作量级展示，不作相等判据。
- 既有 RunCommand/SetupMove/FinishMove 包装器仅新增嵌套诊断作用域，原有引擎调用/顺序保持不变。只有对象匹配的作用域才给出真实 cmd；其它调用显示 cmd=-1、phase=unscoped，latestInput 仅供参考，不能冒充当前命令。
- 未命中某个 prediction scope 不等于网络阶段；阶段归属必须结合真实 caller RVA 后再分析。没有猜读私有预测错误 flags。
- Reset 计数独立于 main-view 的接受/明细预算，汇总所有捕获期间观察到的目标入口调用。caller 固定16个桶，超出显式计 callerOverflow；未知模块单独计 unknownCaller。
- 每窗口最多48组 Reset 明细，每组包含元数据和前/后样本两行；状态/来源/时基/命令或 tick 变化输出，计数先累加再决定明细。窗口外保留最近64条供 prelude，开始时最多输出最近四 tick 中8条同对象事件；prelude 不重复进入窗口总计。
- seq 是自然调用序号，capture/txn 是输出窗口，sourceCapture/sourceTxn 是调用发生时窗口；frame/tick 是调用时刻，避免把 prelude 误算成当前窗口的新调用。

能力边界：本探针证明的是该已验证 Reset 入口是否执行。若引擎另有内联 Reset 代码或历史复制，不一定经过该入口；零调用不能直接证明“所有重建都不存在”。下一步用实际 writer/恢复路径区分，而不是盲目禁用 Reset。

## 2. 压缩与清理

保留第18轮必要的真实 origin 选样明细和全部接受帧统计。外层 InterpolationFrame 从每次事件都输出改为父链状态边缘/异常输出，Part1 仍有边缘和时基异常证据；成功状态省略不影响 Summary。移除永久 history=unknown/nativePrivateState=unknown 文案，指向实际 OriginFrame/OriginReset 证据。新增五个标签进入精确文件白名单，没有新增控制变量或 bind。

没有修改相机、回正、shadow、服务器 anglefix、速度、ground、MoveType 或 brush。当前分支实际为 `codex/portal-official-mechanism`，保持不切换；保留此前未提交工作，本轮不提交推送。

## 3. 游戏内测试

退出旧进程，从主菜单用新 DLL 启动，进入单人 localserver。每条指令均以英文分号结尾。

普通下落前置：

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_bsp_phase1 0;
```

做两次普通自然下落，每次开始前执行：

```text
portal_diag_nativefall;
```

第一次后检查：`PortalNativeOriginResetHook` installed/signatureValidated/abiValidated/writerCallsValidated/globalsOperandValidated/liveBindingValidated 均为 true；Begin version6、originResetHook=true；窗口应有 OriginResetSummary。**正常下落 Reset calls=0 完全可能，零调用本身不是安装失败，不要因此停止。**安装/验证失败则停止追加测试并发日志。

再开启穿越：

```text
portal_bsp_phase1 1;
portal_bsp_status;
```

1. 墙面双门热身一次，两方向各步行一次、跳跃两次；方便时补较高速空中墙门对照。
2. 地板→墙面同一对门五次，出口保留自由飞行距离。
3. 每次首次落地后等待至少2秒，再开始下一次，避免 superseded；捕获期间不额外跳跃或切换移动模式，不改 timescale。
4. 不需要主观标记或新绑定。出现黑天空、门体/viewmodel 异常或崩溃则停止并保留日志；其它情况提供完整日志文件路径。

Phase1 仍默认关闭，穿越前必须执行开启命令。新进程沿用已验证的默认参数；个人 cfg 若改过，可追加恢复块：

```text
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_visual_reorientation 1;
portal_bsp_status;
```

## 4. 日志验收与后续决策

```text
[PortalNativeOriginResetHook] installed=true ... slot=5 stackArgs=0 return=void
[PortalNativeCadenceBegin] version=6 ... originResetHook=true
[PortalNativeOriginResetSummary]
```

实际有命中才出现 OriginReset/OriginResetSamples/OriginResetCaller。没有命中时看 installed=true 和 calls=0 的 Summary，同时对比 OriginFrame 的退化历史，不能要求人为触发多余 Reset。

| 记录/字段 | 解读 |
| --- | --- |
| caller module/RVA/valid | 实际立即调用者及模块可执行验证；函数名需后续按本机机器码核对 |
| nativeBefore/nativeAfter、sdk、sim、tickBase、globalsMatched | 原生 Reset 与既有 SDK 观测的时间身份，不能只看数值相似 |
| before.interval / after.clones / after.atNativeTime | 首个时间邻域是否存在、三份当前值是否重建、重建时间是否等于真实 Reset 时基 |
| Samples s0/s1/s2 valid/time/value | 实际有限读取，invalid 不当作0坐标 |
| phase、cmdKnown、prediction.in、insideRender | 已有作用域的调用上下文；unscoped 不推测成某个阶段 |
| Summary calls/invalid/clonesAfter/lostFirstInterval | 全部窗口目标调用及有效重建结果；不是接受渲染帧计数 |
| frameGroups/tickGroups/commandGroups | 连续分组计数，不是任意顺序的集合去重；replay 可能增加 commandGroups |
| detail.dropped/callerOverflow/prelude.matched/shown | 被预算压缩的明细/来源；prelude.matched 仅指最近64条中满足窗口条件者 |

failure：1 调用前对象/绑定，2 caller 模块，3 原生时钟，4 调用前快照，5 调用后绑定，6 调用后快照。unknown/invalid 不作本轮根因结论。

后续门槛：

- 持续 Reset 命中且对应区间丢失：沿真实 caller 核实合法恢复条件与官方差异，再设计单因子修复；保留合法 teleport/预测错误/创建/换图 Reset。
- 只首次命中或目标入口零调用但退化持续：该入口重复调用假设不成立，转查实际 writer/预测恢复或内联路径；不在本层继续猜屏蔽。
- 因预测误差持续 Reset：先定位真实误差来源或补齐官方恢复/latch；不任意改 GlobalVars 时间，也不以平滑掩盖误差。
- 入口或读取无法安全验证：报告能力缺口，修诊断后再测；没有新判别入口时暂停局部补丁路线。

## 5. 验证与部署身份

最终验证：全部28个测试 runner 通过（26个 portal + 2个 engine trace）；Debug x86 MSBuild exit0，git diff --check exit0。完整 Reset 字节已与本机 client.dll 独立核对；只读隔离复核未发现阻塞项，极小 value 变化漏计已由回归测试先失败、逐分量修正后通过。静态验证不替代游戏内安装、调用覆盖、换图生命周期或日志开销验收。

已部署到 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`，源/游戏目录 SHA-256 一致：

- 修改时间：`2026-10-13 14:25:52`，大小：`1529344` bytes。
- PE 身份：`pe-6ACDCEF0`。
- SHA-256：`5C25BF816567C0210290DBC0EA7C9BBF6AE36C15DF53FE0A883D390D904F8E73`。
- 原 DLL 备份：`src/Debug/deployment-backups/L4D2_Portal.before-origin-reset-20261013.dll`；SHA-256：`6FBA33D2AE4AF5FF7DE0B0444D861EB14670834A63D216A0F4B2EECFA959DB44`。

部署时游戏进程未运行；没有启动游戏、提交或推送。保留现有未提交基线与本轮快照，待实测决定是否继续；旧基线正确性不在本轮增量复核范围。没有第19轮运行态数据，因此尚不能判定粘滞根因或宣称改善。入口未命中的内联/恢复写入仍是明确的能力边界。
