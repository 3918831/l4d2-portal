# 第27轮：官方落速语义单因子 A/B

## 授权与边界

用户已批准第26轮分析提出的单因子方案。目标只是在已确认穿越后的本地空中运动中恢复 `fall=-当前 MoveData.z` 的有符号语义；不修改 origin、velocity、movetype、ground、镜头或原生 Reset，不限制速度。先验证写入和网络变更通知，再由游戏 A/B 判定因果。尚未游戏验收。

## 原生能力研究（Tesla）

结论：当前二进制具有可验证的真实更新点及嵌套网络变量通知，不需要裸写后伪造 edict 状态。

- 官方 `Reference Code/portal-sdk-2013/src/game/shared/gamemovement.cpp:4603`：CategorizePosition/解除地面后，GetGroundEntity 为 NULL 时，直接赋值 `-mv->m_vecVelocity[2]`，之后才执行脚步声、Duck、LadderMove、FullWalkMove。
- `public/networkvar.h:204`：CNetworkVarBase::Set 在值变化时先 NetworkStateChanged，再写值；嵌套 m_Local 转发至实体。
- 当前 server.dll 首选 VA `100FC827..100FC8AE` 的完整块证明：GetGroundEntity 调用 → 空中分支 → 旧峰值 max → `m_Local` 虚调用（this=m_Local、参数=fall 地址、ret4）→ float store。
- `10148E80..10148EB1` 是无参数 thiscall ground getter；本轮只观察上述确切 caller，原 getter 返回值不改变，不增加 GetGroundEntity 调用。
- 构造函数 `1017F3B5` 及本机 vtable 给出 m_Local 通知目标 `101711B0`；49字节完整函数通知 deferred flags 或 edict changed flags，并调用原生 change-accessor helper。这些 VA 仅用于离线核验，不作为运行态实体地址/字段偏移兜底。
- 实际 GameMovement 接口的 slot18 为 CTerrorGameMovement/CCSGameMovement 外层，不能在外层附近误找基类更新。启动时仅扫描 server PE 可执行区的完整135字节更新块（含 ASLR 重定位），全镜像必须唯一命中；再核验49字节 ground getter、49字节通知及18字节 accessor helper。不会在逐帧或逐cmd全扫描。
- 依赖：运行态 datamap 的 m_Local/fall/ground/movetype/lifeState 类型与布局；原生 PlayerMove 更新块、getter 与通知函数完整指纹；可写字段及通知目标；已确认 localserver 穿越身份。

调用链：已确认 Teleport → 穿越空中生命周期 → 既有服务端 PlayerMove scope → 原生 GetGroundEntity 的确切 fall 分支 caller → 原通知函数 → fall 写入/读回 → 原 PlayerMove 继续。先写正确候选使紧随其后的 L4D2 max 分支收到相同候选；不改变分支跳转、ground 返回或实际速度。

这是复用 L4D2 原生调用链的适配，不声称完整移植 Portal 的 PreThink。通知 called/dirty evidence 也不等同于已验证网络包到达，后者必须由下一轮两端 fall/预测误差日志确认。

## 执行计划

1. 纯策略及字节指纹测试：签名拒绝、独立字段交叉验证、有符号候选、失败不写、先通知后写、生命周期停止；先 RED 再 GREEN。
2. 原生写入/通知与局部 Hook 接入：默认关闭 `portal_server_fallfix`；只允许确切 caller/local command/本地实体，按自然落地/死亡/移动类型/身份/场景重置退出，不依赖诊断采样窗口，不引入秒数恢复期。
3. 限量明细与总计，记录拒绝原因、写权限、通知及读回，防止日志再次膨胀；更新文档、回归测试、构建和独立审查。
4. 备份旧 DLL，部署并核对 SHA256；A/B 各5次同一对地板→墙面门，另做普通下落及墙门步行/跳跃回归。

## 判定与停止条件

实验组须同时出现 writeValidated、notifyValidated、applied/readback；无能力则 fail closed，不把开关=1当生效。比较延迟空中 fall 预测误差、Reset 和 origin 历史夹取是否一起减少，再看主观体验。若 fall 已一致但粘滞仍在，停止把全部症状归因于 fall；不追加镜头补偿。落地伤害/冲击为安全回归项。

## 执行与部署记录

默认 `portal_server_fallfix 0`。能力校验失败则拒绝，不能绕过。完整字节指纹和独立 datamap 必须一致；MEM_COMMIT/可读写、m_Local 通知目标、engine accessor 链及本地 edict 必须匹配。通知仍执行引擎原函数，不手工 OR edict flags。值变化时先通知，确认原生 dirty/deferred 标志后才写入，随后 bitwise 读回。

候选保留负号；超过4096的 MoveData.z 仅拒绝，不做限速/钳位。数值边界是本轮保守写入能力约束，不是全场景跌落伤害规则。实际速度仍由游戏原 movement 管理。

每次穿越最多8条成功明细，一旦失败记录并停止该次实验；所有调用/候选/通知/写入/读回有汇总。生命周期不依赖 transactiondiag、cl_showerror 或采样窗口。关闭开关只停止后续维护，不回滚已写 fall；必须落地后再切组。

本轮复用了第26轮八阶段日志，不删除这些仍需因果对照的诊断。没有恢复任何旧 movement/镜头补偿路径。

## 完整测试指令

重启游戏并在主菜单注入新 DLL，进入单人 localserver。不要热覆盖已注入模块。保持普通 timescale；无需主观 bind。每次落地后等至少2秒，第一次穿越只作预热。

初始化并做普通下落对照：

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_visual_reorientation 1;
portal_server_command_anglefix 1;
portal_server_fallfix 0;
cl_showerror 2;
portal_bsp_phase1 0;
portal_bsp_status;
```

普通下落2次，每次下落前单独执行：

```text
portal_diag_nativefall;
```

随后打开穿越基线：

```text
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_placement_floorsnap 1;
portal_ground_livecommit 1;
portal_bsp_status;
```

先墙门走/跳各方向一次并预热，再放好一对地板→墙面门；A/B期间不要重放门。

A 控制组，地板→墙面连续完整5次：

```text
portal_server_fallfix 0;
portal_server_fallfix;
portal_log_flush;
```

B 实验组，**已落地后切换**，同一门对5次：

```text
portal_server_fallfix 1;
portal_server_fallfix;
portal_log_flush;
```

B刚切换尚未穿越时 writeValidated/notifyValidated=false 是未绑定，不代表失败；首次成功穿越后日志须有 `[PortalFallWriteBind] bound=true`，随后 `[PortalFallWrite] enabled=true ... notifyCalled=true dirtyObserved=true applied=true readback=true`。若为 binding-rejected/write-or-notify-failed，停止重复实测并提供日志，不把它当B成功。

B后保持开关1，墙→墙步行、跳跃/高速各方向各2次；再关闭brush做2次普通下落（每次前 portal_diag_nativefall），注意普通落地冲击/伤害是否异常。所有观察不要求按键标记。

```text
portal_bsp_phase1 0;
portal_diag_nativefall;
```

全部结束后：

```text
portal_server_fallfix;
cl_showerror 0;
portal_server_fallfix 0;
portal_log_flush;
```

验证：全29/29 runner通过；Debug x86构建成功；四段磁盘fixture一致；新日志5处格式参数数量一致，保守最大776/2048字节。首次构建缺少现有IVEngineServer接口头，补齐后通过。一次独立只读增量审查无Critical/Important，可交付受控实测。

审查保留一个非阻塞日志小项：Reset后状态命令的reason仍表示上次绑定结果，可能active=false但reason=ready；当前生效与否看active/writeValidated/notifyValidated，停止原因/总应用数看 `[PortalFallWriteSummary]`。落地后当前作用域计数归零是正常，不代表之前没有生效。

上述验证不代替游戏中的Hook命中、真实网络状态更新和主观/落地伤害回归。

### 已完成部署

- Debug x86 DLL：build `pe-6AD1B572`，修改时间 `2026-10-16 13:26:10 +08:00`，大小 `1,699,328` 字节；schema仍为13。
- 源：`src/Debug/L4D2_Portal.dll`；目标：`J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`。
- 源/目标 SHA-256 一致：`EE512EF9C4CE0A765ABEAE62B7F0292022076EAE507290B8867F37DA109D1B4B`。
- 旧DLL已备份为 `src/Debug/deployment-backups/L4D2_Portal.before-fall-write-test27-20261016.dll`，SHA-256：`0B6E39B29CBEC1479F1A72FF30780B1F3D28682F01BA36C25BBCA9064B399A34`。
- 部署前确认游戏未运行，校验旧文件与备份，再复制并复核哈希。未启动/终止游戏，未替换启动器，未提交/推送或切换分支。

本轮编码与部署交付完成；真实Hook命中、网络状态同步及主观效果仍待上述A/B实测。
