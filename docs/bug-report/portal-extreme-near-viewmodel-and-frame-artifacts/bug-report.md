# L4D2 传送门极近距离 viewmodel 消失与门体条块异常

日期：2026-07-28

分支：`codex/bsp-portal-traversal`

状态：实机视觉验收与日志验收通过

## 1. 范围与结论

本问题只处理 BSP whole-brush 物理穿越基线之上的两个极近距离渲染异常：

1. 玩家极其贴近传送门时，第一人称手臂和武器 viewmodel 消失，传送门框也可能不可见。
2. 另一些极近站位中 viewmodel 仍存在，但传送门模型、门框或承载面会以巨大竖条、局部门体的形式重新出现在主画面。

本轮没有处理穿越前后的主观顿挫，也没有重新启用旧 noclip、持续 movement mutation、出口 push 或已经证伪并删除的 `PortalMaskRepair`。

第三轮实机测试没有再观察到上述两种异常。日志同时证明修复分支按设计执行，因此本问题可以在当前支持范围内关闭。

## 2. 报告与复现

### 2.1 第一种异常

第一轮实测中，玩家可以在极贴近门的固定站位稳定复现：

- 朝某些方向时 viewmodel 完全消失。
- HUD、远景和其他画面正常。
- 转向后 viewmodel 又会恢复。
- 同一画面中传送门框可能不再显示。

这说明 viewmodel 并不是被游戏状态永久关闭，而是受到当前视角相关的渲染状态影响。

### 2.2 第二种异常

第二轮实测又发现：

- viewmodel 可以保持正常。
- 传送门或承载面的一部分却会以巨大竖直条块覆盖画面。
- 发光边框可能只沿条块的一侧出现。
- 缓慢转动视角时，条块的覆盖范围会发生变化。

两种异常都只在 portal plane 进入或接近主视图 `zNear` 时出现。

### 2.3 验收配置

第三轮使用以下配置：

```text
portal_bsp_phase1 1
portal_bsp_clearance exact
portal_visual_nearclip 1
portal_visual_exitguard 0
portal_visual_entryhandoff 1
portal_visual_stencilproxy_guard 0
portal_visual_viewmodel_depthclear 1
portal_visual_nearframe_guard 1
portal_bsp_status
```

日志确认最终状态为：

```text
clearanceMode=ExactTransform
legacyCollisionBypass=false
movementMutation=false
controlledNoclip=false
teleportPredictionSync=true
extremeNearGuard=false
viewmodelDepthClear=true
nearFrameGuard=true
```

其中 `teleportPredictionSync` 是既有的 Teleport 提交边界一次性预测同步，不是持续 movement mutation。

## 3. 调查过程

### 3.1 先增加 transaction 级诊断

为避免继续凭主观画面猜测，本轮先建立 `PortalVisualTxn` 诊断：

```text
entry handoff
→ teleport commit
→ prediction boundary
→ 后续五个 main-view pre-render 样本
→ transaction summary
```

每个样本明确记录：

- 眼位与视角；
- 玩家物理位置与速度；
- FOV；
- command、frame、traversal epoch；
- 数据属于入口物理空间、出口物理空间还是出口视图空间；
- 相邻可比样本的差值及 transaction 内最大差值。

该诊断只观察既有 Teleport 和 prediction sync 边界，不新增移动控制。

### 3.2 viewmodel 逻辑状态没有被关闭

第二轮诊断显示异常期间：

- `RENDERVIEW_DRAWVIEWMODEL` 仍为 true；
- 本地玩家的 `m_bDrawViewmodel` 在渲染前后都为 true；
- 相机没有切到第三人称；
- portal 通常以 ordinal 1 绘制，viewmodel 随后以 ordinal 2 或 3 绘制。

因此第一种异常不是 `ShouldDrawViewModel` 或第三人称状态错误，而更符合 portal 绘制遗留深度拒绝后续 viewmodel。

### 3.3 near-plane proxy 不能在极近距离停画

曾尝试在：

```text
abs(eyeDepth) <= frontClipDistance + epsilon
```

时停画 near-plane stencil proxy。

该假设被第二轮日志否定：

- 异常可以在 `eyeDepth` 大于 `0.3` 时出现。
- `eyeDepth` 约为 `1.1` 至 `1.95` 时，proxy 已经覆盖大部分屏幕。
- 在 `0.3` 两侧启停 proxy 反而制造离散的 stencil footprint 边界。

Portal SDK 2013 的做法也不是停画，而是把 front clip 钳制到 camera depth 后继续生成 render-fix mesh。因此该 guard 只保留为显式 A/B 开关，默认关闭。

### 3.4 当前路径缺少完整的官方 depth 生命周期

Portal SDK 的 near-plane render-fix mesh 属于完整 stencil/depth transaction 的一部分：

1. aperture mesh 与 render-fix mesh 写 stencil；
2. 在 stencil 内清承载面的 depth；
3. 绘制链接侧场景；
4. 执行 post-stencil depth/fog 修复；
5. 世界和 portal 完成后，单独绘制 viewmodel；
6. 玩家极近 portal 时，在 viewmodel pass 前清 depth。

本工程当前仍是 RTT 与自定义 stencil 合成，没有接管完整 Portal renderer。near-plane proxy 被强制写到 `NDC z=0.00001` 后，缺少 close-range viewmodel depth clear；其大面积深度可以拒绝随后绘制的手臂和武器。

### 3.5 遗留门框重绘具有不同轮廓

原实现关闭 stencil 后，会把整个 portal model 前移 `0.1`，换成 border sprite material 再绘制一次。

极近斜视时同时存在三种不一致轮廓：

- near-plane proxy 的屏幕轮廓；
- RTT portal content 的有效轮廓；
- stencil 外独立重绘的整个 portal model/frame 轮廓。

第二种异常中的巨大硬边、局部门体和单侧发光边框符合第三条路径越过近裁剪平面后的投影，而不是 BSP brush 恢复或物理穿越失败。

## 4. 根因

两个异常共享“portal plane 进入主视图近裁剪区”这一触发条件，但对应两个可分离的渲染缺口：

1. **viewmodel 消失：** near-plane stencil proxy 留下极近 depth，主 viewmodel 绘制前没有执行 Portal 式 close-range depth clear。
2. **门体条块：** stencil 关闭后独立重绘整个 portal model 作为门框，其几何轮廓与 near-plane proxy、RTT content 不一致；极近斜视时会投影成巨大条块。

## 5. 修复

### 5.1 统一极近判定

新增 `PortalNearViewRenderPolicy`。只有以下条件同时满足才进入修复：

```text
开关启用
&& near-plane aperture/proxy 确实参与
&& eyeDepth 与 zNear 都有效
&& abs(eyeDepth) <= zNear + 0.01
```

普通距离 portal 不受影响。

### 5.2 在首个 viewmodel 前清 depth

主视图每帧记录 portal 与 viewmodel 的绘制 ordinal。近门 proxy 参与后挂起一次 depth clear，并在该帧首个 viewmodel `DrawModelExecute` 前执行：

```cpp
ClearBuffers(false, true, false);
```

同一帧只执行一次，递归 portal view 不参与。

### 5.3 抑制极近区域的遗留整模型门框重绘

在同一个近裁剪冲突区，不再执行 stencil 外的 legacy whole-model frame redraw。普通距离仍保留原门框路径。

这首先是对巨大条块根因的隔离和修复。极近范围内门框可能暂时不显示；若未来要求极近门框也连续，需要实现与 aperture 一致的程序化轮廓，不能恢复已经证伪的整模型重绘。

### 5.4 单因子 A/B

保留三个显式诊断开关：

```text
portal_visual_stencilproxy_guard <0|1>
portal_visual_viewmodel_depthclear <0|1>
portal_visual_nearframe_guard <0|1>
```

正常基线为 `0 / 1 / 1`。测试时一次只改变一个因子。

## 6. 自动化验证

新增或扩展测试覆盖：

- `PortalVisualContinuityTrackerTests`
  - transaction 生命周期；
  - handoff 的 side/epoch/command/时效绑定；
  - prediction 与 main-view 去重；
  - 空差值仍作为可比观测；
  - reset 与 supersede。
- `PortalNearViewRenderPolicyTests`
  - 两个真实异常日志中的 `eyeDepth/zNear`；
  - 普通距离不触发；
  - aperture 未参与不触发；
  - 两个 A/B 开关可独立关闭。
- `PortalRenderFixGeometryTests`
  - 默认极近路径继续生成 proxy；
  - 旧 guard 仅在显式 A/B 时拒绝。
- `PortalRenderFixMeshWriterTests`
  - dynamic mesh 索引 ABI 保持正确。
- `PortalFileLogPolicyTests`
  - transaction 诊断进入文件日志且不污染控制台。

Debug x86 solution 构建结果为 0 warning、0 error。

## 7. 第三轮实机与日志验收

日志位置不纳入 Git：

```text
TestLog/穿越状态连续性修复/3/portal_l4d2_traversal.log
```

日志共 3328 行。定向统计结果：

- `PortalCrossingTransaction`：18 次。
  - 蓝→橙：10 次，其中 projected 9 次、live 1 次。
  - 橙→蓝：8 次，其中 projected 7 次、live 1 次。
- `PortalTeleportCommit`：18 次。
- `PortalVisualTxn stage=begin`：18 次。
- `PortalVisualTxn stage=summary`：18 次。
- transaction aborted：0 次。
- 18/18 `exactError=0.000`。
- 所有记录的 `clearancePush=0.00`。
- `movementMutation=true`、`legacyCollisionBypass=true`、`controlledNoclip=true`、`toolsNoClip=true` 均为 0 次。
- `PortalMaskRepair` 为 0 次。
- `ExtremeNearFrontClipGuard` 与 `guardSuppressed=true` 均为 0 次，证明 proxy 没有被旧 guard 隐藏。
- `[PortalRenderFix][ViewmodelDepth]`：46 次，分布在 46 个不同帧。
- 46/46 都满足 portal ordinal 小于 viewmodel ordinal。
- 同帧重复 depth clear：0 次。
- 顺序倒置：0 次。
- `[PortalRenderFix][NearFrame]`：8 次；该日志有 0.25 秒限频，不能直接与逐帧 depth clear 数量比较。
- 未发现真实 error、fatal、crash、断言失败或非有限数值。
- 退出时两个 brush 都恢复成功，恢复后 `destructiveWrites=false`。

人工观察没有再出现：

- viewmodel 消失；
- 巨大竖条；
- 局部门体或单侧门框覆盖；
- 由本问题引发的其他异常画面。

因此视觉结果与日志执行路径互相印证。

## 8. 与残余顿挫的边界

本轮 transaction 日志仍记录到 Teleport commit 与首个 main-view sample 之间的眼位差：

- 多数 transaction 的最大眼位差约为 `1.78` 至 `7.85` units。
- transaction 18 的最大眼位差为 `22.0781` units。
- 最大差值通常出现在 `teleport-commit → main-view-pre-render`。

这些数据不否定本次极近渲染异常修复，而是为后续独立的顿挫研究提供了基线。下一阶段应继续对照 Portal SDK 的 `PlayerPortalled`、`DetectAndHandlePortalTeleportation`、`UpdatePortalEyeInterpolation` 和 `CalcPortalView`，不要把插值历史修复重新混入本问题。

## 9. 最终结论

在当前单人 local server、BSP whole-brush、主视图 RTT/stencil 架构范围内：

- viewmodel 消失由缺少极近 portal 的 viewmodel 前 depth clear 引起；
- 门体巨大条块由 stencil 外的 legacy whole-model frame redraw 引起；
- 保留官方式 near-plane proxy、补充有界 depth clear、并在冲突区抑制旧整模型门框，能够关闭两个异常；
- 第三轮 18 次双向穿越的视觉观察与日志都支持该结论；
- 残余顿挫仍是独立未完成课题。
