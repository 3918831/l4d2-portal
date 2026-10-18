# L4D2 Portal BSP 物理穿越与连续性技术总结

初始基线日期：2026-08-06；最新验收整理：2026-10-18。

方案与基线归属：`codex/bsp-portal-traversal`。`codex/portal-official-mechanism` 是另一套独立复刻方案，不是本 BSP 清空方案的交付分支。2026-10-18 对误提交/误推送的归属纠正见[累计审查](portal-fall-baseline-worktree-audit.zh-CN.md#5-分支归属纠正)。

阶段定位：单人、local listen server、世界 BSP brush 上的玩家物理穿越重大基线。

## 1. 文档目的

本工程在本阶段开始前已经能够 放置并渲染一对具有实时远端画面的传送门，但“看起来像门”不等于“玩家真的能从墙体穿过”。本阶段从已有视觉效果出发，逐层补齐：

1. 世界 BSP 承载面的真实碰撞开口；
2. 玩家跨越入口平面的判定与 server-side `Teleport`；
3. 位置、角度和速度在客户端预测与服务端命令空间中的变换；
4. 极近传送门时的墙体、门框、viewmodel 和 PVS 连续性；
5. 高速空中与地面行走两条不同 movement 路径的视觉和速度连续性；
6. 可回滚、可诊断、可重复实测的基线。

本文既记录最终实现，也记录关键探索、被证伪路径和后续限制，避免未来重新走回旧 noclip、movement mutation、出口 push 或 `PortalMaskRepair`。

面向其它复刻方案，优先阅读[连续性技术资产复用索引](portal-continuity-cross-scheme-reference.zh-CN.md)：它将官方语义、L4D2 接入适配、已验证结论与被证伪实验分开，避免把本长文中的历史实验当作当前推荐实现。

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
→ 出口空间锁与空间解锁判据防止错误反复传送（无固定时间冷却）
→ 门关闭、换图时恢复 brush 原值（DllMain有最小恢复，不代表支持安全热卸载）
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

`ExactTransform` 是当前物理基线：完成门对变换后不再沿出口法线追加 `FullHull`、`PlaneEpsilon` 或其他 push。防止错误反复传送由出口空间锁及空间解锁判据承担；后续高速循环修复已经移除固定时间冷却，解锁直接回Idle，不能继续把旧冷却描述当作当前机制。

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

本节记录早期墙门空中基线及后续 tail 实验，**不是所有实验仍生效的当前开关清单**。`portal_visual_convergenttail` 后续在第12轮被证伪并删除；单眼位历史实验默认关闭，见第12节时间线及[复用索引](portal-continuity-cross-scheme-reference.zh-CN.md)。Shadow predictor 是本工程适配，不应称为官方完整预测实现。

早期有效因子是 render-eye-only shadow predictor：

- 以已提交的门对变换和当前 velocity 为输入；
- 只在一个 runtime tick 的有界 horizon 内外推；
- 不写玩家 origin、velocity 或 move type；
- raw eye 开始追上后进入 release tail；
- `portal_visual_convergenttail 0` 保留原固定 tail 控制组，它在收敛或 horizon 到期时结束；
- `portal_visual_convergenttail 1` 使用官方式 moving-baseline tail：每个 frame-clock owner 先携带 authoritative baseline delta，再以 `100 units/s` 缩小 residual，并把最大 lag 限制到 `150 units`；它不会以 `horizon-expired` 作为正常结束条件；
- 同一 render frame 的重复 `CalcView` 不重复携带或追赶；
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
portal_visual_crossleafhandoff        0
portal_visual_commitseed              1
portal_visual_shadowtail              1
portal_visual_convergenttail          0
portal_visual_projectedhistoryrebase  1
portal_visual_reorientation           1
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
- `portal_visual_convergenttail 0`：恢复固定 one-tick tail 控制组；切换任一方向都会先清理当前 shadow 与 transaction visual ownership；
- `portal_visual_shadowpredictor 0`：关闭 render-eye shadow predictor；
- `portal_visual_crossleafhandoff 0`：恢复严格 leaf equality 回退控制组；
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

以下保留2026-08-06当时的问题记录，不代表2026-10-18仍未获得进展。当前已采用spatial-only rearm，第29轮正常作用域335次3500提交约12.1秒，详见[候选验收](portal-floorwall-test29-candidate-acceptance-analysis.zh-CN.md)。这不消除whole-brush孔径外漏碰撞的独立风险，也不外推任意地图无限循环。

当时观察：

- 在地板和天花板放置两个对等传送门；
- 玩家受重力在两门之间反复穿越；
- 速度持续增大到一定程度后，可能从地板掉入地图外；
- 主观上像穿越后存在一段尚未恢复完毕的状态窗口。

当时未判断它来自：

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

### 12.5 地面门到墙面门的延迟视觉交接断点

`高速穿越连续性修复/9` 验证了 `portal_visual_commitseed` 的作用边界：它修复的是穿越后的第一渲染帧，但当前粘滞主要发生在稍后的 eye-history / shadow predictor 交接阶段。

本轮控制组为事务 3～10，实验组为事务 11～18，16 个标记均为 `commitseed_sticky`。关键数据如下：

- 实验组 8/8 在 predictor restart 帧满足 `candidate=true valid=true applied=true source=official-command-end`，发布到首帧的推进时间为 `2.045..2.625 ms`，且 restart 帧没有 safety fallback；
- 前五个主视图样本的 `maxEyePositionDelta` 均值从控制组 `27.727` 降至实验组 `6.992`，说明第一帧 seed 确实生效；其中较大的事务 16 主要包含 crossing 后 command 剩余时间内的合法位移，不能单独视为首帧失败；
- 实验组事务 11、13～18 共 7/8 在 restart 后 `29..187` 个渲染帧触发 `candidate-leaf-mismatch`；额外的地面到墙面事务 24 也在 171 帧后触发同一原因；
- 这些 fallback 的 `candidateExitDepth` 均为正值，且失败原因已经越过 `outsideWorld`、`playerSolidContents` 和非法 leaf 检查，因此候选点当时并非 solid、地图外或无效 leaf，唯一失败条件是 `candidateLeaf != originalLeaf`；
- fallback 会在一帧内同时关闭 eye history 和 shadow predictor。对应 cadence handoff 的实际位移为 `5.688..26.389` units，明显高于当帧期望的 `0.787..2.602` units；例如事务 16 在 frame 55305 关闭两层状态，下一服务器 tick 出现 `24.945` units 位移，而期望仅为 `2.504`；
- 唯一没有 leaf mismatch 的事务 12 正常进入 release tail，但在 horizon 到期时仍以 `4.628` units 完成交接，期望为 `1.995`，因此仍可能产生较轻的延迟粘滞；
- 墙面到墙面的事务 19～23 没有 leaf-mismatch fallback，release tail 收敛或有界结束，主观上保持正常。

因此，当前根因不是传送瞬间、角度旋转或物理速度变换，而是自定义插值视角与已推进的物理眼位自然处于相邻 BSP leaf 时，严格 leaf 相等保护把合法的跨 leaf 插值误判为危险状态并突然回退。Portal SDK 的 `UpdatePortalEyeInterpolation` 会持续携带自由移动并跨空间追赶，没有“插值眼位必须与物理眼位位于同一 leaf”的不变量。

下一轮不能直接删除 leaf 保护，因为它曾参与黑天空/非法视点的 fail-closed 防线。应先增加只读诊断：记录被拒绝 candidate 的完整 contents/outside/leaf、`originalEye -> candidateEye` 双向或至少单向 world trace、距离、trace fraction/startsolid/allsolid，以及回退前后的 handoff delta。只有确认不同 leaf 之间存在无阻挡可见路径后，才评估把严格 leaf equality 收敛为“合法 contents + 非地图外 + 可通行 trace”的几何安全门，或实现有界的跨-leaf交接；不得重新启用旧 push、noclip、movement mutation 或 `PortalMaskRepair`。

Task 9 已按上述边界加入诊断实现，但没有改变这一行为结论或现有 fallback：

- `PortalEyeLeafHandoffDiagnostics::Evaluate(...)` 是不依赖引擎的纯 POD 评估器；只有 candidate/original 两个点快照均 available、坐标 finite、leaf 为正且不同、`playerSolidContents == 0`、不在地图外，同时正反两个 trace 均 available、终点 finite、未 `startsolid`/`allsolid`，且 fraction 位于 `[0.999, 1.0]` 时，才输出观察字段 `geometryClear=true`；超过 `1.0` 的异常 trace 值严格 fail closed；
- 运行时只在既有理由已经确定为 `candidate-leaf-mismatch` 且 `portal_visual_transactiondiag 1` 时，使用 `CTraceFilterWorldAndPropsOnly` 和 `MASK_PLAYERSOLID` 执行 `originalEye -> candidateEye` 与反向两条只读 trace；
- 单条 `[PortalEyeLeafHandoffProbe]` 记录包含 transaction/epoch/command/frame/call ordinal、当前 reason、candidate exit depth、candidate/original distance、fallback delta 向量与长度、两点的 eye/leaf/world/player-solid/outside/available，以及两条 trace 的 available/fraction/startsolid/allsolid/end；末尾显式记录 `diagnosticMutation=false viewMutation=false physicsMutation=false`；
- `geometryClear` 没有进入安全决策。原来的 leaf mismatch 顺序仍然先判定 fallback，随后 reset eye history、shadow predictor 和 pending commit seed，再把 `eyeOrigin` 恢复为原始引擎视图。
- 该诊断轮已完成纯契约与精确日志路由的 RED/GREEN、两轮独立审查和 7 个 runner 回归；根代理重新验证 Debug x86 构建为 0 warning/0 error。部署 DLL 长度为 `1349632`，SHA-256 为 `C58A28633ED010880CEF8E2B1BC753C36A0E53221954A5402999DE2A65D6AA1F`。

#### Test 10：延迟粘滞被拆分为两条独立退出路径

`高速穿越连续性修复/10` 共记录 15 次提交。事务 1～12 是地面门到墙面门测试，事务 13～15 是地面/空中回归；用户对稳定期事务 5～12 的 8 次穿越全部标记为 `leafhandoff_sticky`。配置、Hook、ABI、commit seed 和 reorientation 均处于预期状态。

物理和角度层已经排除：15/15 次提交的 `moveType=2`；提交前后速度模长差仅为 `0` 或 `-0.000061` 的浮点噪声；8 个标记事务的最大视角角度差仅为 `0.0194..0.0835` 度。因此本轮主观不变不是速度丢失、MoveType 或视角旋转回归。

8 个粘滞事务正好分为两组：

- 事务 6、9、10、11 在 predictor 仍携带连续眼位时触发 `candidate-leaf-mismatch` 并硬回退。四次 probe 的 candidate/original 距离为 `11.495..20.463` units；两点均 finite、leaf 为正、`worldContents=0`、`playerSolidContents=0`、`outside=false`、`candidateExitDepth>0`，正反两个 trace 都满足 `fraction=1.0000`、`startsolid=false`、`allsolid=false`，因此 4/4 均为 `geometryClear=true`。连同未标记预热事务 2，本轮所有 5/5 个真实 leaf mismatch 都是几何可通行的相邻 leaf，而不是墙内、solid 或地图外候选；
- 事务 5、7、8、12 没有 leaf mismatch，而是在固定一个 tick（`0.033333s`）的 release tail 到期时，以 `2.746..10.699` units 的未收敛残差直接结束。对应主观标记的最大眼位差为 `4.497..8.694` units。当前 `0.5` unit 收敛阈值并未满足，结束原因仅是 `horizon-expired`。

这解释了为什么 commit seed 已经明显改善第一帧，而本轮整体主观体验仍与上一轮相同：每一个标记事务最终仍命中两种延迟硬切之一。第一种是严格 leaf equality 对合法相邻 BSP 空间的误杀；第二种是自定义 one-tick release tail 在残差尚未收敛时的固定时限截断。

与 Portal SDK 2013 的差异也因此进一步收敛：`UpdatePortalEyeInterpolation` 每帧保留自由移动增量并以 `frametime * 100` 追赶，最大滞后限制为 150 units；官方路径既没有“candidate leaf 必须等于物理眼位 leaf”的不变量，也没有固定一个 tick 后无条件丢弃历史的交接规则。

下一步保持单因子分两轮推进：

1. 先增加默认关闭的安全跨 leaf A/B。只在原 `candidate-leaf-mismatch` 分支中，当现有双向几何评估严格得到 `geometryClear=true` 时允许 predictor 继续；invalid plane、负 exit depth、outside、player-solid、非法 leaf、trace 不完整/阻挡等任一条件仍按原路径 fail closed。同时记录 `decision=allow-safe-cross-leaf`。这一轮预计只消除事务 6/9/10/11 类型的硬回退，事务 5/7/8/12 类型仍可能粘滞；
2. 第一因子得到游戏内验证后，再独立替换固定 one-tick tail：按官方语义持续携带每帧自由移动并有界追赶，最大 lag 150 units，只在真正收敛、事务/reset 或安全失败时结束，不再因一个 tick 到期而硬切。该轮继续保留逐帧几何安全门。

两轮都只允许修改 render-eye/history/shadow predictor 决策，不修改玩家 origin、velocity、MoveType、ground、BSP、input 或门对变换，也不恢复旧 push、noclip、movement mutation 或 `PortalMaskRepair`。

#### Task 10：几何安全的跨 leaf 眼位交接

Task 10 实现第一条独立因子，并保持第二条 release-tail 因子只读：

- 新命令 `portal_visual_crossleafhandoff <0|1>` 默认关闭；只有开关开启、当前确为 leaf mismatch，且既有 `PortalEyeLeafHandoffDiagnostics::Evaluate(...)` 返回 `geometryClear=true` 时，才在该帧保留 predictor candidate；
- 每一个后续 mismatch 帧都重新采集两点快照和双向 world/props trace 并重新评估。日志只记录事务内第一次授权边缘 `[PortalEyeLeafHandoffDecision] decision=allow-safe-cross-leaf`，以及授权后的第一次安全拒绝；拒绝仍走原有 reset/fallback；
- invalid/negative exit depth、不可用或非 finite 快照、outside world、player-solid、非法 leaf、trace 不完整、partial fraction、`startsolid` 和 `allsolid` 均继续 fail closed；transition 层没有复制第二份几何 truth table；
- `releaseTailEnded=true` 且 `reason=horizon-expired` 时，在 transaction diagnostics 开启条件下记录一次 `[PortalEyeReleaseTailProbe]`，包含 residual、candidate/original distance、leaf/contents/outside 与双向 trace；该 probe 不改变 tail 生命周期、输出候选或 reset；
- 已验证的 reorientation 与 commit-seed 默认改为开启，Phase 1 仍由用户控制；reorientation 只把批准的四个参数调整为 airborne `600 deg/s`、grounded `1200 deg/s`、acceleration `2000 deg/s^2`、settled `300 deg/s`，状态机、Hook、roll-only 写入和重复帧抑制均未改变。
- 独立审查要求补齐运行态状态转换的直接测试。Fix Round 1 抽出由运行态实际消费的纯 controller，覆盖首次允许、重复安全帧不重复记录、授权后任意安全失败拒绝并清理、reset，以及 diagnostics-gated one-shot horizon probe；复审结论为 `ADDRESSED`，无新的 Critical/Important 问题。根代理随后复跑 10 个 runner、Debug x86 构建和差异检查，结果分别为 `10/10`、`0 warning / 0 error`、exit `0`。部署 DLL 长度 `1355776`，SHA-256 `297188F42DF3EA09589640374641330DACA5033D7959FEFBEF1A012CF35F339B`。

因此 Task 10 的 A/B 唯一行为差异仍是安全跨 leaf 授权；固定 one-tick release tail 的替换继续延后到独立 Task 11。

#### Test 11：跨 leaf 因子证伪后，延迟粘滞收敛到 release-tail 硬交权

`高速穿越连续性修复/11` 验证了 Task 10 的真实作用边界。显式控制组事务 8～11 与实验组事务 12～15 的开关顺序正确，八次均为 Blue -> Orange floor -> wall；实验组四个事务均记录了 `decision=allow-safe-cross-leaf`，候选点与原始点 finite、contents 为空、非 outside、双向 trace 完整且 `geometryClear=true`。因此跨 leaf 许可已实际生效，并非命令、默认值或运行态接线失败。

两组仍然出现延迟粘滞，但触发层不同：

- 控制组事务 8～11 在 commit 后 `0.200..0.233s` 的 `candidate-leaf-mismatch` 处按原安全策略硬回退，候选与原始眼位的差值分别为 `22.329`、`24.420`、`26.698`、`11.822` units，均值 `21.317`；
- 实验组事务 12～15 成功保留跨 leaf candidate，却都在稍后的固定 tail 到期处以 `reason=horizon-expired` 结束，剩余残差分别为 `5.421`、`14.807`、`10.214`、`5.622` units；
- 这些 tail 样本结束时 candidate/original 已位于同一有效 leaf，world/player-solid contents 均为 0、outside=false，正反向 trace 均为 `fraction=1.0000` 且没有 `startsolid/allsolid`。`geometryClear=false` 只是当前 probe 复用了“必须 leaf mismatch”的 Task 10 诊断契约，不能解释为几何不安全；
- 实验组硬交权统一发生在 commit 后约 `0.267s`，终端残差均值 `9.016`，比控制组降低 `57.7%`，但仍在单帧切回 original eye。cadence 也记录到控制组终端 adapter delta `14.234..29.225`、实验组 `4.784..14.933`，均明显高于各自当帧 expected delta；因此改善幅度不足以消除主观粘滞。
- 实验因子只把控制组的 leaf 硬回退推迟到 one-tick tail 截止点，没有改变 tail 生命周期。残差也不是单调收敛：事务 13 从 tail 开始的 `12.345` 增至 `14.807`，事务 14 从 `9.252` 增至 `10.214`，证明单纯延长现有 velocity tail 不可靠。

当前结构中存在两套结束条件：`PortalEyeContinuity::State` 已按官方式 `frametime * 100` 追赶，并在 `originalViewResidual <= 0.5` 时释放自身 override；其外层 `ShadowPredictor` 仍保存独立的 `m_shadowEye`，在 `continuityActive=false` 后只继续积分 `velocity * dt`，并在一个服务器 tick 的 horizon 到期时无条件 `Reset()`。到期返回帧直接恢复函数入口处的 baseline/original eye，因此未收敛的 shadow residual 会成为可见跳变量。

这与 Portal SDK 2013 的主要剩余差异已经明确：官方只维护一套 transformed interpolated/uninterpolated eye history，每帧先携带真实 `EyeFootPosition` 自由移动增量，再以 `frametime * 100` 有界追赶，只有真正收敛、显式 reset 或安全失败时结束；没有第二层 velocity-only shadow，也没有固定一 tick 到期后硬交权。

下一轮进入独立 Task 11，但仍采用单因子和 fail-closed 原则。先用一个压缩的 terminal-handoff 诊断/离线候选比较锁定正确交权公式，再启用行为 A/B，避免把现有非单调 velocity tail 机械延长：

1. 只在终端前一帧、交权帧和之后三个 command boundary 记录 `previousAppliedEye/original/finalEye`、实际 jump、offset 及 closing rate、normal/tangent 分量、expected/actual delta、roll/rate 与安全快照；同时只读模拟 moving-original 上的 `50/75/100ms` offset decay，比较最大单帧 jump，不改变实际输出；
2. 依据该证据建立纯状态机 RED，用 Test 11 的 `5.421..14.807` 非零残差场景证明 fixed-horizon 会产生硬切，并验证“携带权威 baseline/free-movement delta，再以官方 `100 units/s` 追赶”能够单调收敛；
3. 行为 A/B 中 tail 只在残差自然收敛到当前容差、事务/reset、invalid/non-finite、出口负深度、outside/player-solid、非法 leaf 或 trace 不安全时结束；删除 `horizon-expired` 作为普通结束原因，不扩大任何物理 mutation；
4. 保留跨 leaf 的逐帧双向几何检查，并用交权边缘日志验证结束帧不再携带数单位残差；角度回正继续作为独立公共因子。日志显示事务 12～15 约在 `0.341..0.343s` 完成，末帧从不足 1 度归零；它可能影响“旋转停止”的主观感受，但不能解释实验组在 tail 到期处已量化的 `5..15` units 位置跳变，本轮不与 Task 11 混改。

本轮日志中的主观标记仍全部使用旧标签 `leafhandoff_smooth`，没有 `crossleaf_sticky`。因此标记字段不参与本次分类，结论以用户明确反馈、A/B 命令边界和事务状态数据为准；下次重新绑定后再恢复标记统计。

#### Task 11 / 本轮 Task 1：官方式 moving-baseline convergent shadow tail

本轮把 Test 11 已量化的第二条硬切路径实现为独立、默认关闭的 A/B 因子：

- `ShadowPredictor::Update(...)` 的每个调用点必须显式选择 fixed 或 convergent 模式；固定模式的 `velocity * dt` 与 one-tick horizon 行为保持为控制组；
- convergent release 只在 frame-clock owner 上把 `baselineEye - previousBaselineEye` 携带到 shadow eye，然后按 `max(0, frameTime) * 100` 朝 authoritative baseline 追赶；residual 超过 `150` 时，当帧追赶量严格改为 `residual - 150`；
- residual 小于当帧 follow distance 时直接输出 baseline 并以 `converged` 结束；正常路径不再使用固定 horizon。disable、reset、non-finite input 和原有 runtime spatial safety failure 仍立即 fail closed；
- `portal_visual_convergenttail <0|1>` 默认值为 `0`。任一方向的实际模式切换先清理 eye continuity、shadow predictor、cross-leaf authorization、render cadence 与 transaction visual capture，再写入新模式；相同值重复设置不产生 reset edge；
- 新的 `[PortalEyeConvergentTail] event=start|progress|end` 只在生命周期或每 5-unit residual bucket 边缘记录，包含 baseline/carry/catch-up、residual before/after、maximum-lag clamp、terminal visible jump 和 `physicsMutation=false`；`[PortalEyeShadowPredictor]` 同步记录 tail mode 与这些纯结果字段；
- 每一帧输出候选仍经过既有 finite、positive exit depth、world/player-solid contents、outside-world、valid leaf 及双向 trace/cross-leaf 安全门；本轮没有新增或恢复任何 player origin/velocity/base velocity/MoveType/ground/usercmd/brush/portal-transform mutation。

纯状态机 RED/GREEN 用 `14.807` 与 `10.214` 两个原本会增长的 residual 锁定 moving-baseline carry；30/60/300 FPS 的 `14.807` 样本分别在 `0.166667/0.150000/0.150000s` 收敛，终端 jump 均小于 `1.5` unit；`160`-unit lag 的第一帧严格变为 `150`。九个聚焦 runner 全部通过，Debug x86 构建为 `0 error`；构建仍报告 SDK `convar.cpp` 的 5 条既有 C4355 警告，portal transform runner 仍报告 2 条既有 C4819 编码警告。

#### Task 12 / 本轮 Task 2：Portal SDK 普通 roll 曲线的显式五倍时基

上一版为满足约半秒内回正，直接把官方普通 roll 分支的四个量扩大为 `600/1200/2000/300`。这只能得到“官方输出速率乘五、但仍按原始加速度时间线推进”的近似；它不是把官方状态机整体播放五倍。以空中 `roll=+90`、初始 rate 为 `0`、`frameTime=1/60` 为例，旧近似首帧只把 roll 推进到约 `89.444444`，而显式五倍官方时基应得到：

```text
officialDeltaTime = frameTime * 5 = 1/12
officialRate       = 0 + 400 * 1/12 = 33.333333 deg/s
effectiveRate      = officialRate * 5 = 166.666667 deg/world-s
roll               = 90 - officialRate * officialDeltaTime = 87.222222
```

本轮实现因此恢复官方状态量，并只在时基上做用户批准的显式加速：

- controller 内部保存 Portal SDK 的 official rate；普通分支严格使用 airborne cap `120`、grounded cap `240`、acceleration `400`、upright settled rate `60`，分支仍是严格的 `upZ < 0.75`；
- rate 状态更新和 roll 积分都使用同一个 `officialDeltaTime = frameTime * 5.0`；没有新增 cubic、spring、exponential 或自定义 ease；
- `FrameResult::rate` 与 `Controller::GetRate()` 继续输出 effective world-time rate，即 internal official rate 乘 `5`，所以既有运行态速率日志仍可与此前的 `600/1200/300` 上限直接比较；
- 正负普通 roll 对称、单调且不越过零；live pitch/yaw 保留，只写 roll；同一 frame count 不重复推进；active re-arm 保留 internal official rate；
- exact zero 在 arm 边界立即成为 inactive no-op，rate、transaction 和 frame identity 全部清零；正常完成同样清理 dormant state，但末帧仍把精确 `roll=0` 交给调用者；
- `FL_ONGROUND` 仍是唯一已验证的 L4D2 地面输入。未实现官方 exact `+180` 随机方向、pitch reorientation、`cl_reorient_in_air`、`g_bUpsideDown` 或私有 eye-angle interpolator，这些仍是显式非目标，不做猜测式近似；
- begin/end 与 `portal_visual_reorientation` 查询/变更日志增加 `curve=portal-sdk-2013 timeScale=5.000`；逐帧 bucket 日志不重复该元数据，因此没有新增 per-frame tag 或日志膨胀。

严格 TDD 的首个行为 RED 在旧实现上以 exit `1` 命中 `+90/60 FPS` effective-rate 断言；完整契约随后以缺失纯 official-rate/metadata 接口产生预期编译 RED，metadata 结构测试也先以 exit `1` 失败。GREEN 后覆盖 `+/-`、`0.75` 严格边界、air/ground cap、upright settled rate、30/60/300 FPS、duplicate、active re-arm、completion/reset、negative/non-finite input 与 unsupported mode。十二个 Task 2 + Task 1 聚焦 runner 全部 exit `0`；Debug x86 全量构建为 `0 warning / 0 error`。本轮仍只修改 view angles 与诊断，不写任何物理状态。

#### Task 11–12 综合门与游戏测试基线

两个单因子完成后，根流程重新执行 12 个相关 runner，结果为 `12/12`、失败 `0`；Debug x86 完整构建为 `0 warning / 0 error`，`git diff --check` 为 exit `0`。Task 1/2 隔离差异的禁止 mutation 扫描未发现新增 player origin、velocity、base velocity、MoveType、ground、usercmd、brush 或 portal-transform 写入，也未恢复旧 noclip、movement mutation、出口 push 或 `PortalMaskRepair`。

综合独立审查结论为 PASS，无 Critical/Important。唯一 Minor 是 exact-zero roll 会立即清理控制器，因此运行日志可能只有 `Begin` 而没有对应 `End`；这不改变角度、事务或物理状态，暂留作日志整洁项。最终 Debug 产物已复制到游戏根目录，DLL 长度为 `1367552` bytes，SHA-256 为 `E76C2498E2D0D33EAC21AC9EE9D55F92A2AD2AA6D49C8129D4225F716C189F98`；启动器 SHA-256 为 `48EB5FCCEAFAD50F1A7B0F04C612F20E899680C2226351D6450036D5DEBED744`，源与目标逐项一致。

游戏内验证仍保持单因子：固定开启已验证的 safe cross-leaf handoff；先以 `portal_visual_convergenttail 0` 复测旧 fixed tail，再以 `1` 验证 moving-baseline convergent tail。官方普通 roll 的五倍时间轴在两组中共同生效，不作为 A/B 因子；需要同时回归 floor-to-wall、wall-to-wall、平地墙门和高速空中穿越。

#### Test 12：convergent shadow tail 被游戏实测证伪

`高速穿越连续性修复/12` 否定了上一节的 convergent shadow-tail 产品假设。控制组事务 3～11 与实验组事务 12～18 的 A/B 边界正确，全部主观标记为 `perceived_sticky`。实验组的终端 shadow residual 已分别降到约 `0.023..0.192` units，并且都以自然 `converged` 结束，不再存在上一轮 `5..15` units 的 tail 到期硬切；但粘滞体验没有改善。这说明终端交权跳变是真实问题，却不是当前症状的充分根因，把官方追赶曲线放进第二层 shadow 并不能复刻官方单一眼位历史。

本轮墙面双门回归还暴露了更关键的所有权分裂：测试结束后没有把 convergent 因子切回控制组，事务 19～33 实际仍运行实验路径。跳跃事务中，`PortalEyeContinuity::State` 在 original residual 低于 `0.5` units 时已经释放，而外层 shadow 仍保留 `2.945..6.649` units；步行事务的 shadow residual 多为约 `0.28..0.55` units。因而同一套代码会出现“步行尚可、跳跃重新不连续”的速度相关回归。一次空中卡顿留作独立观察，本轮不混修。

Portal SDK 2013 并不存在 `State + ShadowPredictor` 两级眼位所有权：`PlayerPortalled` 发布待处理矩阵，`DetectAndHandlePortalTeleportation` 一次性变换眼位历史，`UpdatePortalEyeInterpolation` 在同一份历史上携带真实自由移动并按 `100 units/s`、最大 `150` units lag 追赶。因此下一轮不再调节第二层 tail，而是建立默认关闭的单一所有者 A/B：把已经验证的 command-end seed 只推进 publication-to-render gap 后直接写入 `State`，由 `State` 完成后续 CalcPlayerView movement carry 与官方追赶；该实验事务不再启动独立 shadow。旧 `portal_visual_convergenttail` 产品命令、分支和生命周期日志随实现删除。

#### Task 13：单一眼位历史所有者 A/B

新增默认关闭命令 `portal_visual_singleeyehistory <0|1>`。实验路径只在 pending teleport 的 matching transaction 上接受 finite、non-stale、official-command-end seed；推进时间被夹在 `[0, interval_per_tick]` 且全局不超过既有 `0.05s` 安全上限。种子只消费一次，之后 `State` 继续使用既有三轴变换、CalcPlayerView movement carry、`frametime * 100` 追赶、150-unit maximum lag 与出口平面保护。

为减少重复测试指令，已经通过游戏验证且仍保留逐帧双向 trace fail-closed 的 `portal_visual_crossleafhandoff` 同步改为默认开启；控制组与实验组共同使用这一基线，它不是本轮 A/B 差异。Phase 1 brush mutation 仍不在模块初始化时自动开启，避免离开测试地图或门对未就绪时改变世界碰撞。

只有 seed 真正应用成功时，当前 transaction 才锁定 `activeEyeOwner=state-only` 并关闭 shadow predictor；seed disabled、缺失、stale、transaction mismatch 或 non-finite 时严格回退到既有 shadow 控制路径。运行时出口深度、contents、outside-world、valid leaf、双向 trace 与 cross-leaf 安全门均保持不变。切换 A/B 会原子清理 State、shadow、pending seed、cross-leaf authorization、cadence 和当前视觉事务，避免跨模式残留。

压缩日志 `[PortalEyeSingleOwner]` 只记录 mode-switch、consume、state-converged 或 safety end，包含 seed candidate/valid/applied、source、publication elapsed、raw/advanced seed、State 接管后的眼位与 `physicsMutation=false`。已经证伪的 convergent-tail evaluator、诊断 gate、命令与 runner 被删除；固定 shadow 仅保留为控制组和实验 seed 失败时的 fail-closed 回退。

纯状态机测试覆盖了 Test 12 的“State 已接近而 shadow 仍残留”回归夹具、matching/mismatch/non-finite seed、one-tick elapsed clamp 与重复 CalcView 不得二次消费；decision、命令注册和精确日志策略也分别有 focused test。下一门禁仍是游戏内单因子 A/B，尚不能声称主观问题已修复。

部署前验证为 26/26 portal runners 通过，最终 Debug x86 构建 `0 warning / 0 error`，`git diff --check` 为 exit `0`。部署到游戏根目录的 DLL 长度为 `1367040` bytes，SHA-256 为 `554DE9015DC8C09365195BCF5155B100DD037D3A38F54C53374D83D1B7E99036`；启动器 SHA-256 仍为 `48EB5FCCEAFAD50F1A7B0F04C612F20E899680C2226351D6450036D5DEBED744`，源与目标逐项一致。本轮仍未启动游戏、暂存、提交或推送。

#### Test 13：单一所有者生效，但不是剩余粘滞的充分解

2026-09-20 复核 `高速穿越连续性修复/13`：地板→墙面控制组 txn 21–24、实验组 txn 25–31；实验组 7/7 seed 成功接管、shadowFrames=0、State 正常收敛，但用户反馈均无明显改善。墙面 txn 1–20 在切换实验前，不能据此宣称实验模式的墙门回归已覆盖。

关键新增证据是 txn 25 在 State/shadow 均关闭、甚至 roll 已回到 0 后，原始眼位与最终主视图仍重复相同位置，再随一个 simulation tick 集中移动约 19–23 units。平均位移比接近 1 掩盖了帧间的零位移和大步长。单一所有者只解决所有权，不保证输入来源具备官方 native interpolation 的时基与连续性；当前还未实现官方 EyeFootPosition 的 roll 眼高语义，但它不能独自解释 roll=0 后的水平阶梯。

下一轮建议先查 movement command-end → render-start 原生插值 → CalcPlayerView → 最终视图的首个失真边界，不继续调整 shadow 尾巴。修复 FloorWall 明细日志白名单、phase 结束后的真实 command identity，并增加普通下落对照；未定位前不改变运动或旋转。证据、限制、SDK 差异和停止条件见 [第 13 轮原生位置节奏分析](portal-floorwall-test13-native-cadence-analysis.zh-CN.md)。本轮仅记录分析，未构建部署。

#### 第 14 轮编码：原生位置更新链只读诊断

2026-09-20 按用户批准实施：新增独立 NativeCadence 窗口，关联 CreateMove 身份、movement command-end、render-start 前后、CalcPlayerView 前后和最终主视图；窗口不随穿越 phase 清空。恢复 FloorWall 明细白名单并加活动事务/行数预算；加入普通下落一次性采样入口，四个 tick 边界 prelude、状态边界压缩输出和冻结毫秒统计。没有更改物理或视觉算法，没有新增猜测性 native ABI 调用。内部插值私有状态仍 unknown，留待此次证据决定是否深入。

27/27 runner 与最终 Debug x86 构建通过，DLL 已部署并核对源/目标哈希。本轮仅完成诊断编码，未宣称改善体验。完整测试步骤、日志关键字、已知边界和部署标识见 [第 14 轮诊断测试说明](portal-native-cadence-diagnostic-test14.zh-CN.md)。

#### Test 14：原生渲染插值边界的正反对照已成立

2026-09-22 分析第 14 轮日志：29 个完整配对的诊断窗口包含普通下落 2 次和穿越 27 次，环境统一为 fps_max=300、host_timescale=1。原生日志、命令身份及 FloorWall 白名单均生效；无需因测试顺序偏差整轮重测。

剔除最初 2 tick、落地阶段和 txn 18 的零速度接触样本后，11 次地板→墙面事务的 435 条已输出空中边界样本全部表现为 render-start 前后 abs 不变且等于 recv-field；普通下落 46 条和墙门跳跃 77 条均能观察到插值。地板门在 roll=0、State/shadow 已停后继续冻结约一个 tick，再集中前进约 25–30 units；客户端 command-end 则逐命令推进，水平速度保持、竖直速度按重力变化。EF_NOINTERP 并未持续保留，不能以强制清标志作为未经验证的修复。

这将首个已观测失真边界定位到原生 render-start 插值链，而非最后相机交权，但还不能区分 Interpolate 未调用、历史/选时异常或原函数内部后续覆盖。实验 single-owner 的墙门跳跃也暴露 24–49 ms 累计最终冻结，不提升为默认。下一轮固定控制路径，做经过 ABI 验证的 native 插值调用/历史/时间只读探针，再根据实际分支单因子修复；不继续延长 shadow 或混改眼高/旋转。证据行、测试偏差、日志压缩和停止条件见 [第 14 轮原生插值边界分析](portal-floorwall-test14-native-interpolation-boundary-analysis.zh-CN.md)。本次仅文档分析，未构建部署。

#### 第 15 轮编码：原生 Interpolate 调用是否缺失、无效或随后被覆盖

2026-09-22：通过本机 PE32 RTTI/vtable、包装体与父类反汇编确认 `C_TerrorPlayer::Interpolate` 的候选入口和 ABI；运行态只从实际本机玩家槽位 107 和 rel32 找目标，不按固定 RVA。完整签名/父类指纹与模块范围均通过后才接入原样转发 Hook。没有主动调用 Interpolate、修改内部 history、强制重置或改变已有穿越行为。

新增截至主视图提交的调用次数、render-start 内调用、输入时间、返回值、前后位置及返回后位置差；内部 selected time/history 继续 unknown，原函数返回 true 不被解释为插值活动。每个被 tracker 接受的帧均参与计数，native-only 状态变化也能触发有预算的明细；全窗口保留差异次数/最大值及与冻结的关联，避免关键证据被旧眼位节奏压缩。相同阶段改用精确引用，正常命令按批汇总，异常和终端证据保留。

本轮 28/28 runner、Debug x86 构建、独立审查修正及部署哈希核对完成；仍待游戏运行态验证，未宣称主观修复。每一步的待印证假设、继续／停止门槛、完整分号指令和备份信息见 [第 15 轮原生插值入口诊断](portal-native-interpolation-entry-diagnostic-test15.zh-CN.md)。下一步只沿实际命中的 native 边界继续；拿不到安全入口或新可区分证据时，暂停此补丁路线讨论实现范围。

#### Test 15：原生入口正常派发，下一步定位 Part1 的分支和实际选时

2026-09-23 分析第 15 轮日志：14 个配对窗口共 4,776 个接受帧、4,776 次原生调用，Hook 身份/签名/静态 ABI 校验均通过，无无调用帧、无效调用或返回后位置差异。没有旧 DLL 证据；singleeyehistory 全程为 0。普通下落只记录 1 次、墙门空中仅 3 次且速度约 229–262，不能宣称完成速度匹配的全部高速回归，但不影响 5 次地板门的一致结果，无需整轮重测。

排除最初 2 tick 和落地后，地板→墙面 224 条已输出空中边界样本全部 native position delta=0；普通下落 23 条、墙门空中 40 条均有变化。五个地板门全窗口最终视图累计冻结约 404–439 ms，最长单段约 30–33 ms，随后按 tick 集中移动约 25–30 units；累计时长不能解释为一次停顿 0.4 秒。roll=0、State/shadow 均结束后阶梯仍在，因此不继续调相机尾巴或旋转曲线。

五次都在首次观察到落地后的下一个 tick 才观察到 native 插值恢复。它使历史/选时生命周期成为值得继续调查的线索，但落地伴随接触、减速和历史刷新，不能直接认定 ground 是根因。当前定位是“原生函数确实调用，内部没有产生位置变化”，尚不是最终根因。

下一轮只读验证 BaseInterpolatePart1 的进入、STOP/CONTINUE、noMore 及时间引用前后值，从实际分支决定是否再深入 origin watcher。实际 L4D2 Part1 是 ECX this 加四个栈参数、ret 0x10，与本地 SDK 带 oldVelocity 的五参数版本不同；必须按已证实的 native ABI 和 rel32/签名接入，不能直接复制 SDK typedef 或猜偏移。若验证失败或拿不到新的可区分证据，暂停这条补丁路线，不无限追加主观测试。完整数据、证据行、覆盖限制和停止条件见 [第 15 轮原生插值调用分析](portal-floorwall-test15-native-interpolation-call-analysis.zh-CN.md)。本轮仅分析和文档，未修改 DLL 或构建部署。

#### 第 16 轮编码：Part1 分支与最终选时的有界只读探针

2026-09-24 按第 15 轮批准计划实施：从已有父类 rel32 解析 Part1，验证完整 350-byte 函数体及六处 ASLR 数据地址；严格使用 L4D2 的四引用参数、ECX this、ret16，原函数原样调用一次。只关联外层本机玩家 Interpolate，记录父类 caller 匹配、STOP/CONTINUE、noMore、时间引用前后值及局部 origin/rotation；STOP 跳过未初始化旧输出，Part2 前不依赖旧 abs 缓存。没有主动调用插值或修改历史、位置、速度、ground、MoveType、brush、相机算法。

复用原有 prelude/128 组明细预算，全帧汇总不依赖明细输出；审查发现选时停住/回退可能只有汇总而缺少发生帧，已通过 RED→GREEN 增加决定日志输出前的时间关系分类。新 Part1 Hook/Frame/Summary 标签已放入精确白名单，旧 selectedTime=unknown 改为指向 Part1Frame，history 继续 unknown。没有新增测试开关或主观标记。

最终 28/28 runner、Debug x86 构建及 diff 检查通过，DLL 已备份部署并核对 SHA-256 `51D78408F73CD35BEA1635BDFCE8FAADE4173CCACEF32F67A739C5A462D7AACE`。保持 singleeyehistory=0 做普通下落、墙门及地板→墙面诊断；本轮不预期主观改善。完整指令、数据有效性、每种结果的继续／停止门槛和部署身份见 [第 16 轮 Part1 诊断说明](portal-native-part1-diagnostic-test16.zh-CN.md)。未提交推送，等待用户游戏实测后决策。

#### Test 16：Part1 分支和选时正常推进，发现 recv/local 观测混淆

2026-10-08 分析：普通下落 2 次、墙门 8 次、地板→墙面 5 次，共 5,463 接受帧；每帧 Part1 自然调用一次且 caller 匹配，全部 CONTINUE，无 STOP/无效/缺失。选时均比外层输入晚约 66.7 ms（两 tick），正常场景和地板门一致，无停住/倒退。可以排除本轮窗口内的提前返回和选时不推进，但不能在历史区间未知时声称选时完全正确。

地板门 201 条中后段空中事件样本全部外层 native abs 不变；普通下落 46 条、墙门空中 47 条都有变化。五次最终视图累计冻结约 339–366 ms，单段约 30–33 ms；roll=0、State/shadow 停止后仍有阶梯，落地后下一 tick 又恢复。这个较上轮更小的累计数不代表只读版修复有效，轨迹/接触时机不同。测试有效，无需重复同一 DLL；墙门尚缺与地板门约 700 速度匹配的完整对照。

重要勘误：第 16 轮代码把 M_NETVAR 的 m_vecOrigin/m_angRotation 标作 localOrigin/rotation。正常下落同样 Part1 changed=0，且 engine oldAngles 与访问器角度明显不同；因此这项计数只代表 recv 字段，不能证明真正 native-local 没写入。分支、时间、有效 oldOutputs 与外层 abs 证据不受影响。下一轮应先从已验证 Part1/Part2 数据流校正 native-local 来源，同时只读验证 origin entry 绑定、参与状态与真实 watcher 调用，区分“没有写入”和“local 已写、abs 未传播”，不盲目回拨时间、强制插值或再调相机尾巴。

本机已有可沿真实调用链验证的下一层入口；若绑定/ABI 无法安全确认，或新增探针仍无法提供区分结果，就暂停补丁路线。详细证据行、静态与运行态边界、清理项及继续／停止条件见 [第 16 轮 Part1 分析](portal-floorwall-test16-part1-selection-analysis.zh-CN.md)。本轮只分析和文档，未修改产品代码、构建、部署、提交或推送。

#### 第 17 轮编码：校正 native-local 身份，观察真实 map-loop 与条目参与条件

2026-10-08 编码、10-09 部署：保留穿越行为基线，从完整 Part1 指令和独立 Part2 路径校验真正本地 origin/angles，另行记录 SDK recv 字段及既有 outer abs。真实 Interp_Interpolate 循环通过自然调用链接入，构造注册片段和完整插入函数独立证明 entry data 绑定；只读记录总数/参与数、origin/angles 索引与 needs、前后时基、本地变化及 CONTINUE oldOutputs 对照。STOP 旧输出不读取，无效观测不累计为静止。

未验证具体 watcher 虚函数 ABI，因此本轮明确使用批准的保守路径：map-loop 实际调用和条目分支已观测，单个 watcher 调用及历史区间继续 unknown。条件具备不等于已调用；缺失绑定、范围外、needs 跳过与本地已写但绝对位置未传播，是本轮要新增区分的结果。若仍无新增区分证据，暂停补丁路线，不再追加同类无效测试或凭猜测安装 Hook。

cadence schema 升为 4，新标签纳入精确白名单，冗余成功 Part1 明细压缩到状态/时基边缘，接受帧汇总不受明细预算影响。独立审查及控制代理重跑 28/28 runner、Debug x86 构建通过；新版 DLL 已备份部署并核对 SHA-256 `97A95E8998B2A595F1116FE35811F38AFC110919855D729676406BF1FD68B2AD`。未修改 gameplay、未提交推送，等待实测。完整分号指令、字段枚举、能力边界、继续／停止门槛及部署身份见 [第 17 轮 native-local/map 诊断](portal-native-local-map-diagnostic-test17.zh-CN.md)。

#### Test 17：origin 条目可参与，但首次落地前没有 native-local 插值变化

2026-10-10 分析 17 个窗口：普通下落 2、墙门相关 10、地板→墙面 5，共 5,509 接受帧。outer/Part1/map 调用和 caller 一一对应，全部 CONTINUE，无安装失败、invalid、缺失绑定或范围外；本地字段与引擎 oldOutputs 对照 5,509 次全部一致，校正后的 native-local 观测有效。

排除事务头两 tick、首次落地后的额外空中状态以及非正常移动模式，地板门 184 条空中事件样本全部 origin index=6、needs 1→1、branch=4，但 native-local origin 与 outer abs 都没有插值变化；普通下落 48 条、有效墙门空中 41 条都有变化。它排除了本轮异常样本的“未参与/needs 跳过”和“local 已写但 abs 未传播”，尚不能证明单个 watcher 实际调用和选样是否正确。回正及 State/shadow 结束后仍有位置阶梯，最终视图最长单段冻结约 31–32 ms；累计冻结不能当作一次暂停。五组都在首次观察到落地后的下一 tick 观察到 origin 插值恢复。

本机已找到 concrete origin watcher 包装体及内部 GetInterpolationInfo 的静态调用链，下一轮可以验证实际 caller/对象/ABI，观察自然调用的 amount/effectiveTime、选样 return、索引/frac 与有限样本时间位置，区分历史空缺、区间错位或写回问题。当前尚无运行态具体 watcher/历史证据，不直接改选时或恢复旧 mutation。若无法安全验证或没有新增区分结果，暂停此局部补丁路线。完整覆盖限制、证据行、候选链及继续／停止条件见 [第 17 轮原生 map 分析](portal-floorwall-test17-native-map-analysis.zh-CN.md)。本轮仅分析/文档，没有修改 DLL 或部署。

#### 第 18 轮编码：真实 origin watcher 与 GetInterpolationInfo 选样

按照第 17 轮已批准的下一层诊断计划，观察 origin watcher 自然 Interpolate 以及其内部 GetInterpolationInfo 的实际 currentTime/amount/effectiveTime、返回、索引/frac、有限历史样本与本地位置写回。完整构造/原生函数/独立 writer 指纹、精确 ASLR 重定位、运行态 entry/watcher/value 身份和嵌套 caller 校验成立才解释数据；最多读四个样本，不写 history，不额外调用原生插值。位置/角度 outcome 拆开，新 origin 日志纳入精确白名单、既有有界事件预算与全部接受帧汇总。

本轮未改相机、速度、ground、回正或 brush，不宣称主观改善。独立审查的失败读取分类问题经 RED→GREEN 修复，完整 28/28 runner 和 Debug x86 构建通过。部署 PE `6AC92621`，两侧 SHA-256 `6FBA33D2AE4AF5FF7DE0B0444D861EB14670834A63D216A0F4B2EECFA959DB44`，上一轮 DLL 已备份。完整分号测试步骤、状态/失败枚举及继续／停止条件见 [第 18 轮原生 origin 选样诊断](portal-native-origin-selection-diagnostic-test18.zh-CN.md)。等待普通下落、墙门回归、地板→墙面实测，以新的实际选样证据决定后续是否继续局部插值路线；未提交推送。

#### 第 18 轮实测分析：选样成功，首次空中历史区间退化

2026-10-13，21 个有完整汇总的窗口（普通下落2、墙门14、地板门5），其中墙门 capture8/12 提前结束，从完整对照剔除。6,734 接受帧，真实 origin/selection 各6,402次自然调用，安装/对象/caller/读取有效，无空 history/选样失败。正常下落49、完整墙门空中56条事件有真正 origin 插值；地板门208条首次落地前事件全部 count3/head13、newer=older=index2、首末样本同时间同位置、原生位置变化0。样本时间和位置每 tick 推进，缺少时间邻域；index1本轮未读，不能宣称逐项证明三份都相同。回正/State/shadow结束后120条仍退化，五组均在首次观测落地后的下一 tick 观察到插值恢复。

形态与官方/本机 origin Reset 清历史再写三份当前值高度吻合，但尚无 runtime Reset 调用证据。下一轮建议观察具体 watcher Reset 的自然调用者、时间及重建前后有限样本，覆盖预测/网络阶段而非只限 map/render 作用域，区分持续 Reset、预测恢复覆盖和 latch 时间/flush 异常。不得据形态直接禁止空中 Reset、改时间常数或恢复旧私有 history 写入。无需重测当前 DLL；本轮只分析和文档。完整证据、覆盖限制、官方对照及继续/停止门槛见 [第 18 轮 origin 选样分析](portal-floorwall-test18-origin-selection-analysis.zh-CN.md)。

#### 第 19 轮编码：观测具体 origin Reset 的来源与三份样本重建

从已验证 origin 类型的 slot5 解析 void/no-stack-args Reset，核对完整120-byte指纹、三处 writer rel32 与精确 ASLR GlobalVars 操作数；自然调用一次，前后各有限读取三个样本。不限定在渲染/map 作用域，记录实际 caller 模块/RVA、原生时基与既有预测作用域，窗口前事件进入有界 prelude，全部窗口调用与 caller 汇总独立于明细预算。只有真实对象对应的预测作用域才标当前 cmd，其它保留 unscoped/unknown，不猜读预测私有字段。

已有 origin 选样证据继续保留，重复外层成功明细压缩到边缘/异常，永久 unknown 文案改为证据来源。未改游戏行为，不禁止空中 Reset；入口未命中不排除内联/恢复写入。测试步骤和继续/停止门槛见 [第19轮 origin Reset 诊断](portal-native-origin-reset-diagnostic-test19.zh-CN.md)。当前实际分支为 `codex/portal-official-mechanism`，保留原工作区；本轮不切换、不提交推送，等待用户实测。

第19轮交付验证：28/28离线测试通过，Debug x86构建成功，完整Reset指纹和增量只读复核通过；valueChanged小量变化回归修正为逐分量判据。部署身份 `pe-6ACDCEF0`、SHA-256 `5C25BF816567C0210290DBC0EA7C9BBF6AE36C15DF53FE0A883D390D904F8E73`，游戏根目录与源DLL一致，旧DLL已备份。运行态入口/调用者/生命周期/开销及根因判断仍待实测，不等同于修复验收。

#### 第19轮实测与第20轮父来源诊断

第19轮实际Reset探针有效，289次自然origin Reset均来自client+4728E并重建三份当前值。普通下落3次各只有落地附近1次；五次地板→墙面首次落地前，两tick后87条有效Reset明细全部丢已有区间，206条origin事件全部夹取、无插值变化。墙门4窗口主要地面，不夸大空中回归覆盖。完整统计、分析器压缩适配边界及证据见 [第19轮分析](portal-floorwall-test19-origin-reset-analysis.zh-CN.md)。

静态核对4728E位于ResetLatched base watcher循环；本地RTTI slot106的80-byte包装器尾跳到56-bytebase，保留原上游返回地址。第20轮只加该父入口scope，以实际模块/RVA关联子Reset，验证是哪条合法恢复/lifecycle路径持续要求清历史。原函数自然调用一次，不屏蔽、不改时基或私有字段；未知父证据不污染原有子Reset证据，固定caller桶与总计独立于明细预算。没有新行为修复或改善承诺，详见 [第20轮测试说明](portal-native-resetlatched-caller-diagnostic-test20.zh-CN.md)。

第20轮交付：28/28测试、Debug x86构建及只读增量复核通过；完整磁盘指纹核对通过，失败日志tail证据措辞已修正。2026-10-14部署 `pe-6ACE5B75`，源/游戏DLL SHA-256 `1030F86E0956AD9655EDD660BCD0E25360B088C038D7B1785271F2EC0A3DBA35` 一致，旧第19轮DLL已备份。未提交推送，运行态命中、覆盖、开销与原因判定待用户实测。

#### 第20轮分析与第21轮预测误差归属诊断

2026-10-14：第20轮16个完整窗口、364次origin Reset全部父归属有效；119次预测恢复父caller client+17C57C与丢first interval总数相同。五次地板→墙面首次落地前、事务两tick后的82条有效空中Reset均来自该入口；175条origin空中事件都夹取、原生插值变化0，而普通下落及墙门跳跃对照正常。速度对照未完全匹配、落地时点为已输出main-view观测，保留限制。见 [第20轮分析](portal-floorwall-test20-prediction-reset-source-analysis.zh-CN.md)。

官方和本机预测恢复已经有TimeBase−tick临时时基，不应再减tick。新增关键未知是本机 `cl_pred_doresetlatch=2` 强制恢复分支，以及玩家或其它predictable报错；“玩家history被清”不能直接等于“玩家origin报错”。第21轮仅接入真实Prediction root、实体PostNetworkDataReceived及其TransferData自然调用，完整body/跳转表/ABI/caller验证，读自然参数/返回，不读私有预测buffer/datamap字段，不主动再比较。记录真实Reset时的ConVar模式、全部模式直方图与有界实体/错误数归属；unknown不当0错误，计数先于明细压缩。

仍有新的区分信号，按用户继续授权实施诊断；本轮无行为修复和主观改善承诺，无新cvar/bind。保持已有穿越基线及Reset证据，不回拨插值时间或禁用合法恢复。只有运行态确认实际错误实体/触发模式才继续字段级研究，入口不符或无新信号就停止因果补丁。完整分号测试与停止条件见 [第21轮诊断说明](portal-native-prediction-error-owner-diagnostic-test21.zh-CN.md)。

第21轮交付：独立增量审查发现的诊断生命周期污染与prelude错误挤出问题已按RED→GREEN修正，修正后28/28离线runner和Debug x86构建通过，完整原生body/跳转表/重定位与日志格式检查通过。2026-10-14备份部署 `pe-6ACE7636`，源/目标SHA-256 `8C722B32A8A4AED5448E409EA656F208DE5008D9D10DF4E4053F14EDFBAD949A` 一致。未提交推送/启动游戏；离线契约不替代运行态根因、生命周期和开销验收。

#### 第21轮验证与第22轮原生字段报告诊断

2026-10-14第21轮schema8/Hook验证成立；剔除1个提前重新arm的普通下落部分窗口后，16个完整窗口5,283帧、2,552次实体比较，124次正误差全部来自本地玩家，两个viewmodel/武器均0。379次origin Reset实际模式全部1，排除强制mode2；89条中后段空中地板门Reset之前，同frame/tick都有玩家自然误差，195条origin事件全部夹取不变化。已将“玩家误差→原生恢复→历史区间丢失→tick夹取”推进到实体层，尚未识别字段，不能把被重置的origin认作报错字段。数据和覆盖限制见 [第21轮分析](portal-floorwall-test21-player-prediction-error-analysis.zh-CN.md)。

新增静态结论：本机cl_showerror0使用首个误差即停止的fast-path，因此errorCount1不等于只有一个坏字段。第22轮采用原生cl_showerror2诊断因子，由引擎自己报告具体字段；在已验证自然错误比较scope内捕获格式化文本。完整tier0 LogDirect导出/成员指纹、自然rel32、重定位、全局对象和ret16 ABI验证；各原函数参数/返回不改，自然执行一次，epoch/本地身份/嵌套scope/日志重入隔离。无私有predictionbuffer/datamap读取、listener/response-policy改变、DLL ConVar写入或新cvar/bind。

有界消息/字段计数先于48条明细，16个index/class/field桶及固定prelude；unknown不能当0错误。旧Reset和实体归属仍有用，因此保留；没有恢复证伪的行为补丁。仍有新的区分信号，按用户授权直接编码；若没有安全字段报告或归属不符则停止，若持续字段可见再研究其官方同步约定。本轮不预期主观改善，原生详细报告开销不作为性能A/B。完整分号测试和恢复配置见 [第22轮诊断说明](portal-native-prediction-field-diagnostic-test22.zh-CN.md)。

第22轮交付：独立增量审查的原日志listener重入归属问题按RED→GREEN修正，修正后28/28测试、Debug x86构建通过；完整tier0指纹/重定位/独立fixture及日志预算核对通过。备份部署 `pe-6ACF1352`，源/游戏DLL SHA-256 `F34D877C7C9160802399389C44F477BDE07E32B383C4A8914565E2F7015CF874` 一致。未提交推送或启动游戏，等待原生字段报告实测；格式化文本证据不等于私有typed字段读取或主观修复。

### 第22轮字段结论与第23轮下落状态边界

2026-10-14第22轮字段探针真实生效。9,675,949 bytes / 19,270行流式统计，272条native字段报告全部解析，positiveWithoutNativeField/unknown/字段溢出均0。首次地板卡住的capture12单独排除；墙门capture10是superseded部分窗口；之后6个正常地板窗口中，主结论保守使用最后5个。97条有效中后段空中Reset明细全部先有同frame/tick的m_flFallVelocity误差，219条origin空中事件全部夹取、变化0。网络fall保持入门高速下落记录，pred随出口轨迹更新；普通下落/墙门没有持续fall误差。详见 [第22轮分析](portal-floorwall-test22-native-fall-field-analysis.zh-CN.md)。

官方PlayerMove在ground为空时按运动开始Z更新fall；TeleportTouchingEntity走清ground/物理位置/Teleport，不单独清fall。不能任意清0、禁用Reset或把当前值当ack槽。下一步先区分服务端真实fall/ground更新、L4D2语义差异与网络/命令槽错配。旧ServerDataMap描述头与本机L4D2现代格式有差异，第23轮只新增隔离只读decoder，不改旧写入层；原生简单getter、root/embedded/class/type/尺寸/PTR/offset边界校验后，记录server命令前后和client运动前后，计数先于四phase各48条明细。无新cvar/bind/Hook/行为补丁。完整分号测试、判据和停止条件见 [第23轮说明](portal-native-fall-state-diagnostic-test23.zh-CN.md)。

这条路径仍有可证伪的问题可验证；若binding失败，先补ABI证据；若服务端ground条件与官方不符但setter能力缺失，则明确报告缺口；若字段已一致仍有重复Reset，则停止fall补丁假设，重新评估。不把诊断交付当游戏修复验收。

第23轮交付：独立增量审查无P1/P2，客户端查找的P3防御缺口按RED→GREEN补齐；最后修改后28/28runner、Debug x86构建通过，新增日志预算及参数数核对通过。备份后部署build `pe-6ACF8AAC` / `2026-10-14 21:59:08`，源/游戏DLL SHA-256 `5FCCC3BF7E3DF7AF20F03CF7682323DC786935ADE37ADF7BE3311F651F51F135` 一致。无提交推送或游戏启动；真实字段读取、调用阶段配对和开销仍等实测。

### 第23轮实测：诊断开销与字段绑定未达标

17个完整schema10窗口中client fall17/17绑定成功，server只有getter验证成功，fall0/17绑定、1,360次样本全部unknown，不能推断真实server fall/ground。新增全场景短顿和17/17首次绑定后的下一frame dt0.100000相关；旧第22轮普通下落无此大帧间隔。窗口开始Reset清绑定cache及逐字符VirtualQuery设计是首嫌，需要一次性计时证据，不能由dt直接解释500ms墙钟时间。普通下落origin推进但也出现大帧间隔，说明新增性能扰动应与旧地板持续预测误差分离。

旧地板场景110条有效空中Reset明细前仍有fall误差；三个记录到正常落地的窗口46条同型链路、104条origin夹取不变化。但server字段失败令本轮原目标未完成。下一步仅修诊断：身份生命周期cache、有界块式读取、一次性耗时/失败阶段、失败server行压缩；拿到有效server字段且消除新短顿之后，才能继续官方fall/ground路径。未修改代码/DLL或构建部署。详见 [第23轮实测分析](portal-floorwall-test23-diagnostic-overhead-and-binding-analysis.zh-CN.md)。

### 第24轮实施：降低诊断重复扫描开销

用户确认测试手法不变，按第23轮结论先修测量工具，不改变fall/ground/传送/速度/镜头。capture只清窗口counter；绑定按entity/vtable/module/root header缓存成功与失败。字符串16字节有界块读取、同步绑定内可读region缓存，跨帧与实体取值仍检查可读性。一次性QPC扫描/分段时间、read/query次数、具体失败stage/index/class/descriptor/类型/尺寸/text原因与单条探针日志耗时；缺失fall明细压缩但全部unknown计数保留。schema11，无新cvar/bind/Hook，不放宽字段守卫或采用静态offset。完整指令及继续/停止判据见 [第24轮低扰动诊断](portal-native-fall-state-low-overhead-test24.zh-CN.md)。真实server值未可靠读取前，暂停物理修补；新增约半秒帧卡顿与旧地板出口粘滞仍分开验证。

第24轮交付：唯一审查无P1/P2，P3失败归属日志按RED→GREEN修正；最终28/28 runner、Debug x86构建、日志参数/预算检查通过。已备份部署build `pe-6ACFB3BD` / `2026-10-15 00:54:21`，源/游戏DLL SHA-256 `A7712D8AC49E1FB8D05E4FADA4EDB3511F5A62172324F48A23DA3BBFF6782748`一致。只交付低扰动只读诊断，真实服务端字段及卡顿恢复待实测；未启动游戏、commit/push或切换分支。

### 第24轮实测与第25轮动态名称诊断

第24轮build/schema正确，19窗口含2普通下落、12墙门类、5地板→墙面，后者全部完整；一个墙门窗口superseded不计完整。绑定epoch6各只扫描1次、后续各1513 hits；首次server/client扫描0.0274/2.0509ms，支持新增普遍卡顿已解除。单次偶发卡顿按用户要求暂不追查。旧地板粘滞仍有105条fall误差，95条有效延迟空中Reset明细均关联同帧同tick的fall差异，242条同期origin明细夹取且未变。真实server1514次取样仍全部unavailable，不能据此直接清fall或改ground。

失败点终于明确：CTerrorPlayer首个FIELD_VOID/FTYPEDESC_FUNCTIONTABLE条目名称位于server镜像外，诊断器在读任何字符之前拒绝。SDK的DEFINE_FUNCTION_RAW→CDatadescGeneratedNameHolder::GenerateName确实用new char[]生成名称；名称内存不必归属server镜像。第25轮只分离结构/文本reader：结构仍镜像内、名称经可读性及64字节ASCII/NUL校验；不放宽owner/type/size/PTR/offset，不采用静态实体偏移。保留低扰动缓存/计时/压缩，增加一次性externalTextBlocks证据，schema12，无新操作指令。回归先复现模块外函数名称失败，再验证允许名称但仍拒绝模块外结构及坏字符串。

这仍是有明确源码与失败地址支持的诊断推进，不是视觉修复验收。下一轮取得server-pre/post后，先核对同cmd/ground条件/网络ack时序，再判断是否存在可照官方修复的状态差异；若状态已一致而Reset持续，停止fall单字段假设。详见 [第24轮分析与SDK依据](portal-floorwall-test24-cache-and-dynamic-name-analysis.zh-CN.md) 和 [第25轮完整测试说明](portal-native-datamap-text-diagnostic-test25.zh-CN.md)。

第25轮交付：28/28 runner、Debug x86构建、日志参数/长度检查通过；唯一只读增量审查未发现P1/P2/P3。已备份第24轮DLL并部署build `pe-6AD0F6DA`，时间 `2026-10-15 23:52:58`，源/游戏SHA-256 `10BDF8A3524CE8D0619CE917A329DFF9EBC6064C0223EFA6014BA7C0FE82C474`一致。未提交推送或启动游戏；下一步仍等待运行态server绑定和同cmd状态证据，不宣称旧视觉问题已修复。

### 高速连续性第25轮：服务端fall绑定成功，转向更新阶段定位

见[本轮分析](portal-floorwall-test25-fall-update-path-analysis.zh-CN.md)与[第26轮诊断方案](portal-native-fall-update-path-test26.zh-CN.md)。16个完整窗口，1280/1280 server取样有效，两端各扫描一次。地板→墙面116组完整空中同cmd样本中server保留入门前fall，而client按出口竖直速度刷新；102次延迟Reset全部有同frame/tick的fall误差前驱。

关键修正：本机server/client PlayerMove都有max(oldFall,-mvZ)分支，不是仅服务端采用max。官方PreThink会先更新fall；client的对应静态函数也有该写入。服务端是否在此阶段缺失更新、或后续覆盖仍需边界诊断。不能把fall当作玩家即时速度，不能把client ProcessMovement后未回写的实体velocity当作命令最终速度。

第26轮仅替换client前后边界并增加已有PlayerMove/FullWalkMove入口观察，schema13，无新Hook/开关/游戏状态写入。主观预期不变；判定条件和停止条件见方案，不以增加补偿或屏蔽原生Reset替代根因验证。

第26轮交付：28/28 runner、Debug x86、日志参数/长度检查通过；只读增量审查无新问题。已备份旧DLL并部署 `pe-6AD109A3`，时间 `2026-10-16 01:13:08`，源/目标SHA-256 `0B6E39B29CBEC1479F1A72FF30780B1F3D28682F01BA36C25BBCA9064B399A34`一致。未提交推送/启动游戏，等待用户验证阶段覆盖及更新路径。

### 高速连续性第26轮：更新不对称已经定位，待确认单因子修复

见[本轮分析与修复前决策](portal-floorwall-test26-fall-update-asymmetry-analysis.zh-CN.md)。15个完整窗口中包含6次地板→墙面，无需补测。八阶段读取有效；111个完整postcommit空中命令在command-pre时fall一致，client在PlayerMove入口前更新为当前-vz，server未刷新，之后native max分支保留入口峰值。两端起始MoveData.z和命令结束velocityZ均一致；普通下落/墙门90个空中命令未出现这种最终fall分离。

105次延迟Reset全部有fall误差前驱，263条延迟origin详情均夹取不变。说明已值得提出fall状态同步单因子实验，但不等于主观根因最终闭环，也不证明server PreThink完全没被调用。fall还影响落地冲击/伤害，现有绑定仅可读，原生写入及网络通知仍须独立验证。

按照用户本轮“如需要修复或变更测试，先告诉我再决定”的要求，本次只记录结论及建议，未改C++或重新部署。后续建议以官方fall=-当前MoveData.z语义为目标，验证合法时点/通知后A/B；不使用归零、时间恢复期、velocity/movetype修改或屏蔽Reset。待用户确认。

### 高速连续性第27轮：官方 fall 语义与原生通知单因子实验

用户已批准修复实验，见[实现与完整A/B方案](portal-native-fall-write-test27.zh-CN.md)。只新增 `portal_server_fallfix`，默认0。运行态datamap与完整原生指纹交叉校验后，在基类PlayerMove实际fall更新处维护 `-MoveData.z`，保留负值；复用 m_Local 原生通知再写float并读回，不裸写网络状态。

作用域为本地已确认穿越至原生落地/死亡/身份或运动类型失效，独立于诊断窗口，无定时恢复期；不改实际origin/velocity/ground/movetype/camera/Reset。入口是精确caller过滤的GetGroundEntity observer，原getter调用及返回不改。外层CTerrorGameMovement不等于基类更新函数，已用磁盘RTTI及完整字节块避免错误定位。

本轮是因果A/B，不是游戏验收：先验证通知/写入/读回，再核对fall预测误差→Reset→origin夹取链与主观症状。29组测试和Debug x86构建通过；最终审查、部署记录见测试文档。

第27轮交付：只读增量审查无Critical/Important；旧DLL已备份，build `pe-6AD1B572`（`2026-10-16 13:26:10 +08:00`）已部署游戏根目录，源/目标SHA-256 `EE512EF9C4CE0A765ABEAE62B7F0292022076EAE507290B8867F37DA109D1B4B`一致。未提交推送或启动游戏；保留原八阶段诊断作因果对照，等待用户A/B，不宣称游戏问题已修复。

### 高速连续性第27轮实测：fall单因子因果验证成功，转入基线回归

详见[本轮完整分析](portal-floorwall-test27-fall-write-ab-analysis.zh-CN.md)。build正确，22窗口完整。A地板→墙面7次中capture9/15无server-walk-pre及fall caller调用，符合卡住异常，单独排除；其余5个可分析窗口对比B5次。B原生通知/写入/读回120次全部成功，110/110个同cmd空中样本fall一致，延迟空中fall误差/Reset为0，127条origin明细全部正常插值变化。A则105/111个fall不一致，100条延迟fall误差、至少95条相关Reset，222/229条origin夹取。

B后3次重放过门，不能声称全组同门位；更严格的同门位自然落地A4次/B2次仍为72/72不一致对40/40一致，68对0延迟Reset，158条夹取不变对47条全部变化，因果结论不依赖异常样本或门位变化。用户主观已感觉不到粘滞。由此确认本场景主要原因是入口fall峰值残留→两端预测分离→重复Reset→origin历史夹取，而非实际速度被本次修复改动。

边界仍需保留：B每次提交后首tick有一条fall比较误差，既有前2tick/落地Reset未全消除；无主观症状不追加补偿。文件内墙门回归全部在开关0，开关1后的墙门、上下高速循环及穿越后落地安全需补验收。普通下落开关1无穿越绑定/写入，作用域未外溢，但不替代伤害验证。下一步用现有DLL补回归，再默认化、压缩完成使命的诊断、修正reason残留、审查累计修改并经授权提交；本次仅分析和文档，不改DLL。

### 第28轮回归及默认基线候选

[第28轮分析](portal-floorwall-test28-baseline-regression-analysis.zh-CN.md)继续支持fall修复：53次绑定成功、260次原生通知/写入/读回，拒绝0；常规地板→墙面95/95空中配对一致，无延迟fall/Reset链。墙门及普通下落未见该链复发；用户关闭无敌后确认掉血正常。上下循环37次，仅到约2706.7后死亡，不能扩展为本轮3500验收；反向墙门动作覆盖也需补齐。

候选版本只默认开启fallfix、区分当前停止原因与上次绑定原因、增加默认关闭的 `portal_diag_native` 重型取证开关；保留成功摘要、失败原因及全部能力验证。按[第29轮短回归](portal-fall-baseline-candidate-test29.zh-CN.md)确认诊断关闭不改变功能后，再审查累计工作区并提交。

另行记录放置问题：部分NoBrushFound/NonWorldHit导致PairUnresolved、carvingActive=false，显示门不等于物理brush已绑定。它发生在传送之前，与本轮fall状态修复分开；没有擅自回退到全局碰撞绕过。

### 第29轮：默认候选核心验收与技术资产

[第29轮分析](portal-floorwall-test29-candidate-acceptance-analysis.zh-CN.md)确认build/hash正确，419次commit/绑定/摘要闭合，342次原生通知/写入/读回全部成功，拒绝0。重型诊断开关关闭/开启/再关闭各有120/95/127次成功写入；详细地板→墙面87/87同cmd空中fall一致、延迟fall误差/Reset0，101条origin选样全部正常变化。普通下落无新写入，用户主观正常无新增异常。

上下循环336次提交速度为3500，剔除切到非WALK的尾端txn414后，正常作用域335次、约12.1秒；足以覆盖原定5秒上限测试。反向墙门跳跃/高速和出口立即落地不能从日志逐项确认，但2026-10-18用户已明确回复“两项都测过且正常”，人工覆盖关口已满足，无需重测。

新建[有符号fall技术资产](portal-signed-fall-continuity-technical-asset.zh-CN.md)记录官方语义与L4D2接缝差异、通知/ABI验证、负值、生命周期、回退和性能；[累计审查](portal-fall-baseline-worktree-audit.zh-CN.md)区分本次功能、必要诊断、历史探索和不纳入的其它研究。29/29测试及Debug x86构建复核通过，已测DLL哈希未变。本次验收收尾仅文档/证据工具整理，不改变游戏逻辑、未部署新DLL；人工确认后按已授权计划进入重大基线提交/推送。

重要限制：过程寿命Hook尚无统一热卸载通路，换DLL必须完全退出游戏；DllMain恢复brush不等于全部Hook已拆除。该限制以及诊断内存校验重复、活动scope切换的集成测试缺口均单独记录，不混入已验证fall算法。

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
