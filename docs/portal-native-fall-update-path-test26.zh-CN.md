# 第26轮：双端fall更新边界诊断

承接[第25轮分析](portal-floorwall-test25-fall-update-path-analysis.zh-CN.md)。用户授权继续可行的诊断编码。本轮不改变物理、视角、速度、原生预测处理，不新增Hook、cvar或标记。

## 实施计划

- [x] client采样从ProcessMovement前后改为RunCommand前后，避免把实体尚未回写的velocity当作命令最终速度。
- [x] 在已有双端PlayerMove、FullWalkMove入口追加只读fall采样，分别观察运动前与PlayerMove内部更新后的状态。不是调用/替换PreThink。
- [x] 运动对象player必须与该端已绑定实体一致；客户端还须匹配当前Local。内存只读且逐次验证；MoveData.z和实体velocityZ分列，不能混淆。
- [x] 保留每阶段48条上限、所有调用统计及unknown压缩，元数据成功/失败缓存不变。替换旧client两阶段，不叠加保留。schema升至13。
- [x] 离线回归、增量审查、Debug x86构建、备份并核验部署。运行态和主观验收由用户完成。

## 验收

日志开头目标：`PortalNativeCadenceBegin version=13 ... fallUpdatePath=command-player-walk-command`。

总明细上限由4×48变为8×48条/窗口，不是逐渲染帧新增。已有标签复用、过滤规则无需变化；预期日志略增长，但每阶段独立预算，缺失值只计数。当前`velocityZ`仍表示实体字段，新增`moveVelocityZ`表示正在运行的MoveData；二者不同本身不代表速度突变。

`PortalNativeFallState`八阶段：`server-pre/post`、`client-command-pre/post`、`server-player-pre`、`server-walk-pre`、`client-player-pre`、`client-walk-pre`。新运动阶段应有`movementMatched=true moveVelocityZValid=true`；身份不符/不可读只进入unavailable计数，不扫描其它实体。

按capture+cmd串联，先看每阶段次数及重放/多次调用，不能在多次调用时强行一对一；传送提交cmd与其后延迟窗口分开。比较ground/flags/movetype与fall更新，依据第25轮三种假设决定是否有足够证据做单因子状态修复。缺少FullWalkMove时只报告缺失，不能用其它阶段冒充。

## 完整测试方法

重启游戏，从主菜单加载新DLL，单人localserver。与上一轮相同：

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
cl_showerror 2;
portal_bsp_phase1 0;
```

普通自然下落2次，每次开始前：

```text
portal_diag_nativefall;
```

每次落地后等至少2秒。然后：

```text
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_visual_reorientation 1;
portal_bsp_status;
```

墙门热身1次；两个方向各步行1次、跳跃2次（脚离地过门），落地后等至少2秒。同一对地板→墙面门5次，出口留自由飞行空间，落地后等至少2秒。不新增bind/主观标记，不调整timescale/movetype。

```text
cl_showerror 0;
```

本轮预期主观不变；如出现普遍长卡顿或新异常，注明场景。孤立一次卡顿暂不追踪。

## 验证与部署

新增边界选择测试先在未实现入口失败于`runcommand-pre-original`不能得到phase2，随后对象读取测试失败于有效prefix未返回匹配player/mv；实现后通过。覆盖空地址、身份缺失/不符、读取拒绝、地址溢出、空mv、只读不改源数据，以及超过48条后仍统计movement状态、非有限moveZ不计有效。这里验证诊断软件行为，不是假装复现视觉症状。

最后代码修改后28/28测试runner通过，Debug x86 MSBuild exit0。fall相关五条printf的参数数量44/20/21/22/23均匹配；保守最大1401/2048 bytes，新增状态明细737/2048 bytes。`git diff --check`通过（只有仓库既有LF→CRLF提示）。一次独立只读增量审查未发现新P1/P2/P3问题；人工游戏覆盖仍未完成。

实际改动：fall policy/probe/diagnostics、既有GameMovement Hook的四个只读入口、对应测试和文档。未改变原函数次数、其它诊断/功能开关或穿越行为；保留工作区其它既有修改，未提交/推送。

已备份并部署到 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`：

- build `pe-6AD109A3`，schema13，修改时间 `2026-10-16 01:13:08`，大小1,673,728 bytes。
- 源/游戏DLL SHA-256一致：`0B6E39B29CBEC1479F1A72FF30780B1F3D28682F01BA36C25BBCA9064B399A34`。
- 上轮备份 `src/Debug/deployment-backups/L4D2_Portal.before-fall-update-path-test26-20261016.dll`；SHA-256 `10BDF8A3524CE8D0619CE917A329DFF9EBC6064C0223EFA6014BA7C0FE82C474`。
- 已核对源与旧目标、确认备份不存在后复制备份并校验；两次确认游戏进程0，再覆盖目标并校验。没有启动游戏或复制EXE。

结论边界：可以进入第26轮运行态诊断，不能据离线通过宣称PreThink缺失、fall修复成功或粘滞已解决。若无新增阶段或movementMatched/MoveData无效，先查看阶段计数和身份/可读性，不采用静态实体offset兜底。
