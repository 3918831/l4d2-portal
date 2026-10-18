# 第23轮：原生下落状态命令边界诊断

承接 [第22轮结论](portal-floorwall-test22-native-fall-field-analysis.zh-CN.md)。本轮不改变穿越行为，主观体验可能不变；目的是确认真实服务端fall/ground更新，再决定修复。没有新控制台变量或bind。

## 读取与解释边界

新增隔离只读模块，不改旧ServerDataMap写入层。服务端slot11目标必须是完整六byte的mov-eax-constant / ret简单getter且属于server executable image；不主动调用函数。root class必须CTerrorPlayer，m_Local必须CBasePlayer的单个非指针embedded，child class必须CPlayerLocalData，fall必须单个4byte float、offset在embedded尺寸内。循环/重复/越界/类型错/不可读均unknown；运行态offset由本机真实描述项读取，不编译静态实体offset。

可选velocity/ground/flags/MoveType/tickBase分别按描述项类型尺寸校验，不可用不输出为真实0。客户端fall由现有SDK公开RecvTable路径找到float、累加embedded table offset；其它客户端状态沿已有SDK只读访问。不是prediction私有buffer读取，没有额外调用比较、改变原函数参数/返回、写实体、禁用Reset或主动改变配置。

`instantaneousFallPlusZ`仅用于展示当前值关系，不是预测误差：官方fall记录运动开始时Z，command执行后的重力阶段本来就可能与当前Z相差一tick。原生字段报告才是网络/预测差异证据；当前实体值不自动等于ack对应预测槽。四phase各48条明细，全调用counter先计数，明细耗尽后仍有valid/unknown、ground、fall范围/变化/保持统计。schema10标明新探针。

## 完整测试指令（均有英文分号）

重启游戏，从主菜单加载新DLL，单人localserver。维持之前相同地图与自由飞行出口，不改变timescale或cl_pred_doresetlatch：

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
cl_showerror 2;
portal_bsp_phase1 0;
```

先做2次普通自然下落，每次开始前输入，落地后等至少2秒、不要提前重新arm：

```text
portal_diag_nativefall;
```

再开启既有穿越前置：

```text
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_visual_reorientation 1;
portal_bsp_status;
```

1. 墙门热身一次，之后两方向各步行1次、跳跃2次，脚离地进入，不要恰好落地过门。
2. 地板→墙面同一对门5次正常测试，出口保留自由飞行距离，落地后至少等2秒再下次。
3. 不需要任何主观标记/绑定。若卡住先停止该次；需要救出时可以结束该次再另测，保留日志并说明。不要将卡住后的MoveType改变混为正常样本。

全部结束恢复原有cl_showerror0（若本轮前自行修改，则恢复你自己的原值）：

```text
cl_showerror 0;
```

## 预期日志与判据

- `PortalNativeCadenceBegin version=10 ... fallStateProbe=read-only-command-stages`。
- `PortalNativeFallStateProbe domain=server getterValidated=true fallBound=true`；ground/velocity是否可用分别记录，不臆测。
- `PortalNativeFallStateProbe domain=client fallBound=true`。
- `PortalNativeFallState phase=server-pre/server-post/client-movement-pre/client-movement-post`，带cmd、fallValid、fall、groundValid等。
- `PortalNativeFallStateSummary`四phase的完整调用计数和明细预算。
- 原生`PortalNativePredictionField`/Reset继续保留，用于关联持续fall误差。

若server-pre/post有效原值已更新而比较net保持，转向发送/命令槽；若原值不更新且ground非空，沿官方ground路径核实；若ground为空仍不更新，查L4D2运动实现/早退。若核心binding失败，先修探针，不把unknown当无误差。此步有清晰可证伪结果，不继续改相机amount或增加恢复期补偿。

## 验证状态

离线读取/计数与日志路由测试先失败后通过。唯一独立只读增量审查无P1/P2阻塞，指出客户端首匹配与子表错误吞掉的P3防御缺口；已核实并补上完整树查找，重复字段、损坏子表、循环、类型错误和offset相加越界测试先失败后通过。不增加第二审查座席或新行为因子。

最后代码修改后重新运行完整28个runner，28/28通过，Debug x86 MSBuild exit0；旧transform C4819、较早全量构建convar C4355警告已知，未当新错误。4条新增日志参数数量15/2/19/20匹配，最大保守1394/2048bytes；git diff --check exit0。增量按编码前快照复核，原函数仍自然执行一次、原有参数/返回/字段写入层不变。

已部署 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`：

- build `pe-6ACF8AAC`，修改时间 `2026-10-14 21:59:08`，大小 `1641472` bytes。
- 源/游戏DLL SHA-256一致：`5FCCC3BF7E3DF7AF20F03CF7682323DC786935ADE37ADF7BE3311F651F51F135`。
- 旧Test22备份 `src/Debug/deployment-backups/L4D2_Portal.before-native-fall-state-20261014.dll`，SHA-256 `F34D877C7C9160802399389C44F477BDE07E32B383C4A8914565E2F7015CF874`。

部署时游戏进程0；复制前验证源/旧目标哈希和备份不已存在，备份后核验，再复制并核验目标。未复制EXE、启动游戏、提交推送或切换分支；保留现有脏基线。

真实getter、map嵌入链、client RecvTable、命令生命周期、同cmd跨端时序、运行态读取开销仍由本轮游戏实测补证。线程/同步重入、TOCTOU、hot-unload没有新的运行态证明，不把离线审查作为这些能力的完整验收。采集边界可能只有某phase而无配对，按capture/phase/cmd关联，不能当原子快照。不能用离线测试宣称游戏内顿挫已修复或本轮已适合合并。
