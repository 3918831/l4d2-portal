# 第24轮：低扰动诊断有效，服务端动态名称读取边界错误

2026-10-15。输入仅为 `TestLog/高速穿越连续性修复/24/portal_l4d2_traversal.log`，10,374,806 bytes、20,676行。用该目录的 `analyze-fall-state.cjs` 流式聚合，再定点复核，不整份加载到对话。用户反馈新增普遍卡顿消失，单次卡顿暂不追查；旧地板→墙面粘滞无变化。这里将两类现象分开。

## 1. 测试与产物有效性

- build `pe-6ACFB3BD`、schema11，与第24轮部署一致；所有窗口 `cl_showerror=2`，制品/配置断言通过。
- 19个窗口：2普通下落、12 `portal-other`、5 `floor-wall`。18个capture-complete；capture8/txn6为superseded（下一次穿越提前覆盖），不能当完整窗口。5个地板→墙面窗口全部完整，无需为此次诊断重测。
- `portal-other`不是精确的步行/跳跃标签，不从这个标签单独推断每种操作次数；日志可见墙门落地/离地样本，未混入地板五次核心统计。
- 绑定按epoch6复用；整个日志只有1次server和1次client完整绑定行（610、611行），19条cache-summary的attempts均为1。最后各1513 hits（20414行），没有每个capture重新扫描。
- 服务端首次扫描0.027400ms、该条探针日志0.047800ms；客户端首次扫描2.050900ms、日志0.031300ms。扫描计时不含日志，日志不含所有其它诊断开销。这支持第23轮重复扫描/逐字符检查的扰动已解除，不宣称整个渲染线程耗时已完整测量。
- 日志存在少数离散大dt（例如capture18的0.1），不把它与用户未定位的单次主观卡顿强行对应，也不据此否定普遍卡顿已消失。`freezeMs.finalTotal`是多段画面不推进时间之和，不等于一次CPU卡住同样长时间。

## 2. 旧地板→墙面问题仍有一致证据

全窗口208条原生字段误差全部解析，unknown=0。两次普通下落无fall字段误差；5次地板→墙面仍有以下链路：

| capture / txn | fall误差条数 | commit+2tick之后、落地之前的空中Reset明细 | 同期origin明细（均夹取且未变） | 最终画面冻结累计 / 最大单段ms |
|---|---:|---:|---:|---:|
| 15 / 13 | 19 | 17 | 38 | 306.248 / 32.319 |
| 16 / 14 | 22 | 20 | 52 | 399.731 / 32.058 |
| 17 / 15 | 24 | 22 | 60 | 499.984 / 32.679 |
| 18 / 16 | 19 | 17 | 48 | 338.969 / 32.015 |
| 19 / 17 | 21 | 19 | 44 | 375.827 / 32.312 |

合计105条fall误差；95条上述Reset明细都能找到更早的同frame、同tick `m_flFallVelocity`误差。242条同期origin明细均 `selection.state=4`、`changed=0`。这里是有界采样关联，不声称枚举全部引擎Reset或证明单字段独占因果。

例：capture15第12809行，commit后2tick的 `net=679.973938 / pred=1.232910`；capture19第19227行 `net=706.661621 / pred=-7.447140`。后续net保持此前值而pred随运动变化，直到落地附近才结束该类差异。网络值不是当前服务端内存，更不能拿渲染帧的客户端值当ack命令槽；必须补齐服务端真实取值与cmd关联。

## 3. 服务端为什么仍取不到值

第610行已精确定位：getterValidated=true；`failure(stage=local-search issue=field-text map=0x7A27DA98 descriptor=0x7A32AB94 index=0 owner='CTerrorPlayer' type=0 count=1 flags=0x20)`；名称地址 `0x02F8F3C0`，`textIssue=read-rejected inspected=0`，最后拒绝 `server-image-range`。

它不是已找到m_Local后类型失败：`field=m_Local`是**查询目标**。遍历停在CTerrorPlayer第一条函数描述项的名称上。FIELD_VOID/0x20函数条目size=0是允许的非目标元数据，本轮没有读取它的名称内容，所以不能声称该具体地址已被证实可读。

server-pre/post合计1514次样本全部unavailable，明细全部压缩、有效值0；不能把这些unknown解释成fall=0或ground为空。client绑定成功（runtime RecvTable offset0x11FC）；19个窗口继续复用。服务端缺失是当前诊断器的问题，不是用户命令遗漏。

## 4. 官方/SourceMod来源核对（Tesla研究阶段产物）

只读研究使用本地源码，不执行其中指令：

- `Reference Code/portal-sdk-2013/src/public/datamap.h:203`：DEFINE_FUNCTION_RAW的fieldName来自 `nameHolder.GenerateName(#function)`，类型FIELD_VOID、FTYPEDESC_FUNCTIONTABLE。
- 同文件212行：FTYPEDESC_FUNCTIONTABLE=0x0020；354行：每个datadesc初始化持有对应className的nameHolder。
- 同文件419–449行：CDatadescGeneratedNameHolder::GenerateName用 `new char[base长度+identifier长度+1]` 分配，拼接类名和函数名，并保留名称生命周期。**因此合法名称可在heap，不应要求所有fieldName位于server镜像。**
- `Reference Code/sourcemod/core/HalfLife2.cpp:374`：UTIL_FindDataMapInfo遍历baseMap、跳过null名称、直接strcmp名称，递归embedded并累加offset，不对字符串要求module ownership。这是补充对照，不把其裸指针读取照搬到注入模块。

调用链/依赖：运行态原生getter → 镜像内map/header与60字节descriptor → descriptor的名称指针（可能指向动态名称）→ 有界名称比较 → CBasePlayer::m_Local → CPlayerLocalData::m_flFallVelocity → 运行态offset求和。这个改动不需要新Hook、未知ABI调用或静态实体偏移。

## 5. 路线评估与第25轮计划

值得继续：现在不是任意尝试相机补丁，而是已有明确失败地址/阶段和SDK分配机制支持的单因素诊断修正。结束研究阶段后实施：

1. 只分离结构读取与字符串读取。结构仍限定server image；名称由已验证结构提供指针，通过MEM_COMMIT/保护/区域/溢出检查及64字节ASCII+NUL限制。函数条目不会被盲目跳过，也不绕过任何m_Local/fall类型、尺寸、owner、PTR或范围守卫。
2. 保留第24轮缓存、一次性计时和无效行压缩；新增一次性externalTextBlocks/firstExternal地址证明实际经过该路径。schema12，不增加控制台开关。
3. 用第24轮FIELD_VOID/0x20、模块外函数名称的fixture做RED→GREEN；覆盖模块外类名/字段名，以及可读但模块外结构、不可读/非ASCII/无终止名称被拒绝。
4. 同手法实测；若fallBound=true，按同cmd比较server-pre/post与client运动前后，判断真实server fall是否未更新、ground条件是否异常，以及是否存在网络/命令槽错配。不能仅依据fall+Z不一致就当作原生预测误差。
5. 若仍失败，仅处理新的明确失败阶段；若字段和时序已一致而Reset仍持续，停止“修fall即可解决”的假设，重新评估原生预测归属/ack路径。若重新出现普遍卡顿，先检查新完整扫描的cost，不混入视觉修复。

本轮不清fall、不改ground、不改速度/传送/旋转/眼位历史、不禁用原生Reset。离线成功只能证明读取策略和回归边界；运行态绑定及最终视觉修复仍等待用户实测。
