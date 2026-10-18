# 第20轮：持续 Reset 已定位到预测恢复分支，下一步追报错实体

日期：2026-10-14。输入 `TestLog/高速穿越连续性修复/20/portal_l4d2_traversal.log`。本轮用户未报告新增异常。以下把直接证据、静态归属和仍未知的原因分开。

## 1. 结论与覆盖

测试有效。实际build为 `pe-6ACE5B75`；第78行ResetLatchedHook安装、80/56-byte指纹、尾跳、ABI、map layout/live binding均true。日志7,999,576 bytes / 16,146行，流式脚本 `TestLog/高速穿越连续性修复/20/analyze-parent.cjs` 按capture/frame/已输出main-view事件和Reset归属关联，不整体读入模型。

16个完整窗口：普通下落2、墙门9、地板→墙面5，共4,865接受帧。364次自然origin Reset全部parent.state4，invalid/unknown caller/parent overflow/detail dropped均0。全部重建三份当前值；119次丢已有first interval。119次预测恢复父调用与区间丢失总数相同，其余父来源主要在初次穿越恢复序列中出现；不能把所有Reset都当作错误。

| 父caller（client.dll RVA） | 全窗口origin Reset次数 | 本轮可解释范围 |
| --- | ---: | --- |
| 17C57C | 119 | 本机预测命令起点计算中的ResetLatched循环；持续空中丢区间全部来自此处 |
| 68B1E | 14 | 初次穿越相关序列，本轮不修改 |
| 4DFE8 / 3B0B7 / 692A9 | 各77 | 初次恢复序列，不据次数禁止 |

首次落地前、事务两tick后、MoveType2的明细：

| 场景 | 有效origin事件 | 原生变化 | 夹取 | 有效空中Reset / 丢区间 |
| --- | ---: | ---: | ---: | ---: |
| 普通下落 | 47 | 47 | 1 | 0 / 0 |
| 四个墙门空中窗口 | 46 | 46 | 0 | 0 / 0 |
| 五个地板→墙面 | 175 | 0 | 175 | 82 / 82 |

本轮补到了墙门跳跃对照（capture8–11），但其速度仍不等于地板门的650–900高速；不夸大为全速度匹配。地板门82条空中Reset全由17C57C触发，nativeTime==simulationTime，调用前count4/head12有不同时间邻域，后count3/head13为同时间同值。59条对应最终视图冻结；普通下落/墙门空中没有此现象。

| capture / txn | Reset总计 / 预测来源 | 有效空中Reset | 首次main-view落地tick | 最终累计冻结ms / 最长单段ms |
| --- | ---: | ---: | ---: | ---: |
| 12 / 10 | 40 / 21 | 18 | 21 | 359.986 / 32.404 |
| 13 / 11 | 34 / 15 | 12 | 15 | 201.432 / 31.853 |
| 14 / 12 | 37 / 21 | 17 | 20 | 327.434 / 32.654 |
| 15 / 13 | 40 / 21 | 17 | 20 | 334.997 / 32.555 |
| 16 / 14 | 40 / 21 | 18 | 21 | 364.638 / 32.236 |

累计冻结不是连续暂停0.3秒，而是接近一个30Hz tick的小段累计。落地时点是最早已输出main-view观测，不是精确setter时点。明细数不是全部接受帧数；汇总计数独立于预算。

## 2. 可复核证据与反馈回路

- 第9519行：capture12/tick+3，子caller4728E、parent.state4/17C57C；真实native162.53334等于sim，前count4有interval，后count3/clones/atNativeTime。
- 第9559行：下一tick再次同源丢interval。
- 第10647行及后续对应ParentCaller：五个地板窗口预测恢复父来源持续出现。
- `analyze-parent.cjs --assert` exit0：valid=true、repeatedFloorAirPredictionReset=true；`--expect-continuous` exit1，捕获历史恢复反例。它是代理可运行的捕获证据反馈回路，不是自动游戏运行或主观修复回归测试。

## 3. 官方与本机研究（Tesla只读研究结论）

主参考为本地官方Portal SDK 2013和支持版本client.dll，不以其它游戏的源码命名代替ABI验证。

官方路径：`prediction.cpp:411` PostNetworkDataReceived逐个预测实体调用 `C_BaseEntity::PostNetworkDataReceived`；任何实体返回hadErrors就置上一ack错误flag。`c_baseentity.cpp:4624` 比较服务器原始网络数据与ack-1预测帧的networked字段；`predictioncopy.h:83` / `predictioncopy.cpp:1383` TransferData返回错误数。`prediction.cpp:1373–1467` ComputeFirstCommandToExecute在预测错误恢复时Reset所有predictables，并将curtime暂置为TimeBase−TICK_INTERVAL，再恢复。

本机17C57C位于完整337-byte函数（入口RVA17C490）：slot相关状态基址为this+0x18+20*slot；循环调用实体slot106；GetTimeBase后减原生interval，再临时写curtime，循环完恢复。**这一步本机已经与官方相同，不能再补一次减tick。**本轮不直接读写该私有状态。

额外判别条件：L4D2有 `cl_pred_doresetlatch`。其本机初始化完整指向名称、默认字符串1和ConVar对象，当前恢复分支除“错误flag且开关非0”外还允许开关值2强制执行。当前日志未读该值，所以不能把17C57C的命中直接等同于“某个实体确实有预测错误”。正常空中对照不持续Reset使强制模式假设较弱，但仍要验证，不能用推测替代日志。

已安全可解析的新边界：

- I::Prediction的真实RTTI CPrediction/CCSPrediction、slot6完整597-byte PostNetworkDataReceived。
- 该函数自然call+185到完整265-byte实体PostNetworkDataReceived，调用后按bool返回设置错误flag。实体ABI bool thiscall、一个ack参数、ret4。
- 实体在call+225自然执行完整150-byte TransferData，返回int错误数后以 `>0` 生成hadErrors；ABI三个栈参数、ret12。其16-byte跳转表和绝对重定位也可完整验证。

不能把Portal SDK的CPredictionCopy私有结构直接cast到L4D2；本机实现有不同op dispatch。本轮只读自然参数/返回，不读取预测帧buffer或datamap/field私有布局，不替换callback，不主动调用比较函数。

## 4. 假设与第21轮有界诊断

1. 玩家本身networked字段持续不匹配：对应玩家实体自然比较hadErrors=true/errorCount>0，且在持续Reset之前出现。后续才追具体字段和坐标/命令历史。
2. 武器/viewmodel等其它predictable报错，连带清掉玩家历史：玩家比较正常、其它实体持续错误；不能把“玩家origin被Reset”误当成“玩家origin就是报错字段”。
3. cl_pred_doresetlatch=2强制恢复：实体比较不报错但该值2持续Reset。只读验证，不先替用户改配置。
4. 自然比较链没有错误而开关非强制：说明还有未观测的flag写入/时序，需要报告缺口并核实真实state setter，不屏蔽恢复。

第21轮只新增“预测网络比较结果与实体来源”一个诊断链条。三个关联入口仅是root scope、实体bool结果、TransferData错误数；各原函数自然原样调用一次，完整验证才装。记录packet序号、root/实体ack（不要求二者相等）、frame/tick、自然entindex、实体RTTI类型和是否本地玩家、比较调用/错误数/返回一致性；失败/未知不作0错误。固定实体桶和事件预算，汇总先计再压缩，窗口前少量prelude避免遗漏首个错误。

只使用已存在单人localserver测试范围，未读取分屏slot，不推广成分屏因果证明。保留现有Reset/父来源证据；没有行为修复、速度/eye/ground/brush改动、新cvar/bind、提交推送或游戏启动。用户本轮“同样要求”包含可继续则直接编码，按此有界设计继续，不额外重复设计确认。

继续评估：已经从猜测history形态推进到真实重复Reset、真正父路径与原生恢复机制，新探针能区分强制模式、玩家和非玩家报错，仍值得继续。若完整入口无法验证、来源归属不一致或没有新的区分信号，则先停止/修诊断；若证实具体entity错误，下一步以官方恢复/数据比较语义查字段根因，不先禁用Reset或靠相机补偿掩盖。
