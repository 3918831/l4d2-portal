# L4D2 Portal 官方式穿越视角旋转设计

日期：2026-08-09

分支：`codex/bsp-portal-traversal`

状态：用户已于 2026-08-09 确认书面规格；P1 实施计划已完成

## 0.1 L4D2 二进制接缝复核增补（2026-08-09）

在用户确认设计后，对当前测试版本
`J:\Games\Left 4 Dead 2 v2.2.4.3\left4dead2\bin\client.dll`
进行了只读反汇编复核。结论是：源码级 `CInput::ClampAngles(QAngle&)`
在该优化构建中没有保留为可独立 Hook 的函数，而是被内联进
`CInput::AdjustAngles(int slot, float frametime)`。

当前版本的确定证据如下：

- `CInput::AdjustAngles` 首地址（默认 image base 下）为 `0x10133E10`；
- 函数 ABI 为 x86 `__thiscall(this, int slot, float frametime)`，尾部为
  `ret 8`；
- 函数先经 engine `GetViewAngles` 取得本地角度，完成 pitch 合法化后，
  在函数偏移 `+152` 读取本地 `roll`；
- 偏移 `+157` 和 `+170` 的两条 `movss` 分别引用 `+50.0f` 与
  `-50.0f`，随后把结果写回本地 `QAngle`；
- 最后经 engine `SetViewAngles` 写回，正是日志中下一输入命令把完整
  roll 截断至 `+/-50` 的实际接缝。

因此实施目标从“Hook 编译器可能已经消除的独立 `ClampAngles` 函数”
细化为“Hook 承载该官方调用点的 `CInput::AdjustAngles`，调用原函数后只
恢复进入函数前的 roll”。这仍然处在 Portal SDK 的输入角度合法化层，
不会退化为 `ClientMode::CreateMove` 事后改写，也不会只覆盖渲染角度。

Hook 必须同时通过以下验证才允许实验 mutation：

1. 长签名匹配 `AdjustAngles` prologue、engine view 调用与内联 roll-clamp
   指令序列；
2. 目标位于 `client.dll` 可执行代码区；
3. `+157/+170` 两个常量引用运行态解引用为 `+50.0f/-50.0f`；
4. `+210` 为 `C2 08 00`；
5. 首次调用的 `this` 与 `I::IInput` 相同。

任一条件失败时只记录诊断，`portal_visual_reorientation 1` 也不得产生
角度 mutation，且不得自动回退到 `CreateMove` 或 render-only 方案。

## 1. 目标

在不改变当前 BSP whole-brush 物理穿越、位置连续性、速度连续性和 server-side `Teleport` 基线的前提下，补齐 Portal SDK 2013 的穿越后视角旋转生命周期：

1. 穿越提交时保留入口到出口矩阵产生的完整 pitch/yaw/roll；
2. Portal 活动视角不受普通 Source 游戏的 roll `[-50, 50]` 输入限制；
3. 穿越后按渲染帧平滑回正，而不是立即把 roll 清零；
4. 最终 roll 收敛到 `0`，使画面正下方重新与世界地面垂直；
5. 后续单独补齐完全倒置时的官方鼠标输入方向；
6. 全过程保持单因子 A/B，可记录、可回滚、可验证。

本设计不重新启用旧 noclip、movement mutation、出口 push 或 `PortalMaskRepair`。

## 2. 已确认根因

`高速穿越连续性修复/4` 的 11 次 floor-to-wall 事务均保持了速度模长，`moveType=2`、`onGround=false`、`ground=-1` 也符合预期。共同异常发生在角度边界：

- 穿越提交后的 roll 为 `173.7–179.8` 或 `-86.9–-92.5`；
- 下一条用户命令将 roll 精确截断到 `50` 或 `-50`；
- 单次角度损失为 `36.9–129.8` 度；
- 主观标记中的最大视角跳变与该损失一一对应。

因此当前不是没有执行角度矩阵变换，而是：

```text
完整角度变换成功
-> 下一输入命令执行普通 L4D2 roll clamp
-> 完整 roll 被截到 +/-50
-> 工程缺少 Portal 的 FixTeleportationRoll
-> 视角长期停在错误姿态
```

## 3. Portal SDK 官方行为

### 3.1 完整 roll 是正式传送状态

Portal 客户端在 `DetectAndHandlePortalTeleportation` 中对完整 `QAngle` 做门对矩阵变换，分别 normalize x/y/z，并重置 `m_iv_angEyeAngles`。

Portal 服务端同样使用 `TransformAnglesToWorldSpace` 变换完整角度。`CPortal_Player::PlayerRunCommand` 的旧命令竞争修复也把 `viewangles.z` 纳入比较和变换。

### 3.2 Portal 构建不限制 roll

`CInput::ClampAngles` 始终保留 pitch 限制，但 roll `[-50, 50]` 限制位于 `#ifndef PORTAL` 内。Portal 构建允许玩家持有完整的 `[-180, 180]` roll。

### 3.3 回正由独立状态机完成

`C_Portal_Player::ClientThink` 每帧调用 `FixTeleportationRoll`：

- `REORIENTATION_RATE = 120 deg/s`；
- `REORIENTATION_ACCELERATION_RATE = 400 deg/s^2`；
- 玩家落地时最大回正速度乘 2；
- 普通 floor-to-wall 默认采用 roll reorientation；
- roll 单调趋近 `0`，禁止越过目标；
- 特殊 wall、floor-to-floor、wedge 条件才启用 pitch reorientation；
- 完全倒置时维护 `g_bUpsideDown`，供 Portal 专用鼠标方向逻辑使用。

官方并不在穿越后立即令第三轴等于零。短暂非零 roll 是正确的传送姿态；错误的是当前 L4D2 的瞬间 `+/-50` 截断和永久残留。

## 4. 方案比较

### 4.1 方案 A：精确 Hook `CInput::ClampAngles`，推荐

运行态解析并验证 L4D2 `CInput::ClampAngles`。活动 Portal 角度事务内调用原始逻辑后恢复进入函数前的 roll，从而保留引擎原生 pitch clamp，只跳过 roll clamp。

优点：

- 对应 Portal SDK 的确切输入层；
- 后续用户命令、预测、服务端和渲染共享同一完整角度；
- 可继续复刻官方倒置鼠标语义。

风险：

- 非虚函数需要游戏版本相关签名；
- 必须进行可执行区、模块范围和函数序言验证；
- 签名未验证时必须拒绝 mutation，不能自动落入近似方案。

### 4.2 方案 B：`ClientMode::CreateMove` 原函数后恢复 roll，仅诊断备选

当前工程已有 `ClientMode::CreateMove` Hook，可以在普通输入 clamp 完成后重写 `CUserCmd` 和 engine angles。

缺点：原始鼠标/输入阶段已经观察到截断值，内部语义与 Portal 不一致。只用于验证“保留完整 roll 是否改善主观体验”，不作为最终实现。

### 4.3 方案 C：只覆盖最终 `CViewSetup::angles`，拒绝

渲染角度会与用户命令、预测和服务端角度分裂，无法复刻官方，也会制造新的瞄准、viewmodel 和输入问题。

## 5. 推荐架构

### 5.1 `PortalViewReorientation`

新增纯逻辑、可单测的角度状态机，职责限定为：

- 保存活动 transaction、当前完整角度、回正速率；
- 保存 `pitchReorientation`、`upsideDown`、是否落地；
- 按官方常量和分支推进一个渲染帧；
- 输出下一帧 canonical view angles；
- 在死亡、换图、关闭 brush/portal、事务失效时清理。

该模块不访问 hook、实体内存或日志，不修改 origin/velocity。

### 5.2 输入 roll clamp seam

新增 `CInput::ClampAngles` Hook：

1. 保存调用前 roll；
2. 调用原始函数，保留 L4D2 的 pitch/yaw 合法化；
3. 仅当本地玩家存在有效 Portal reorientation transaction 时恢复 roll；
4. 其他时刻完全保持原版 L4D2 行为；
5. 记录一次安装验证和事务内 clamp bypass 统计。

### 5.3 事务启动

现有 `TryCommitTeleport` 已得到 `newAngles`。提交成功并发布 eye-history 后：

- 用完整 `newAngles` 启动 reorientation；
- 记录 entry/exit normal、是否 floor-to-wall、pitch mode 决策；
- `EngineClient->SetViewAngles(newAngles)` 继续作为第一帧权威角度；
- 保留现有 client prediction angle sync 和 server `PlayerRunCommand` 一次性修复。

### 5.4 每帧推进

复用 `BaseClient::FrameStageNotify` 的 `FRAME_RENDER_START`，每个渲染帧只推进一次：

1. 读取当前 engine view angles，以保留玩家当帧 pitch/yaw 输入；
2. 使用 canonical roll 和官方状态机计算回正；
3. 写回 `EngineClient->SetViewAngles`；
4. 后续 `CalcPlayerView` 和主视图自然读取一致角度。

不在 `CalcPlayerView` 中推进时间，避免递归 portal render 或同帧多次 CalcView 导致回正速度倍增。

### 5.5 倒置鼠标

作为第二个独立因子实现：

- 由 canonical forward/up 计算 `upsideDown`；
- 仅完全倒置时按官方方式反转 mouse yaw/pitch；
- 不以“roll 非零”作为倒置判断；
- 在第一轮 roll preservation/reorientation 验证完成前不改变鼠标输入。

## 6. 安全边界

- 不修改 origin、velocity、base velocity、move type、ground entity；
- 不改变 brush contents 生命周期；
- 不启用旧 collision bypass 或 noclip；
- clamp Hook 未通过运行态验证时拒绝启用实验组；
- 只允许本地存活玩家的活动事务绕过 roll clamp；
- 非 Portal 时 roll 继续受 L4D2 `[-50, 50]` 约束；
- 所有状态在 `LevelShutdown`、portal close 和 DLL unload 清理。

## 7. TDD 与验证

### 7.1 自动化测试

先写失败测试，再实现：

1. `179.8 -> 0` 单调收敛且不越界；
2. `-92.5 -> 0` 单调收敛且不越界；
3. 空中速率不超过 `120 deg/s`；
4. 地面速率上限为 `240 deg/s`；
5. 加速度为 `400 deg/s^2`；
6. 非活动事务不允许 bypass；
7. floor-to-wall 默认选择 roll reorientation；
8. 状态清理后下一普通命令恢复 L4D2 clamp；
9. 同一 `framecount` 重复调用不得二次推进。

### 7.2 第一轮游戏 A/B

第一轮只启用“完整 roll + 官方回正”，鼠标保持静止：

- 控制组：当前 `+/-50` 行为；
- 实验组：完整 roll 保留并回正；
- floor-to-wall 至少 8 次；
- wall-to-wall、地面行走、高速空中、上下门循环各做回归。

通过标准：

- 下一用户命令不再把 roll 瞬间截到 `+/-50`；
- roll 按渲染帧单调回到 `0`；
- 无单帧角度跳变；
- 速度模长与现有基线一致；
- 无黑天空、门体、viewmodel、墙体条带或高速连续性退化。

### 7.3 第二轮游戏 A/B

仅在第一轮通过后加入完全倒置的鼠标方向：

- 静止鼠标控制；
- 小幅水平输入；
- 小幅垂直输入；
- upside-down 进入与退出边界。

## 8. 日志策略

修复 `PortalFloorWall*` 白名单，并新增压缩日志：

- `[PortalInputRollClampHook]`：签名、模块、可执行区、安装状态；
- `[PortalInputRollClamp]`：事务首帧及状态变化时的 before/clamped/restored；
- `[PortalViewReorientationBegin]`：完整角度、模式、初始速率；
- `[PortalViewReorientationFrame]`：只在整度区间、模式或地面状态变化时记录；
- `[PortalViewReorientationEnd]`：最终角度、耗时、帧数；
- `[PortalViewReorientationReset]`：清理原因。

不得逐渲染帧无条件记录，避免日志再次膨胀。

## 9. 实施阶段与 Markdown 记录

每完成一个阶段，都必须先更新本节，再进入下一阶段。

| 阶段 | 状态 | Markdown 交付物 | 退出条件 |
|---|---|---|---|
| D0 官方研究与根因 | 已完成 | 本文第 2–4 节 | 日志与 SDK 差异闭环 |
| D1 设计规格 | 已完成，待复核 | 本文 | 用户确认书面规格 |
| P1 详细实施计划 | 未开始 | `docs/superpowers/plans/2026-08-09-portal-official-view-reorientation.md` | 任务、测试、文件级改动明确 |
| I1 纯状态机 RED/GREEN | 未开始 | 计划文档执行记录 + 测试结果 | 自动化测试通过 |
| I2 ClampAngles 解析与 Hook | 未开始 | 计划文档执行记录 + 运行态验证字段 | 安装安全、默认无 mutation |
| I3 事务与 FRAME_RENDER_START 接入 | 未开始 | 计划文档执行记录 | 构建与自动化测试通过 |
| I4 第一轮部署与 A/B | 未开始 | 测试手法、DLL 哈希/时间、日志分析 | 用户确认视觉与回归结果 |
| I5 倒置鼠标语义 | 未开始 | 独立设计增补与测试记录 | 第一轮基线不退化 |

## 10. 非目标

本轮不处理：

- 首次穿越资源预热卡顿；
- portal aperture 局部 brush 挖洞；
- 最大速度长期上下门循环的额外兜底；
- predictor 的 `candidate-leaf-mismatch` 策略；
- 远程服务器和多人同步。

这些问题不得与角度修复合并为同一 A/B。

## 11. 2026-08-11 速度调优补充

游戏内验证证明完整 roll 保存和普通 reorientation 的方向与时序正确，但用户认为原 Portal SDK 常量在 L4D2 视角中回正过慢，并明确要求约半秒内完成。该调优保留既有分段状态机、单调不越零、逐渲染帧推进、重复帧抑制和输入 roll 保存，只把四个速率常量统一放大 5 倍：

- 空中低 `upZ` 上限：`600 deg/s`；
- 地面低 `upZ` 上限：`1200 deg/s`；
- 加速度：`2000 deg/s^2`；
- upright settled rate：`300 deg/s`。

对最坏普通 roll `179.8` 度的离线状态机推演结果为：30 FPS 和 60 FPS 均在 `0.466667s` 完成，300 FPS 在 `0.460000s` 完成。自动化测试必须覆盖 30/60/300 FPS、正负 roll、单调性和 `<=0.5s` 完成边界。

这是用户授权的 L4D2 体验调优，不再声称速率数值与 Portal SDK 原常量完全相同；官方的状态机拓扑、Hook 承载点与生命周期不变量仍保持不变。该速度在 Task 10 控制组和实验组中始终一致，因此唯一 A/B 行为变量仍是安全跨 leaf 许可。
