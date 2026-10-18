# 第 18 轮分析：真实选样正常执行，但空中 origin 历史退化为 Reset-like 形态

日期：2026-10-13。输入：`TestLog/高速穿越连续性修复/18/portal_l4d2_traversal.log`。本轮仅分析、离线流式重放和文档；未修改产品代码、构建、部署、提交或推送。

## 1. 结论

测试有效，新增探针已实际命中。本轮把第 17 轮的“origin 可参与却不产生变化”解释到更具体的一层：**地板→墙面首次落地前，原生选样成功，但 available history 没有不同时间/位置的有效区间，选样持续夹到最末端，因而 origin 插值调用不能产生帧间运动。**

这不是 watcher 未执行、不是空 history，也不是 GetInterpolationInfo 返回 false。普通下落与完整墙门空中对照的 amount 相同，历史区间正常，有原生位置插值。地板门在回正、eye State/shadow 都结束后仍保持退化；相机追赶尾巴不是这段位置阶梯的解释。

history 的 count=3、capacity=16、head=13，读取到的首/末样本时间和位置一致；时间及位置又随每个 tick 一起推进。这与官方及本机原生 Reset 重新生成三份当前值的形态高度吻合。**还没有直接观察 Reset 的调用，不能把“持续 Reset”或其具体调用者当成已证明根因。**下一轮应直接追实际 origin watcher Reset 的调用者和重建前后历史，而不是继续改相机或选时常数。

## 2. 有效性、覆盖与阅读方式

日志 9,247,892 bytes、18,654 行。流式脚本 `TestLog/高速穿越连续性修复/18/analyze-origin.cjs` 按 capture/frame/prelude 关联有限事件行，Summary 提供全部接受帧计数。没有整份读入模型，也没有把事件样本当作全帧计数。

```powershell
& 'C:/Program Files/nodejs/node.exe' 'TestLog/高速穿越连续性修复/18/analyze-origin.cjs' --assert
```

输出 `verdict=diagnostic-valid`、`floorDuplicateClampStaircase=true`，退出码 0。再以 `--expect-continuous` 执行同一捕获证据检查，退出码 1，稳定指出“有效地板门空中，端点重复、夹取、无原生变化且最终视图冻结”的反例。它是本轮捕获机制的 agent-runnable 判别器，不是自动运行游戏、主观体验或修复成功的回归测试。

- 第 3 行 build `pe-6AC92621`，与第 18 轮部署身份一致。
- 第 76 行 Origin Hook：installed、selectionInstalled、constructor/type/inner ABI/selection ABI/ring layout/live binding 全为 true；caller RVA 与已验证链一致。
- 第 646 行 singleeyehistory=0；第 649 行 Phase1 关闭，第 1816 行开启；两次普通下落分别在第 685、1231 行 armed。
- 共 21 个有四层 Summary 的窗口：normal-fall 2、portal-other 14、floor-wall 5。
- 19 个 capture-complete；墙门 capture 8、12 提前被下一事务替代（superseded）。它们有截止时汇总，但从完整窗口空中对照剔除。五个地板门和两个普通下落均完整，因此无需重测当前 DLL。
- 6,734 接受帧，outer/Part1/map 各自然调用 6,734 次，Part1/map invalid=0；entry-local/oldOutputs 对照 6,734 次一致。
- origin watcher 和 selection 各实际调用 6,402 次；origin/selection invalid、hookUnavailable、eligibleNoCall、selection missing/empty/returnedFalse 均为 0。
- origin changed=5,309、noChange=1,093；选样 clamped=1,081。全窗口统计包含落地后运动，不能用总 changed 掩盖异常空中段。
- 阶段别名解析缺失为 0；全窗口事件预算均没有 suppressed 或 commandRowsDropped。

覆盖限制：墙门正常空中速度约 220–320，仍不是 650–870 的完整高速匹配；普通下落达到约 690，且同样没有退化。floor capture 18/19/20 在首次落地后各有 7/10/6 条 MoveType=8 空中事件，已剔除；capture 17/21 没有这项。不得混合落地后的额外移动模式。服务器命令日志有 origin/velocity 的 stateValid=false，所填的零不能用来声称速度清零；本轮主要位置结论来自已验证的 client-native origin/selection。

## 3. 首次自由飞行段的直接证据

筛选：非 prelude、事务开始两 tick 后、第一次观测到 onground 前、MoveType=2。以下为有界事件样本：

| 场景 | 有效空中事件 | watcher 实际调用/读取有效 | 原生 origin 变化 | 夹取最末端 | 正常 interval/Hermite |
| --- | ---: | ---: | ---: | ---: | ---: |
| 两次普通下落 | 49 | 49 | 49 | 0 | 49 |
| 完整墙门空中窗口 | 56 | 56 | 56 | 0 | 56 |
| 五次地板→墙面 | 208 | 208 | 0 | 208 | 0 |

普通下落：count=6，head 正常循环；newer/older 时间跨度约 33.333 ms，有 Hermite；effective 位于区间内。墙门：count=4–6，跨度同样约 33.333 ms。地板门：count 恒为 3、head 恒为 13，newer=older=index2；首样本 index0 与选中的 index2 时间/位置完全一致，跨度为 0，frac=0、Hermite=false。

**精度边界：index1 本轮没有被实际选择和读取。**不能宣称已经逐项证明“三个样本全相同”，当前直接证明的是 count=3、首末重复、实际选择区间退化。下一轮重建探针应补读三个有效条目（仍有界）。

所有场景实际 interpolationAmount=`0.0333333351`，不是 GlobalVars 的归一化帧比例。地板门 effective 比被选样本早约 0.427–33.310 ms；由于没有更早的有效区间，原生逻辑一路走到最后一项并夹取。noMore 多为 0，needs 仍为 1，符合“继续需要更新”但不代表该帧位置已变化。

在这 208 条事件里，newest 时间都等于 simulationTime，newest 值都等于 watcher 调用前本地 origin。五组各自相邻有效 tick 的样本时间均前进约 33.333 ms，首末样本位置一起前进约 21.83–28.87 units，而 head/count 保持不变。这不是一份历史长时间完全冻结；形态更像每次更新都重建当前值、未保留上一 tick 的有效时间邻域。

回正 roll 已归零且 eye State/shadow 均结束后仍有 120 条有效空中事件，120 条依旧夹取/无原生 origin 变化，其中 70 条最终视图冻结；其余帧表现为 tick 跳变。local/outer 变化状态不一致=0，不支持“watcher 已写但 Part2 丢失”。

| capture / txn | 有效空中事件 | 首次 onground 相对 tick | 首次落地后观察到 watcher 变化 tick | 最终视图累计冻结 / 最长单段 ms |
| --- | ---: | ---: | ---: | ---: |
| 17 / 15 | 38 | 20 | 21 | 333.057 / 31.867 |
| 18 / 16 | 43 | 22 | 23 | 379.752 / 31.270 |
| 19 / 17 | 47 | 21 | 22 | 342.914 / 31.992 |
| 20 / 18 | 46 | 19 | 20 | 274.666 / 33.121 |
| 21 / 19 | 34 | 19 | 20 | 290.520 / 31.538 |

恢复 tick 来自已输出事件的首次观察，不是精确全帧因果时点。累计冻结不是一次暂停 0.3 秒；最长单段约一个 30 Hz tick。落地与恢复相关，但尚未证明落地函数本身负责修复历史。

可复核的局部证据：

- 第 755–776 行：普通下落的真实选样 amount、不同时间样本和 watcher 非零位移。
- 第 12570、12581 行：floor capture17，count3/head13、index0/index2 同时间同位置，effective 早于样本，原生变化=0。
- 第 12864–12882 行，frame37401/37402：phase Idle、roll0、State/shadow 已结束；第 12871、12882 行仍有原生 clamped 选样；后一帧最终视图位移0。
- 第 12899、12910 行，下一 tick frame37411/37412：样本时间已由166.266678推进到166.300003，位置跳约26.76 units，但首末仍重复，count/head 不变。不是 currentTime 不推进。
- 第 13195、13212 行：落地后区间和 Hermite 重建，count5→6，watcher 重新产生实际 origin 变化。
- 五个 OriginSummary 位于第 13477、14763、16105、17444、18595 行。

## 4. 对照官方及本机实现

本轮完整阅读本地 Portal SDK 2013 对应函数，来源路径可直接复核：

1. `Reference Code/portal-sdk-2013/src/game/client/interpolatedvar.h:740`，`Reset()` 清历史，再以同一 curtime/current value 连续 AddToHead 三次。这用于不连续恢复，不是正常持续运动历史。
2. 同文件 `:627`，`NoteChanged()` 正常追加当前样本并清旧项；同文件 `:799`，`GetInterpolationInfo()` 以 currentTime−amount 寻找区间，目标早于全部样本时夹到末端；同文件 `:968`，Interpolate 使用实际选择并返回 noMore，不保证产生位置变化。
3. `Reference Code/portal-sdk-2013/src/game/client/c_baseentity.cpp:5829`，实体 Interp_Reset 遍历 watcher->Reset；ResetLatched 是上层入口。
4. `Reference Code/portal-sdk-2013/src/game/client/prediction.cpp:1748`，SetViewOrigin 直接重置 origin interpolator，不必经过实体 ResetLatched。因此上层未观察到 ResetLatched 不排除具体 origin Reset。
5. 同文件 `:1431–1457`，预测错误恢复明确说明：三份同一预测时间戳会导致 jerky movement；官方把重置时间减一个 tick，为后续正常 latch 留出时间邻域。这个注释解释当前形态为什么有害，**不能据此直接认定 L4D2 正在走这一分支或直接套用减 tick 修复。**
6. `Reference Code/portal-sdk-2013/src/game/client/portal/c_prop_portal.cpp:118` 起，EntityPortalled 变换 rotation/origin 的真实历史并处理玩家 origin/center 偏移，而不是以相机缓动替代实体历史生命周期。

本机 client.dll SHA-256 再核对仍为 `D2553CCA3A63BDD844864F5FEE2C20A233AB19DEE816463A90857B95309982A2`。已验证 origin 类型的 vtable slot5 对应磁盘 VA `0x1004C4C0`；只读完整反汇编120 bytes 显示：清 head/count、取当前值、从本机 GlobalVars 获取 curtime、连续三次调用已验证 writer `0x1004ABF0`，最后拷贝 current 到 last networked，void/no-stack-argument、ret。capacity16 下三次 AddToHead 的 head13 与日志形态一致。

这些是下一轮可安全校验入口的静态依据，不是运行态命中证据。仍需从实际 origin watcher 的 vtable 解析、完整指纹/ASLR 校验，再观察真实对象和 caller。

## 5. 假设排序与判别信号

| 假设 | 若成立应观察到 | 如何证伪 |
| --- | --- | --- |
| 1. 某路径在异常空中重复执行 origin Reset | 本地 origin Reset 持续命中；调用后 count3/同时间同值；caller/阶段重复且对应区间丢失；落地附近减少或改变 | Reset 不命中或仅事务初次命中，后续仍持续出现相同形态 |
| 2. 预测恢复/数据复制把 Reset-like 历史覆盖回来 | Reset 没有匹配的持续调用，历史仍在特定恢复阶段从正常区间退化 | 每次退化都能直接对应原生 Reset 调用，并无恢复覆盖证据 |
| 3. 正常 latch 的 changeTime/flush 路径异常 | NoteChanged/AddToHead 的实际 timestamp/flush 操作反复删除时间邻域，且没有相应 Reset | 正常 latch 输入与区间生成正常，退化直接来自另一重建来源 |

三者不会通过仅观察 count3 就最终区分。已排除本轮的 empty、选样失败、amount 特有变化、caller/绑定错误及 local→abs 传播丢失；render-stage effects=0x4、MoveType=2，不能把异常直接叫作持续 EF_NOINTERP 或 noclip，也不能由渲染时的标志否定其它阶段的短暂状态。

## 6. 下一轮建议：先追重建调用者，不做行为修复

第 19 轮建议只增加一个底层观察因子：**实际 origin watcher Reset 的自然调用及调用者**。

1. 从本轮已验证 live origin binding/type 的 slot5 解析目标，验证完整机器码、三次 writer rel32、GlobalVars 操作数与 void thiscall/no-stack-argument ABI；未知模块/指纹失败则明确 unavailable，不安装猜测入口。
2. 只观察当前本地 origin watcher；原样调用原函数一次。记录 caller module/RVA、frame/tick、已有 command/prediction 阶段、Reset 时实际 curtime/simulationTime/tickBase、当前 value、前后 ring 元数据和最多三个有效样本。
3. **不能把观测范围只套在 map/Interpolate 作用域。** 重建很可能在网络/预测阶段发生，早于 render-start；以既有 capture/txn 和本地对象身份关联这些阶段，避免再次出现“装上了但根因阶段没看到”。
4. 汇总按 caller、command/tick/frame 计数；明细只输出重建形态/来源变化和必要异常，受预算限制；全部调用与失败计数不受明细预算影响。不加主观 bind 或大量控制台变量。
5. 保留本轮必要的 origin selection 证据，删除冗余外层成功行与永久 unknown 文案；清理以诊断信号替代旧无效推断，不把原有只读安全检查删除。继续普通下落2、墙门回归、地板门5，每次落地后等至少2秒；指令保持英文分号。

本轮值得继续：第一次拿到正常/异常历史形态的直接分界，且重建最底层已有完整、可解析的本机入口；新一轮会回答“谁在何时丢掉时间邻域”，不是重复 eligibility。它仍不构成主观改善承诺。

### 后续门槛

- Reset 持续命中并关联具体 caller：沿该调用者与官方恢复/latch 条件核实原因，再给单因子行为修复。必须保留合法 teleport、预测错误、换图/创建等 Reset；不做“空中一律禁止 Reset”。
- Reset 仅初次命中，后续形态仍重建：此假设证伪，转入已明确的新判别路径（预测恢复或实际 writer），不在 Reset 层继续加屏蔽逻辑。
- 存在合法预测错误造成持续 Reset：先定位真实误差来源/复刻官方恢复链；不能为了平滑掩盖错误或随意改 gpGlobals 时间。
- Reset 观测无效：先修诊断/报告能力缺口，不让用户重复无效同版测试。
- 无法安全验证 caller 或无法产生新的区分信号：暂停局部补丁路线，讨论完整预测/历史生命周期复刻；不恢复相机硬推、速度补偿或私有 history 猜写。

目前无需重新测试第18轮，也不建议立即调 currentTime/amount、屏蔽所有 Reset、重新启用旧 entityhistory 私有写入，或改速度/brush/ground。官方路径的剩余差异已从相机层进一步落到具体实体插值历史的创建、保留和预测恢复生命周期。
