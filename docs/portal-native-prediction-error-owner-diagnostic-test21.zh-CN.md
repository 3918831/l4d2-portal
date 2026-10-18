# 第21轮：预测误差数量与报错实体的只读诊断

承接 [第20轮分析](portal-floorwall-test20-prediction-reset-source-analysis.zh-CN.md)。本轮不修相机、速度或旋转，不预期主观变化；只判断持续预测恢复的真实触发条件和报错实体。

## 已确认与待验证

第20轮五次地板→墙面首次落地前反复丢 origin 历史区间，父 caller 全为本机预测恢复入口 client+17C57C。原生逻辑已经使用 TimeBase−tick 临时重置历史，与官方该步骤一致，不能再减一次 tick。

但 `cl_pred_doresetlatch=2` 也能强制进入该分支；上轮未记录它。另一个未知是哪个 predictable 报错：玩家 origin 被重置不等于玩家 origin 就是报错字段。

## 接入边界

- 验证真实 I::Prediction RTTI（CPrediction/CCSPrediction）与 slot6 的完整597-byte PostNetworkDataReceived，再由实际 rel32 解析实体265-byte PostNetworkDataReceived、150-byte TransferData 和16-byte跳转表。绝对地址按完整 PE 重定位位点验证，不使用固定运行地址或通配关键指令。
- 按本机 ABI，root 为 void thiscall / 一个栈参数 / ret4，实体为 bool thiscall / 一个ack / ret4，TransferData为 int thiscall / 三个栈参数 / ret12。每个原函数自然调用一次，参数和返回保持原样，不主动比较第二次。
- 只观察 validated root 的自然实体调用及其自然错误比较，嵌套 scope 恢复、非匹配入口清空作用域。读取自然 entity/index/map 参数和错误数/布尔返回，不读取预测帧缓冲区、datamap字段或SDK私有结构。不替换比较callback。
- RTTI类型用于标签；未知类型仍可有有效比较结果。按窗口内原生实体地址分桶，不宣称跨生命周期的实体身份。没有分屏slot归属证明，仅用于当前单人localserver。
- root ack 与实体累计ack分别记录，不假设二者相等。ack0且无比较属于 notChecked；缺失、多次、负错误数或布尔/错误数不一致为 invalid，不当作0错误。
- 记录既有 `cl_pred_doresetlatch` / `cl_pred_optimize` / `cl_showerror` 的 public ConVar 数值，Begin同时输出float/int。每次真实origin Reset再读当时的reset模式，全部Reset模式直方图独立于明细预算。
- 每窗口48条误差边缘明细、16个实体桶、128条近期环、最多8条前4tick的prelude；总计先累加再压缩。prelude在预算内优先保留最近错误、其次invalid、再正常结果，并按发生顺序输出；匹配/展示数量显式记录，不重复计入窗口汇总。既有Reset及其父证据保留，不叠加已证伪行为修复。
- 原诊断重置、关闭诊断和本地对象失效/改变时推进生命周期epoch；root、实体观察结束和prelude都验证epoch。关闭时即使无active窗口也清近期环，旧自然调用返回后不能重新污染新生命周期。仅修改诊断状态，不改变原函数行为。

## 实测步骤（全部命令已带英文分号）

重启游戏，从主菜单加载新版DLL，进入单人localserver：

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_bsp_phase1 0;
```

先做两次普通自然下落，每次开始前：

```text
portal_diag_nativefall;
```

落地后至少等2秒。不要为了本轮手动改变 `cl_pred_doresetlatch` 或 `cl_showerror`，自动日志会读取现值；若之前个人cfg设置过，也先保持并提供日志。

再开启穿越及既有视觉前置：

```text
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_visual_reorientation 1;
portal_bsp_status;
```

1. 墙门热身一次，然后两方向各步行一次、跳跃两次；跳跃让脚离地进入门，避免刚好落地再过门。
2. 同一对地板→墙面门做5次，出口留自由飞行距离，首次落地后至少等2秒再开始下一次。
3. 不改timescale/移动模式，不额外绑定或主观标记。出现新增黑天空、门体/viewmodel异常或崩溃则停止并留日志。

## 目标信息与停止条件

```text
[PortalNativePredictionErrorHook] installed=true predictionIdentityValidated=true signatureValidated=true abiValidated=true jumpTableValidated=true
[PortalNativeCadenceBegin] version=8 ... predictionErrorHook=true predictionIdentityValidated=true
[PortalNativeCadenceCvar] ... name=cl_pred_doresetlatch ... intValue=...
[PortalNativePredictionError] ... entity=... local=... type=... outcome=... errorCount=... hadErrors=...
[PortalNativePredictionErrorSummary] ... outcome(...) ... localErrors=... entityOverflow=...
[PortalNativePredictionErrorEntity] ... type=... outcome(...) ... errorSum=...
[PortalNativeOriginReset] ... resetLatch(modeKnown=true mode=...) ... parent(... rva=0x17C57C)
[PortalNativeOriginResetSummary] ... resetLatch(unknown=... mode0=... mode1=... mode2=... other=...)
```

Outcome：0 invalid、1 notChecked、2 noErrors、3 errors。汇总包含全部已观察且root归属有效的实体调用，不只48条明细；不覆盖未进入该root的其它路径。普通下落零错误或零Reset允许，不要求每窗口都有正错误。

- mode2：先讨论强制模式，不能把Reset直接归因于预测误差。
- 非强制且玩家报错：下一步追真实networked字段与坐标/命令历史。
- 非强制且其它实体报错：沿该实体链调查，不能仅修玩家origin。
- Hook失败、全程无自然比较、invalid/溢出影响归属或报错与Reset不关联：停止因果修复，核实观察缺口。
- 只有找到可区分来源才继续字段级研究；不禁用Reset、不改amount/选时、不用相机补偿掩盖。

## 验证与部署

离线契约覆盖完整body/跳转表/ASLR/rel32/ABI尾部、root归属、ack0/错误未知/负数/多次/返回不一致、有限时间和空身份、prelude与预算后全计数、模式变化与强制模式直方图。它们不能代替游戏内安装、生命周期、开销和本轮因果判断。

2026-10-14最终交付验证：修正后28/28 runner通过，Debug x86 MSBuild exit0；完整磁盘body/跳转表/PE relocation与产品、独立测试fixture核对通过，8组新增/修改日志参数数量一致且最宽保守上限1324/2048bytes。git diff --check exit0。已有transform测试C4819编码警告未扩大为本轮错误。

唯一独立增量审查无Critical，发现Important“重置/关闭后旧事件能进入新prelude”及Minor“后续正常事件挤掉前置错误”。核实后补epoch及inactive关闭清环、优先且按序的有限prelude；新回归先失败再通过，完整测试/构建重跑。运行态安装、覆盖/生命周期/开销及报错字段/因果结论仍需本轮实测；既有脏基线与进程级hot-unload不作为本轮已验证能力。没有再分派实现代理或第二轮全量审查。

已备份并部署 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`，源/目标SHA一致：

- 修改时间 `2026-10-14 02:19:34`，大小 `1569280` bytes，build `pe-6ACE7636`。
- SHA-256 `8C722B32A8A4AED5448E409EA656F208DE5008D9D10DF4E4053F14EDFBAD949A`。
- 旧Test20备份 `src/Debug/deployment-backups/L4D2_Portal.before-prediction-error-owner-20261014.dll`，SHA-256 `1030F86E0956AD9655EDD660BCD0E25360B088C038D7B1785271F2EC0A3DBA35`。

部署时游戏未运行，没有启动游戏、复制EXE、提交推送或切换分支。保留 `codex/portal-official-mechanism` 与已有未提交基线；增量范围按编码前快照隔离复核，不将整个HEAD差异当作本轮代码。
