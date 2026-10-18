# 第 17 轮：native-local 字段校正与原生插值条目参与诊断

编码日期：2026-10-08；复核部署：2026-10-09。依据：[第 16 轮分析](portal-floorwall-test16-part1-selection-analysis.zh-CN.md) §6；用户批准继续编码并自行游戏实测。

## 1. 目的与边界

保持现有穿越行为，只修正上一轮 recv/local 观测混淆，并读取实际原生插值循环及 origin/angles 条目状态。这轮不预期主观粘滞改善，不切换行为 A/B。

需要区分：

1. 位置条目不在自动参与范围，或 needs 条件不成立；
2. 原生循环调用、条目具备参与条件，但真正本地位置未发生变化；
3. 本地位置已变化，变化却未传播到最终绝对位置。

不修改真实 origin/rotation/velocity/ground/MoveType、插值历史或 map 状态；不主动额外调用 Interpolate；不改 shadow、回正速度、Teleport、brush、near-clip 等现有基线。不恢复 noclip、movement mutation、出口 push、PortalMaskRepair。SDK netvar 访问器本身的语义不变。

## 2. 已明确的能力限制

具体 watcher 虚函数目标/ABI 尚未完成独立验证，因此本轮不安装该虚函数 Hook。采用已批准的保守路径：观察真实 Interp_Interpolate 循环调用，校验 entry 数据绑定、参与范围、needs 与时基，并记录真实本地字段前后值。

`mapCallObserved` 表示循环确实调用；`eligible` 仅表示经验证的循环分支条件成立；`watcherCallObserved=false / watcherCallState=unknown` 明确表示没有直接观测单个 watcher 调用。后者在本轮是预期能力边界，不是“探针未安装”的同义词。历史区间和 interpolationAmount 继续 unknown，不能凭这些条件声称实际 watcher 已执行或历史已经正确。

如这些新数据仍不能提供区分结果，则暂停该补丁路线；不以未验证的 ABI 补齐表面上的日志成功。

## 3. 测试步骤

完全退出旧游戏进程，从游戏目录启动器重新加载 DLL，在单人 localserver 测试。不要运行时重复注入 DLL。没有新测试开关或按键绑定，无需主观标记。

### A. 普通下落对照两次

进入地图后执行：

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_bsp_phase1 0;
```

在没有活动门、可以正常落地的平台，每次准备好后执行：

```text
portal_diag_nativefall;
```

看到 armed=true 后自然跳下。落地后等待至少 2 秒，再次执行同一命令并跳下，共两次。不要使用 noclip、水或梯子。此对照用于检验真实本地字段能否解释正常插值，不可只测传送场景。

### B. 墙门回归与地板→墙面五次

```text
portal_bsp_phase1 1;
portal_bsp_status;
```

- 墙面双门预热一次，不评价首次卡顿；步行双向各一次、空中双向各两次。
- 安全且方便时，补一两次与地板门约 700 速度接近的墙门空中样本；不要为速度匹配冒险掉出地图。
- 同一对地板→墙面门做五次。出口留足自由飞行空间，每次落地后等待至少 2 秒再继续，避免窗口互相替换。
- 不切换 singleeyehistory；不改 timescale。若出现新的黑天空、异常位置或崩溃，停止并保留日志。

### 默认参数为何不用再逐条输入

已核对 `PortalTransitionDecision.cpp`：新进程默认 exact clearance、nearclip=1、exitguard=0、entryhandoff=1、transactiondiag=1、singleeyehistory=0，既有 eye/shadow、server anglefix 与 reorientation 默认开启。本轮不修改这些默认值。**brush Phase 1 仍默认关闭，必须执行 B 组的开启命令。**

如果个人 cfg 或测试中曾改过参数，以下块可显式恢复相关前置项（没有重新启用旧方案）：

```text
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_visual_reorientation 1;
portal_bsp_status;
```

## 4. 下一步的判断规则

| 本轮数据 | 后续动作 |
| --- | --- |
| 正常下落无法验证 native-local 来源或 oldOutputs 对应 | 先解决观测有效性，不将 invalid/unknown 当成不动，也不让用户重复无效测试 |
| 地板门条目缺失、不在参与范围或 needs 条件特有地停止 | 跟踪注册、排除、latch/恢复生命周期，获得具体差异后再提单因子行为修复 |
| 具备参与条件，native-local 没变化 | 验证具体 watcher 调用和有效历史区间；本轮尚不能声称 watcher 已调用 |
| native-local 有变化、outer abs 无变化 | 转查 Part2 的失效/缓存传播，不继续猜时间偏移 |
| 需要的布局/ABI 无法验证或没有新增区分结果 | 暂停当前补丁路线，讨论更完整的预测/历史生命周期方案 |

用户的主观反馈用于回归与安全检查；诊断是否有效主要看身份、有效性及正常/异常场景的底层数据差异，不能将“体验没变”当作本轮失败或成功。

## 5. 本轮实现与日志验收

真实本地 origin/angles 偏移从完整验证的 Part1 指令提取，并以独立 Part2 比较路径再次核对。实际原生 map-loop 通过 Part1 内 rel32 解析；完整函数体验证栈参数、ret8、条目步长、参与范围、needs 与时基分支。entry data 绑定另由两处构造注册片段及完整插入函数交叉验证，不能从 loop 中未使用的字段猜布局。当前支持的本机 client.dll SHA-256 为 `D2553CCA3A63BDD844864F5FEE2C20A233AB19DEE816463A90857B95309982A2`；不匹配或存在未支持的入口改写时拒绝安装，不放宽验证。

Hook 只原样转发自然发生的调用一次。map 数据读前校验可读区、容量/总数/参与数，按真实数据指针寻找 origin/angles，不假定首条；前后身份变化、重复绑定、非有限值等判为 invalid。缺少 origin 或 angles 绑定分别记录，不能因为缺少其中一项而隐藏另一项。CONTINUE 的 entry-local 与引擎 oldOutputs 对照；STOP 的旧输出不读取。recv、native-local、outer abs 的身份和有效性分别保留。

正常运行态预期看到：

```text
[PortalNativeCadenceBegin] version=4 ... mapProbe=true watcherHook=false
[PortalNativeMapHook] installed=true ... loopValidated=true ... localIdentity=true ... bindingValidated=true
[PortalNativeMapFrame]
[PortalNativeMapSummary]
```

实际日志字段之间可能夹有其他字段，无需匹配整行。`PortalNativeMapHook` 中 `watcherCallObserved=false watcherCallState=unknown watcherHookInstalled=false` 是本轮刻意保留的能力限制；如果 `installed=false` 或相关 validated 字段失败，才是当前入口不可用。第一次普通下落后即可检查；失败时保留日志并停止追加重复测试。

| 字段/记录 | 解读 |
| --- | --- |
| mapCallObserved / mapCalls | 同一关联作用域中实际原生循环调用，不是单个 watcher 的调用 |
| origin(... branch=...) / angles(... branch=...) | 0=未调用，1=无效，2=参与范围外，3=needs 不成立，4=条件具备，5=回退重启条件具备，6=该绑定缺失 |
| nativeOutcome | 0=未调用，1=无效或未观测，2=有效调用但本地值不变，3=有效调用且本地值变化 |
| nativeOriginChanged / nativeLocalOrigin / nativeLocalAngles | 已验证的真实本地字段，替代旧日志中误标 local 的 recv 计数 |
| recvOrigin / recvAngles / recvValuesValid | SDK 接收字段单独保留，不能代替 native-local |
| entryLocalOldOutputs(matched=... mismatched=...)；汇总 corroborated / corroborationMismatch | CONTINUE 引擎 oldOutputs 与调用前本地字段的对照证据，需同时看有效性 |
| PortalNativeInterpolationFrame | 同一 frame 的外层绝对位置观测，用于判断 local 变化是否传播 |

分支 0 只有在 Hook 与外层观测可用时才可解释为真实未调用；任何结论都需同时查看 installed/valid/unavailable。未知值不是 0 位移，条件具备不证明具体 watcher 已执行。旧成功 Part1 明细改成状态/时基边缘输出，map 明细复用有界窗口预算；全接受帧汇总不受 128 组明细限制。新日志标签加入精确白名单，避免上一轮“实现存在但文件未记录”的问题。

## 6. 验证与部署身份

- 实现者完成 RED→GREEN；控制代理独立重跑 28 个测试 runner，`TOTAL=28 FAILURES=0`。现有两个 C4819 编码告警仍在 PortalTransformTests 的旧头文件中，不属于新增失败。
- 独立审查未发现需修改项，并再次以本机二进制反汇编核对调用边界、布局和 ABI；静态审查不代替游戏运行态验证。
- Debug x86 MSBuild 构建成功，`git diff --check` 无错误。保留原工作区修改；本轮未提交、推送或启动游戏。
- 2026-10-09 已部署至 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`，源文件与游戏文件 SHA-256 一致。
- DLL 大小 `1459712` bytes；文件修改时间 `2026-10-08 23:46:28`；PE 标识 `6AC7BAD4`。部署发生于次日并不改变源文件修改时间，不要单凭日期误判旧 DLL。
- SHA-256：`97A95E8998B2A595F1116FE35811F38AFC110919855D729676406BF1FD68B2AD`。
- 原游戏 DLL 备份：`src/Debug/deployment-backups/L4D2_Portal.before-native-map-20261009.dll`；SHA-256 `51D78408F73CD35BEA1635BDFCE8FAADE4173CCACEF32F67A739C5A462D7AACE`。

尚未完成的是用户游戏内的实际安装/调用验证。本轮仅部署诊断，不宣称修复粘滞；下一步严格按 §4 的新增区分证据决定是否继续。
