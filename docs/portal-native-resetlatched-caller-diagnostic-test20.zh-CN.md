# 第20轮：只读追踪 ResetLatched 上游来源

承接 [第19轮证据分析](portal-floorwall-test19-origin-reset-analysis.zh-CN.md)。只新增一个父链观察因子，没有行为修复，不预期主观变化。

## 机制与安全边界

- 从已验证RTTI的本地玩家vtable slot106解析完整80-byte包装器，再由完整尾部E9解析56-bytebase实现；与已有map offset0x18、entry stride12、watcher slot5调用点交叉验证。
- base ABI为void thiscall、无栈参数。包装器尾跳保留上游返回地址；取入口 `_ReturnAddress`、验证PE32/可执行模块及RVA，不走猜测栈、不扫描返回地址。
- 父base自然原样调用一次，嵌套thread-local scope恢复；非本地对象嵌套进入先清scope，不能继承本地caller。每次验证本地slot106目标以及origin绑定。没有额外调用原生函数或读取未知预测flags。
- origin Reset独立观察保持原样；新增parent上下文不会把父证据缺失误判成子Reset无效，也不会根据stage猜“网络调用”。
- 不写player/map/history/GlobalVars，保留原生Reset与包装器合法清理，不修改时基、amount、eye/shadow/rotation、server anglefix、速度、ground或brush。旧禁用路径不恢复。
- 无新增cvar/bind。每窗口仍最多48组Reset明细；全部子Reset总计先累加再压缩。父caller16个固定桶，overflow独立显式计数；prelude仍最多8组、不重复进入窗口总计。
- 新版Begin version7；新增精确文件标签 `PortalNativeResetLatchedHook` / `PortalNativeOriginResetParentCaller`，父字段并入现有Reset/ResetSummary，避免另打一组逐帧日志。

`parent.state`：0 unavailable、1 unscoped、2 identity/对象/子返回点 mismatch、3 invalid caller、4 matched。父caller桶计的是已关联的origin Reset次数，不是所有实体的ResetLatched入口次数。

覆盖边界：父body调用原生IsClientCreated可能跳过watcher，不存在origin子Reset时本探针没有子事件。这里的统计不能用于声称“所有父入口都命中”，也不能识别入口外的内联重建。共同base caller仍需按本机机器码核实函数和实际条件，不能看到inPrediction=true就叫预测错误。

## 实测步骤

重启游戏，从主菜单加载新DLL，进入单人localserver：

```text
sv_cheats 1;
host_timescale 1;
fps_max 300;
portal_visual_transactiondiag 1;
portal_visual_singleeyehistory 0;
portal_bsp_phase1 0;
```

做两次普通自然下落，每次开始前：

```text
portal_diag_nativefall;
```

落地后至少等2秒。检查 `PortalNativeResetLatchedHook` 的 installed、wrapperValidated、baseValidated、mapLayoutValidated、abiValidated、liveBindingValidated 均true。普通下落Reset calls=0允许；没有parent caller行也不单独作为失败。

再开启穿越；显式恢复既有视觉前置，避免个人cfg干扰：

```text
portal_bsp_phase1 1;
portal_bsp_clearance exact;
portal_visual_nearclip 1;
portal_visual_exitguard 0;
portal_visual_entryhandoff 1;
portal_visual_reorientation 1;
portal_bsp_status;
```

1. 墙门热身一次，两方向各步行一次、跳跃两次。跳跃要让人物脚离地进入门，不能刚好落地再过门。
2. 同一对地板→墙面门做5次，出口保留足够自由飞行距离，首次落地后等至少2秒再开始下一次。空中不额外跳跃/改移动模式，不改timescale。
3. 不需标记或新bind。出现黑天空、门体/viewmodel异常、崩溃则停止并留日志。

## 目标日志与决策

```text
[PortalNativeResetLatchedHook] installed=true ... slot=106 ... return=void
[PortalNativeCadenceBegin] version=7 ... resetLatchedHook=true
[PortalNativeOriginReset] ... parent(... state=4 ... module=client.dll rva=0x...)
[PortalNativeOriginResetSummary] ... parent(... matched=...)
[PortalNativeOriginResetParentCaller] ... calls=...
```

父来源RVA应不同于子watcher caller4728E。若安装成功但parent一直unknown/mismatch，提供日志，不能把此结果强行解释成某条恢复路径。若匹配稳定，下一步按真实父caller核实预测恢复、teleport/nointerp或其它lifecycle条件，不直接禁止空中Reset。

## 构建、复核与部署

2026-10-14最终验证：全部28个runner通过，Debug x86 MSBuild exit0、git diff --check exit0。本机80/56byte完整磁盘指纹再次独立比对通过。独立只读增量复核无Critical/Important；唯一Minor“验证失败也肯定tail保留caller”的元数据已改为随完整包装器验证结果输出，并重新构建、全套测试通过。日志specifier与参数59/29/12/14匹配，最宽元数据保守上限小于1266bytes/2048。

部署到 `J:/Games/Left 4 Dead 2 v2.2.4.3/L4D2_Portal.dll`，源/目标SHA一致：

- 修改时间 `2026-10-14 00:25:25`，大小 `1539072` bytes。
- PE：`pe-6ACE5B75`。
- SHA-256：`1030F86E0956AD9655EDD660BCD0E25360B088C038D7B1785271F2EC0A3DBA35`。
- 原第19轮DLL备份：`src/Debug/deployment-backups/L4D2_Portal.before-resetlatched-caller-20261014.dll`，SHA-256 `5C25BF816567C0210290DBC0EA7C9BBF6AE36C15DF53FE0A883D390D904F8E73`。

部署时游戏未运行；没有启动游戏、提交或推送。保留 `codex/portal-official-mechanism` 及原未提交基线，仅比较本轮快照。离线测试只保证读取/归属/计数/指纹契约，不能代替运行态安装、换图生命周期、开销或主观改善验收。真实上游函数名与条件必须等新捕获再核对。
