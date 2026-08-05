# L4D2 Portal BSP 物理穿越与连续性技术总结

日期：2026-08-06

分支：`codex/bsp-portal-traversal`

阶段定位：单人、local listen server、世界 BSP brush 上的玩家物理穿越重大基线。

## 1. 文档目的

本工程在本阶段开始前已经能够放置并渲染一对具有实时远端画面的传送门，但“看起来像门”不等于“玩家真的能从墙体穿过”。本阶段从已有视觉效果出发，逐层补齐：

1. 世界 BSP 承载面的真实碰撞开口；
2. 玩家跨越入口平面的判定与 server-side `Teleport`；
3. 位置、角度和速度在客户端预测与服务端命令空间中的变换；
4. 极近传送门时的墙体、门框、viewmodel 和 PVS 连续性；
5. 高速空中与地面行走两条不同 movement 路径的视觉和速度连续性；
6. 可回滚、可诊断、可重复实测的基线。

本文既记录最终实现，也记录关键探索、被证伪路径和后续限制，避免未来重新走回旧 noclip、movement mutation、出口 push 或 `PortalMaskRepair`。

## 2. 当前结论

当前实现已经在支持范围内形成如下完整链路：

```text
已有 Portal RTT / stencil 视觉
→ 放置时解析承载 BSP brush
→ 事务性清空蓝门和橙门的 brush contents
→ 原始 client/server movement 获得真实可通过空间
→ PortalTransitionSimulator 观察玩家跨越入口平面
→ PortalTransform 变换 origin / velocity / angles
→ local server entity Teleport 提交
→ Server PlayerRunCommand 一次性变换命令角与输入空间
→ 客户端相机、眼位历史与 shadow predictor 接续
→ 地面使用 observed live crossing，空中保留 projected crossing
→ 出口空间锁与冷却防止立即反复传送
→ 门关闭、换图或卸载时恢复 brush 原值
```

第 53 轮基线实测：

- 16 次成功穿越，其中 12 次地面、4 次空中；
- 首次预热未做主观标记，其余 15 次均标记为 `perceived_smooth`；
- 12 次地面穿越全部使用 `path=live`；
- 4 次空中穿越继续使用既有路径，没有被地面修复改写；
- `runtimeSafetyFallback`、`stateSafetyFallback`、`startsolid`、`allsolid`、越界和玩家实体陷入均为 0；
- 没有黑天空、条状墙体、门后墙体、门体巨型条块、viewmodel 消失或高速闪屏；
- brush 恢复路径保持有效。

这是一项阶段性重大基线，不等同于完整复刻 Portal 游戏，也不解决本文第 12 节列出的后续问题。

## 3. 支持范围与明确边界

当前声明支持：

- 单人或 local listen server；
- 本地幸存者玩家；
- 世界 BSP brush 承载面；
- 蓝门、橙门之间的双向穿越；
- 地面行走和空中高速穿越；
- origin、velocity、view angles 与 command angles 的门对变换；
- 传送门生命周期内 brush 修改和恢复。

当前不声明支持：

- 远程服务器、多玩家所有权和网络同步；
- NPC、感染者、投掷物、子弹和任意物理实体的完整 portal 行为；
- 玩家 hull 旋转、身体切割、portal ghost 或完整 Portal environment；
- 只在椭圆孔径内修改 BSP 碰撞；
- 地板/天花板重力循环的长期稳定性；
- 首次穿越的资源预热卡顿。

## 4. 总体物理方案：修改 BSP，而不是修改玩家移动

### 4.1 为什么选择 brush

早期方案曾尝试在 movement trace、`WalkMove`、`StayOnGround` 或玩家位置上制造例外。这些方法能够证明某个局部阻塞点，却会把穿越能力绑定到：

- 特定 movement 调用顺序；
- 持续 origin/velocity mutation；
- controlled noclip；
- assisted embedding 或小幅 push；
- client/server 两套不同的补丁。

最终方案改为修改世界碰撞真相本身：把承载传送门的 BSP brush `contents` 事务性写为 `CONTENTS_EMPTY`。这样原始 client/server movement、hull trace 和地面逻辑看到的是同一个真实开口，穿墙不再依赖某条 trace 被单独篡改。

### 4.2 Windows x86 BSP 数据定位

工程从 L4D2 的 `EngineTrace` 运行态入口反向定位 `CCollisionBSPData`，并验证最小结构布局：

- `CNode = 12`
- `CPlane = 20`
- `CLeaf = 16`
- `CBrush = 8`
- `CBrushSide = 8`
- `CBoxBrush = 48`

查询链路为：

```text
portal placement hit position / plane normal
→ 向承载面内部采样
→ BSP node
→ leaf
→ leafbrush
→ brush
→ 保存 brush index、原始 contents 与 map generation
```

纯查询逻辑与实时引擎地址分离，因此 node、leaf、convex brush、box brush、损坏数据和越界条件均可使用合成数据测试。

### 4.3 可恢复事务

`CPortalBspCollisionCarver` 把两扇门的修改作为同一事务：

- 首次写入前保存原始值；
- 所有目标先验证、后修改；
- 第二个目标失败时回滚第一个；
- 同 brush 门对只写一次，以 owner mask 保存所有权；
- 恢复前比较当前值，避免覆盖未知外部修改；
- map generation 不匹配时拒绝解引用旧地址；
- 关门、换图、卸载和显式 restore 均进入幂等恢复路径。

## 5. 最底层 Teleport 与官方路径

本工程没有用自定义 origin 写入替代官方物理提交。最终传送仍由 local-server 玩家实体的 `Teleport` 完成：

```text
entry origin / velocity / angles
→ PortalTransform 门对矩阵
→ transformed origin / velocity / angles
→ server entity Teleport
```

这与 Portal SDK server `prop_portal` 最底层仍调用实体 `Teleport` 的原则一致。差异主要不在 Teleport API，而在 Portal 官方客户端围绕 Teleport 维护的相机、插值历史、命令角和 portal environment。本阶段的后续工作因此集中在这些交接层，而没有替换最底层传送函数。

`ExactTransform` 是当前物理基线：完成门对变换后不再沿出口法线追加 `FullHull`、`PlaneEpsilon` 或其他 push。防止立即反复传送改由出口空间锁和解锁后的时间冷却承担。

## 6. 从“能穿”到“看起来连续”

### 6.1 先建立 transaction 级证据

视觉顿挫无法只靠单帧截图定位。本阶段为每次穿越建立 transaction id，并将以下信息对齐：

- transition phase、epoch、usercmd、frame 和调用序号；
- entry/exit side；
- client/server origin、velocity、angles；
- projected crossing 与 observed crossing；
- Teleport commit；
- `PlayerRunCommand` 前后命令角与输入；
- `CalcView`、raw eye、history eye、shadow eye；
- render cadence、主观 F8/F9/F10 标记；
- leaf、world contents、player-solid 和越界安全结果。

诊断窗口最终压缩为 `commit-plus-1`，保留因果证据而避免日志无限膨胀。

### 6.2 官方客户端不变量

方法论主要来自 Portal SDK 2013：

- `C_Portal_Player::PlayerPortalled`
- `UpdatePortalEyeInterpolation`
- `CalcPortalView`
- entity `Teleport` 与 interpolation history 的关系

工程没有盲目复制 Portal 专属类，而是提取 L4D2 可验证的不变量：

- 物理 Teleport 前可能需要入口相机交接；
- 眼位历史必须在同一门对空间中比较；
- 客户端命令角和服务端命令角必须处于一致空间；
- render-time 眼位可以补偿，但不能修改玩家物理位置；
- 任何候选眼位进入 solid、非法 leaf 或地图外必须回退；
- 地面与空中的 movement crossing 时机不能假设相同。

## 7. 主要视觉异常及解决过程

### 7.1 黑天空

现象表现为天空变黑或场景像从非法 leaf/PVS 观察。它曾在眼位历史实验中回归，说明“数学上连续”的候选相机仍可能位于墙体、实体或地图外。

最终处理原则：

- 精确物理 Teleport 点不为视觉补丁让路；
- camera origin 与 PVS/visibility sampling origin 分工；
- 每个 history/shadow 候选执行 leaf、world contents、player-solid、outside-world 与 trace 安全检查；
- 候选不安全时回退到引擎原始视图；
- 不用出口 push 把玩家实体挪到视觉安全点。

第 53 轮所有安全回退和非法空间计数均为 0，黑天空没有回归。

### 7.2 门后墙体、条状墙体和高速闪屏

相机越过入口平面、物理命令尚未提交 Teleport 时，旧实现仍会从入口侧绘制，形成门后墙体或高速闪帧。

修复分为三层：

1. 入口相机 handoff：满足相位、入口深度和孔径条件时，把 render eye 通过门对矩阵变换到出口空间；
2. 远端 RTT 相机使用精确门对变换，不额外外推；
3. clip plane 对齐官方 `dot(normal, exitOrigin - normal * 0.5)`。

这关闭了“物理提交晚于渲染观察点”的显著错误窗口。

### 7.3 near-plane mask、viewmodel 消失和门体巨型条块

极近站位曾出现两组不同现象：

- viewmodel、手臂和武器被裁掉，门框也可能消失；
- viewmodel 仍存在，但门体或边框投影成巨大竖条和局部覆盖。

诊断证明 viewmodel 逻辑仍允许绘制，根因位于渲染阶段和深度状态：

- Portal SDK 风格 near-plane proxy 写入了极近深度；
- 当前 RTT/stencil 管线缺少官方完整的 stencil 内清深度和恢复链；
- 首个 viewmodel pass 会被遗留深度拒绝；
- stencil 外 legacy whole-model frame redraw 在极近斜视时形成巨大条块。

最终修复：

- 保留官方式 near-plane render-fix proxy；
- 在近门冲突帧的首个 viewmodel 前执行有界 depth clear；
- 冲突区抑制 legacy whole-model frame redraw；
- 普通距离继续保留既有门框路径；
- 修正 dynamic mesh `IndexDesc_t` / `CIndexBuilder` 语义，按 16 位元素步进并加上 `firstVertex`。

该问题在第 3 轮后关闭，后续测试没有重新出现。

## 8. 速度与命令空间连续性

### 8.1 为什么只变换实体 velocity 仍不够

server `Teleport` 可以正确变换实体 velocity，但下一条 `PlayerRunCommand` 仍可能携带入口空间的 view angles、forward move 和 side move。速度大小看似保留，movement 求解器却会按旧方向重新解释输入，表现为速度表瞬时掉速或方向粘滞。

工程运行态验证并接入 server `PlayerRunCommand`，在正确 transaction/command 上执行一次性官方式命令角变换，同时保持：

- 只消费一次；
- 非目标命令不修改；
- command、portal matrix 和 transaction 必须匹配；
- 不启用持续 movement mutation。

`portal_server_command_anglefix 1` 验证后成为默认基线。

### 8.2 证伪的 `SetGroundEntity` 探针

地面粘滞曾怀疑来自 ground entity 清除或重分类。运行态确认：

- `CTerrorGameMovement::SetGroundEntity(trace_t*)` 的候选 vtable slot 为 63；
- client/server player setter 与 movement wrapper 的调用顺序可被观察；
- 探针始终 call-through，`mutation=false`；
- 直接 ground-clear 假设没有形成稳定主观改善；
- 最终有效方案是改变地面 crossing/commit 时机，而不是主动调用 `SetGroundEntity(NULL)`。

因此重大基线删除固定 RVA、无条件安装的运行时探针，只保留本节研究结论和普通 transaction ground 状态。未来仅在斜坡、台阶、高低差出口、垂直门或 stale ground handle 有独立证据时，才通过签名扫描和独立 adapter 重新接入，不恢复固定 RVA。

## 9. 空中高速连续性

### 9.1 问题

高速跳跃进入传送门时，速度方向会瞬间改变。物理速度大小可以正确变换，但客户端原始 eye/history 的更新节奏落后于已变换速度，主观上表现为数帧粘滞。

### 9.2 Shadow predictor

最终有效因子是 render-eye-only shadow predictor：

- 以已提交的门对变换和当前 velocity 为输入；
- 只在一个 runtime tick 的有界 horizon 内外推；
- 不写玩家 origin、velocity 或 move type；
- raw eye 开始追上后进入 release tail；
- release tail 在收敛或 horizon 到期时结束；
- 所有候选经过空间安全检查。

第 40 轮首次显示高速空中视角明显连续；第 42 轮补齐 release tail 后，高速空中的视觉和速度连续性在正常速度及降低 `host_timescale` 的慢放观察下均无可感知断点。

空中路径此后冻结为回归基线。后续地面实验必须同时复测空中，且不得改变其 projected crossing 路径。

## 10. 地面行走连续性

### 10.1 近地面门底吸附

手工放门很难保证门底与地面无缝贴齐。少量高度差会让玩家 hull 在进门时先碰地面或台阶，使不同方向的测试数据看似不对称。

工程加入 placement-only floor snap：

- 参考 Portal SDK 2013 的 provisional half-height；
- 沿 portal up 方向计算门底；
- 探测附近地面；
- 在最大 30 units 范围内把门底匹配到地面；
- 只调整门的放置中心，不修改玩家 movement；
- 当前 portal 模型并非 Portal 原模型，因此 half-height 被明确标记为 provisional。

该功能消除了人工放置高低差的大部分干扰，但不是地面连续性的最终原因。

### 10.2 Projected crossing 与 observed live crossing

空中高速需要 projected crossing 兜底，地面却不同。地面 movement 会在同一 command 中经过 walk、step、ground categorization 和最终 origin 合成。如果提前按 projected candidate 提交，渲染/物理可能使用尚未成为最终 movement 结果的点，产生偶发粘滞。

最终采用分流：

```text
onGround == true  && portal_ground_livecommit == true
    → 不消费 projected candidate
    → 等待原始 movement 产生 observed live crossing
    → 在真实跨面时提交

onGround == false
    → 保留已验证的 projected crossing
```

这不是新的 origin push，也不跳过 `WalkMove`。它只改变 ground-only Teleport 的提交时机。保留 `portal_ground_livecommit 0` 作为 projected rollback control。

第 46 至 53 轮逐步确认：

- 近地面放置干扰被 floor snap 降低；
- observed live crossing 能在双向地面穿越中出现；
- 偶发丝滑逐渐变为稳定；
- 第 53 轮 12/12 地面事务均走 `path=live`，主观标记全部丝滑；
- 空中路径没有劣化。

## 11. 当前默认基线与回滚入口

关键状态：

```text
portal_bsp_phase1                      1
portal_bsp_clearance                  exact
portal_placement_floorsnap            1
portal_visual_nearclip                1
portal_visual_exitguard               0
portal_visual_entryhandoff            1
portal_visual_eyehistory              1
portal_visual_shadowpredictor         1
portal_visual_shadowtail              1
portal_visual_projectedhistoryrebase  1
portal_server_command_anglefix        1
portal_visual_transactiondiag         1
portal_ground_livecommit              1
```

必须保持：

```text
legacyCollisionBypass=false
movementMutation=false
controlledNoclip=false
clearanceMode=ExactTransform
teleportPredictionSync=true
predictionDiagnosticWindow=commit-plus-1
```

单因子回滚：

- `portal_ground_livecommit 0`：地面恢复 projected crossing control；
- `portal_visual_shadowtail 0`：关闭 predictor release tail；
- `portal_visual_shadowpredictor 0`：关闭 render-eye shadow predictor；
- `portal_placement_floorsnap 0`：关闭近地面放置吸附；
- `portal_bsp_restore`：恢复所有已修改 brush。

## 12. 下一步计划与已知问题

本节只记录现象和方向，不在本次基线中展开分析或实现。

### 12.1 Whole-brush 开口必须收敛为门框内孔径

当前 `CONTENTS_EMPTY` 会清空传送门所在的整个 BSP brush，而不是只清空椭圆门框内区域。直接后果包括：

- 玩家可能从孔径外穿过同一承载 brush；
- 如果在地面放置传送门，整个地面 brush 可能失去碰撞，玩家直接掉到地图外；
- 同 brush、薄墙和大面积地板的副作用尤其明显。

用户提出的下一步思路：

> 先清空整个原 brush，再增加新的碰撞 brush 覆盖孔径外区域，以“移除原 brush + 添加周边 brush”的方式间接形成门框内孔洞。

该方向当前仅作为技术路线记录，尚未分析新 brush 的形状分解、生命周期、client/server 一致性、导航或物理接口。

### 12.2 地板—天花板重力循环存在疑似“恢复期”

当前观察：

- 在地板和天花板放置两个对等传送门；
- 玩家受重力在两门之间反复穿越；
- 速度持续增大到一定程度后，可能从地板掉入地图外；
- 主观上像穿越后存在一段尚未恢复完毕的状态窗口。

本次不判断它来自：

- 出口空间锁或 rearm；
- crossing/commit 冷却；
- gravity 与 command timing；
- ground state；
- whole-brush 地板碰撞缺失；
- 高速下跨面漏采样或其他因素。

该问题必须在孔径化 brush 方案和现有高速空中基线均可观测的条件下独立诊断。

### 12.3 首次穿越一次性卡顿

首次穿越偶尔比后续事务明显卡顿。当前实测通常把第一次作为 warm-up 排除。后续应独立记录首次与第二次穿越的：

- portal render target、材质和模型是否首次创建；
- shader/stencil path 是否首次触发；
- hook/code page 和日志文件是否首次写入；
- transaction 本身是否出现异常长帧。

该现象与 `SetGroundEntity` 没有现有因果证据。

### 12.4 支持范围扩展

后续仍需独立评估：

- 两门位于同一 brush；
- 门替换、关闭、换图、卸载和强制失败矩阵；
- 多玩家、NPC、投掷物和其他实体；
- 非 BSP 承载面；
- 地板/天花板 portal 的 hull 朝向与长期重力循环；
- floor snap 使用当前 portal 模型真实边界替换 provisional half-height。

## 13. 被证伪或禁止恢复的路径

以下方案不得因为新问题出现而自动恢复：

- controlled noclip；
- 持续 movement mutation；
- movement trace wall bypass；
- assisted embedding；
- 出口 `FullHull` / `PlaneEpsilon` push；
- `PortalMaskRepair`；
- 固定 RVA 的 `SetGroundEntity` 运行时 Hook；
- 用真实玩家 origin 推进 solid brush 或非法 leaf；
- 把 dynamic mesh index size 当字节步长；
- 极近区域 legacy whole-model frame redraw。

如果未来确有新证据需要其中某个底层能力，必须重新建立独立问题、签名/ABI 验证、单因子 A/B 和明确回滚，不得直接把旧实现重新打开。

## 14. 可复用工程经验

1. **先让世界碰撞真相正确，再修玩家。** 修改 BSP 让原始 movement 消费真实开口，比在多个 movement 阶段维护例外更稳定。
2. **物理与渲染分层。** Teleport origin、PVS origin、render eye 和 interpolation history 有不同职责。
3. **地面与空中不能共用未经验证的 crossing 时机。** 空中 projected path 与地面 observed live path 是本阶段最重要的分流。
4. **速度连续不只是 velocity。** 下一条 usercmd 的角度和 forward/side 输入空间也必须随门对变换。
5. **主观体验必须绑定 transaction。** F8/F9/F10 标记只有与 cmd、frame、render cadence 和 safety 数据对齐才有诊断价值。
6. **候选视图必须有空间安全门。** 连续但位于 solid/非法 leaf 的相机比轻微顿挫更糟。
7. **失败实现及时删除。** 诊断 Hook、固定 RVA 和没有改善的 mutation 不作为“备用能力”长期挂在主路径。
8. **首次样本单独建模。** 预热卡顿不能混入稳定事务的连续性结论。
9. **恢复属于功能本身。** brush 能清空但不能安全恢复，不算完成。
10. **官方源码用于提取不变量。** L4D2 缺少 Portal 专属类时，应复刻可验证的时序与空间语义，而不是凭感觉发明补丁。

## 15. 相关技术资产

- `docs/portal-bsp-phase1-stage-retrospective.zh-CN.md`
- `docs/handoff/portal-visual-continuity-next-session.zh-CN.md`
- `docs/bug-report/portal-teleport-camera-handoff-discontinuity/bug-report.md`
- `docs/bug-report/portal-near-plane-mask-clipping/bug-report.md`
- `docs/bug-report/portal-extreme-near-viewmodel-and-frame-artifacts/bug-report.md`
- `docs/research/portal-sdk-2013-extreme-near-rendering.zh-CN.md`
- `docs/portal-sdk-2013-technical-report.md`
- `docs/portal-traversal-logic-study.md`
- `src/Portal/PortalBspData.*`
- `src/Portal/PortalBspCollisionCarver.*`
- `src/Portal/PortalTransitionSimulator.*`
- `src/Portal/PortalTransition.*`
- `src/Portal/PortalEyeContinuity.*`
- `src/Portal/PortalPlacementFloorSnap.*`
- `src/Hooks/ServerPlayer/ServerPlayerRunCommand.*`
