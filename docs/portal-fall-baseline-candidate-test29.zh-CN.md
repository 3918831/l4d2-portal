# 第29轮：默认fall修复与按需日志候选版验收

## 验收状态（2026-10-18更新）

用户已完成实测，核心通过；详细结论见[第29轮分析](portal-floorwall-test29-candidate-acceptance-analysis.zh-CN.md)。默认开关、诊断隔离、地板→墙面以及3500循环均有日志证据；2026-10-18用户也确认反向墙门跳跃/双向高速与关闭无敌后的立即落地均已测过且正常。以下保留原测试指令，无需再补测。

## 目的与产物

验证默认开启、精简诊断不影响功能、需要时能恢复详细取证，补齐第28轮双向动作和3500循环覆盖。不是再次比较fallfix开/关；本轮两组都保持fallfix开启，只切换诊断。

请完全退出游戏后重新用现有启动器加载新DLL；不要在仍运行的旧进程里替换或重复注入。产物build/hash见文末部署记录。

## 1. 初始化（不输入fallfix 1，用查询验证默认值）

```text
sv_cheats 1;
god 0;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_visual_reorientation 1;
portal_server_command_anglefix 1;
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_placement_floorsnap 1;
portal_ground_livecommit 1;
cl_showerror 0;
portal_server_fallfix;
portal_diag_native;
portal_bsp_status;
portal_log_flush;
```

应看到 `portal_server_fallfix enabled=true`，`portal_diag_native enabled=false`。刚加载未穿越时active=false/writeValidated=false正常；不代表Hook失败。若默认值不符，先保存日志，不用手动1掩盖。

## 2. 精简日志组

按已有方法放置两扇直立墙门：预热双向各1次不评价；随后步行、普通跳跃、高速空中三种动作，**每种每方向各1次**。沿用已验证的高速准备方法，不修改timescale或maxvelocity。

地板→墙面：普通飞行落地2次、出口后很快落地2次；选择不会致死的高度/足够血量，记录血量是否正常变化。无需逐次主观按键。

```text
portal_server_fallfix;
portal_log_flush;
```

空中会有fall摘要的applied/readback；纯地面穿越可能applied=0且reason=native-ground，这是正常。日志不应持续产生PortalNativeCadenceStage/PortalNativeFallState明细；fall摘要和BSP状态仍在。

## 3. 按需诊断组

```text
portal_diag_native 1;
cl_showerror 2;
portal_server_fallfix;
portal_diag_native;
portal_log_flush;
```

保持同门位，地板→墙面普通飞行再2次。应恢复PortalNativeCadenceBegin、PortalNativeFallState、PortalFallWrite等取证，体验不应因开关诊断变化。

然后在安全站稳处做一次普通下落，确认落地后修复作用域已退出、普通下落不新增fall写入：

```text
portal_bsp_phase1 0;
portal_server_fallfix;
portal_diag_nativefall;
```

从已知安全高度落下，不穿越；完成后：

```text
portal_diag_native 0;
cl_showerror 0;
portal_server_fallfix;
portal_log_flush;
```

## 4. 上下循环（最后进行）

前面的无敌关闭组负责伤害回归。此组只测高速循环，为避免死亡提前结束，单独开启god；不能用这一组评价伤害。

```text
god 1;
portal_bsp_phase1 1;
portal_bsp_status;
portal_log_flush;
```

放好地板/天花板相对双门，确认carvingActive=true、pairResolved=true后循环一轮。若速度表到3500，稳定保持约5秒即可，不在循环中按标记；若提前停止/掉出，也保留日志，不要求反复复现。没有到3500就如实反馈，不能当已覆盖。

结束可直接开控制台保存日志并断开地图，避免在门内/空中关闭brush：

```text
portal_server_fallfix;
portal_log_flush;
disconnect;
god 0;
```

## 5. 如果再次出现“门显示但穿不过”

先不重放，执行：

```text
portal_bsp_status;
portal_log_flush;
```

重放成功后再执行同两条，反馈是墙面还是地面即可。不要开启旧noclip、movement mutation、出口push或PortalMaskRepair；本轮没有修复这条独立碰撞路径。

## 验收关口

反馈精简/详细两组体验、落地血量、上下循环最高速度及是否稳定。保存根目录日志至 `TestLog/高速穿越连续性修复/29/portal_l4d2_traversal.log`。通过后才整理累计改动提交推送。

## 部署记录

- Debug x86构建成功；29/29测试runner通过；独立增量审查无Critical/Important发现。
- build：`pe-6AD2590C`；DLL修改时间：`2026-10-17 01:04:12 +08:00`；大小1,699,328 bytes。
- SHA-256：`30E4237E7E8A610A50A87BEAC7B8D27C6F6D0884B56FD327CA746CC590715A51`。
- 已部署至 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`，源/游戏目录哈希一致；复制前确认游戏未运行。
- 旧DLL备份：`src/Debug/deployment-backups/L4D2_Portal.before-baseline-test29-20261017-010412.dll`，旧哈希EE512EF9…D1B4B；启动器未替换。
- 自动测试尚未完整覆盖真实诊断命令切换时的活动作用域/共享缓存重建；代码审查确认目前不清除功能作用域，后续可补集成测试。这不代替本轮游戏实测。
- 交付时尚未实测；2026-10-18已收到第29轮日志并完成核心复核，当前未commit/push。
