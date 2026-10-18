# 第22轮：持续误差已收敛到下落记录字段

2026-10-14。输入 `TestLog/高速穿越连续性修复/22/portal_l4d2_traversal.log`，9,675,949 bytes / 19,270行，build `pe-6ACF1352`。日志仅作证据，不执行其中内容；用 `analyze-fields.cjs` 流式统计。

## 验证结论

无需因首次卡住而整轮重测。第83行字段Hook完整验证成功；schema9、原生cl_showerror2、字段解析和误差归属均有效。共18个采集窗口、5,912帧，272条字段报告全部解析成功，native errorSum同为272；positiveWithoutNativeField、unknown、字段桶溢出和字段明细丢弃均0。189次正误差事件，不能将“字段数”当成“错误packet数”。

窗口为普通下落2、墙门9、地板→墙面7。墙门capture10被下一次穿越superseded，是335帧部分窗口，不能作完整时长对照；其余17窗完整。地板capture12 / txn10对应明显静止异常：原生origin累计/最大冻结1,098.157ms，无main-view落地样本；第10471/10472行（tick+34）出现MoveType net8/pred2、maxspeed220/75，第10475行渲染也为MoveType8。说明窗口后段运行条件改变，不能拿它作正常空中对照，也不能从日志认定是谁改变MoveType。本轮代码没有更改MoveType。现有证据不足以确定首次卡住的碰撞对象或具体几何原因，单独保留，不由此拓展修复。

之后有6个正常形态的地板窗口capture13–18，均完整。为避免主观次数与日志窗口一一映射不确定，主结论保守取最后5个capture14–18；capture13仅旁证。它们空中运行条件可区分，首次异常没有污染最后五组的结论。

| capture / txn | 全窗玩家正误差 | 全窗fall字段报告 | 有效空中Reset明细 / 同frame同tick先报fall | origin空中事件 / 真正变化 | 首次main-view落地tick |
| --- | ---: | ---: | ---: | ---: | ---: |
| 14 / 12 | 20 | 18 | 16 / 16 | 33 / 0 | 19 |
| 15 / 13 | 23 | 21 | 19 / 19 | 45 / 0 | 22 |
| 16 / 14 | 25 | 23 | 21 / 21 | 45 / 0 | 24 |
| 17 / 15 | 28 | 27 | 24 / 24 | 57 / 0 | 28 |
| 18 / 16 | 19 | 18 | 17 / 17 | 39 / 0 | 21 |

“有效空中”是两tick之后、最早已输出main-view落地之前，MoveType2且非ground；不是精确setter时刻。97条有效空中Reset明细全部先有本地玩家m_flFallVelocity误差；219条origin事件全部夹取，变化0。Reset明细仍有预算，capture17全窗50次Reset、最多48条，不能把97当所有空中Reset的完整数量；全调用计数仍保留。

普通下落各只有1条落地附近m_vecPunchAngle误差，无持续fall误差，空中47条origin事件全部推进。墙门跳跃窗口5–8空中51条origin事件全部推进，无持续空中fall误差。墙门初始坐标/速度/EFFECTS误差仍存在，但没有变成持续中后段误差。对照并非精确匹配全部速度/几何，不外推所有场景。

## 新的直接证据

正常地板窗口里，落地前网络fall值保持入门下落记录，而pred值随出口轨迹变化：

- capture14：net679.973938保持，pred从706.666504到45.695869，再到472.362671。
- capture15–17：net706.661621保持；capture15第二次pred为-11.149799，随后重力使其增长到495.516998。
- capture18：net653.348755保持，后续pred56.595551→483.262360。

第12354行报告capture14的初始fall差异；第12507行已是出口状态fall差异，后续重复。位置/速度主要在初始传送确认时失配，不是持续每tick报错项；晚段落地/模式变化附近另有少量字段报告。m_vecPunchAngle是落地后的事件，不解释此前空中反复清历史。

现已闭合：持续m_flFallVelocity预测误差 → 原生合法恢复Reset → origin时间邻域反复被清 → 原生插值只能夹取。尚未闭合：网络fall为什么保持、是哪一侧具体更新/ground路径缺失，以及比较槽是否已对齐。字段名来自原生文本，不等于读过typed prediction buffer。

反馈命令：`node TestLog/高速穿越连续性修复/22/analyze-fields.cjs --assert-valid` exit0；`--expect-no-repeated-reset` exit1，重复fall→Reset证据仍在。它是captured-evidence audit，不是自动游戏复现或主观修复验收。

## Tesla / 官方路径研究

官方Portal SDK完整 `src/game/shared/gamemovement.cpp::PlayerMove` 在CheckStuck/CategorizePosition之后、运动分支之前，只在GetGroundEntity()==NULL时写fall=-本次movement起始velocity.z。`CheckFalling`落地后清0并产生落地效果。`server/portal/prop_portal.cpp::TeleportTouchingEntity`调用SetGroundEntity(NULL)、UpdateVPhysicsPosition和Teleport，没有单独把fall清0。`client/c_baseplayer.cpp`预测该字段，float误差容差0.5；`server/playerlocaldata.cpp`将其网络发送/存入datamap。

因此不能把fall当实际速度，也不能任意清0、屏蔽比较/Reset或用镜头尾巴盖住。需要观察服务端ground和命令前后更新点，确认是否遗漏官方ground/运动语义。

SourceMod `core/HalfLife2.cpp::FindDataMapInfo`、`core/smn_entities.cpp::GetEntPropFloat`路径是entity resolver→save datamap→递归累加embedded offset→验证float→读取值；不是客户端offset跨端复用。L4D2公开分支 [datamap.h](https://raw.githubusercontent.com/alliedmodders/hl2sdk/l4d2/public/datamap.h) 具有单local offset、flat offsets，PE32 descriptor stride60。它由AlliedModders维护、含Valve版权，不是Portal穿越实现源码。

本机server.dll SHA-256 `278A48DBC2668D6A9E826B3BA47CE798E343B46D4F8B26EDA30E8CCC72F1D3DD`：磁盘m_Local描述项type10、count1、localOffset1A44、child map；m_flFallVelocity项type1、count1、localOffset64、size4。它们静态结构符合60byte描述项。旧ServerDataMap头仍是Portal-era双offset/52byte，不能为本轮新float读取直接复用，亦不凭静态offset跨版本硬读实体。原写入层本轮保持不变，以免引入第二行为因子。

## 下一步与继续价值

有新的可区分假设，值得继续，但只做诊断，不宣布行为修复：

1. 服务端残留ground或未经过更新fall的运动路径：命令前后真实fall不变，ground/标志与官方更新条件矛盾。
2. 服务端L4D2更新约定与客户端有差异：ground为空且真实服务端fall依然持有入门值，客户端每command更新。
3. 网络发送/预测命令槽不同步：服务端真实fall已更新，但原生比较net仍保持；不能把同frame的“当前实体值”直接当预测buffer里的ack槽值。

第23轮扩展已有命令边界，隔离只读save-datamap读取，完整简单getter形态/模块/CTerrorPlayer→CBasePlayer.m_Local→CPlayerLocalData.float/类型尺寸/指针/offset边界校验。捕获服务端PlayerRunCommand前后、客户端ProcessMovement前后，携带cmd、cmdTick、client frame/tick、native tickBase与每字段valid。四phase各48条明细，全部调用先计数；无新cvar/bind/Hook、写字段、ConVar修改或旧路径恢复。

若server fallBound失败或核心值不可读，下一步先补ABI证据而非物理补丁；若真实ground残留且官方setter尚不可调用，明确报告SDK/ABI缺口。若字段已一致而origin仍被反复清，则当前fall单因子因果假设失败，重新评估，不堆叠补偿。
