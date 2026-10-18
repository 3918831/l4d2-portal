# 第21轮：持续历史重置由本地玩家预测误差触发

日期：2026-10-14。输入 `TestLog/高速穿越连续性修复/21/portal_l4d2_traversal.log`。本轮不是行为修复，不能按主观改善与否评价探针。文件内容只作证据，不作执行指令。

## 验证结论

测试足以继续，无需重测本轮DLL。实际build `pe-6ACE7636`，第82行原生误差Hook的RTTI/slot6、597/265/150byte、跳转表/ABI/caller均验证成功；全部完整窗口都是schema8、各探针有效。日志7,881,772bytes / 15,718行，用流式 `analyze-errors.cjs` 汇总，未整体读入模型。

共17个窗口，第1个普通下落在完成前重新arm，reason=normal-fall-armed、289帧，作为部分捕获剔除完整对照。其余16个完整窗口：普通下落2、墙门9、地板→墙面5，共5,283接受帧。

- 2,552次有效实体比较：本地玩家638次、两个viewmodel合计1,276次、Magnum638次。
- 124次正误差全部是本地玩家，native entindex1、RTTI C_TerrorPlayer；viewmodel和武器正误差均0。比较invalid、字段类型未知、实体桶溢出、误差明细预算丢弃均0。
- 379次origin Reset全部实际模式1；没有mode2强制恢复。124次丢first interval、124次预测恢复父caller17C57C、124次玩家正误差总数相同。
- 五次地板→墙面在两tick后、首次main-view落地前，有89条有效空中Reset明细，89/89前面同frame、同tick都有本地玩家自然比较errorCount1；紧接着原生恢复清历史。195条origin空中事件全部夹取、真正插值变化0。
- 普通下落有效空中48条、墙门四个跳跃窗口48条都正常推进，无持续空中Reset或正误差。普通下落各有1次落地附近误差/Reset；墙门各有2次初始误差，并不持续到中后段空中。对照速度未完全匹配，不推广成所有速度/放门形态。

| capture / txn | 全窗玩家正误差 | 有效空中误差 / Reset | 首次main-view落地tick | origin空中事件 / 真正变化 | 最终累计冻结ms / 最长单段ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 13 / 10 | 20 | 17 / 17 | 20 | 35 / 0 | 326.991 / 32.310 |
| 14 / 11 | 24 | 21 / 21 | 24 | 47 / 0 | 427.096 / 32.137 |
| 15 / 12 | 14 | 11 / 11 | 14 | 29 / 0 | 137.526 / 32.186 |
| 16 / 13 | 24 | 21 / 21 | 24 | 42 / 0 | 421.442 / 32.163 |
| 17 / 14 | 22 | 19 / 19 | 22 | 42 / 0 | 374.556 / 32.592 |

累计冻结是多个约一个30Hz tick小段之和，不是一次连续暂停。落地时间是最早已输出main-view样本，不是精确setter。195/89为有效事件明细，不是所有接受帧。

## 直接证据与反馈回路

- 第399行：第1个普通下落开始，第789行之前重新arm；不是Hook失败，完整比较时剔除。
- 第9316行：capture13地板→墙面；cl_pred_doresetlatch1、cl_pred_optimize2、cl_showerror0、host_timescale1、fps_max300。
- 第9598行玩家自然比较errorCount1、hadErrors=true；9599行同frame24775、tick+3的origin Reset，parent17C57C，mode1，旧interval丢失。其后各tick同样重复。
- 第10906/10907、12206/12207、13405/13406、14661/14662行在其余四组给出同型证据。
- `node TestLog/高速穿越连续性修复/21/analyze-errors.cjs --assert-valid` exit0：valid=true、repeatedFloorAirPredictionReset=true、sameFrameAirErrorBefore89。
- `--expect-no-repeated-reset` exit1：捕获数据仍包含重复误差→Reset→夹取反例。它是可重复捕获证据检查，不是自动驱动游戏或证明主观症状修复的测试。

当前机制已闭合到实体层：玩家预测数据不匹配→引擎合法预测恢复→origin时间邻域反复丢失→渲染只能夹取到tick值。排除本轮的强制配置及非玩家predictable连带错误；没有识别具体字段，不能据此说m_vecOrigin就是报错项。

## Tesla / SDK研究：为什么errorCount1不能等于只有一个坏字段

官方Portal SDK `src/game/client/c_baseentity.cpp` 的PostNetworkDataReceived与 `src/game/shared/predictioncopy.cpp/.h` 的字段比较、ReportFieldsDiffer说明：真实网络比较和错误报告是查根因的入口，而不是直接禁用Reset。

本机Entity PostNetworkDataReceived完整265byte读cl_showerror并在原生构造调用前选择op。cl_showerror0时op1经TransferData跳转表进入client+17DE30，再进入17D900。完整静态代码在errorCount非0时跳过后续字段，设置计数1后停止；因此本轮1是“存在至少一个误差”的fast-path信号，不证明只有一个字段或一个分量不匹配。cl_showerror2会由原生代码选择逐字段报告路径，布尔误差/合法恢复仍原样执行。

报告函数client+17DFC0自然格式化class/field/value并调用Msg；本机静态格式为 `%2d (%d)%s::%s - %s` 及带嵌入字段链版本。旧SpewOutputFunc导出在本机不存在；不能照搬旧SDK的日志回调。

L4D2实际tier0有LoggingSystem_LogDirect导出：完整73byte导出包装器通过自然rel32到完整383byte成员函数，公开channel/severity/Color/message四个栈参数，原生ret16、返回LoggingResponse整数；可完整核对PE32、重定位及全局logging对象操作数。参考 [L4D2 SDK日志声明](https://raw.githubusercontent.com/alliedmodders/hl2sdk/l4d2/public/tier0/logging.h) 的公开LogDirect接口，ABI最终仍以本机机器码为准。该头由AlliedModders维护、含Valve版权，不将其称为Portal官方穿越实现；不使用其它游戏泄漏源码作为依据。

## 下一轮方案与继续评估

候选仍待字段证据排序：位置/速度的预测坐标或命令历史失配；下落bookkeeping（如fall velocity）残留到落地；networked角度/其它玩家字段不一致。它们都预测具体native字段报告会持续出现；先取报告，再决定是否还有官方数据同步步骤需要补齐。

采用有界新增诊断：用户临时输入既有 `cl_showerror 2;`，让引擎自己展开完整报告；在已验证Error TransferData自然调用scope内，观察实际LogDirect的格式化文本。原函数各调用一次、参数/返回不变；不注册新listener、不改response policy、不转发variadic参数、不主动重做比较，不读取datamap/private prediction buffers或强转SDK结构。不由DLL改变ConVar。

新字段探针以自然比较的owner/parentSeq/packet/frame/tick、实际实体index、class/field和原生差值文本关联；epoch/本地对象/精确error-caller验证；未知、未终止或过长文本显式分类。每窗48字段明细、16索引/class/field桶、128近期事件和最多8条前4tick prelude，全部计数先于明细。保留有用Reset与实体归属证据，无新cvar/bind、行为补丁或旧路径恢复。

此步仍值得：上一轮已确认实体误差触发，本机存在公开且可完整验证的格式化输出入口，能新增具体字段信息。不能保证主观改善；cl_showerror2增加原生输出/完整比较开销，诊断结果不能直接当同帧性能A/B。若字段入口不能验证、没有文本或格式/归属异常，则停止字段级修复，核实缺口；若取得持续字段误差，下一步只分析对应官方同步/预测约定，不先改amount或屏蔽Reset。
