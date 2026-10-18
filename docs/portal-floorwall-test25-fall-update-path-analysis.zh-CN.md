# 第25轮：fall 状态绑定成功，差异收敛到更新路径

## 结论

本轮测试有效，无需补测。正确 build `pe-6AD0F6DA` / schema12，日志9,785,092 bytes、19,572行；流式扫描后按capture/cmd提取，不全量载入上下文。16个窗口全部完成：普通下落2、其它穿越8、地板→墙面6。用户反馈主观不变、无新增异常，符合本轮只修探针的目标。

服务端1280/1280次采样有效。两端各只扫描一次：server 2.2071ms、client 1.6105ms，后续各1279次cache hit；没有恢复第23轮的反复扫描。server fall/velocity/ground/flags/movetype全部绑定；tickBase没有绑定，不猜偏移，继续按cmd对齐。

地板→墙面之后，服务端一直保留入门前约706.6665的fall值，客户端按出口后的竖直速度更新。直到落地，双方fall清零。这里的fall是原生跌落状态，不是玩家当前速度大小；不能解读为玩家一直以706.67下落。

## 同命令证据

每个窗口均有40组server-pre/post，客户端对应命令采样各一次。以下空中数量指server-pre与server-post的ground均为空且movetype=2；跨端比较另行区分提交命令：

- 普通下落：两次各25组，server-post fall均等于-server-pre velocityZ。
- 墙门空中样本：9/23/26组，也遵循上述更新。
- 六次地板→墙面：21/25/14/15/19/22组，共116组（含各自提交cmd）；server fall全部保持旧值，全部符合max(previousFall,-preZ)，无一等于当前-preZ。剔除六个提交cmd后仍有110组，结论不变。client提交cmd本身还可能使用入口空间，不能拿它代表后续更新；其后client fall按出口-preZ刷新。
- 不能按最近两行或相同frame硬配；client/server非空ground handle的编码不同，只比较有无ground。

代表性 capture11 / txn9 / cmd3389：

| 日志行 | 阶段 | fall | velocityZ |
|---|---|---:|---:|
|10239|server-pre|706.666504|-0.900354385|
|10240|server-post|706.666504|-27.5670223|
|10247|client-movement-pre|0.900354385|-0.900354385|
|10249|client-movement-post|0.900354385|-0.900354385|
|10257|原生预测比较|net=706.661621，pred=0.900354|diff=-705.761267|

客户端ProcessMovement结束时实体velocity尚未经过FinishMove回写，所以此时的velocity不能当作本命令最终速度。这个采样边界歧义将在第26轮消除。

六次地板→墙面共114次fall字段误差、102次延迟Reset；102/102次Reset之前，同frame/tick都记录了fall误差。261条origin详情均被夹取且不变。关联链仍是 `fall预测不符 → 原生错误处理/Reset → origin历史夹取`；它有稳定关联，但尚不能把全部主观粘滞唯一归因于此字段。

## SDK / 本机二进制研究（只读）

### 结论及调用链

官方参考路径：`RunCommand → PreThink → SetupMove → ProcessMovement/PlayerMove → FullWalkMove → FinishMove → PostThink`。

官方PreThink在存活、非地面条件下按`-GetAbsVelocity().z`更新fall，PlayerMove在ground为空时按`-mv->m_vecVelocity.z`更新，落地路径清零。当前问题应继续追踪这些边界，而不是在传送点任意清零fall。

关键本地来源：

- `Reference Code/portal-sdk-2013/src/game/client/c_baseplayer.cpp:1983`：PreThink；约2006行更新fall。
- `Reference Code/portal-sdk-2013/src/game/server/player.cpp:3809`：PreThink；约3864行更新fall。
- `Reference Code/portal-sdk-2013/src/game/shared/gamemovement.cpp:4603`：PlayerMove更新fall；CheckFalling约3902行。
- Valve公开[服务器玩家源码](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/hl2/hl2_player.cpp)亦展示非地面PreThink更新；本项目以本地Portal SDK调用链为主要参考，不把公开HL2实现直接等同L4D2运行态。

以本轮运行态发现的字段offset寻找本机DLL交叉引用，随后逐指令检查（以下为PE preferred VA，仅供离线定位，不写入代码作为RVA）：

- server.dll `0x100FC827..0x100FC8AE`、client.dll `0x101052D1..0x10105333`：**两端**PlayerMove均存在旧fall>0、MOVETYPE_WALK时取max(oldFall,-mvZ)的规则。不能把本轮现象说成“只有服务端PlayerMove使用max”。零常量与XOR负号掩码也已核验。
- client.dll `0x10069190..0x100691E8`：与SDK PreThink形状一致，lifeState/FL_ONGROUND检查后将负absVelocity.z写入fall。静态归属有依据，但尚未新增Hook验证调用。
- 两端PlayerMove均从GameMovement+4读player、+8读mv，再从mv+0x48读velocityZ。对应现有工程的GameMovement布局和CMoveData；下一轮仍需运行态player身份匹配及内存可读性检查。

### 已有能力与缺口

已有：运行态datamap/RecvTable只读字段绑定、双端RunCommand边界、双端PlayerMove/FullWalkMove Hook。无需新增函数签名或虚表调用即可做下一轮观察。

缺口：未确认服务端PreThink具体执行路径，未证明fall是在PreThink没更新，还是随后又被恢复。不能把client静态函数地址当作server函数，更不能直接调用或绕过原生预测错误处理。客户端实体velocity与MoveData也必须分开记录。

## 下一轮假设与可行性门

1. 优先：服务端命令开始至PlayerMove之间没有像客户端一样刷新fall。预期server PlayerMove入口已是旧值，client入口已是新值。
2. 次选：入口两端一致，但PlayerMove到FullWalkMove的max/其它分支再次保留或覆盖旧值。预期差异首次出现在FullWalkMove入口。
3. 次选：上述阶段一致，差异来自后续命令结束/网络比较或取样对象。预期只在command-post或net/pred出现分离。

三者能由已有Hook的只读边界区分，故值得继续。第26轮只改诊断，不承诺主观改善。若阶段均一致而错误仍在，停止“直接补fall更新即可解决”的假设；若服务端原生路径或合法更新接口仍不足，明确报告能力缺口，不引入清零、禁用Reset或眼位补偿。
