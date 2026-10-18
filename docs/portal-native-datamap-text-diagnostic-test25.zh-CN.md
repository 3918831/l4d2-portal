# 第25轮：运行态datamap动态名称读取诊断

承接 [第24轮结论](portal-floorwall-test24-cache-and-dynamic-name-analysis.zh-CN.md)。本轮只修服务端只读探针，不修改穿越/运动/视角，不新增指令、标记、bind或Hook。主观体验可能不变；目标是补齐服务端真实fall和ground取值，避免继续依据缺失数据猜测修复。

## 修改与边界

1. `Find/BindFall`显式区分结构reader和名称reader，不保留含糊的单reader入口。server map、descriptor、getter仍限制在server镜像；动态名称允许来自其它可读内存。
2. 名称仍要求由已验证结构给出地址，并经过MEM_COMMIT、保护、地址/区域边界检查，最多64字节ASCII+NUL、每块最多16字节。同步绑定内的region缓存及跨帧重新校验不变；不跳过函数条目、不猜实体offset、不放宽字段身份/类型/尺寸/PTR/范围。
3. 一次性server probe新增 `textRead(policy=readable-bounded-64 externalBlocks=... firstExternal=0x...) structureRead=server-image`。externalBlocks只统计成功读出的模块外块（会包含重复遍历，不是独立名称数量）；它证明走过新读取路径，不单独证明最终fall绑定成功。
4. `PortalNativeCadenceBegin version=12 ... fallMetadataText=bounded-readable`。既有cache-summary、QPC耗时、失败stage与四phase计数保留。没有有效值时继续压缩unknown明细，不能把“没明细”误判成没调用。

## 完整测试指令（与第24轮相同）

重启游戏，从主菜单加载新DLL，使用单人localserver。先输入：

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
cl_showerror 2;
portal_bsp_phase1 0;
```

普通自然下落2次，每次开始前输入，落地后等待至少2秒：

```text
portal_diag_nativefall;
```

开启穿越：

```text
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_visual_reorientation 1;
portal_bsp_status;
```

- 墙门热身1次；之后两个方向各步行1次、跳跃2次，脚离地进入。落地后留至少2秒，尽量避免下一次穿越覆盖窗口。
- 同一对地板→墙面门5次，出口留自由飞行空间，落地后至少等待2秒。
- 不新增主观标记，不调整timescale或movetype。体验有变化照常说明；单次偶发卡顿暂不作为本轮修复目标。若重新出现普遍长卡顿，注明场景，先评估探针cost。

结束：

```text
cl_showerror 0;
```

## 本轮验收与下一步决策

- 正确DLL：schema12，并出现新的textRead字段。
- 理想结果：server `fallBound=true`、非零运行态fallOffset，`failure(stage=bound issue=none)`，server-pre/post有有效fall值；辅助velocity/ground/flags/moveType/tickBase分别检查Bound，不能凭fall成功就当全部有效。client继续有效。
- 同身份各只有一次完整扫描，后续attempts保持1、hits增加；检查新完整扫描耗时，不能用第24轮的失败早退0.0274ms代替本轮实际耗时。
- 若仍false，按stage/issue/textIssue及descriptor继续判断；externalBlocks>0但false表示越过了旧失败点却遇到另一验证失败，不等于可以采用静态偏移。
- 成功后按同cmd关联server-pre/post、client运动前后，并与net/pred字段日志、Reset、origin夹取配对，区分真实server更新、ground条件、网络/ack时序。先确保比较对象对应，不能按两个最近行直接相减。
- 如果状态/时序已经一致而旧误差链仍在，停止fall单字段修复假设；如果缺少后续可靠的运行态获取方法，则明确说明能力缺口，不增加眼位补偿或禁用Reset掩盖。

## 离线验证与部署

最后代码改动后28/28 runner通过，Debug x86 MSBuild exit0；日志参数数量44/20/21/20/20匹配，保守最大1401/2048 bytes。`git diff --check`通过。一次独立只读增量审查未发现P1/P2/P3；审查不代替游戏内ABI、地址可读性、耗时和字段一致性验收。

回归先在原单reader路径真实失败于模块外生成名称（`dynamicNameBinding.valid`断言），分离reader后通过；同时验证模块外结构及坏字符串仍被拒绝。没有修改原生预测比较/Reset、玩家字段、服务器原函数调用次数或任何物理/镜头逻辑。

已备份并部署 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`：

- build `pe-6AD0F6DA`，schema12，修改时间 `2026-10-15 23:52:58`，大小1,666,560 bytes。
- 源/游戏DLL SHA-256一致：`10BDF8A3524CE8D0619CE917A329DFF9EBC6064C0223EFA6014BA7C0FE82C474`。
- 第24轮备份：`src/Debug/deployment-backups/L4D2_Portal.before-dynamic-datamap-text-test25-20261015.dll`，SHA-256 `A7712D8AC49E1FB8D05E4FADA4EDB3511F5A62172324F48A23DA3BBFF6782748`。
- 复制前确认源/旧目标哈希、备份不存在；备份后核验，再次确认游戏进程0后复制、核验目标。未启动游戏、复制EXE、切换分支、提交或推送。

运行态绑定和最终视觉效果不属于离线验证结论。测试期间名称内存生命周期、并发/热卸载及跨端命令配对仍需真实证据；当前只解除已证实错误的模块内名称假设，不宣称所有剩余元数据检查都会通过。
