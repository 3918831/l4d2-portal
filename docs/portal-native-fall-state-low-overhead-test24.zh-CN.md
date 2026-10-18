# 第24轮：下落状态探针低扰动诊断

2026-10-15。承接 [第23轮分析](portal-floorwall-test23-diagnostic-overhead-and-binding-analysis.zh-CN.md)。用户确认测试手法不变。本轮只处理诊断器重复扫描与安全读取开销，补足绑定失败证据；不改已有穿越、速度、ground、fall、眼位历史、视角旋转或原生预测比较/Reset行为。不会宣称旧地板→墙面的延迟视觉断点已修复。

## 修改范围

- `PortalNativeDiagnostics::Begin`只清四phase窗口计数，不再重置元数据绑定。真正的诊断/玩家生命周期Reset仍清缓存；运行态另以entity、vtable、module、root map和根header识别变化。成功和失败结果均缓存，不因新capture重新全树遍历。客户端身份还包含CBasePlayer RecvTable的props/count。缓存身份检查仍保留少量安全读取，不是永久信任裸指针。
- `Text`按最多16字节读取，总上限64。块被边界拒绝时缩小块，仍要求非空、ASCII和有界NUL；剩余块按64上限裁切，不能因首块缩短越界。不会把64字节未终止的元数据名称放宽为有效。
- 仅在一次同步绑定内缓存最多16个已验证可读memory region，消除同region重复VirtualQuery。保护/commit/地址溢出和server image限制保持；绑定结束后实体取值和跨帧身份检查重新验证可读性，不复用保护缓存。
- 一次性`PortalNativeFallStateProbe`记录QPC `totalMs / imageMs / walkMs`、read/query/region-hit/rejected计数。此耗时不包含首条探针日志格式化/写入；其输出耗时`probeLogMs`在窗口结束的cache-summary记录。计时失败标为不可用，不把nan当0。
- 服务端失败输出stage、issue、map、descriptor、index、owner、目标field和类型/尺寸/flags/offset/embedded，另带textIssue/address/inspected、最后一次readReject范围。`readReject`是扫描过程最后一个拒绝，不必然等于根因：成功的边界缩块也可能产生rejected；应先看fallBound和failure stage/issue。
- 没有核心fall值或值非有限的样本，保留全部调用/unknown/ground统计，但不打印无效命令行，也不占48条有效明细预算。`suppressedUnavailable`明确记录此压缩，和预算耗尽的dropped分开。四phase及cmd关联不变。
- schema由10升为11。沿用现有三个精确日志标签/路由，没有新增开关、bind、Hook或旧方案。

本轮不假定server绑定失败原因，也不采用磁盘静态实体offset、不放宽身份/字段校验，不读取ack预测buffer，不写实体。若server仍fallBound=false，预期能取得具体失败位置，下一步只修那个经过证实的读取条件；不能据此继续fall清零、ground或运动修补。

## 完整测试指令（手法不变）

重启游戏，从主菜单加载本轮DLL，单人localserver。先输入：

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
cl_showerror 2;
portal_bsp_phase1 0;
```

做2次普通自然下落，每次开始前输入；落地后等待至少2秒：

```text
portal_diag_nativefall;
```

然后开启原有穿越前置：

```text
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_visual_reorientation 1;
portal_bsp_status;
```

1. 墙门热身1次，之后两个方向各步行1次、跳跃2次，脚离地进入，避免恰好落地时过门。
2. 同一对地板→墙面门做5次正常测试，出口保留自由飞行距离，落地后至少等待2秒再开始下一次。
3. 无新主观标记、bind、timescale或movetype操作，不要求额外transactiondiag=0的A/B。若卡住需救出，保留日志并说明，将该次和正常样本分开。主观变化照常反馈，特别区分“第23轮新增约半秒帧卡顿”和“原来地板出口稍后出现的粘滞”。

结束：

```text
cl_showerror 0;
```

## 日志判据与路线评估

- `PortalNativeCadenceBegin version=11 ... fallBindingCache=identity-success-and-failure`。
- 同一epoch/身份初次出现server/client probe；后续窗口不应再次出现相同身份attempt递增的完整绑定行。`phase=cache-summary`中attempts稳定、hits持续增加。重放命令也只取值/计数，不全树重绑。
- 一次扫描的totalMs/queries应能回答是否仍是长帧首因；probeLogMs单独回答首条日志输出是否异常。只测探针扫描和该条日志，不代表整个帧、所有其它诊断或原生cl_showerror输出耗时。
- server若失败，具体stage/issue/textIssue与地址是本轮诊断产物；不能把unknown当速度0或ground空。失败server明细为0，summary unavailable与suppressedUnavailable应等于实际缺失样本数，calls不丢。
- client应继续fallBound=true且四phase有效明细/汇总正常。原生PredictionField→Reset→origin夹取证据仍可关联。

继续条件：新增全场景帧卡顿恢复、缓存/耗时证据符合预期，且明确服务端失败点或拿到真实server值。若缓存稳定、扫描/日志很快而卡顿依旧，不继续猜测offset或加相机补偿；转查剩余逐帧诊断/原生日志开销。真实server核心值未可靠取得前，暂停物理修补。首次scan可能仍有一次开销，需按QPC实测而非离线假定已消失。

## 验证与部署

最后代码修改后重新运行全部28个runner，28/28通过；Debug x86 MSBuild exit0。已知transform C4819警告未作为新错误；本轮增量构建无新增警告。五条fall日志参数数42/20/21/20/20均匹配，含前缀的保守最大1286/2048 bytes；`git diff --check` exit0。

唯一独立只读增量审查无P1/P2阻塞。P3指出失败日志可能沿用上一层map/字段信息；通过RED→GREEN补上当前map/descriptor元数据清理，以及embedded-class失败不沿用父类owner的测试。新增字符串跨边界缩块的64字节越界反例同样先失败后通过。不新增第二审查或修改运动逻辑。

已备份并部署 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`：

- build `pe-6ACFB3BD`，修改时间 `2026-10-15 00:54:21`，大小1666560 bytes。
- 源/游戏SHA-256一致：`A7712D8AC49E1FB8D05E4FADA4EDB3511F5A62172324F48A23DA3BBFF6782748`。
- 旧Test23备份 `src/Debug/deployment-backups/L4D2_Portal.before-low-overhead-fall-probe-20261015.dll`，SHA-256 `5FCCC3BF7E3DF7AF20F03CF7682323DC786935ADE37ADF7BE3311F651F51F135`。
- 复制前两次确认游戏进程0，并验证源/旧目标哈希、备份不存在；备份后核验，再复制并核验目标。未复制EXE或启动游戏。

增量按编码前快照复核：只涉及fall policy/probe、诊断窗口计数/汇总、相应测试和文档；ServerPlayerRunCommand原函数仍按既有代码调用一次，旧写入层/物理修复/相机算法未改。离线测试不能证明游戏内帧卡顿或旧地板→墙面问题已修复；运行态ABI/元数据、生命周期、同cmd跨端配对、线程重入/TOCTOU/hot-unload仍需游戏证据。工作区已有未提交基线保留，无本轮commit/push/分支切换。
