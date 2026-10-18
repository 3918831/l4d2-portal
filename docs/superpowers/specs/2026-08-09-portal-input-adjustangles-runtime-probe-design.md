# Portal Input AdjustAngles 运行态探针设计

日期：2026-08-09

分支：`codex/bsp-portal-traversal`

状态：用户已确认按“安全默认值 + 晚期只读探针”方案实施。

## 1. 已确认现象与已排除假象

`portal_visual_reorientation` 查询返回：

```text
enabled=false hookValidated=false signatureValidated=false abiValidated=false activeTxn=0 active=false
```

游戏目录加载的 DLL 与本地构建产物一致，目标 `client.dll` 的磁盘映像也包含预期的
`CInput::AdjustAngles(int, float)` 长签名、`+50/-50` 常量引用和 `ret 8` 尾部。

普通 PowerShell `Get-Content` 曾把 UTF-8/LF 的 `InputAngles::Init();` 显示在
`ClientMode::Init(); // ...` 的行尾，看起来像被行注释吞掉。原始字节、UTF-8 解码结果
和 `git diff` 共同证明两者之间存在 `0A` 换行，注册语句实际上是独立有效行；因此不能
据此修改 hook 注册。

当前仍可确定的是：运行日志没有 `[PortalInputAdjustAnglesHook]` 初始化记录，查询时
signature/ABI 均为 false。仅凭当前日志还不能区分 `Init()` 未执行、保存目标为零、
运行态验证失败或 hook 创建失败。晚期只读探针正用于把这些分支一次分开。

## 2. 实施方案

### 2.1 锁定注册点

保留现有初始化顺序：MinHook 初始化成功后、`ClientMode::Init()` 之后、
`ClientPrediction::Init()` 之前创建 AdjustAngles hook，最后仍由统一的
`MH_EnableHook(MH_ALL_HOOKS)` 启用。

增加一个按 UTF-8 原始文本读取的源代码级回归测试：去掉每行 `//` 之后的文本后，
`Hooks.cpp` 中必须恰好保留一次有效的 `InputAngles::Init();`。这个测试专门防止相同的
“注册语句被注释吞掉”回归。

### 2.2 晚期只读 re-probe

`portal_visual_reorientation` 无参数查询时调用
`Hooks::InputAngles::LogLateDiagnosticProbe()`。该函数只读取并记录：

- `Init()` 是否进入并正常返回；
- 初始化时保存的目标地址；
- `U::Offsets.m_dwInputAdjustAngles` 当前地址；
- 使用同一长签名进行的晚期重扫地址；
- 保存地址与晚期地址分别经过完整 PE32、代码区、页权限、固定指令和常量验证的结果；
- hook 创建是否尝试、是否成功；
- signature/ABI gate 当前状态。

探针不得改写 offset、不得创建或重新创建 hook、不得启用 reorientation，也不得修改
视角、origin、velocity、move type、ground entity 或 brush。

探针日志使用独立标签 `[PortalInputAdjustAnglesProbe]` 并通过 focused file-log policy；
查询后仍由 `portal_log_flush` 明确刷新缓冲区。

### 2.3 默认值策略

保留现有安全默认值。已经验证的 continuity 参数继续默认生效；特别保留：

- `portal_bsp_phase1` 默认关闭，因为它会进行 whole-brush 写入；
- `portal_visual_reorientation` 默认关闭，因为本轮首先验证 hook/signature/ABI；
- 不增加隐藏状态的一键预设，也不恢复旧 noclip、movement mutation、出口 push 或
  `PortalMaskRepair`。

本轮运行态诊断只需要：

```text
portal_visual_reorientation;
portal_log_flush;
```

不需要放门或穿越。签名与 ABI 都通过后，下一轮再使用
`portal_visual_reorientation 1` 做单因子 A/B。

## 3. 验收条件

1. 源代码注册测试证明当前注册语句有效；不得为制造 RED 而改写产品代码。
2. focused log policy 明确接收 `[PortalInputAdjustAnglesProbe]`。
3. 所有现有六个 focused runners 继续通过。
4. Debug x86 完整构建成功，0 error。
5. 部署后的无参数查询至少产生 probe summary、stored validation、late validation 三类记录。
6. 本轮未默认启用 BSP 或 reorientation，且未产生任何新的物理 mutation。

## 4. 运行态探针结论与编译编码修复（2026-08-09）

`高速穿越连续性修复/6` 的运行态探针已经把断点收敛到初始化调用之前：

- `initAttempted=false`、`hookCreateAttempted=false`；
- initial target 未记录，但 stored target 与 late-rescan target 相同；
- stored/late 的 PE32、代码区、可执行页、固定指令、`+50/-50` 常量和 `ret 8`
  全部验证通过。

进一步检查实际 `Hooks.obj`，`CGlobal_Hooks::Init()` 的 relocation 序列中存在
`ClientMode::Init()`，随后直接是 `ClientPrediction::Init()`，没有
`InputAngles::Init()`。根因是 `ClientMode::Init(); // ...右键` 这一 UTF-8 中文行尾
注释之后使用单独 `LF`；工程未启用 `/utf-8`，MSVC 按 CP936 解释源码时，注释末尾
UTF-8 字节可能吞并换行，使下一物理行仍属于 `//` 注释。

`ClientMode::Init()` 本身在当前产物中一直有效，近期 BSP、空中/地面连续性测试也都在
该 hook 存在的条件下完成。它的 `CreateMove` detour 仍承担 traversal simulator 更新和
传送门输入，因此本轮不得禁用、重排或修改其逻辑。

修复仅把危险的中文行尾注释替换为 ASCII，使其在当前源码字符集下安全结束；随后必须
同时验证：

1. 源码字节测试拒绝“高位字节紧邻 LF、下一行是关键注册”的布局；
2. 新 `Hooks.obj` 同时包含 `ClientMode::Init()` 与 `InputAngles::Init()` relocation；
3. 游戏查询变为 `initAttempted=true`、`hookCreateAttempted=true`，并继续由签名/ABI
   gate 决定是否允许 reorientation mutation。
