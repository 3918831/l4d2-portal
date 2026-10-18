# 第 15 轮：原生 Interpolate 入口只读诊断

日期：2026-09-22。依据：第 14 轮分析第 6 节，用户批准继续；每一步必须评估仍有何问题待印证、何时停止。

## 1. 本轮目的与边界

本轮是定位实验，不预期直接改善主观粘滞。第 14 轮已发现：地板→墙面自由飞行阶段，物理 command-end 持续推进，但原始相机和实体位置呈 tick 阶梯；普通下落、墙面双门的对应阶段有帧内插值。现在只区分：

1. 玩家原生 `Interpolate` 没有被调用；
2. 被调用，但返回位置仍未改变；
3. 返回时位置改变，随后 render-start 结束时又不同。

保持物理、brush、速度、roll 曲线及 shadow 控制路径不变。`singleeyehistory` 固定为 0；不混入 EyeFootPosition 修改，不恢复 entity-history mutation、强制插值/重置、noclip、movement mutation、出口 push、PortalMaskRepair。无新增控制台开关。

## 2. 入口证据与安全边界

本机 `left4dead2/bin/client.dll` 为 PE32 i386。静态 RTTI/vtable 与反汇编相互校验得到：

- `C_TerrorPlayer` vtable 槽位 107 指向 `Interpolate` 包装函数；本机磁盘 VA 为 `0x10222220`。
- 24-byte 包装体通过 ECX 传 this、栈传一个 float，直接调用 `C_BaseAnimating::Interpolate`，以 AL 返回 bool，`ret 4` 清理参数。
- 父类本机磁盘 VA 为 `0x100344f0`，181-byte 函数体通过引用传入时间、旧位置、旧角度、插值结果至 `BaseInterpolatePart1`。此父类包含 STOP 后仍返回 true 的路径，**返回 true 不等于进行了位置插值**。
- `C_BaseEntity::Interpolate`（本机 `0x1004ddb0`）也调用同一个 Part1（本机 `0x10048eb0`），与本地 Portal SDK 的分层结构吻合。

以上地址仅是复核证据，生产代码不按固定 RVA 定位。运行时从实际本机玩家读取 RTTI、模块内 vtable 槽位与 rel32 调用目标；校验 PE32、模块/可读/可执行范围、完整包装体（仅调用位移单独解析）、完整父类函数体，全部通过才安装 Hook。父类字节不含 ASLR 敏感的绝对地址，更新版本或已有 detour 将拒绝接入，不尝试绕开第三方 Hook。

Hook 只观察真正发生的调用；原函数参数、调用次数和返回值保持不变。没有主动调用 Interpolate，没有写入任何玩家私有字段。首次本机玩家出现时尝试一次；失败保留日志，不每帧反复扫描/安装。

## 3. 新增证据与日志压缩

- `[PortalNativeInterpolationHook]`：installed、RTTI/签名/静态 ABI 校验、槽位、目标/父类 RVA、MinHook 状态。
- `[PortalNativeInterpolationFrame]`：同帧截至主视图提交的调用总数（不包含提交之后的调用）、render-start 原函数期间次数、返回 true 数、位置实际改变次数、无效样本数、输入时间、调用前后 abs、render-start 返回位置与**窗口内最后一次**返回位置的差。
- `renderWindowObserved=true` 才能把该窗口的零调用当作证据；`valuesValid=false` 的零坐标只是占位。`afterRenderComparable=false` 时不能根据 difference=0 宣称没有覆盖。
- `selectedTime=unknown history=unknown` 是明确缺口；没有把传入 currentTime 冒充 Part1 内部最终采用的时间，也没有把 m_vecOrigin 访问器冒充插值历史。
- Native 汇总覆盖所有被 tracker 接受的帧，不只已输出的事件帧；原冻结时长统计不变。新增调用/有效性/位置改变/返回后差异状态的边界采样，不依赖眼位变化触发；汇总保留返回后差异次数和最大值，以及“函数改变位置但眼位冻结”的帧数。采样仍受 128 组预算，终端帧保留。原始返回 bool 不能用于推断 STOP 分支。
- 同帧相邻阶段所有已记录字段**精确相同**时，用 `sameAs=上一阶段 exact=true` 引用，差值为 0；不同或无效/非有限数据不被近似吞掉，阶段身份仍保留。
- 正常 FinishMove 按最多 8 个命令聚合输出 `PortalNativeCadenceCommandBatch`；首次、重放、命令缺口、缺少 ProcessMovement、状态改变、实体/move 不一致、非有限值或明显速度变化展开。终止/替换窗口会输出尾批。展开明细仍有预算，超限计数可见。
- `PortalNativeCadenceBegin version=2` 区分本轮。删去的是上述重复输出，不删除仍有待验证的实验功能；未把证伪的历史写入代码重新引入。

## 4. 分步评估与继续／停止门槛

| 步骤 | 已有证据 | 仍待印证 | 决策 |
| --- | --- | --- | --- |
| 静态入口 | RTTI、24-byte 包装体、181-byte 父类、float/AL/ret4 相符 | 游戏进程是否为相同函数、是否已有其他 Hook | 值得做一次只读接入；不匹配则禁用 |
| 运行态派发 | 已实现对象/模块/签名校验与原样转发 | 普通下落、墙门、地板门的调用次数和实际输出是否不同 | 用本轮最小对照验证，不宣称已确认调用 |
| 压缩 | 精确相等引用、批次/预算、完整冻结统计 | 游戏内开销、阶段是否真正齐全 | 保留明确 dropped/invalid/unknown；缺失先查诊断自身 |
| 后续定位 | 能区分未调用／调用无变化／返回后变化 | 内部时基、history、STOP 分支或覆盖者 | 只沿实际命中的一条分支继续，不同时堆叠相机补丁 |

下一轮判定：

- 校验失败：停止行为修复，先报告具体失败项；不要让用户反复测同一无效 Hook。
- 地板门只缺调用：进一步查原生插值列表/调度条件；不强制入表。
- 调用但输出不插值：安全验证 Part1 的实际选时/分支、origin watcher 生命周期；若偏移或 ABI 不可证实，明确暂停，不猜。
- 调用给出连续位置而后续变化：再查实际覆盖者；单次非零差不等于错误写入，须与正常对照比较。
- 三类场景均相同且外层证据不支持 native 假设：复核观测区间；无新可证伪差异则停止这条补丁路线，讨论完整预测生命周期的实现范围。

每次只继续有新证据可区分的实验。不会仅凭“仍有可能”要求无限轮主观测试。

## 5. 完整测试指令（全部以英文分号结尾）

完全退出旧游戏进程，通过启动器在主菜单加载新 DLL。进入单人 localserver。本轮无需任何主观标记，无需切换实验开关，不使用慢动作。

### A. 普通下落两次

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_bsp_phase1 0;
```

没有活动传送门、普通平台处输入一次：

```text
portal_diag_nativefall;
```

看到 armed=true 后 30 秒内自然跳下，正常落地并等 2 秒。再输入一次命令重复。勿使用 noclip、梯子、水或跳向地图外。尽量有一段可观测的下落，但无需为了匹配速度冒险。

### B. 墙门对照与地板门复现

```text
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_bsp_status;
```

这几条前置显式写出，方便整组复制；其余已验证默认参数不再逐项输入。确认状态正常后：

1. 墙面双门先预热一次；步行双向各 1 次、空中跳跃／高速双向各 2 次，确认已有方案无劣化。
2. 地板→墙面 5 次，尽量同一入口、出口和输入方式，出口留足空间观察至少约 1 秒自由飞行；每次之后等 2 秒再开始。
3. 全程保持 singleeyehistory=0，不做本轮 A/B。首次卡顿不作为主要样本。

若 hook installed=false 或某项 valid=false，保存原始日志即可，不用为“补齐日志”重复测试。出现安全异常立即停止并保留日志。最终退出游戏保存日志。

## 6. 本地验证与部署

- 实施前 27/27 runner 通过；新增入口签名、父类完整指纹、调用统计、render-window 时间归属、精确 stage 引用、批次、native-only 状态边界和全窗口差异统计测试。关键断言均先观察 RED，再实现 GREEN。
- 独立只读审查发现“native 异常没有眼位变化时可能被压缩”的问题，已补齐 native 状态边界与汇总；复查确认该项解决。未知内部状态仍明确 unknown。
- 最终 28/28 runner 通过，Debug x86 构建成功。首轮相关全量构建只有既有 convar.cpp 的 5 条 C4355 警告；最终增量构建无新增警告。`git diff --check` 通过（仅现有 CRLF 转换提示）。
- 已部署 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`，大小 1,420,800 bytes；构建源和部署目标 SHA-256 一致：`CD23E74DEA6298D2E0178CB939FAFFDD68457E2B308E118FDE49F5A5524C0EC9`。
- 上轮 DLL 备份：`src/Debug/deployment-backups/L4D2_Portal.before-native-interpolation-20260922.dll`，SHA-256 `3CB3997A8D93390DF63E9BC95C48D72CE6BDFB70ECA42BE9C20544325B596C17`。
- 未启动游戏，未修改游戏配置，未暂存、提交、推送；保留同分支已有工作和无关研究文件。

单元测试和编译不能替代实际运行态派发验证，也不能证明主观问题已修复。最终继续／停止决策依据本轮游戏日志，而不是本地构建成功。
