# 第19轮：重复 origin Reset 已证实，继续追 ResetLatched 的真正来源

输入：`TestLog/高速穿越连续性修复/19/portal_l4d2_traversal.log`。本轮结论与第20轮探针设计，不宣称行为修复。

## 结论与测试有效性

测试有效，无需重测第19轮。实际 DLL 为 `pe-6ACDCEF0`，OriginResetHook 完整指纹、ABI、writer、时钟操作数和 live binding 均验证成功。6,307,037 bytes / 13,050行通过流式脚本 `TestLog/高速穿越连续性修复/19/analyze-reset.cjs` 统计；未整体读入模型。12个完整窗口：普通下落3、墙门4、地板→墙面5，共3,670接受帧。

自然 origin Reset 共289次，invalid、unknownCaller、callerOverflow、detail.dropped 均为0；全部来自 `client.dll+0x4728E`，调用后全部生成 count3/head13 的三份当前值。115次直接丢失已有 first interval；原函数没有改变当前 value，破坏的是时间邻域。全部是 unscoped，不能据此称作网络阶段；明细呈现 prediction.in=true、insideRender=false。

| 场景 | Reset总计 | 首次落地前、两tick后空中Reset明细 | 该空中段丢失 first interval | origin有效事件：变化 / 夹取 |
| --- | ---: | ---: | ---: | ---: |
| 普通下落3次 | 3 | 0 | 0 | 68 / 0 |
| 墙门4次 | 78 | 0 | 0 | 无匹配空中段 |
| 地板→墙面5次 | 208 | 87 | 87 | 0 / 206 |

普通下落每次只有1次Reset，在首次观察到落地的tick26。墙门窗口从tick1已落地，不能把本轮当成速度匹配的墙门空中对照；它们最终视图累计冻结均为0。下一轮补真正脚离地的跳跃过门，不以当前地面对照夸大覆盖。

地板门空中每个已输出有效Reset事件均把 count4/head12、有不同时间条目的正常区间改为 count3/head13、同时间同值；87次 nativeBefore==simulationTime、after.atNativeTime=true。原生 watcher 在206个有效空中事件均夹取且没有变化，其中82条最终视图冻结。五个窗口累计冻结200.511–476.279ms，最长单段31.594–32.568ms；不是一次连续暂停半秒。

| capture / txn | Reset总计 | 丢失first interval总计 | 有效空中Reset / 丢区间 | 首次main-view落地tick | 最终累计冻结ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 8 / 5 | 37 | 21 | 18 / 18 | 21 | 351.267 |
| 9 / 6 | 38 | 16 | 12 / 12 | 15 | 200.511 |
| 10 / 7 | 44 | 22 | 18 / 18 | 21 | 361.547 |
| 11 / 8 | 47 | 25 | 22 / 22 | 25 | 476.279 |
| 12 / 9 | 42 | 20 | 17 / 17 | 20 | 318.328 |

firstGround按所有已输出main-view事件取最早样本，只是观测时点，不是精确ground setter时点。落地后其它移动模式排除。帧明细计数不是全接受帧计数，Reset总计独立于渲染接受和明细预算。

## 可复核证据与分析器边界

- 第77行：ResetHook全部验证成功；第1154行Begin version6。
- 第1439行：普通下落 capture1 唯一Reset，flags0x81；之前区间正常。
- 第6462–6464行：地板门 capture8/tick+3，Reset前count4/head12，后count3/head13；三个实际样本都重复。第6477行选样夹到index2。
- 第6501行：下一tick再次同样丢区间，证明不只初次teleport一次。
- 第7578/8891/10214/11652/13023行：五个地板门Reset汇总；按标签/字段定位，不依赖固定行号。

`analyze-reset.cjs --assert` exit0：`valid=true repeatedFloorAirReset=true`；`--expect-continuous` exit1，稳定捕获原生历史重建与夹取反例。这是捕获证据反馈回路，不是游戏自动运行或主观体验测试。

不能原样沿用第18轮分析器对outer日志的逐帧要求：第19轮已将outer成功行压缩，缺少某帧outer行不等于缺少实际origin事件。新脚本关联origin、main-view和Reset，Summary保留全部计数。旧脚本输出的 `floorDuplicateClampStaircase=false` 不是改善证据。

## 官方与本机调用链研究（Tesla只读研究结论）

官方 `Reference Code/portal-sdk-2013/src/game/client/c_baseentity.cpp:5829` 的 `Interp_Reset` 遍历VarMapping watcher并Reset；`:5842` 的ResetLatched跳过IsClientCreated后调用它。本机调用点RVA4728E位于相同形态的entity map循环，完整实现为56bytes：读取map+0x18、count+0x24、entry stride12、watcher+8、vtable+20。没有靠函数名或固定RVA推定ABI。

本机CTerrorPlayer真实slot106候选包装器为80bytes，尾部E9到该56-byte实现；slot107与既有RTTI/入口验证相邻。slot106包装器会保留原调用者返回地址，因此在base入口取 `_ReturnAddress` 可读取上游调用者，不需要猜栈或扫描栈。依赖完整包装器/base指纹、尾跳解析、map偏移与已有origin live binding验证，运行态必须重新验证。

官方合法来源至少包括：

1. `prediction.cpp:1431–1457` 预测错误恢复，保留合法Reset并减一个tick，避免同时间三样本；不能仅据时基相似认定本机走此分支。
2. `c_baseentity.cpp:3013–3033` ProcessTeleportList 的不插值/teleport恢复。
3. `gamemovement.cpp:4120/4226` 等duck完成时的ResetLatched。

第19轮尚未读到这些上层caller，更没有证明预测错误flag或teleport条件。共同底层ResetLatched只是路径归属，不是调用原因。不能空中一律禁止Reset、修改GlobalVars/amount、猜写private history，或恢复旧movement/noclip/exit push/PortalMaskRepair。

## 第20轮单因子诊断设计与继续/停止评估

值得继续：最低层重复Reset假设本轮首次得到直接证实，并且具体父路径已有可安全解析的完整本机入口。问题已从“是否丢历史”推进到“谁要求清历史”。

仅新增ResetLatched base入口只读scope：由RTTI验证过的本地slot106取完整80-byte包装器，解析尾跳到完整56-bytebase并核对map/slot5循环；自然原样调用一次，嵌套scope恢复。origin Reset继续独立计数，新增父scope是否匹配、来源模块/RVA及固定caller桶；父入口未验证或未覆盖时保留unknown，不能污染原有Reset证据。预算和总计分离，不增bind或开关。

这次用户明确授权“可继续诊断则直接编码”，按此有界设计实施，不额外等待设计确认。保留当前分支和未提交基线，快照隔离；没有新游戏行为、提交或推送。

- 若air连续Reset均关联同一合法预测恢复调用点：读取真实错误/restore/latch条件并与SDK完整函数对比，优先找误差来源或时序；不首先屏蔽。
- 若是ProcessTeleportList/其它entity lifecycle路径：核实实际判定输入为何持续成立，再设计单因子修复。
- 若父scope始终未知或指纹不匹配：先修诊断，不要求同版盲测。
- 若无新的可安全验证判别点：停止补丁堆叠，讨论能力缺口。仍没有主观改善承诺。
