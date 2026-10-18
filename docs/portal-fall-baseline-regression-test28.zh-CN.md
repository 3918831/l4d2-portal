# 第28轮：fall 修复基线回归（沿用第27轮 DLL）

## 目标与当前产物

本轮只补齐第27轮未覆盖的实验开启后回归，不修改代码、不重新编译/部署、不重做长A/B。

- DLL：`J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`。
- build `pe-6AD1B572`，修改时间 `2026-10-16 13:26:10 +08:00`。
- 源/游戏文件SHA-256重新核对一致：`EE512EF9C4CE0A765ABEAE62B7F0292022076EAE507290B8867F37DA109D1B4B`。
- `portal_server_fallfix` 本版默认仍为0，本轮初始化显式设1。
- 单人localserver、正常timescale、沿用现有mod配置；不新增无敌、免摔或限速设置。不启用旧noclip/movement mutation/出口push/PortalMaskRepair。

本轮不需要F键绑定或逐次主观标记。按下面顺序测试，最后说明各组结果；发生异常立即保存日志，不为了凑次数继续。

## 1. 初始化

重启游戏，在主菜单注入，进入地图后完整执行：

```text
sv_cheats 1;
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
portal_server_fallfix 1;
cl_showerror 2;
portal_bsp_status;
portal_server_fallfix;
portal_log_flush;
```

确认输出build为pe-6AD1B572、enabled=true、hookValidated=true。尚未首次穿越时writeValidated/notifyValidated=false是未绑定，不代表失效；后续须有成功绑定。不要用reason=ready单独判断运行中，准确生效/停止状态仍以能力字段和Summary为准。

## 2. 墙面双门：旧连续性回归

尽量复用之前已经验证顺滑的墙面门位，先双向各过1次作预热，不计下面次数。

| 方式 | 蓝→橙 | 橙→蓝 |
|---|---:|---:|
| 平地步行 | 2次 | 2次 |
| 普通跳跃 | 2次 | 2次 |
| 既有高速空中方式 | 2次 | 2次 |

每次走开并自然落地后等至少2秒再开始下一次。观察视觉/速度连续性、黑天空、门框和viewmodel。不要临时换加速方式、调timescale或速度上限。

本组结束：

```text
portal_server_fallfix;
portal_log_flush;
```

验收不要求地面步行也出现applied>0：若原生已落地，作用域应立即退出。墙门跳跃/空中场景需要核对合法候选与无拒绝、无旧行为劣化。

## 3. 地板→墙面：复核改善和落地边界

一对门内保持位置固定，选择安全高度，确保路线末端没有模型挡住出口：

1. 正常空中飞行后落地3次，复核第27轮粘滞不再出现。
2. 选择出门后很快自然落地的安全门位/路线，再做3次。先低速试走确认出口合法，不刻意把人物送进墙或地面。
3. 每次落地后停至少2秒。记录起跳/下落前与落地后的HUD血量，并说明冲击、镜头下沉、减速是否明显异常；可截图HUD，不需要空中操作或逐次标记。

这不是要求两组伤害相同：出口的竖直速度、剩余落差可能不同。也不要把入门前落差直接当作出口应承受的伤害。当前日志没有足够健康值/伤害事件，HUD观察是明确的验收输入；若已有无敌/免摔mod导致不能检查，报告“伤害项未验证”，不要记作通过。

本组结束：

```text
portal_server_fallfix;
portal_log_flush;
```

## 4. 普通下落：确认落地后作用域结束

务必先站在安全实体地面、离开门口，再恢复brush。保持fallfix=1：

```text
portal_bsp_phase1 0;
portal_server_fallfix;
portal_diag_nativefall;
```

从一个熟悉、非致死的安全高度普通下落1次，落地等2秒，再执行并重复1次：

```text
portal_diag_nativefall;
```

记录HUD血量和普通落地感受；不经过传送门时不应因开关为1而新增fall写入。完成后：

```text
portal_server_fallfix;
portal_log_flush;
```

## 5. 地板↔天花板：最后做高速连续循环

把可能快速掉图的场景放最后，保留前面已经完成的日志。恢复穿越：

```text
portal_bsp_phase1 1;
portal_server_fallfix 1;
portal_bsp_status;
portal_log_flush;
```

复用之前已经通过的上下门位置，连续循环约10–15秒；若速度表明确达到3500，可在稳定达到后再观察约5秒。做2轮，轮间正常离开门区/落地。看速度随重力变化和达到上限后的循环是否连续，是否掉图、卡住、黑天空或突然归零。不要求过程中数次数、标记或开控制台，不修改游戏最大速度。

如果没有能避开已知whole-brush整块地板缺口的门位，说明该项暂缺，不靠新碰撞补丁或强行完成次数掩盖。快速循环导致诊断capture被新事务替换（superseded/new-commit）属于预期；分析时以事务/写入生命周期和已有高速记录交叉检查，不要求每次40tick窗口都完整。

若循环退出不安全，不要在人物仍处于门中/半空时关闭brush；结束测试并先保存日志，注明异常发生在哪一组。

## 6. 结束及交付

```text
portal_server_fallfix;
cl_showerror 0;
portal_log_flush;
```

本轮不在收尾关闭fallfix，便于继续正常体验；退出游戏后下次启动仍需初始化显式开启。保存本轮日志到 `TestLog/高速穿越连续性修复/28/portal_l4d2_traversal.log`。

反馈四项即可：墙门走/跳/高速；地板→墙面正常/立即落地；普通下落及HUD血量；上下循环及是否确认达到3500。漏测或受mod影响的项目写“未验证”，不需要补编结论。

## 验收与下一步

- 修复有能力并生效，拒绝/写入失败不被误认为成功；作用域自然落地后结束，无普通下落外溢。
- 地板→墙面延迟错误链不复发；合法首tick/落地Reset不作为“必须全部抹除”的目标。
- 开关1下墙门与上下循环不劣化；落地冲击/伤害无新增异常，或明确指出尚未验证的项目。
- 如失败，先区分fall语义、作用域、旧碰撞问题；暂停默认化和上库，不叠加修复。
- 通过后再实施默认化、状态报告澄清和日志收敛。该候选DLL还须用户短回归通过，才整理技术资产、审查、提交与推送。
