# Portal fall 候选基线：累计未提交改动审查

日期：2026-10-18。方案/交付分支 `codex/bsp-portal-traversal`；初审工作区误处于 `codex/portal-official-mechanism`，归属纠正见第5节。

## 1. 固定范围与结论

固定点：`cd636baeb5baf2568b2ed1d29827ac38a8439d1e`（审查时HEAD）。用户明确要求“累计未提交改动”，因此使用 `git diff HEAD --` 加 `git ls-files --others --exclude-standard`，不是空的 `HEAD...HEAD`，也不是只对比第28轮备份。

开始审查时：33个已跟踪文件修改，4003行增加/110行删除；其中src/tests为30个已跟踪文件，另有52个未跟踪源代码/测试/fixture。本文及第29轮总结/证据工具是在审查期间新增的文档资产，不计入初始数字。

**累计代码具备整理为候选基线的条件；没有确认新的阻塞性正确性缺陷。** 不能把这些文件全称为“最后一轮fall补丁”：它还包括早期高速循环、角度回正、单眼位实验和多轮只读原生诊断。第29轮证明的是已有部署组合的实际运行，不证明每个默认关闭的实验均有效。

初审阶段暂缓commit/push，等待完整反向墙门动作和立即落地冲击的覆盖确认。2026-10-18用户已明确回复“两项都测过且正常”，人工关口关闭；可按本清单提交并推送 BSP 主验证分支，不省略或冒充日志证据。

## 2. 代码按功能归属

| 功能/文件组 | 审查归属与处理 |
| --- | --- |
| `PortalFallWrite.cpp/.h`、`PortalFallWritePolicy.h`、`PortalFallWriteFingerprint.h` | 本次核心，保留；默认true、signed值、原生通知、精确caller、fail-closed、读回及生命周期 |
| `PortalNativeFallProbe.cpp/.h`、`PortalNativeFallPolicy.h`、`PortalNativeOriginFingerprint.h` | 共享运行态字段/指纹能力，功能依赖；不能以“诊断结束”为由删除 |
| `Hooks/GameMovement/CCSGameMovement.cpp`、`PortalPlayerTeleport.cpp` | 功能接缝：初始化、命令scope、Teleport成功后Arm；包含旧诊断入口 |
| `PortalTransitionSimulator.cpp/.h`、`PortalTransitionDecision.cpp/.h` | 累计空间rearm、live/projected分流、事务/诊断状态接入；保持既有基线，非仅fall变化 |
| `Hooks/Input/InputAngles.cpp/.h`、`PortalViewReorientation.cpp/.h`、`Util/Offsets/Offsets.cpp/.h`、`Hooks/Hooks.cpp/.h` | 原生AdjustAngles验证、完整roll和官方形状回正；重要历史成果，不当无关修改删除 |
| `PortalEyeContinuity.cpp/.h`、`PortalTransition.cpp/.h`、`Hooks/BasePlayer/BasePlayer.cpp` | 累计眼位/commit seed/单owner实验及观察；singleeyehistory默认关闭，不能称所有实验已完成有效性验收 |
| `PortalEyeLeafHandoffDiagnostics.cpp/.h`、`PortalRenderCadenceTracker.cpp/.h` | 有界眼位和渲染步进证据；仍支撑已知安全边界，不因fall成功全部移除 |
| `PortalNativeDiagnostics.cpp/.h`、`PortalNativeCadence.h` | 按需窗口协调；默认关闭高频原生取证，与功能Runtime分离 |
| `PortalNativeInterpolationHook.cpp/.h`、`PortalNativeInterpolationPolicy.h`、`PortalNativeOriginPolicy.h`、`PortalNativePart1Policy.h`、`PortalNativeResetPolicy.h` | 原生入口、选样、Reset证据；保留按需复核能力，不作为新的history写入修复 |
| `PortalNativePredictionErrorHook.cpp/.h`、`PortalNativePredictionErrorFingerprint.h`、`PortalNativePredictionErrorPolicy.h`、`PortalNativeFieldPolicy.h`、`PortalNativeLoggingFingerprint.h` | 实体/字段归属及自然日志路径观察；fail-closed且不主动二次执行比较 |
| `Hooks/BaseClient/BaseClient.cpp`、`Hooks/ClientMode/ClientMode.cpp`、`Hooks/ClientPrediction/ClientPrediction.cpp`、`Hooks/ServerPlayer/ServerPlayerRunCommand.cpp` | 生命周期、输入、预测、服务端命令阶段的累计接入；原函数调用/返回保留 |
| `PortalBspCommands.cpp`、`PortalBspPhase1.cpp`、`Util/Logger/PortalFileLog.h` | 操作命令、状态清理、日志路由及开关；保留回退和拒绝报告 |
| `l4d2_base.vcxproj`及filters | 与新增模块对应的构建登记，需随代码一并纳入 |
| 已修改的4个测试文件与所有新增tests/fixtures/runner | 代码契约资产，全部保留；fixtures为小型指纹数据，不是DLL或大日志 |

ClientMode在HEAD已经初始化；本轮累计diff只是更新旧“会不停跳跃/右键”的过时注释并接入只读输入观察，不能误说本批才重新打开整个ClientMode。

清理结论：obsolete convergent-tail运行路径已在累计修改中删除；没有再启用noclip、movement mutation、出口push或PortalMaskRepair。旧探索的MD保留为历史证据并从当前结论链接，不把历史“未开始/待验证”当当前状态。尚未证伪的按需诊断/能力验证不作无证据大删改。

## 3. 文档和其它资产的纳入/排除

准备纳入：

- 总体技术总结及本次聚焦fall技术资产、累计审查、test29证据JSON；
- 第13–29轮floorwall分析、第14–29轮对应诊断/回归操作说明；
- 08-09 view/input、08-11 tail/roll、08-16 single-eye历史设计/计划，以及10-16当前验收计划；
- `tests/audit_portal_fall_evidence.cjs`：现有流式分析工具提取为可入库资产，要求显式输入，不隐式扫描目录。

保留但**不纳入本次候选提交**：

- `docs/research/2026-09-07-*` demo分析和证据、09-08 TAS、09-09 bhop、09-10 TAS协作研究；
- `docs/research/portal-official-rebuild/**`及10-10 official-mechanism-rebuild计划/scope：独立M0–M2方案及证据，不代表本次fall方案已实现那些能力；
- `.superpowers/**`本机执行账本/备份、`.local/**`其它工作台、`TestLog/**`原始日志与探索脚本、`src/Debug/**`产物及备份；
- 本机MCP/权限配置。不使用 `git add .`，不把全workspace误装成一次修复。

额外注意：`.claude/settings.local.json`已在旧HEAD被跟踪，虽有ignore规则，仍不是“彻底从历史排除”；它本轮无修改，不进入本批差异。是否单独取消跟踪/清理历史是独立仓库治理，不在本次验收中删除本地配置或改写历史。

## Standards

独立只读审查（`baseline_standards_review`）未确认新的阻塞性安全缺陷。两项文档化偏差及一项启发式维护问题：

1. AGENTS通用约定要求Hook置于Hooks目录并经现有包装/中央初始化；部分原生Hook在Portal模块中用raw MinHook懒初始化。它们依赖晚到的运行态身份，实际所有者/接入点现已在[技术资产](portal-signed-fall-continuity-technical-asset.zh-CN.md)说明；本次不搬迁已验证Hook。
2. AGENTS的卸载清理约定和早期规格尚未完整满足。旧HEAD也没有统一MinHook teardown，新增模块继承该限制；记录为**不支持热卸载/重复注入**，必须重启游戏换DLL。不能将DllMain的brush恢复当作全Hook清理。
3. 启发式“重复代码”：几个原生模块的VirtualQuery/PE/可读可写校验形态重复。将来可抽有界reader，但各模块的可写/归属要求不同，不能验收时机械合并。它不是当前游戏失败证据。

本轴：2项非阻塞工程约定偏差、1项维护启发式；最重边界是安全热卸载未实现，当前只声明进程寿命使用。

## Spec

独立只读审查（`baseline_spec_review`）未确认fall实现缺项或错误：有符号值、原生通知先于写入、读回、失配拒绝、默认开启、诊断隔离、落地/死亡/身份/新事务清理成立。single-eye仍默认关闭，旧convergent-tail行为已移除。

发现1项文档P2：总技术总结仍写解锁后有时间冷却，与实际spatial-only、直接回Idle且cooldownUntil=0冲突。本次已修正，并在旧问题段保留“当时观察”与本轮3500证据的区别。

继承限制：早期规格的DLL unload要求仍不能宣称完成（见Standards，不把它伪装成新fall回归）。真实活动scope中切换诊断/缓存重绑的自动集成测试仍窄于纯策略测试，保留加固项。

本轴：1项文档发现已修复；代码未确认新的阻塞缺陷。手工覆盖未确认项不能由静态审查替代。

## 4. 主代理复核与新鲜验证

- 检查实际Hooks.cpp/DllMain：无统一teardown，故接受“进程寿命、不支持热卸载”的限制，不在loader lock内补复杂清理。
- 检查Simulator：空间解锁直接Idle、cooldownUntil=0，确认文档意见并修正。
- 2026-10-18重新运行29/29 `tests/run_*.cmd`，全部exit0；Debug x86 MSBuild exit0。
- 源/游戏DLL哈希仍与第29轮候选完全一致；没有构建新的行为版本或复制DLL。
- `git diff --check HEAD`无空白错误；LF/CRLF提示不是测试失败。
- 独立审查为静态/测试契约审查，未自行运行游戏；主观结果来自用户，原生运行态结果来自日志。

暂存收尾补充：把未跟踪文件纳入index后，`git diff --cached --check`发现`PortalNativeLoggingFingerprint.h`及`tests/fixtures/PortalNativeLoggingBodies.h`各一个末尾多余空行；仅删除这两行，无逻辑/字节指纹变化。随后再次构建成功。新本地产物SHA-256为`1FD10EB1324BB2A3B65CA74733CEC5A5479BE2FAA91EF8D380523FDD0173E611`，仅作编译验证，**未部署或宣称为第29轮实测产物**；游戏目录保持已实测的`30E4237E…715A51`。因此前述“源/游戏哈希相同”描述的是格式清理前的验收复核时点，不是重新链接后的两个文件。

本次明确暂存清单共129文件：55个源代码/项目文件、28个测试/fixture/runner/重放工具、46个技术文档/证据文件。暂存差异无TestLog、Debug二进制、本机配置或独立research目录。

最终人工覆盖已确认；明确文件清单/暂存diff和验证通过后形成基线提交 `eec85ad760ed5741f42038410e8a08c27fe31147`。首次推送误创建了 `origin/codex/portal-official-mechanism`；这不是用户期望的成果归属，按下节纠正。`origin` 为 `3918831/l4d2-portal`，`fork-origin` 不在本次操作范围。

## 5. 分支归属纠正

用户2026-10-18明确：本次是 BSP 清空方案的连续性技术积累，应交付到 `codex/bsp-portal-traversal`；`codex/portal-official-mechanism` 属于另一套大方案。不能把“当前检出的分支”当成“成果应归属的分支”。

纠正前只读核实：

- 本地 BSP 分支为 `cd636ba`，是基线 `eec85ad` 的直接父节点；远端 BSP 为 `7ae34cd`，其间两个既有提交 `36a5fd4`、`cd636ba` 是本 BSP 探索的视角回正设计/计划。
- official-mechanism 本地分支创建于 `cd636ba`；远端同名分支原先不存在，由本次误推创建，当前仍为 `eec85ad`，没有他人追加提交。
- 28个独立研究文件未跟踪，另有忽略的本机执行账本/测试日志，全部保持原状，不混入迁移。

因此可保持基线提交标识和内容，直接快进 BSP 分支，不需要 cherry-pick、代码冲突解决或重新实测。归属纠正只补文档；源代码、测试和游戏目录DLL均不改变。远端 BSP 普通快进推送成功后，使用期望旧值为完整 `eec85ad` 的 lease 删除误创建的远端分支；本地 official-mechanism 引用恢复 `cd636ba`，不删另一方案研究文件。远端若发生任何新变化，拒绝覆盖并停止核对。

本轮同时补充[跨方案技术资产复用索引](portal-continuity-cross-scheme-reference.zh-CN.md)。原始129文件基线、官方语义/运行态适配界限和证伪过程完整保留；不是简单撤销代码，更不是把 whole-brush 或 shadow predictor 宣称为官方完整机制。
