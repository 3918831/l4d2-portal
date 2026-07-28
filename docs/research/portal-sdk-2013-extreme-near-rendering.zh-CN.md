# Portal SDK 2013 极近门面渲染研究

## 结论摘要

本轮两组画面不是两个互不相关的物理问题，而更像同一套不完整渲染状态机的两种结果：

1. 当前工程把 Portal 的 **stencil near-plane render-fix mesh** 接到了 **RTT 全屏纹理合成** 上；Portal SDK 的 stencil 模式并不这样做。官方路径先写 stencil、在 stencil 内清深度、直接把远端场景渲染到 back buffer，随后恢复雾和深度。当前工程缺失 stencil 内清深度与 post-stencil 深度恢复，所以 render-fix 在 `z=0.00001` 写下的极近深度会继续影响之后的第一人称 viewmodel。
2. 这能解释第一组“viewmodel 消失”：日志显示 viewmodel 逻辑状态一直为可绘制，且 draw ordinal 位于 portal 之后，因此它并非被逻辑关掉，而是更符合被前面遗留的极近深度拒绝。
3. 第二组“viewmodel 仍在，但门或承载面局部出现”则符合另一个分支：错误 stencil/深度覆盖没有挡住 viewmodel 所在区域，但 RTT aperture、普通 portal model/frame 和承载面深度三者的轮廓没有统一，于是极近斜视时露出竖直条块。
4. 新增的 `eyeDepth <= 0.3` guard 不属于 Portal SDK 的做法，而且日志证明异常大量发生在 `eyeDepth=1.1～1.95`。官方反而把默认 `0.3` front clip **钳制到相机深度**，继续画 render-fix；它依赖后续完整的 stencil/depth 生命周期保证正确。
5. 第一修复方向不应继续调 `0.3` 阈值。需要先恢复 Portal SDK 的合成不变量：render-fix、portal content、depth clear/restore、fog、viewmodel 必须处于同一个明确的渲染阶段和状态作用域。

## 资料可信度说明

第一方事实源使用 Valve 官方 [`ValveSoftware/source-sdk-2013`](https://github.com/ValveSoftware/source-sdk-2013)。虽然该仓库不发布 Portal 游戏专属类，但公共 `viewrender.cpp` 保留了 Valve 自己的 `#ifdef PORTAL` 集成代码，能够直接确认 portal stencil 所在阶段、viewmodel 顺序以及极近 portal 时的深度处理。

Portal 专属类来自仓库内 `Reference Code/portal-sdk-2013`，其远端为社区移植仓库 [`klaymen1n/portal-sdk-2013`](https://github.com/klaymen1n/portal-sdk-2013)，当前固定提交为 `52806f1b0d34733467e1da392184409038e10bb6`。该仓库 README 声称它将原 Portal 代码移植到 SDK 2013；源文件带 Valve copyright，但它不是 Valve 官方 GitHub 仓库。因此本文把它称为“Portal SDK 2013 参考实现”，并用 Valve 官方 `viewrender.cpp` 交叉验证能验证的集成点。

## 1. Portal stencil 的官方阶段

Valve 官方 `viewrender.cpp` 明确把 `DrawPortalsUsingStencils()` 放在 opaque 之后、translucent 之前，而不是放进任意 portal model 的 `DrawModelExecute` 回调中：

- [`CRendering3dView::DrawTranslucentRenderables`, Valve 官方，4110–4119](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/viewrender.cpp#L4110-L4119)
- 调用返回后会重新建立 visibility 和 draw lists：[4120–4134](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/viewrender.cpp#L4120-L4134)

Portal 专属实现的完整 stencil 生命周期是：

1. `DrawPreStencilMask()`。
2. `DrawStencilMask()`，stencil 由父层值递增。
3. `ClearBuffersObeyStencil(false, true)`，只在新 portal stencil 区域清深度。
4. `RenderPortalViewToBackBuffer()`，直接把链接门另一侧场景画到当前 back buffer。
5. 恢复 `CViewSetup`、fog 和完整 stencil state。
6. `DrawPostStencilFixes()`，再次修正 depth 和 fog。
7. 全屏 stencil decrement 回到父层，并最终关闭 stencil。

精确源码：

- [`CPortalRender::DrawPortalsUsingStencils`, 453–730](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/PortalRender.cpp#L453-L730)
- stencil mask 与首次清深度：[599–628](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/PortalRender.cpp#L599-L628)
- 远端场景和状态恢复：[631–669](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/PortalRender.cpp#L631-L669)
- post-stencil fixes 与 stencil 回退：[671–704](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/PortalRender.cpp#L671-L704)

这条顺序很重要：near-plane proxy 不是一个可以孤立追加的“遮罩补丁”，它只是上述 stencil/depth 事务的一部分。

## 2. 官方 render-fix mesh 实际做法

Portal SDK 对 near-plane 缺口确实使用生成式屏幕空间多边形，当前工程最初模仿的几何思路基本正确：

- 只在主视图 recursion level 0 使用。
- 相机离 portal 中心小于 `PORTAL_HALF_HEIGHT` 才启用。
- 使用 12 个、按 `1.1 ×` 门孔尺寸生成的侧边界平面。
- 默认 front clip 为 `0.3`。
- 若 `0.3 > cameraDepth`，把 front clip 缩小到 `cameraDepth`；不会因为 `cameraDepth <= 0.3` 而直接停画。
- 在 `zNear + 0.05` 处生成 `±40` 的相机近面 quad。
- 依次裁剪 portal 边界和当前 view frustum。
- CPU 投影后强制 `NDC z = 0.00001`，以 identity model/view/projection 绘制。

精确源码：

- 12 个 aperture plane 和 front/back plane：[portalrenderable_flatbasic.cpp 156–171](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/portalrenderable_flatbasic.cpp#L156-L171)
- `DrawRenderFixMesh()` 的 main-view、距离和 front clip 规则：[904–941](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/portalrenderable_flatbasic.cpp#L904-L941)
- near-plane quad：[943–976](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/portalrenderable_flatbasic.cpp#L943-L976)
- aperture/frustum 裁剪：[978–1018](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/portalrenderable_flatbasic.cpp#L978-L1018)
- `NDC z=0.00001` 与 identity matrices：[1020–1077](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/portalrenderable_flatbasic.cpp#L1020-L1077)

### 当前 guard 为什么没有修复

当前工程在 [`PortalRenderFixRenderer.cpp`](../../src/Portal/PortalRenderFixRenderer.cpp) 中新增：

```text
abs(eyeDepth) <= frontClipDistance + epsilon => suppress proxy
```

这与官方“把 front clip 钳制为 camera depth 后继续画”相反。第二轮日志也直接否定了单独的 `0.3` 阈值：

- 普通日志在 `eyeDepth=1.1333` 时，proxy `ndcArea=3.9919`，几乎全屏。
- `eyeDepth=0.4127`、`0.3955`、`0.4990` 时，proxy 已是完整 `ndcArea=4.0000`。
- 异常 2 日志在 `eyeDepth=1.9500` 时长期保持 `ndcArea≈3.62`；guard 完全不会触发。
- guard 在 `0.2946` 抑制一帧后，`0.3130` 立刻恢复成完整全屏 proxy，产生明显的离散边界。

因此“抑制亚 0.3 单位 proxy”既没有覆盖真实异常区间，还在 `0.3` 两侧引入了 stencil footprint 突变。

## 3. 当前工程混用了官方两种模式

Portal SDK 的两种模式是成套设计，不能取一半拼接：

### Stencil/back-buffer 模式

`DrawStencilMask()` 同时画普通 portal mesh 和 render-fix mesh；随后 stencil 内清深度，并直接调用 `ViewDrawScene_PortalStencil()` 把远端世界画进洞中：

- [`DrawStencilMask`, 669–673](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/portalrenderable_flatbasic.cpp#L669-L673)
- [`RenderPortalViewToBackBuffer`, 420–521](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/portalrenderable_flatbasic.cpp#L420-L521)

### Texture 模式

官方 texture 模式不会用 generic full-screen quad 填充 render-fix stencil，而是用 portal projection mesh 和专用 render-fix material 直接绘制：

- [`DrawPortal` texture 分支，1206–1228](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/portalrenderable_flatbasic.cpp#L1206-L1228)

### 当前工程

当前 [`ModelRender.cpp`](../../src/Hooks/ModelRender/ModelRender.cpp) 的主路径是：

1. portal model 写 stencil；
2. 追加官方形状的 near-plane proxy；
3. 对 stencil 匹配区调用 `DrawScreenSpaceQuad(dynamicPortalMaterial)`；
4. 关闭 stencil；
5. 把同一个 portal model 前移 `0.1`，换 border sprite material 再画成 frame。

也就是说，当前实现把“官方 stencil proxy”接到了“自定义 RTT 全屏合成”上，但没有移植官方 stencil 内深度清理、远端场景 back-buffer draw、post-fix depth/fog、stencil parent restore。这是当前极近异常最关键的结构差异。

## 4. 为什么 viewmodel 会消失

日志已经排除“viewmodel 被逻辑关闭”：

- `drawFlag=true`
- `localBefore/localAfter=true`
- `drawViewmodelBefore/drawViewmodelAfter=true`
- `thirdPersonBefore/After=false`
- draw order 一般是 `portal ordinal=1`，之后 `viewmodel ordinal=2/3`

所以第一组消失更符合深度拒绝，而不是 `ShouldDrawViewModel()` 返回 false。

官方实现中，portal/world 场景完整结束后才绘制主 viewmodel：

- 主场景、portal、post effects 后才 `DrawViewModels()`：[Valve 官方 `RenderView`, 1959–2000](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/viewrender.cpp#L1959-L2000)
- viewmodel 使用独立的 `zNearViewmodel/zFarViewmodel/fovViewmodel`：[969–1004](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/viewrender.cpp#L969-L1004)
- 如果本地玩家极接近 portal，Valve 明确禁用普通 viewmodel depth-range hack，并先完整清 depth：[1005–1023](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/viewrender.cpp#L1005-L1023)

这段 Valve 官方注释非常直接：普通 depth-range hack 对 Portal 不够可靠，改这些范围还会导致某些模型错误，所以极近 portal 时要清 depth。

当前 proxy 被强制到 `NDC z=0.00001`，但当前路径没有官方 stencil 内清深度和 post-stencil 深度恢复，也没有在 viewmodel pass 前执行 Portal 的 close-range depth clear。于是大 footprint proxy 留下的极近深度可以拒绝之后的 viewmodel。不同 yaw 下 proxy footprint 改变，因此会出现：

- proxy 覆盖枪械区域：viewmodel 全消失或被切掉；
- proxy 没覆盖枪械区域：viewmodel 保留，但 portal/frame/承载面条块仍出现。

## 5. 眼睛跨过门平面时官方如何组合主视图

Portal SDK 不等待整个玩家碰撞体完成物理传送才改变画面。`CalcPortalView()` 会：

1. 计算眼睛相对当前 portal plane 的距离；
2. 眼睛已在门后、且玩家仍在 portal hole 内时，先把 `eyeOrigin/eyeAngles` 变换到链接门；
3. 设置 `m_bEyePositionIsTransformedByPortal`；
4. 临时禁止远端 portal surface 中绘制本地玩家，避免身体/表面伪影。

源码：

- [`C_Portal_Player::CalcPortalView`, 1511–1613](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_portal_player.cpp#L1511-L1613)
- 眼睛在 plane 后且仍位于 hole 的判定：[1538–1579](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_portal_player.cpp#L1538-L1579)
- 远端 surface 中的本地玩家抑制：[1602–1613](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_portal_player.cpp#L1602-L1613)

远端视图还使用链接门平面作为 custom clip plane，并向门后偏移 `0.5`，专门减少半穿物体的视觉伪影：

- [`RenderPortalViewToBackBuffer`, custom clip plane，469–477](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/portalrenderable_flatbasic.cpp#L469-L477)

因此官方的“极近连续性”不是仅靠扩大 stencil aperture，而是由四个同步部分组成：

- 主眼点在 plane 后立即变换；
- aperture/stencil 跟随 near plane；
- 远端世界按 exit plane 裁剪；
- 本地/远端人物副本按当前眼点侧选择。

## 6. 人物、第三人称副本和 viewmodel

Portal SDK 对穿门实体使用真实实体 clip plane 加链接侧 ghost renderable：

- 先清理上帧 clip plane，然后查找门附近可移动实体：[c_prop_portal.cpp 253–289](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_prop_portal.cpp#L253-L289)
- 原实体在入口侧按 portal plane 裁剪：[292–377](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_prop_portal.cpp#L292-L377)
- 链接侧 ghost 按 portal transform 复制，并使用另一条 clip plane：[380–420](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_prop_portal.cpp#L380-L420)
- 本地玩家 ghost 根据 `m_bEyePositionIsTransformedByPortal` 和 recursion level 互斥显示：[C_PortalGhostRenderable.cpp 238–264](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/C_PortalGhostRenderable.cpp#L238-L264)

第一人称 viewmodel 被明确排除在 ghosting 之外：

- [`if (pEntity == pLocalPlayerViewModel) continue`, 283–328](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_prop_portal.cpp#L283-L328)

这意味着 portal RTT/递归视图中的第三人称人物和主视图的第一人称 viewmodel 是两条不同管线。当前截图在 portal 内容中看到 Nick 第三人称模型，本身不证明 third-person camera 被打开；它更可能是递归/RTT 世界视图画出的 local player world model。当前工程没有 Portal SDK 的 clip-plane ghost 和“眼点已变换”互斥规则，因此该模型可以完整出现在错误扩张的 portal footprint 中。

## 7. “承载面或门局部出现”的机制判断

官方路径通过以下三重约束避免 portal 的 carrying surface 回到洞中：

1. 先以 aperture mesh + render-fix 写出统一 stencil；
2. 在 stencil 内清掉承载墙已有深度；
3. 远端场景使用 exit portal custom clip plane 绘制；
4. 绘制结束后，再在同一 stencil 中恢复 fog 和正确 portal depth。

`DrawPostStencilFixes()` 的源码明确说明它在修复 fast clipping 破坏的 depth，补回 fog，然后分别重画普通 portal mesh 和 render-fix mesh 的 depth：

- [`DrawPostStencilFixes`, 675–688](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/portalrenderable_flatbasic.cpp#L675-L688)

当前工程缺少这套 post-stencil fixes，并把 frame 作为 stencil 关闭后独立重画的 portal model。于是极近斜视时存在三种不一致轮廓：

- near-plane proxy 的屏幕轮廓；
- RTT portal-content 的有效投影；
- 原 portal model/frame 与承载墙的深度轮廓。

截图中的竖直硬边、棕黑条块、发光边框只沿条块一侧出现，正符合这些轮廓在近门视角下分离。它不是新的 BSP 穿越失败证据；从渲染结构看，优先级仍应放在 stencil/depth/frame 合成事务。

## 8. 对当前实现的差异清单

| 不变量 | Portal SDK 参考实现 | 当前工程 |
|---|---|---|
| portal 绘制阶段 | opaque 后、translucent 前，由 portal manager 统一执行 | 在 portal model 的 `DrawModelExecute` 中即时执行 |
| stencil 内容 | 普通 aperture mesh + render-fix mesh | portal model + render-fix mesh |
| stencil 后深度 | `ClearBuffersObeyStencil(false, true)` | 未执行 |
| 远端内容 | 直接向 stencil back buffer 渲染 scene | RTT + generic full-screen quad |
| exit 裁剪 | linked portal custom clip plane，后移 `0.5` | RTT 自有裁剪与 stencil 事务分离 |
| post fix | 再清 stencil depth、补 fog、恢复 simple/fix mesh depth | 未执行 |
| stencil 退出 | parent reference decrement，完整恢复 | 每个 model 局部开关，未形成递归 parent 生命周期 |
| viewmodel | 世界/portal 结束后独立 pass；极近时先 clear depth | 依赖 L4D2 原 pass，但前面的 proxy depth 未按 Portal 规则清理 |
| 穿门人物 | 原实体 clip + 链接侧 ghost；本地人物按 eye side 互斥 | RTT 中可能直接绘制完整 local-player world model |
| portal frame | portal renderer 生命周期末尾绘制 portal surface/material | stencil 关闭后把原 model 前移 `0.1` 并换 border sprite material |
| `0.3` front clip | 钳制到 camera depth，继续绘制 | `abs(eyeDepth)<=0.3` 时停画 |

## 9. `missing handoff` 与官方眼位状态机

当前日志里 transaction 与 `PortalEntryViewHandoff` 并非一一匹配。Portal SDK 的源码说明，不能把“是否采样到 entry handoff”当成官方视觉状态迁移是否发生的唯一证据，因为官方有两条独立但衔接的路径：

### 9.1 眼睛先过 plane、身体尚未物理传送

这是前文的 `CalcPortalView()`：只要眼睛在 portal plane 后且玩家仍位于 hole，当前帧 main eye 立即变换到链接侧。它是逐帧几何判定，不是必须发送一次的 transaction event。

### 9.2 服务器/预测实体完成物理传送

`PlayerPortalled()` 不直接修改 camera，而是保存本次 portal matrix 并设置 pending flag；本地玩家同时通知 `CPortalRender::EnteredPortal()`：

- [`PlayerPortalled`, 1026–1036](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_portal_player.cpp#L1026-L1036)

随后 `DetectAndHandlePortalTeleportation()` 消费 pending flag，并且同时变换：

- `m_vEyePosition_Interpolated`
- `m_vEyePosition_Uninterpolated`
- 上帧 eye angle
- viewmodel 的 `m_vecLastFacing`

源码：

- [`DetectAndHandlePortalTeleportation`, 1097–1147](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_portal_player.cpp#L1097-L1147)

`CalcView()` 的调用顺序也很明确：先消费 teleport pending，再更新 portal eye interpolation，最后才计算/变换当前 portal view：

- [`CalcView`, 1395–1408](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_portal_player.cpp#L1395-L1408)

启用的 `UpdatePortalEyeInterpolation()` 会保留真实未插值眼位的每帧自由移动增量，同时让插值眼位以 `frametime × 100` 追赶，并把最大落后限制在 150 units；不插值时则把两份历史都初始化为 `EyeFootPosition()`：

- 文件顶部启用宏：[`c_portal_player.cpp 39`](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_portal_player.cpp#L39)
- [`UpdatePortalEyeInterpolation`, 1299–1374](https://github.com/klaymen1n/portal-sdk-2013/blob/52806f1b0d34733467e1da392184409038e10bb6/src/game/client/portal/c_portal_player.cpp#L1299-L1374)

因此，本日志的 `missing handoff` 最合理的解释是：当前 `PortalEntryViewHandoff` 只绑定“某个渲染采样里看见 eye depth 越过 plane”的瞬态事件；高速、低 tick 或 commit 与 render sampling 顺序不同的穿越，可以在没有留下该采样的情况下完成 teleport。官方不依赖这个瞬态事件维持 teleport 后连续性，而是由 `PlayerPortalled` 的 pending matrix 在物理 teleport 边界无条件变换两份 eye history。

这条解释只说明日志语义：`missing handoff` 不能证明 camera transform 没发生，也不能用来解释本轮静止极贴门的竖直渲染条块。后者仍由 stencil/depth 生命周期独立解释。若后续继续诊断顿挫，应把“逐帧 eye-behind-plane 变换”和“teleport transaction 消费 pending matrix”作为两个不同观测点，不应继续只用 entry handoff 命中率代替二者。

## 10. 下一轮应验证的单因子

本文不直接修改代码。根据官方差异，下一轮最有判别力的顺序是：

1. 撤回“`0.3` 内停画 proxy”作为修复假设；保留日志即可。
2. 首个渲染 A/B 只验证 **proxy depth 是否导致 viewmodel 消失**：在不改变 stencil footprint 和 portal content 的前提下，使 viewmodel pass 前的深度状态对齐 Valve 官方极近路径。若 viewmodel 恢复而竖直 portal 条块仍在，两个现象的共享根因与剩余分支就被分离。
3. 第二个 A/B 再验证 **stencil 内清深度 + content 后恢复 depth**。它决定 carrying surface/门条块是否由缺失 stencil depth transaction 造成。
4. 只有上述深度事务正确后，再评估 frame 的独立 model/material 路径；否则 frame 轮廓会与错误 aperture 一起变化，难以归因。
5. 每个 A/B 应继续记录：proxy NDC bounds/area、portal 与 viewmodel ordinal、viewmodel draw state、清深度前后阶段、portal content draw 和 frame draw 的 stencil/depth 状态。

这条顺序仅处理残余视觉连续性，不涉及物理穿越或移动链路。
