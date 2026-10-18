# L4D2 Portal：有符号 fall 状态连续性的技术资产

日期：2026-10-18。适用候选：`pe-6AD2590C`，Debug x86，单人localserver。

资产所属基线：`codex/bsp-portal-traversal`；不是另一条 `codex/portal-official-mechanism` 方案的实现交付。本文的官方状态语义、因果证据和能力验证契约可独立复用；跨方案入口见[连续性技术资产复用索引](portal-continuity-cross-scheme-reference.zh-CN.md)。

本文件提炼实现契约，不替代各轮原始分析。总路线见[物理穿越与连续性总结](portal-bsp-physical-traversal-and-continuity-technical-summary.zh-CN.md)，因果A/B见[第27轮](portal-floorwall-test27-fall-write-ab-analysis.zh-CN.md)，回归见[第28轮](portal-floorwall-test28-baseline-regression-analysis.zh-CN.md)及[第29轮](portal-floorwall-test29-candidate-acceptance-analysis.zh-CN.md)。

## 1. 真正修复的是什么

地板→墙面穿越已正确旋转实际速度，但玩家仍在出口飞行一段时间后出现画面粘滞。角度回正、眼位/实体历史和插值时基的修补没能消除它，因为还有一个参与预测比较的运动状态没有随新轨迹刷新：`m_Local.m_flFallVelocity`。

这里的 fall 不是玩家当前速度模长，也不是直接用来平移摄像机的量。它记录用于落地/冲击等路径的竖直下落状态，会参加客户端预测/网络值比较。入口下落很快，出口主要水平甚至上升时，旧正峰值与新 `-velocity.z` 可以差很大。

本场景已验证的因果链：

```text
入口下落峰值在服务端残留，客户端按出口竖直轨迹更新
    → m_flFallVelocity 预测比较持续不一致
    → 原生预测纠错触发 ResetLatched / origin 历史重置
    → origin 可用插值区间反复丢失，选样夹取且不变化
    → 实际速度可以连续，但画面呈现延迟停滞
```

不能因为 origin history 被清就断言 origin 本身报错。经过实体归属、原生字段报告、八阶段同cmd比较，才从“相机问题”收窄到 fall 更新不对称。保留原生Reset，修正其持续触发原因。

## 2. 从探索到单因子证据

| 阶段 | 获得的可复用证据 | 避免的误判 |
| --- | --- | --- |
| 原生插值入口/选样 | 确认目标、ABI、真实时间与历史区间 | 未拿到运行态就改未知对象布局 |
| Reset caller与预测实体 | 重置来自本地玩家预测纠错，而非强制mode2 | 把viewmodel或所有Reset都算主因 |
| 原生字段文本 | 持续错误字段为m_flFallVelocity | 直接猜速度/ground/movetype |
| datamap与八阶段 | client在PlayerMove前刷新，server未刷新；native max保留旧峰值 | 误说只有server有max分支，或整个PreThink没执行 |
| 第27轮开关A/B | signed fall + 原生通知使错误链消失 | 任意清0、禁用Reset、再加镜头补偿 |
| 第28/29轮回归 | 默认生效、正常落地/下落隔离、3500循环、日志按需 | 只用原场景通过就宣称整个工程无回归 |

第27轮严格同门位正常落地对照：A的72/72个空中命令不一致、68条延迟Reset、158条origin夹取不变；B的40/40一致、延迟Reset0、47条origin全部正常变化。效果与用户“已感觉不到粘滞”一致。不能外推为所有几何/碰撞问题均解决。

## 3. 官方语义和本机适配必须分开

本地参考源码（不随工程提交）核对点：

- `Reference Code/portal-sdk-2013/src/game/shared/gamemovement.cpp:4603`：ground为空时赋 `-mv->m_vecVelocity[2]`；
- `.../src/game/server/player.cpp:3864`：PreThink中的 `-GetAbsVelocity().z`；
- `.../src/public/networkvar.h`：网络变量变更通知与赋值顺序。

本机L4D2 client/server的PlayerMove均有保留旧fall峰值的max分支；运行态八阶段确认差异出在进入该分支之前。当前修复**没有完整移植Portal的PreThink，也不证明server PreThink完全未调用**。

适配是在本机已验证更新块的精确ground getter返回点、原生max逻辑之前，写入当前运动开始时的有符号 `-MoveData.z`。它恢复官方的状态语义，接入位置是L4D2二进制适配，不宣称与Portal源码逐函数同构。

官方网络变量赋值还隐含通知；因此不能只找到offset然后裸写。必须调用当前实体内 `m_Local` 的原生网络变更回调，再存值，再读回确认。

## 4. 模块与职责

| 文件 | 职责 |
| --- | --- |
| `src/Portal/PortalFallWritePolicy.h` | 纯有符号值策略、通知→存储→读回顺序、作用域/默认开关 |
| `PortalFallWriteFingerprint.h` | 完整更新块/getter/通知/accessor指纹、重定位和字段交叉核验 |
| `PortalFallWrite.cpp/.h` | localserver绑定、精确caller Hook、原生通知、作用域与摘要 |
| `PortalNativeFallProbe.*`、`PortalNativeFallPolicy.h` | 独立只读datamap发现、身份缓存、结构和名称读取边界；也是功能绑定依赖 |
| `Hooks/GameMovement/CCSGameMovement.cpp` | 用现有server PlayerMove创建RAII命令scope、初始化能力 |
| `PortalPlayerTeleport.cpp` | 确认server Teleport成功后Arm新epoch |
| `PortalTransitionSimulator.cpp` | 场景/模拟器清理时Reset功能状态 |
| `PortalNativeDiagnostics.*`、`Util/Logger/PortalFileLog.h` | 默认关闭的详细取证；不是修复开关 |

`PortalNativeFallProbe`虽名为Probe，不能随“删除诊断”整块移除，它提供实际写入必需的身份/字段发现。纯记录Hook则由按需诊断控制，不能用其开关决定功能是否执行。

## 5. 原生能力验证契约

绑定必须逐项成立，任一失败即拒绝，不猜offset、不绕过通知：

1. 当前server.dll为有效x86 PE；完整135字节fall更新块在可执行节内唯一匹配，验证重定位；不能只从外层CTerrorGameMovement附近猜基类入口。
2. 解析原生rel32得到ground getter，验证49字节完整body与thiscall/pointer/ret0 ABI。Hook调用原函数一次，返回值不改。
3. 只接受更新块返回地址 `site+18`，并且处于本地server命令的PlayerMoveScope。其它getter用途不产生写入。
4. 从当前玩家datamap取得fall/local/ground/movetype/lifeState，字段类型/大小/owner/范围和指纹立即数一致。动态生成的字段名称允许模块外的有界可读文本，结构指针仍受模块约束。
5. 当前m_Local vtable和49字节notify函数、18字节accessor、engine实例/方法均验证。检查字段可写、回调反向定位仍属于同一玩家；非延迟通知路径还要edict对应本地玩家。
6. 通知后检查原生dirty/pending状态，再写4字节float并按位读回；没有“通知不可用则直接写”的fallback。

当前支持版本的线索是ground caller RVA `0xFC839`、getter `0x148E80`、notify `0x1711B0`、accessor `0xED4B0`、engine方法97。观察到的字段偏移包括fall `0x1AA8`、local `0x1A44`、ground `0x244`。**它们是证据和交叉核验值，不是绕过验证的固定地址API。** 游戏更新后不匹配就禁用该能力，重新建立证据。

`dirtyBefore==dirtyAfter==0x185`只表明已dirty，不能独自证明本次网络送达。第27轮还核对了客户端接收的量化fall值和后续同cmd两端一致，才完成通知效果闭环。

## 6. 负值、范围和生命周期

出口向上时z>0，fall应为负；清0或取绝对值都会改变语义。旧值/z必须有限，当前安全契约拒绝 `abs(z)>4096`，不是限速或clamp。已验证实际平台为3500，未来不能自动外推更高速度。

状态流为：

```text
本地成功Teleport → Arm(player, epoch, committedCmd)
    → 后续合法PlayerMove + 精确getter位置 + airborne/WALK/alive
    → signed fall通知/写入（相同值不重复写）
    → 原生落地 / 身份或上下文失效 / 死亡或非WALK / 场景Reset：停止
    → 新Teleport：替代旧epoch，而不是等待一个时间恢复期
```

早于commit的命令不写。关开修复开关清活动状态，重新开启后等下一次有效commit；不会接管任意普通下落。RAII恢复嵌套命令scope，非本地RunCommand不提供活动命令。

本修复不改origin、实际velocity、ground、movetype、相机输出、原生Reset或返回的ground。落地时原生路径仍消费fall并处理冲击/伤害；第28轮无敌关闭用户反馈掉血正常；2026-10-18又确认第29轮关闭无敌后的出口立即落地冲击/掉血正常。这是人工回归结论，不是精确伤害公式等价证明。

## 7. 操作、日志与回退

```text
portal_server_fallfix;
portal_diag_native;
portal_bsp_status;
```

默认fallfix=true、native diagnostics=false。未穿越或作用域已结束，active/writeValidated/notifyValidated为false正常，需结合reason和lastBindingReason；不能凭最后状态行判定功能没生效。

临时详细取证：

```text
portal_diag_native 1;
cl_showerror 2;
portal_log_flush;
```

完成后恢复：

```text
portal_diag_native 0;
cl_showerror 0;
portal_log_flush;
```

功能回退仅 `portal_server_fallfix 0;`；之后可 `portal_server_fallfix 1;` 等下一次穿越重建作用域。不开旧noclip、movement mutation、出口push或PortalMaskRepair。

成功详细写入每epoch有界，摘要和失败保留；重型原生采样默认关闭。字段缓存按实体/vtable/module/datamap身份复用，换身份/场景重新核验。第23轮逐字符VirtualQuery和频繁重建造成的诊断扰动已有专门修正，不能在清理时撤回。

## 8. 自动测试、证据工具和交付边界

- `tests/PortalFallWriteTests.cpp` + `fixtures/portal-fall-write-x86.hex`：完整字节/ASLR/字段约束、通知拒绝不写、负fall、有限性/范围、默认开关、连续epoch和停止条件。
- `PortalNativeCadenceTests.cpp`、`PortalNativeInterpolationTests.cpp`及fixtures：字段读取/原生指纹/采样选择等独立契约。
- `PortalFileLogPolicyTests.cpp`：重型诊断默认关闭、功能不依赖logger flag、失败不被成功限额吞掉。
- `audit_portal_fall_evidence.cjs`：schema13单次记录的流式八阶段和Reset/origin证据重放。不是通用跨地图合并器；capture/cmd编号重置的多段日志应分段审计。

第29轮：419次成功绑定、342次通知/写入/读回、零拒绝；87/87同cmd空中一致、101/101延迟origin正常变化；正常作用域有335次3500提交约12.1秒。完整证据与限制见第29轮分析，不把微小摘要当游戏实测替代品。

自动测试尚不能完整模拟真实Hook/共享缓存在同一个活动scope中切换诊断。当前证据支持分组开关隔离；更强集成测试留为加固项。

## 9. 已知工程债务与后续隔离

Hook由进程寿命持有：部分原生Hook因运行态身份/懒初始化而放在Portal模块内，使用raw MinHook而非统一Hooks包装；入口分别由GameMovement/原生诊断协调。与AGENTS通用目录/注册约定有偏差，当前记录而不在验收时迁移。

**不支持运行中FreeLibrary热卸载或重复注入。** 现有工程缺少统一Hook禁用/拆除通路，DllMain的brush恢复不等于全部Hook已撤销；这是继承的工程限制，不能声称历史设计中的DLL-unload要求已满足。换DLL必须完全退出游戏再启动。

后续应分开推进：门放置失败/brush绑定、孔径内局部开洞、首次资源预热、过程寿命Hook清理和公共内存校验模块。都不是本次fall问题再次添加补偿的理由。

## 10. 给后续复刻方案的迁移契约

优先迁移的是“预测参与状态必须随出口运动保持两端一致”，不是某个固定 getter Hook。另一方案若已在原生 PreThink/PlayerMove 等合法路径刷新有符号 fall 并完成网络通知，不应再叠加本 adapter；先用同cmd、同阶段证据确认是否还存在差异。

可直接作为设计和测试输入的资产包括：负 fall 与 `-MoveData.z` 语义、通知→存储→读回契约、身份/命令/生命周期隔离、正常下落不接管、落地伤害回归，以及 fall error→Reset→origin 夹取的取证方法。具体二进制指纹、offset、返回地址、插入点和作用域覆盖必须按新接入路径重新验证；不能将本文某个地址视为通用 API。

当前修复不读写 brush 数据，因此其状态一致性结论可以帮助其它碰撞方案；但“可借鉴”不等于“新方案已验证”。复用后仍须独立完成同门位 A/B、普通下落/立即落地、双向墙门、3500上下循环和失配拒绝测试。保留正常预测纠错，不用禁用 Reset 或附加镜头平滑代替状态修复。
