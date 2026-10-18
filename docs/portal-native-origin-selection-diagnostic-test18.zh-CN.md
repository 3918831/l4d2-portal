# 第 18 轮：真实 origin watcher 与原生选样只读诊断

日期：2026-10-10。承接 [第 17 轮分析 §5](portal-floorwall-test17-native-map-analysis.zh-CN.md)。本轮是新增判别证据，不是粘滞行为修复；不应期待主观效果改变。

## 1. 实现范围与约束

自然调用链：已验证 outer → Part1 → map-loop → **origin watcher slot6 Interpolate** → inner → **GetInterpolationInfo**。只观察实际调用，一次转发原参数并返回原结果。未添加主动插值调用，未写 player、map、history；未调整相机、回正、shadow、速度、ground、MoveType 或 brush；禁止的旧方案保持关闭。

支持的 client.dll SHA-256 为 `D2553CCA3A63BDD844864F5FEE2C20A233AB19DEE816463A90857B95309982A2`。完整构造段保持 EDI 的 origin watcher 身份直到绑定插入；从验证指令读取对象偏移和最终 vtable，再由真实 slot6 与两个 rel32 解析 wrapper/inner/selection。构造、完整 inner/selection 与独立历史 writer 的所有绝对操作数均按 ASLR 重定位严格验证，不通配。

- wrapper：ECX watcher、一个 float、ret4、int noMore；完整 28 bytes。
- inner：两个 float、ret8；完整 475 bytes，选样返回后才写本地位置。
- selection：ECX watcher、info*/currentTime/amount/noMore*、bool AL、ret16；完整 631 bytes。
- 独立 writer：完整 397 bytes，核对 count/head/capacity、16-byte stride 与时间+内联 vec3。它不被 Hook、不被主动调用。
- live entry watcher 必须等于构造验证的玩家内嵌 origin watcher；vtable、slot、value pointer 与 map.entry.data 一致，元素数量必须是一个 vec3。
- map-loop 和 inner 的实际 caller、同帧/玩家作用域、实际 time/amount 必须匹配。身份或读取失败明确保留 invalid/unknown，不代替零位移。
- 最多读取 newest 与实际选中 newer/older/oldest 四个位置，不扫描完整 history。验证环形容量/索引/指针溢出、有限值和前后头部稳定性；环形容量上限 4096，超限停止解释。
- selection 的 bool、三个索引与 frac 由完整函数证明在返回路径上初始化；不读取 padding，不将 false 解释成“所有输出未定义”，也不把 -1 当有效样本。

nativeOriginOutcome 与 nativeAnglesOutcome 分开；删除 MapFrame 冗余 recv 向量和过时的 watcher 永远 unknown 字样。既有 recv 计数仍只表示 recv。新增三个精确白名单标签。origin 状态变化纳入既有 cadence 事件触发，明细仍使用每窗口 128 组预算和有限 prelude；所有接受帧先进入 Summary，预算耗尽不丢汇总。

## 2. 测试步骤（全部指令有英文分号）

退出旧游戏进程，用部署后的 DLL 从主菜单启动，进入单人 localserver。新进程沿用已有默认 eye/shadow、server anglefix、reorientation；不要恢复旧 mutation 开关。

### A：普通下落对照，做两次

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_bsp_phase1 0;
```

每次从适当高度自然下落前执行：

```text
portal_diag_nativefall;
```

每次落地后等待至少 2 秒。第一次后先查看 `PortalNativeOriginHook`：installed、selectionInstalled 和各 validated 应为 true；OriginFrame 应有 calls>0、selection(called=true)，正常读取应 outputsValid/ringValid/samplesValid 成立。正常下落不能验证时停止追加测试，直接提供日志；不重复测试同一个失败入口。

### B：墙门回归与地板→墙门诊断

```text
portal_bsp_phase1 1;
portal_bsp_status;
```

1. 墙面双门先热身一次；两方向各步行一次、跳跃两次。方便时补两次较高速空中墙门对照，不必为追求完全同速改变移动模式。
2. 同一对地板→墙面门做五次，墙门出口留足自由飞行距离。每次首次落地后等至少 2 秒，避免窗口相互替换。
3. 捕获过程中不再跳跃或切换 noclip/移动模式；不切 singleeyehistory、不改 timescale。无需主观标记或新绑定。
4. 出现新黑天空、门体/viewmodel 异常或崩溃则停止、保留日志。否则发回完整日志文件路径即可。

Phase1 仍默认关闭，B 组必须显式开启。若个人 cfg 曾修改前置参数，可附加以下恢复块；没有旧方案：

```text
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_visual_reorientation 1;
portal_bsp_status;
```

## 3. 日志验收与解释

```text
[PortalNativeOriginHook] installed=true selectionInstalled=true ... passive=true behaviorMutation=false
[PortalNativeCadenceBegin] version=5 ... watcherHook=true selectionHook=true
[PortalNativeOriginFrame]
[PortalNativeOriginSummary]
```

不要求匹配整行。Frame 的计数是本帧聚合，向量/选样字段为最后一次对应调用；无调用时字段为 unknown/NAN，不作位置结论。Summary 聚合全部接受帧，prelude 明细不计入窗口汇总。

| 字段 | 含义 |
| --- | --- |
| calls / callerMatched / identity / watcherValid | 实际 origin 调用及已验证的对象、写入地址和本地读取；不是 map eligibility 推断 |
| changed / noChange / value(before/after) | 该 watcher 原生调用前后真实本地 origin 的变化 |
| watcher noMore / selection.noMore | 完成标志：1 表示无需更多变化，0 表示仍需要插值；不表示“位置动了” |
| selection.returned / Summary selection.returnedTrue | 原生选样 bool：true 表示选样成功、false 表示选样不可用；与 noMore 是不同输出 |
| selection.current / amount / effective | 实际选样输入；effective=current−amount，不是 GlobalVars interpolation_amount |
| selection.returned / hermite / fraction / indices | 原生实际返回与取样；不在 observer 中重算官方选择 |
| ring / newest/newer/older/oldest | 已验证的环形元数据与有限实际样本，各自携带 valid/time/value |
| hookUnavailable / eligibleNoCall / invalid / selection.missing | 将安装缺失、条件具备却没有观察到调用、无效读取和缺少选样分开 |

selection state：0 unknown，1 invalid，2 empty，3 no-selection，4 newer==older（clamped），5 使用的历史位置相同（repeated），6 interval，7 Hermite。clamped 或 repeated 本身未必是错误，必须对比 effective、历史时间/位置、当前模拟状态、正常下落及墙门。

watcherFailure：0 无读取失败，1 caller 不符，2 类型/绑定不符，3 time 或调用前读取不符，4 返回/调用后验证不符。selection.failure：1 caller，2 实际参数，3 输出读取，4 ring header，5 输出语义/索引，6 样本读取，7 ring 不稳定。invalid/unknown 不作“没有插值”的证据。

## 4. 下一步决策门槛

- 普通下落安装/实际调用/读取不成立：先修诊断或报告缺口，不让用户重复无效测试。
- 地板门实际 empty/no-selection、有效时基错位或历史样本不推进，而对照正常：追实际 latch/预测恢复链，再设计单因子修复；不先改时间常数。
- 区间有效但位置相同：分析历史填充/恢复来源，不把它当作 watcher 未执行。
- watcher 已写变化、Part1/outer 丢失：追已证明的覆盖/传播边界。
- 实际选样、样本与写回都正确仍无法解释阶梯，或无法安全校验下一入口：暂停局部插值补丁路线，讨论完整预测/历史生命周期的能力与成本。

## 5. 验证与部署

- 增量回归经过 RED→GREEN：位置/角度 outcome、精确白名单、ring/身份/选样边界、非有限历史、选样状态触发压缩边缘。
- 一次独立审查发现 false native result 下的失败 newest 读取可能被误分类为 no-selection；增加 State/Calls/事件边缘复现测试，RED→GREEN 修复。修复后控制代理重跑全部 28 个 runner，`FINAL_SUITE=28/28 failed=`；Debug x86 MSBuild 退出 0。首轮全构建仅有既存 convar C4355 告警，最终增量构建没有新增告警。
- 五段完整指纹分别与支持的本机 PE 字节逐字节核对，通过。新 OriginFrame 采用 float 可往返的 `%.9g`，最坏格式上界 1256 bytes，在现有 2048-byte 日志缓冲内；静态范围和回归测试不代替游戏运行态 ABI 验证。
- 本轮改动相对实施前快照隔离审查。保留之前未提交工作，未提交、推送或启动游戏；仍在 `codex/bsp-portal-traversal`。
- 2026-10-10 部署 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`。源文件与游戏文件均为 `1499136` bytes，修改时间 `2026-10-10 01:36:33`，PE 标识 `6AC92621`。
- 两侧 SHA-256：`6FBA33D2AE4AF5FF7DE0B0444D861EB14670834A63D216A0F4B2EECFA959DB44`。
- 上一轮 DLL 备份：`src/Debug/deployment-backups/L4D2_Portal.before-origin-selection-20261010.dll`，SHA-256 `97A95E8998B2A595F1116FE35811F38AFC110919855D729676406BF1FD68B2AD`。只覆盖游戏根目录 DLL，备份可用于恢复。

下一步是用户游戏内确认 installed/caller/identity 与实际选样，再按 §4 判断继续、修诊断或停止。部署完成不等于粘滞修复完成。
