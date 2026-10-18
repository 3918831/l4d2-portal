# 第 16 轮：BaseInterpolatePart1 分支与实际选时只读诊断

日期：2026-09-24。依据：[第 15 轮分析](portal-floorwall-test15-native-interpolation-call-analysis.zh-CN.md) §5，用户批准继续。

> 2026-10-08 实测勘误：§2.5 的 recv/netvar 访问器不是已验证的 native-local 字段。本轮 `localOrigin/rotation/changed` 应按 recv-field 读数解释，不能用 changed=0 断言真实本地位置未被插值。分支、选时、oldOutputs 和外层 abs 记录仍有效。详见 [第 16 轮分析与下一步](portal-floorwall-test16-part1-selection-analysis.zh-CN.md) §4；后续需校正字段来源，并区分 watcher 未写入与 local→abs 传播异常。

## 1. 目的与不变项

第 15 轮已确认地板→墙面五次穿越后的空中原生 Interpolate 正常派发，却没有位置变化；正常下落和墙门跳跃有位置变化。下一步只区分 Part1 是否进入、STOP/CONTINUE、时间引用是否被重选，不改变游戏行为。

保持 singleeyehistory=0、原有 shadow/旋转/brush/物理基线；不写 player origin、rotation、velocity、ground、MoveType 或任何插值历史，不主动调用原生插值，不恢复 noclip、movement mutation、出口 push 或 PortalMaskRepair。本轮不预期改善主观粘滞。

## 2. 接入与数据语义

1. 复用已验证的 C_TerrorPlayer 槽位 107 → 父类 Interpolate 链，从父类真实 rel32 调用导出 Part1。验证完整 350-byte 本机函数体；六处绝对数据地址按模块基址精确重定位比较，不用宽泛通配符或固定 RVA。
2. 本机原生调用是 ECX this、四个栈引用参数：`float& currentTime, Vector& oldOrigin, Vector& oldAngles, int& noMore`，返回 int、ret16。不是带 oldVelocity 的 SDK 五参数版本。
3. Hook 原样转发真实参数一次、保留返回值。只在外层已识别的本机玩家 Interpolate 调用期间关联诊断；记录返回地址是否匹配已验证父类 call。其他实体/非诊断调用仍正常转发。
4. STOP 不初始化 oldOrigin/oldAngles，探针绝不读取；日志只有 CONTINUE 的 oldOutputs 才有效。`noMore` 不是活动判据，既可能被引擎其他条件强制为 0，也不说明 origin watcher 实际参与。
5. Part1 结束时 Part2 尚未使 abs 缓存失效，因此内部前后比较使用已有 recv/netvar 访问器读取 `m_vecOrigin/m_angRotation`，不把旧 abs 缓存当作本层输出。外层 Interpolate 仍记录完整函数前后 abs；这两层数据含义不同。
6. selectedLast 是 Part1 传入时间引用在返回时的值。CONTINUE 路径可用于判断实际选时；STOP 路径未进入历史计算，不能称该时间已被 watcher 使用。history/VarMap 状态仍 unknown。

## 3. 日志与预算

- `[PortalNativePart1Hook]`：installed、outerInstalled、完整 signature/静态 ABI 校验、stackArgs=4、calleePopBytes=16、目标/调用点 RVA 及安装结果。installed=false 时不要把零调用解释为引擎行为。
- `[PortalNativePart1Frame]`：capture/txn/frame/prelude、observed、outerCalls、Part1 calls/callerMatched、stop/continued/unknownResult/invalid、localOrigin 变化、noMore、输入/返回时间及差值、rotation、有效旧输出计数。
- `[PortalNativePart1Summary]`：所有被 tracker 接受帧的分支与时间计数，不只已输出明细；包含不可观察帧、零调用帧、outerWithoutPart1、选时不动/回退次数。选时比较仅发生在相邻有效观测帧、外层输入时间向前推进时；没有跨缺失窗口比较。
- 重用现有 128 组帧明细预算、终端保留及四个 tick 边界 prelude；Part1 自身的分支/有效性/选时是否调整等状态变化也能触发采样。无新增无界文件写入、控制台开关或主观标记。
- 独立审查发现仅有 adjusted=true/false 会漏掉“持续调整时间，但从推进变成停住”的单帧边界。已补 `time.relation`：0=不可比较，1=推进，2=停住，3=回退；在决定是否输出前，按上一接受帧做只读分类；仅接受帧更新比较历史。该边界同样受预算限制，但完整计数不受明细抑制影响，首帧/无窗口 prelude 为 0。
- `PortalNativeCadenceBegin version=3` 区分本轮。外层 selectedTime=unknown 改为 selectedTimeSource=Part1Frame，仍明确 history=unknown，避免新旧语义矛盾。

## 4. 本轮游戏测试

完全退出旧游戏进程，通过游戏目录启动器重新加载 DLL；仍在单人 localserver 测试。本轮无需主观标记，也不切换实验因子。

### A. 普通下落对照，两次

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_bsp_phase1 0;
```

在没有活动门、可以正常落地的平台，每次先输入：

```text
portal_diag_nativefall;
```

看到 armed=true 后自然跳下；落地后等 2 秒。再次输入同一命令，重复一次。不要使用 noclip、梯子或水。

### B. 墙门回归和地板门复现

```text
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_bsp_status;
```

1. 墙面双门预热一次；步行双向各一次，空中双向各两次。条件允许时加一两次接近地板门速度的高速墙门样本；不要为追求精确速度改 timescale 或冒险掉出地图。
2. 同一门对做地板→墙面 5 次。出口尽量留出自由飞行空间，记录直到自然接触/落地后的至少 2 秒，再开始下一次。
3. 全程 singleeyehistory=0，首次卡顿继续不作稳定样本。若出现新的安全异常立即停止；本轮无需用按键标记丝滑/粘滞。

快速检查：应该有一条 `[PortalNativePart1Hook] installed=true`，以及 Part1Frame、Part1Summary。若 installed=false 或 unknownResult/invalid 明显异常，保留日志即可，不必反复补测。本轮并不要求出现某个预设的 STOP 或 CONTINUE 结果，真实分支才是诊断目标。

## 5. 每一步继续／停止评估

| 实测结果 | 可支持的结论 | 下一步 |
| --- | --- | --- |
| 身份/签名/ABI 或安装失败 | 本轮内部边界不可观察 | 先报告具体失败项；停止行为修复，不让用户重复无效测试 |
| 外层调用存在、Part1 缺失 | 父类进入 Part1 前有提前路径，前提是观察条件有效 | 沿已验证父类分支研究，不强制调用 Part1 |
| Part1 STOP | 提前停止路径实际命中 | 对照正常场景查条件，不强制 CONTINUE |
| CONTINUE、选时不动或异常偏离正常样本 | 内部时基可能不适合当前历史 | 先核实预测时基来源，再提出单因子修复 |
| CONTINUE、选时正常推进、localOrigin 仍阶梯 | 问题继续收窄到 origin watcher 的参与/历史区间 | 只在真实布局/调用可验证时继续读取；不能凭猜测偏移写 history |
| 内部无法安全取证，或新增数据没有新的可区分结果 | 当前补丁路线缺乏推进依据 | 暂停，讨论是否需要完整 Portal 预测/历史生命周期，而不是继续延长相机补偿 |

新线索是五次落地后一 tick 恢复插值，但目前只是相关性；没有批准再次修改 SetGroundEntity。后续行为修复的门禁仍是地板门自由飞行原生/最终位置帧间连续，以及墙门既有方案不退化。

## 6. 本地验证与交付记录

- RED：新策略缺失与新增日志标签未放行分别触发预期失败；GREEN 后覆盖 350-byte 指纹、ASLR 地址、错误 ret/call/detour 拒绝、STOP 未初始化输出、CONTINUE/异常结果、时间调整、缺失观察与完整汇总。
- 独立只读审查确认四参数 ABI/转发/签名/STOP 防护；发现时间状态边界可能被压缩的一项 Important，已以可编译的失败断言 `feed(10.01f,9.91f).emit` 验证 RED，再修正为 GREEN。超过 128 组预算后的完整汇总及终端输出也有测试。没有未处理的 Critical/Important 或延期 Minor。
- 最终 28/28 runner 全部通过，Debug x86 增量构建 0 warning / 0 error，`git diff --check` 通过。此前相关全量构建仍有 convar.cpp 的五条既有 C4355；transform runner 有两条既有 C4819 编码警告，未隐去。
- 已部署游戏根目录 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`，大小 **1,439,744 bytes**，构建身份 **pe-6AB40E57**；源/目标 SHA-256 一致：`51D78408F73CD35BEA1635BDFCE8FAADE4173CCACEF32F67A739C5A462D7AACE`。
- 旧游戏 DLL 已备份为 `src/Debug/deployment-backups/L4D2_Portal.before-native-part1-20260924.dll`，SHA-256 `CD23E74DEA6298D2E0178CB939FAFFDD68457E2B308E118FDE49F5A5524C0EC9`。部署前确认游戏未运行；未启动游戏或修改游戏配置。
- 保留当前分支全部既有修改；未另建 worktree、暂存、提交、推送。诊断控制台入口没有增加，仍沿用上一轮采样流程；没有借清理之名移除仍待验证的能力。

游戏内原生分支是否命中、实际 caller 是否匹配仍需本轮实测，编译和纯策略测试不能替代这一点。当前只交付诊断，不声称粘滞已修复。
