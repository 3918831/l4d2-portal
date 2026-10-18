# 第 14 轮：原生位置更新链只读诊断

日期：2026-09-20。依据：用户批准第 13 轮分析文档第 6 节的诊断方案。

## 范围与当前状态

本轮不修相机，不改 origin、velocity、MoveType、ground、usercmd、碰撞、门变换或 roll 曲线。未恢复旧 noclip、movement mutation、出口 push 或 PortalMaskRepair。singleeyehistory 仍默认关闭，不提升为基线。

已实现：

1. 在已有 Hook 上读取同帧的 render-start 原函数前/后、原始 CalcPlayerView、适配后 CalcPlayerView、最终主视图；CalcView 过滤非本机玩家和递归传送门视图，阶段样本带独立 frame、tick 和 validity。最终主视图入口适用于现有 render modes 1/2/3。
2. 记录 curtime、frametime、interpolation_amount、simulationTime、tickBase、effects、flags、MoveType、预测状态及 abs/render/recv-field/eye/velocity。`recvField` 明确是现有 `m_vecOrigin()` 访问器的值，不能仅凭名字认定是未经修改的网络历史。
3. CreateMove 只读记录真实 command identity，不再用短寿命穿越 phase 的 lastCommandNumber 代替当前命令；0 号额外鼠标采样或旧预测重放不能倒退这个身份。旧 cadence 诊断也改用这一来源。
4. 所有穿越自动启用独立窗口，持续至 40 tick、2048 render frames 或 5 秒 realtime 中任一上限；不因 traversal phase 回到 Idle 而终止。窗口被新穿越替换时输出 superseded summary。
5. 四个近期 tick 边界快照作为 prelude；活动窗口只在 tick、冻结开始/结束、眼位历史/释放、落地和 roll 完成等状态边界输出。每窗口最多 128 组普通 frame 明细，另保留终端帧；汇总统计不因压缩停止。
6. 原生 movement 两个阶段合计最多 160 组；恢复的 FloorWall 客户端和服务端明细分别最多 160 行，且必须属于当前活动事务。超限有 dropped/suppressed 计数，避免旧预测重放无限扩大日志。
7. `portal_diag_nativefall` 一次性等待正常 WALK 类型的离地下降，30 秒未触发自动取消；不要求传送，不绑定主观标记，也不修改玩家状态。`txn=0 kind=normal-fall` 是合法对照，`capture` 是独立编号。
8. 每个窗口读取并记录 cl_predict、cl_interpolate、cl_interp、cl_interp_ratio、cl_smooth、cl_smoothtime、host_timescale、fps_max；缺失 cvar 输出 valid=false，不进行写入。

## 尚未声称具备的能力

- 未增加未经验证的 native private 字段或新的 ABI 调用；final predicted time、内部 interpolation list/VarMap 状态仍为 `nativePrivateState=unknown`。
- 服务端原有字段若不可读，继续 `stateValid=false`，不能用零值计算物理结论。
- 本轮定位的是已有接口之间的首个异常边界，尚不能保证一次测量就识别具体原生分支。
- freezeMs 的时基明确为 `engine-frametime`，不是任意 timescale 下的真实墙钟时长。本轮统一 host_timescale=1，才可直接按毫秒比较。
- native State 信息只在现有 cadence 能提供同帧结果时 valid=true；缺失不是 active=false 的证据。帧级视图来源仍照常采集。

## 游戏内测试

必须完全退出旧游戏进程，重新由启动器在主菜单加载新 DLL，再进入单人 localserver。无需重设那些已经默认开启的视觉参数；本轮无需 F8/F9/F10 或其他主观标记。

### A. 环境和普通下落对照

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_bsp_phase1 0;
```

在没有活动传送门、普通可站立的平台上，输入：

```text
portal_diag_nativefall;
```

看到 `armed=true` 后，30 秒内自然跳下平台，正常落地，至少等待 2 秒。不要使用 noclip、梯子、水面，不要跳向地图外。重复输入此命令再做一次。平台不必精确，但尽量让下落速度接近之前地板门入口速度；不方便时普通跳跃也能提供低速对照。

### B. 控制组

```text
portal_bsp_phase1 1;
portal_bsp_status;
```

确认放好一对门后 status 中 enabled/carvingActive 正常。先预热一次，之后：

- 墙面双门：步行双向各 2 次、跳跃/高速空中双向各 2 次。
- 地板→墙面：5 次，在相同站位、输入方式、视角下重复；出口尽量留足空间，避免立刻撞墙或落地混入主要自由飞行窗口。
- 每次完成后等待约 2 秒，再重新开始。

### C. 实验组

```text
portal_visual_singleeyehistory 1;
portal_bsp_status;
```

重复 B 的墙面回归和地板→墙面测试。特别注意墙面回归也必须在开关为 1 后执行，补齐第 13 轮未覆盖的组合。不要在飞行途中切换参数。

结束后退出游戏并保存本次日志；如需继续正常使用，可切回 `portal_visual_singleeyehistory 0`。游戏内不用寻找具体帧或手动标记。

## 日志自检与下一步判定

应出现：

```text
[PortalNativeCadenceBegin] version=1 ... kind=normal-fall
[PortalNativeCadenceBegin] version=1 ... kind=floor-wall
[PortalNativeCadenceStage] ... stage=render-start-pre
[PortalNativeCadenceStage] ... stage=render-start-post
[PortalNativeCadenceStage] ... stage=calc-original
[PortalNativeCadenceCommand] ... stage=processmovement-post-original
[PortalNativeCadenceSummary] ... freezeTimebase=engine-frametime
[PortalFloorWallCommand]
[PortalFloorWallServerCommand]
```

不同接口是否真的回调、validity 是否成立正是本轮要验证的事项。缺失标签或 valid=false 请原样保留，不要仅因缺少某条日志反复重测。普通下落 Begin 应为 txn=0；不是丢失事务。

判定顺序：先比较 command-end；再看 render-start 前后是否改变位置；再看原始相机和最终主视图。对照普通下落与墙门，排除常规原生行为。若只有 floor-wall 命中同一异常边界，再决定修复该边界；不根据 residual 小或平均速度比接近 1 宣称连续。

## 验证与部署记录

- 首个 RED：旧日志策略拒绝 PortalFloorWallCommand；新增 tracker 空实现无法保存实际输入命令。随后分别实现并转 GREEN。
- 新增测试覆盖命令不倒退、重复帧去重、冻结毫秒数、缺失原始相机样本、非有限数据拒绝、tick/realtime 截止、frame 日志预算保留终端、command 行预算及眼位释放边界。
- 全部 27 个 `tests/run_*.cmd` 通过；最终 Debug x86 构建通过。首次全量相关编译报告既有 convar.cpp 的 5 条 C4355 警告，最后增量构建无新警告。
- 只读审查未发现新增游戏状态写入；日志预算、释放边界、递归过滤已补齐。运行态 ABI/时序、日志开销和体验效果留给游戏实测，不能由单元测试保证。
- DLL 已部署：`J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`；大小 1,393,152 bytes；SHA-256 `3CB3997A8D93390DF63E9BC95C48D72CE6BDFB70ECA42BE9C20544325B596C17`，构建源与游戏目录一致。
- 旧 DLL 备份：`src/Debug/deployment-backups/L4D2_Portal.before-native-cadence-20260920.dll`。
- 未启动游戏，未暂存、提交、推送。保留工作区原有修改与无关研究文档。
