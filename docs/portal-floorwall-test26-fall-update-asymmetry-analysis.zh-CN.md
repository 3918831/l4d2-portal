# 第26轮：fall 更新分歧定位及修复前决策

## 1. 验证结论

本轮有效，无需补测。日志11,302,824 bytes、22,225行，build `pe-6AD109A3` / schema13正确。采用 `TestLog/高速穿越连续性修复/26/analyze-update-path.cjs` 流式统计，随后按capture/cmd读取有限原文；没有把整个日志载入上下文。

15个窗口全部完成：普通下落2、其它穿越7（热身/墙门步行跳跃）、地板→墙面6。地板→墙面并非缺次数，用户记忆中的5或6次不影响结论。

八阶段都有有效记录。共4798条状态明细，2398条运动入口明细均匹配已绑定player且MoveData.z有效。两端仍各绑定一次：server 2.2919ms、client 1.6115ms，最后cache hits分别2397/2399；没有重复全扫描。用户反馈主观不变、无其它异常，符合只读诊断预期。

两个窗口各有一个命令没有server-walk-pre：capture12 cmd5540、capture13 cmd5665。原文显示它们都在落地后、窗口末尾，server movetype已为8，不是本轮目标的MOVETYPE_WALK空中段；不能补造FullWalkMove调用。其余阶段可读有效，且这些命令没有纳入根因统计，不要求用户重测。

## 2. 同一命令中，差异在哪一步产生

六次地板→墙面，排除提交cmd，并要求八阶段齐全、双方全程ground为空、movetype=2，得到111个连续空中命令：18/18/15/19/23/18。每个阶段每个cmd只有一次，没有把重放样本混配。

111/111满足：

- 命令开始时两端fall一致（允许网络量化误差，统计阈值0.05）。
- PlayerMove入口两端MoveData.z一致；命令结束实体velocityZ也一致。
- server从命令开始到PlayerMove入口未刷新fall，之后到FullWalkMove及命令结束也保持旧值。
- client在命令开始至PlayerMove入口之间已经刷新fall，值等于当前`-velocityZ`/`-MoveData.z`，随后保持至命令结束。

代表：capture10 / txn8 / cmd5150：

| 阶段 | server fall | client fall |
|---|---:|---:|
|命令开始|679.999878|679.973938|
|PlayerMove入口|679.999878|43.7312317|
|FullWalkMove入口|679.999878|43.7312317|
|命令结束|679.999878|43.7312317|

此命令两端起始velocityZ与MoveData.z均为-43.7312317，最终velocityZ均为-70.3979034。对应日志行11295–11315；11317行原生比较为`net 679.973938 pred 43.731232`，diff=-636.242706。

另一方向的数值同样成立：capture14 / cmd5802起始velocityZ=+48.7661819，client fall=-48.7661819，而server保留733.33313（行18831–18853）。负fall在上升阶段是合法的，不能用归零或max(0,候选)代替官方有符号公式。

**已证实的是有效更新路径不对称，不是已经证明服务端完全没有调用PreThink。** 分歧发生在command-pre至PlayerMove入口区间；仅靠这些边界不能命名该区间的每个实际函数或断言某个函数没执行。

## 3. 为什么普通下落/墙门没问题，地板→墙面会暴露

普通下落50个同条件空中命令、墙门40个同条件空中命令，也能观察到server命令前半段不刷新、client提前刷新的差别。但进入FullWalkMove时双方已经一致，且没有本轮这种fall预测误差。

结合第25轮已核验的本机两端PlayerMove分支：oldFall>0且MOVETYPE_WALK时保留`max(oldFall,-mvZ)`；否则直接更新为`-mvZ`。自然重力下下落速度逐步增加，新候选一般追上/超过旧值；跳跃上升阶段的负fall亦走直接更新。所以普通轨迹掩盖了差异。

地板→墙面把原先很大的向下速度转成主要水平方向，出口当前`-mvZ`骤降，但server仍保存入口的正fall峰值；client提前改成当前值，两个相同的max分支收到不同输入，最终产生持续不一致。无需假设发生了movetype切换或竖直速度突然停顿。

六次地板→墙面记录117次非prelude的fall字段预测误差。105次延迟空中Reset全部有同frame/tick的fall误差前驱；263条延迟origin详情均为历史夹取且不变。链条为：

`入口fall旧峰值残留 → 两端fall预测不一致 → 原生错误处理/Reset → origin历史夹取`

这是有稳定对照和阶段定位的强候选根因，仍不是主观症状的最终因果证明。必须用单因子A/B观察误差、Reset、origin和主观体验是否一同改善。

## 4. SDK研究及实现能力（Tesla只读研究）

### 官方机制

- `Reference Code/portal-sdk-2013/src/game/server/portal/portal_player.cpp:586`：CPortal_Player::PreThink调用BaseClass::PreThink。
- `Reference Code/portal-sdk-2013/src/game/server/player.cpp:3809`，约3864行：存活且非地面时按`-GetAbsVelocity().z`更新fall。
- `Reference Code/portal-sdk-2013/src/game/client/c_baseplayer.cpp:1983`，2005行：客户端对应更新。
- `Reference Code/portal-sdk-2013/src/game/shared/gamemovement.cpp:4549`，4603行：PlayerMove在ground为空时直接赋值`-mv->m_vecVelocity[2]`；不是保留入口峰值。
- 同文件CheckFalling约3902–3964行：fall还用于落地冲击、伤害和落地回调，之后清零。因此它不是可随意调整的纯视觉变量。

Valve公开源码可对照[客户端玩家](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/c_baseplayer.cpp)和[GameMovement](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/gamemovement.cpp)。本地Portal分支是主要行为参考，不能把SDK源码与L4D2二进制视为完全相同。

### 底层写入依赖与缺口

`server/playerlocaldata.h:60`将fall声明为CNetworkVar。`public/networkvar.h`的赋值链是`operator= → Set → NetworkStateChanged → m_Value=val`，嵌套m_Local还会把通知转发至所属实体。

SourceMod旁证：`Reference Code/sourcemod/core/smn_entities.cpp:1767`的SetEntPropFloat先解析实体、datamap/sendprop及字段类型，之后写float；只有Prop_Send分支主动通知edict。`core/HalfLife2.cpp:531`的SetEdictStateChanged处理引擎变更信息。它不是一个可以直接等同官方CNetworkVar赋值的现成函数。

工程已有本地server实体解析、穿越事务身份、双端运动Hook和运行态字段绑定；但当前`PortalNativeFallProbe`明确只读，没有已验证的fall setter/网络通知调用。`src/SDK/L4D2/Includes/edict.h`有StateChanged声明，未发现工程内对应实现，不能因为头文件存在就声称可直接调用。

因此允许读取的0x1AA8/0x11FC只是本次运行结果，不是后续写入硬编码。若进入修复，需独立验证写权限、字段类型/范围、实体生命周期和原生通知路径；任一不满足则拒绝写入并报告，不用旧ServerDataMap写接口兜底。

## 5. 下一步建议：先确认，再进入单因子修复

按用户本轮要求，这次不自动修改DLL。修复范围是已有穿越流程的单个fall状态维护，属于小范围变更；先提供短设计，待用户确认后实施。

建议实验：在本地玩家已确认的穿越相关空中运动中，对齐官方`fall=-当前MoveData.z`更新规则，保留符号，不改origin、velocity、movetype、ground、镜头、Reset或预测误差阈值。优先验证原生写入/通知及真正的更新时点；**现有FullWalkMove入口只是已验证的观察边界，不能不经验证就称为官方相同写入点。** 没有验证完成就不启用mutation。

规则范围不应是任意“恢复若干秒”的计时补丁；需要由确认穿越、正常空中movement和原生落地/失效边界约束，具体接入点在编码前落实。不要为了消除错误全局接管所有玩家的L4D2跌落伤害规则。

离线反事实检查：以六组首个完整postcommit空中样本刷新一次fall，其后仅套用已观察的native max规则与原记录MoveData.z，111/111个候选都与client落在0.05以内。这支持“旧状态残留是可消除因素”；**不是游戏模拟或修复验收，也不把一次性播种说成官方完整逐命令实现。** 不据此新增一次性补偿方案，正式实验仍以官方更新语义和合法时点为准。

测试将需要A/B：保留原场景与前置设置，控制组和实验组各5次地板→墙面，普通下落及墙门走/跳继续回归。只增加一个实验开关，不需要主观bind。验收分开看提交cmd与延迟窗口：后者fall误差/重复Reset应明显减少或消失、origin不再反复被夹取，才评估主观改善；落地冲击/伤害作为安全回归项。

若fall一致后Reset或粘滞仍在，停止把全部症状归给fall，不追加镜头补偿。若无法验证写入和通知，明确停在能力缺口，不硬写、钩未知函数或禁用原生Reset。

## 6. 本次改动范围

仅新增本轮流式分析脚本（TestLog下诊断资产）及本文，并更新技术总结；未修改C++、构建/部署DLL、提交或推送。游戏目录仍是第26轮 `pe-6AD109A3`，等待用户对上述修复实验的决定。
