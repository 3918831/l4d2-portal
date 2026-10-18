# Portal 连续性修复：面向后续复刻方案的技术资产索引

日期：2026-10-18。所属基线：`codex/bsp-portal-traversal`，累计代码/证据提交 `eec85ad`。

## 1. 资产与方案分开

当前产品路线是 BSP whole-brush 清空，让原始 movement 消费真实可通过空间；`codex/portal-official-mechanism` 是另一套独立方案。这份资料用于复用研究结论，不表示两条方案已经合并，也不要求后续方案沿用 whole-brush、shadow predictor 或当前 Hook 插入点。

最重要的积累不是“多加一个平滑补丁”，而是从官方源码提取状态/时序契约，再用 L4D2 原生运行态证据定位差异。**症状在相机，根因可能在参与预测的非位置状态；实际速度正确不代表渲染插值历史不会被纠错重置。**

## 2. 官方语义、当前适配与可复用边界

下表汇总既有研究/实测，不宣称新方案已通过测试。源码函数名是检索锚点；完整依据和历史证据以链接文档为准。

| 问题层 | 官方语义/参考锚点 | 本工程已获得的资产 | 新方案不能直接照搬的部分 |
| --- | --- | --- | --- |
| 物理提交 | server `prop_portal` 最底层实体 `Teleport` | 精确门对 origin/velocity/angles 变换；不额外出口push | whole-brush 开口、crossing 时机和空间锁不是完整官方 environment |
| 相机和历史 | `C_Portal_Player::PlayerPortalled`、`UpdatePortalEyeInterpolation`、`CalcPortalView` | 区分物理位置、眼位历史、render eye、PVS采样点；一次变换、同空间比较、自由移动增量 | shadow predictor 是工程适配；单眼位实验默认关闭，不能将其当已验收的完整官方复刻 |
| 命令空间 | 穿越后客户端/服务端命令空间交接 | transaction/cmd 对齐的一次性角度处理；后续命令不能沿入口方向解释输入 | 原生槽位/Hook和命令发布时机必须重新验证，不能持续改movement兜底 |
| 回正 | Portal 普通 roll 回正状态机研究 | 输入时序、角度维度、回正速度曲线与原生入口验证 | 当前含用户批准的加速时基，并未实现全部官方pitch/upside-down/private interpolator语义 |
| 极近渲染 | Portal near-plane proxy、clip plane、`CIndexBuilder` | 深度/stencil/viewmodel生命周期和16位index元素步进证据 | 当前管线有特定depth clear/frame抑制补丁，不能代替另一渲染器的完整状态恢复 |
| fall预测状态 | shared `gamemovement.cpp`、server `player.cpp`、`networkvar.h` | airborne时有符号 `-MoveData.z`；原生通知→存储→读回；误差→Reset→origin夹取因果证据 | L4D2精确getter caller、二进制指纹/offset是适配，不是通用SDK接口 |

入口阅读：[BSP与连续性总路线](portal-bsp-physical-traversal-and-continuity-technical-summary.zh-CN.md)、[早期物理基线复盘](portal-bsp-phase1-stage-retrospective.zh-CN.md)、[官方客户端交接研究入口](handoff/portal-visual-continuity-next-session.zh-CN.md)。长篇总结中实验按时间排列；后续证伪/删除记录优先于早期“下一步”建议。

## 3. 本次地板→墙面问题的可迁移结论

1. 门对变换可以正确保留实际速度，但旧入口fall峰值仍残留在服务端；出口向上时正确fall甚至应为负，清0/取绝对值不等价。
2. 原生预测比较发现fall不一致后，可能重置玩家origin插值历史。因此“看到origin历史被清”不能直接推断origin报错，也不能通过禁用Reset修复。
3. 必须先做同cmd、同阶段比较。第26轮八阶段证据确认：两端起始MoveData.z及最终velocityZ一致，client在PlayerMove前刷新fall，server没有相应刷新；两端后续均有max保峰逻辑。
4. 第27轮单因子只修合法作用域内的fall更新并调用原生通知。严格同门位自然落地子集：A为72/72空中cmd不一致、68次延迟Reset、158条origin夹取不变；B为40/40一致、0次延迟Reset、47条origin正常变化。结合主观症状消失，建立因果而非仅相关。
5. 第28/29轮再验证默认开启、诊断开关隔离、墙门双向、普通下落、立即落地伤害和3500循环。第29轮87/87详细空中配对一致、342次通知/写入/读回、零拒绝；正常作用域335次3500提交约12.1秒。人工确认与日志量化结果分开记录。

完整契约见[有符号fall技术资产](portal-signed-fall-continuity-technical-asset.zh-CN.md)，证据见[第26轮](portal-floorwall-test26-fall-update-asymmetry-analysis.zh-CN.md)、[第27轮](portal-floorwall-test27-fall-write-ab-analysis.zh-CN.md)、[第28轮](portal-floorwall-test28-baseline-regression-analysis.zh-CN.md)、[第29轮](portal-floorwall-test29-candidate-acceptance-analysis.zh-CN.md)。

## 4. 复用诊断方法，而非复制全部补丁

建议后续方案按以下顺序接入：

1. 先确定源码不变量和所需接口。原生路径已经维护的状态不要重复写；API/ABI未验证就明确缺口。
2. 以transaction、cmd、frame、阶段和实体身份关联入口/出口状态；区分实际velocity、fall、raw eye、最终view和origin历史。
3. 保留原生调用与合法Reset，追踪谁触发纠错、哪个字段报错、历史何时丢失。只读证据不足时先补诊断，不直接加相机补偿。
4. 能力绑定采用完整指纹、字段元数据和运行态归属交叉验证；错配拒绝，不以固定RVA/offset作fallback。
5. 单因子A/B使用相同门位和运动条件，首次预热、卡模型、死亡/非WALK、人工放门高差单独分类；高速循环用自动事务记录，不依赖来不及按的主观标记。
6. 原症状通过后必须回归旧场景。记录伤害和作用域终止，关闭详细日志仍应修复；避免日志本身的同步输出/逐字节查询造成新卡顿。

可复用文件入口：

- [fall策略测试](../tests/PortalFallWriteTests.cpp)：signed值、通知失败拒绝、读回、生命周期和能力边界。
- [原生cadence测试](../tests/PortalNativeCadenceTests.cpp)与[插值测试](../tests/PortalNativeInterpolationTests.cpp)：有界元数据/指纹/选样契约。
- [流式证据重放](../tests/audit_portal_fall_evidence.cjs)与[第29轮小型证据](evidence/portal-fall-test29-acceptance.json)：大日志按窗口、身份、阶段归并；schema13单段输入，不把unknown当0。
- [累计审查](portal-fall-baseline-worktree-audit.zh-CN.md)：功能必需的Probe不可误删；重型取证与修复开关必须独立。

这些测试不是运行态Hook验证或游戏实测的替代品；复制到另一方案后，需要针对新的接入时机重建证据。

## 5. 不应带入下一方案的结论

- whole-brush清空不等于官方孔径内碰撞；局部开洞和偶发NoBrushFound仍是独立问题。
- 第二层convergent shadow-tail虽消除了终端残差，却未解决症状并曾劣化墙门，已删除；不作为备用补丁恢复。
- ground setter探索没有证明它是本次粘滞的修复因子；官方存在相似函数不等于可直接调用未知槽位。
- 本次fall修复没有完整移植PreThink，也没有证明server PreThink整体缺席；修的是经证据确认的状态更新不对称。
- 首tick或落地仍有合法预测纠错，不以所有日志归零为成功标准。
- 单人localserver、当前二进制和已测几何之外不自动成立；热卸载、远程服务器、多玩家和任意实体仍未验收。

结论：**基线留在BSP分支，官方语义、可证伪方法和实测证据成为跨方案资产。** 新方案按契约选择复用；不把分支整体合并当作完成官方复刻。
