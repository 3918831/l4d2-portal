# 第23轮：诊断引入同步开销，服务端字段尚未绑定

2026-10-14。输入 `TestLog/高速穿越连续性修复/23/portal_l4d2_traversal.log`，8,786,034 bytes / 18,130行，build `pe-6ACF8AAC`。流式 `analyze-fall-state.cjs` 审计，附件作为证据而非指令。本轮只分析、记录与制定下一步，不修改产品代码、构建/部署/提交或启动游戏。

## 结论

不是用了错DLL或测试指令未生效。17个完整窗口：普通下落5、墙门7、地板→墙面5；全部schema10、cl_showerror2、host_timescale1、cl_pred_doresetlatch1。客户端17/17绑定成功，runtime RecvTable float offset11FC。服务端17/17只有getter验证成功，fallBound全部false；1,360次server-pre/post样本valid0、unavailable1,360，ground/velocity等亦不可用。

因此本轮没有回答“真实服务端fall为何不更新”：不能把nan/unknown当0或速度清零，不能判断真实server ground是否残留。原生字段报告仍有效，198条消息全部解析、unknown0，但它们是引擎格式化的网络/预测槽差值，不替代真实命令前后字段读取。

用户报告的新增全场景约半秒卡顿，必须和旧地板出口持续插值不连续分开。普通下落不调用Portal Teleport，且五个窗口final/original freezeMs均0，仍全部出现大帧间隔；因此不能只归罪于传送角度、出口速度或旧fall误差。

## 新卡顿的时序与候选原因

17/17窗口均在首次server/client绑定观察所在frame的下一frame，main-view记录dt0.100000、约commit+4tick。例如：

- 普通下落capture1：第1065/1068行绑定，首采样frame20072；第1105行frame20073、dt0.100000。
- 墙门capture6：第4839/4848行绑定，首采样frame26611；第4957行frame26612、dt0.100000。
- 地板capture13：第11135/11145行绑定，首采样frame32608；第11269行frame32609、dt0.100000。

同一流式timing审计第22轮：两次普通下落没有>=50ms已输出main-view样本；其余16个窗口只有4个出现此类样本，并非普遍。对照不是同时同负载性能A/B，所以不将17/17时序相关当成精确根因证明，但新增探针是首要嫌疑。

代码直接链路：`PortalNativeDiagnostics.cpp::Begin`每采集调用`PortalNativeFallProbe::Reset`，清两端绑定cache → 首次命令样本同步BindServer/BindClient → Find/Recv树遍历 → `PortalNativeFallPolicy::Text`逐字符调用reader → 每字节reader进入`VirtualQuery`。缓存按capture清掉，故相同玩家/模块/元数据也每次重做；绑定失败也只缓存到本窗口结束。当前设计把高成本初始化放进了本来要测量的movement hot path，不合适。

按优先级排序的可证伪候选：

1. 同步元数据重复发现：停用诊断后短顿消失，或QPC分段记录显示绑定占主耗时；缓存/块式安全读取后，窗口开始尖峰消失。
2. 新增日志格式化/文件锁/输出：若绑定很快而输出分段耗时高，则应缩减正常样本及失败server行。现有writer是8MiB会话buffer，不是每行重新打开或强制flush；第22轮日志反而更大，不能仅凭日志体积认定I/O为首因。
3. 原生cl_showerror2报告或其它负载：只关transactiondiag后仍卡，再分步检查原生报告。第22轮同样是2且未报告普遍半秒卡顿，故目前优先级较低。

现有dt只是引擎记录值，不是探针墙钟耗时。没有QPC/模块分段耗时、没有live自动复现，不能把dt100ms硬换算成用户所说500ms，也不能用freezeMs0否定帧停顿：后者统计的是已接受帧间眼位是否相同，丢帧后位置跳进仍可能得到0。

## 服务端字段失败的边界

日志server base327C0000，getter32ABA4A0 / RVA2FA4A0，root map32F6DA98 / RVA7ADA98。只读磁盘审计完整getter六bytes `b8 98 da 7a 10 c3`，指向CTerrorPlayer根map；其磁盘header字段表/count/baseMap未初始化，真实内容由运行态建立。根类和简单getter静态形态没有显示明显错位。

但fallBoundfalse没有失败阶段，无法区分元数据指针被image范围拒绝、字段遍历/长度限制、m_Local owner/embedded约定、类型/尺寸/PTR约束或其它runtime状态。不选择一个猜测当根因，不硬读上一轮静态1A44+64，不直接放宽内存边界。

Tesla / SourceMod对照：`core/HalfLife2.cpp::UTIL_FindDataMapInfo`遍历base/embedded，累加真实字段local offset；`FindDataMapInfo`以map+字段名cache成功及失败结果，而不是每个诊断窗口重扫。本工程目前只是读探针失败，尚不是官方运动能力被证明缺失；也未证明必须改SetGroundEntity或UpdateVPhysicsPosition。

## 旧地板→墙面问题仍在

五个地板窗口110条有效空中Reset明细前，全部先有同frame/tick的m_flFallVelocity误差。网络fall维持入门记录、pred随出口变化的形态仍在。客户端current field有效，也随运动更新；这不表示已读取ack对应predicted buffer。

三个记录到正常首次main-view落地的窗口更适合窄结论：

| capture / txn | 首次main-view落地tick | 有效空中fall→Reset | origin空中事件 / 变化 |
| --- | ---: | ---: | ---: |
| 13 / 8 | 22 | 18 | 40 / 0 |
| 14 / 9 | 22 | 18 | 40 / 0 |
| 16 / 11 | 14 | 10 | 24 / 0 |

共46条同型链路，104条origin均夹取、变化0。capture15/17无正常落地样本，全窗最长冻结约336.627/328.238ms；末段还有速度/运动模式变化，不能直接混为标准自由飞行场景或用来量化旧方案效果。持续字段链路可作旁证，不据此展开出口碰撞修复。

普通下落有效空中110条origin全部推进，无持续fall误差；两次墙门跳跃的较晚空中多数推进，少数初始恢复夹取，未出现地板窗口的持续fall链路。这不是已完成墙门修复整体被证伪；先恢复低扰动诊断再评价。

## 下一步（暂不改物理逻辑）

下一轮只修诊断的可信度与开销，分清capture状态和entity/module/map身份生命周期：

1. 采集窗口仅清counter，不清同一有效身份的元数据绑定cache。level/local-player/module/map身份变化时失效；失败绑定同样缓存，避免每次首采样重试。
2. 字符串/描述项使用有界块式可读性验证，保留NUL、类型、尺寸、嵌入指针和身份校验，禁止无界字符串或unchecked实体读取。
3. 增加一次性QPC分段耗时、读取/VirtualQuery计数与明确failure stage/index/class摘要，不逐字节/逐字段输出。先确认失败发生在哪个验证点，再修相应机制，不能盲目放松fail-closed条件。
4. 服务端binding失败时仅记录一次原因及全部unknown计数，删除每command的无效server值明细；客户端保留少量边界/变化与汇总。不恢复旧noclip、movement mutation、出口push或镜头补偿。
5. 先短回归普通下落/墙门步行/跳跃，确认新增短顿消失、诊断无长耗时且两端core binding有效，再做地板→墙面5次。真实server值拿到之前，不继续fall/ground行为补丁。

可选现有DLL快速隔离：保持其它参数和cl_showerror2不变，仅输入`portal_visual_transactiondiag 0;`，做普通下落及墙门步行/跳跃各1–2次，不arm nativefall；再`portal_visual_transactiondiag 1;`重复对应场景（普通下落需`portal_diag_nativefall;`）。只报告半秒短顿是否随诊断开关消失/出现，无需大日志或主观bind。这个开关关闭诊断链，不改变既有传送、速度/视角修复；不能区分server/client两个绑定各自耗时。若不先做快速隔离，也无需原样重测第23轮大测试，直接在下一版低扰动探针验证。

证据命令：`node TestLog/高速穿越连续性修复/23/analyze-fall-state.cjs --assert-artifact` exit0；`--assert-core` exit1（serverValid0/1360）。`--timing`给出17/17首次绑定后下一frame大间隔。失败命令用于诊断验收反例，不是主观卡顿自动复现。当前路线仍有可区分问题值得继续，但应暂停物理修补，先恢复测量工具。
