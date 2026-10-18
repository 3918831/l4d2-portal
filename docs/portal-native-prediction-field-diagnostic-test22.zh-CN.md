# 第22轮：捕获原生预测误差字段报告

承接 [第21轮验证结论](portal-floorwall-test21-player-prediction-error-analysis.zh-CN.md)。本轮仍为诊断，不承诺主观改善，不修改已有穿越方案。

## 目的与边界

第21轮已排除强制Reset和非玩家错误；下一步确认本地玩家具体哪些networked预测字段失配。原生默认fast-path在首个误差后停止，所以之前errorCount1不能等于只有一个坏字段。

本轮由你临时设置原生 `cl_showerror 2;`，让引擎自己比较/报告字段。DLL不写任何ConVar；自然比较和合法恢复保留。它会增加原生输出/详细比较开销，因此不将本轮主观卡顿或帧耗时直接解释成行为修复效果。

新增探针由tier0.dll真实导出LoggingSystem_LogDirect解析73byte包装器→383byte成员实现，完整指纹/精确重定位/全局对象/ret16 ABI验证后安装。原函数channel、severity、Color bits、文本参数原样传递，返回不改、自然执行一次。只在现有validated root的精确错误TransferData调用scope里观察message severity；嵌套scope恢复，epoch/本地身份保护，日志重入保护。

不注册listener，不改变response policy、不转发variadic参数、不额外执行比较。不读预测buffer、datamap描述或字段值存储。字段名和值来自原生已经格式化的文本，不能声称为typed数据读取；过长/未终止/不可读/格式不符/index不符均unknown，不当没有误差。

每窗口48条字段明细，16个native index/class/field桶，总计先累加再压缩；128近期事件与最多8条前4tick prelude不重复计入窗口。FieldSummary报告所有格式化回调/解析失败/桶溢出/预算丢弃；ErrorSummary的nativeMessages/nativeFields/positiveWithoutNativeField用于识别报错存在但报告未捕获的缺口。旧Reset和实体归属日志仍用于时序对照，无新cvar/bind或证伪路径恢复。

## 完整测试指令

重启游戏，从主菜单加载新版DLL，进入单人localserver：

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
cl_showerror 2;
portal_bsp_phase1 0;
```

做两次普通自然下落，每次开始前输入，落地后至少等2秒，不要提前重新arm：

```text
portal_diag_nativefall;
```

再开启穿越，恢复既有前置：

```text
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_visual_reorientation 1;
portal_bsp_status;
```

1. 墙门热身一次，然后两方向各步行一次、跳跃两次；脚离地进入门，不要刚好落地过门。
2. 同一对地板→墙面做5次，出口留自由飞行距离，首次落地后等至少2秒再开始下一次。
3. 无新标记或bind，不改timescale/MoveType/cl_pred_doresetlatch。出现新增视觉异常/崩溃则停止留日志。

全部结束后恢复上轮日志中的原始值0（若本轮前自行改过值，恢复你原来的值）：

```text
cl_showerror 0;
```

## 预期日志与判断

```text
[PortalNativePredictionFieldHook] installed=true ... exportValidated=true memberValidated=true objectValidated=true abiValidated=true
[PortalNativeCadenceBegin] version=9 ... predictionFieldHook=true
[PortalNativeCadenceCvar] ... name=cl_showerror valid=true ... intValue=2
[PortalNativePredictionField] ... parentSeq=... valid=true ... class=... field=... value='...'
[PortalNativePredictionFieldAggregate] ... field=... occurrences=... firstValue='...' lastValue='...'
[PortalNativePredictionFieldSummary] ... installed=true messages=... parsed=... unknown=... fieldOverflow=...
[PortalNativePredictionErrorSummary] ... nativeMessages=... nativeFields=... positiveWithoutNativeField=...
```

普通下落/墙门的大部分正常比较不产出字段报告是合理的；不要求每窗都有正误差。持续报错的地板窗口若positiveWithoutNativeField持续大于0，或者只有unknown，则先诊断入口/格式/归属，不能把没有字段行当作修复。

若成功看到持续字段：按时间/实体/packet关联同frame Reset，比较普通下落和墙门，只对相应官方预测/同步步骤研究下一步单因子。若无可区分证据，停下讨论，不禁用Reset、不更改amount或另加相机尾巴。

## 构建部署验收

2026-10-14最终验证：修正后28/28离线runner通过，Debug x86 MSBuild exit0；tier0完整73/383byte函数、12/5处PE重定位与产品/独立fixture核对一致。7组日志specifier和参数数量一致，保守最宽1044/2048bytes。git diff --check exit0。全量编译已有convar C4355、transform测试已有C4819警告，未当作本轮新增错误。

唯一独立增量审查无Critical，指出原日志执行期间可能由listener同步重入的新证据归属风险。已核实并将guard覆盖观察及原函数完整执行，以RAII恢复先前TLS值；测试先失败后通过，嵌套调用逐次转发、仅外层被观察、返回值不改，完整测试与构建重跑。没有再新增全量审查或修改旧行为基线。

部署 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`，源/目标SHA一致：

- 修改时间 `2026-10-14 13:29:54`，大小 `1614848` bytes，build `pe-6ACF1352`。
- SHA-256 `F34D877C7C9160802399389C44F477BDE07E32B383C4A8914565E2F7015CF874`。
- 第21轮备份 `src/Debug/deployment-backups/L4D2_Portal.before-native-error-fields-20261014.dll`，SHA-256 `8C722B32A8A4AED5448E409EA656F208DE5008D9D10DF4E4053F14EDFBAD949A`。

部署时游戏未运行，未启动游戏或复制EXE、提交推送、切换分支。保留 `codex/portal-official-mechanism` 与已有脏基线，增量按编码前快照复核。

离线测试不能替代本机真实Hook安装、Msg是否进入此成员、severity/文本格式、live生命周期/线程归属/异常展开/卸载及开销。字段因果与主观改善未验证；不是合并或游戏修复验收。当前重启→单人localserver的有界实测用于补这些运行态证据，未知文本/缺失回调不当0误差。
