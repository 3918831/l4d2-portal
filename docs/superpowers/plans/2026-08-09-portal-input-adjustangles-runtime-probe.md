# Portal Input AdjustAngles Runtime Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 锁定 `InputAngles::Init()` 的有效注册，并为 `portal_visual_reorientation` 查询增加完整、只读、fail-closed 的晚期运行态探针。

**Architecture:** hook 创建仍只发生在统一 hook 初始化阶段；查询命令只重扫、验证和记录，不改变任何运行态功能状态。晚期探针使用与 `Offsets::Init()` 完全相同的长签名独立重扫，并同时验证 stored target 与 late target，从地址一致性和完整验证结果上检测诊断/正式解析漂移。

**Tech Stack:** C++17、x86 MinHook、Win32 PE/`VirtualQuery`、现有 focused `PortalFileLog`、PowerShell 源代码回归测试、MSVC/VS2022 Debug x86。

## Global Constraints

- 不重新启用旧 noclip、movement mutation、出口 push 或 `PortalMaskRepair`。
- 不修改 origin、velocity、move type、ground entity 或 brush 生命周期。
- `portal_bsp_phase1` 和 `portal_visual_reorientation` 均保持默认关闭。
- 晚期探针不得创建、重建、启用或移除 hook；任何验证失败只记录并 fail closed。
- 保留工作区已有未提交基线；游戏验证前不提交。
- 每项产品修改先观察 RED，再实现最小 GREEN。

---

## File Map

- Create `tests/PortalInputAdjustAnglesRegistrationTests.ps1`: 证明 `Hooks.cpp` 中存在且仅存在一条未被注释的注册语句。
- Create `tests/run_portal_input_adjustangles_registration_tests.cmd`: 执行源代码注册测试。
- Inspect `src/Hooks/Hooks.cpp`: 仅在注册测试证明确有问题时修改；原始字节已证明当前调用独立有效。
- Modify `src/Hooks/Input/InputAngles.h/.cpp`: 记录初始化/hook 生命周期并输出晚期只读探针。
- Modify `src/Portal/PortalBspCommands.cpp`: 无参数查询触发探针。
- Modify `src/Util/Logger/PortalFileLog.h`: focused allowlist 接收 probe 标签。
- Modify `tests/PortalFileLogPolicyTests.cpp`: 锁定 probe 路由。
- Modify `docs/superpowers/plans/2026-08-09-portal-official-view-reorientation.md`: 回填本轮根因、实施和验证证据。

---

### Task 1: Lock Hook Registration and Probe Routing with RED Tests

**Files:**
- Create: `tests/PortalInputAdjustAnglesRegistrationTests.ps1`
- Create: `tests/run_portal_input_adjustangles_registration_tests.cmd`
- Modify: `tests/PortalFileLogPolicyTests.cpp`

**Interfaces:**
- Consumes: `src/Hooks/Hooks.cpp`, `U::PortalFileLog::ShouldCapture(const char*)`
- Produces: source-level registration invariant and exact probe-tag policy.

- [ ] **Step 1: Add the failing registration test**

The PowerShell test reads `src/Hooks/Hooks.cpp`, removes `//` suffixes line by line,
and requires exactly one active match for `InputAngles::Init();`.

- [ ] **Step 2: Add the failing focused-log assertion**

Add:

```cpp
Expect(ShouldCapture("[PortalInputAdjustAnglesProbe] stage=summary initAttempted=true."),
    "late AdjustAngles probes must reach the focused file log");
```

- [ ] **Step 3: Run both tests and verify RED**

Run:

```text
tests\run_portal_input_adjustangles_registration_tests.cmd
tests\run_portal_file_log_policy_tests.cmd
```

Expected: UTF-8 注册测试通过；policy test 因新标签尚未进入 allowlist 而失败。注册测试不得制造虚假 RED。

### Task 2: Preserve Registration and Add Independent Late Resolver

**Files:**
- Inspect: `src/Hooks/Hooks.cpp`
- Modify: `src/Hooks/Input/InputAngles.cpp`

**Interfaces:**
- Produces: private `std::uintptr_t ResolveLateTarget()`.
- Consumes: the exact `CInput::AdjustAngles` long signature already used by `Offsets::Init()`.

- [ ] **Step 1: Confirm `InputAngles::Init();` remains one active executable line; do not edit it when the invariant already passes**
- [ ] **Step 2: Add a read-only late scan with the exact same long signature**
- [ ] **Step 3: Compare initial/stored/late addresses without writing any of them**
- [ ] **Step 4: Run the registration test and verify GREEN**

### Task 3: Implement the Read-Only Late Probe

**Files:**
- Modify: `src/Hooks/Input/InputAngles.h`
- Modify: `src/Hooks/Input/InputAngles.cpp`
- Modify: `src/Portal/PortalBspCommands.cpp`
- Modify: `src/Util/Logger/PortalFileLog.h`

**Interfaces:**
- Produces: `void Hooks::InputAngles::LogLateDiagnosticProbe()`.
- Consumes: private `ResolveLateTarget()` and the existing fail-closed `ValidateTarget` implementation.

- [ ] **Step 1: Track init/hook lifecycle without changing behavior**

Record `initAttempted`, `initCompleted`, `initialTarget`, `hookCreateAttempted`, and
`hookCreated` around existing control flow.

- [ ] **Step 2: Add stable validation logging for stored and late targets**

Emit one summary and one validation record per target under
`[PortalInputAdjustAnglesProbe]`; use `WriteAlwaysFormat` so the late query cannot be
lost to console-interface timing.

- [ ] **Step 3: Call the probe only from the no-argument query path**

Do not call it from the `<0|1>` mutation path and do not update any validation flag
from the probe result.

- [ ] **Step 4: Add the exact probe tag to focused routing and verify GREEN**

Run both focused tests from Task 1.

### Task 4: Verify and Deploy

**Files:**
- Modify: `docs/superpowers/plans/2026-08-09-portal-official-view-reorientation.md`

- [ ] **Step 1: Run all existing focused runners plus the new registration runner**
- [ ] **Step 2: Run `git diff --check`**
- [ ] **Step 3: Build `src/l4d2_base.sln` as Debug x86**
- [ ] **Step 4: Copy `L4D2_Portal.dll` and launcher to the game root**
- [ ] **Step 5: Record hashes/timestamps and the exact two-command test procedure**

No commit is performed in this task because the established project rule requires game validation first.

---

## Execution Record

- Registration invariant: initial PowerShell `Get-Content` parsing produced a false RED because it mishandled the UTF-8/LF boundary. Raw bytes, UTF-8 decoding and `git diff` proved the call is active; the corrected UTF-8 test passes. Product registration code was not changed.
- Probe log policy: observed RED (`late AdjustAngles probes must reach the focused file log`), then GREEN after adding the exact `[PortalInputAdjustAnglesProbe]` allowlist entry.
- Runtime probe: implemented init lifecycle, initial/stored/late target, complete PE/opcode/constant validation, hook-create and ABI status logging. Query path remains read-only and does not update any gate.
- Regression: the six existing view-reorientation runners plus the new registration runner all exited `0`.
- Build: Debug x86 MSBuild exited `0`; 5 existing `convar.cpp` C4355 warnings, 0 errors.
- Deploy: source and game-root DLL both length `1333760`, timestamp `2026-08-09 19:32:05`, SHA-256 `37B00AEA88AF73FB629383866098833EB338FBC8C7460904FF5E2AB3ACB018BB`. Launcher source/destination SHA-256 remains `48EB5FCCEAFAD50F1A7B0F04C612F20E899680C2226351D6450036D5DEBED744`.
- Next gate: user runs the two-command query/flush diagnostic after entering a map and moving the view; no portal placement or traversal is required.

### Runtime Gate Result: High-speed Continuity Test 7

- Loaded build: `pe-6A7878B5` from the game-root `L4D2_Portal.dll`.
- Initialization and creation gates passed: `initAttempted=true`, `initCompleted=true`, `hookCreateAttempted=true`, `hookCreated=true`.
- Stored target validation passed completely and the first call matched `I::IInput`: `signatureValidated=true`, `abiCheckCompleted=true`, `abiValidated=true`.
- `lateTarget=0` is expected after successful MinHook installation because the live function prologue no longer matches the original long signature. It is not a mutation gate; the stored target remains in the validated executable/code range and all fixed-offset opcode/constant checks remain true.
- No input-hook failure, instance mismatch or reorientation lifecycle record occurred while the feature remained disabled.
- Decision: runtime Hook gate is complete. Proceed without further code changes to the default-off `portal_visual_reorientation 0/1` floor-to-wall A/B.

### Runtime Probe Follow-up: CP936 Comment Continuation Fix

- [x] Strengthen `PortalInputAdjustAnglesRegistrationTests.ps1` so it fails when the physical line immediately before `InputAngles::Init();` ends with a non-ASCII byte directly before LF.
- [x] Observe RED against the current mixed UTF-8/LF `Hooks.cpp`: preceding byte was `0xAE`; test exited `1` with the expected CP936 continuation message.
- [x] Replace only the `ClientMode::Init()` trailing Chinese comment with an ASCII comment; do not remove, reorder or change `ClientMode::Init()`.
- [x] Re-run the registration test and observe GREEN after rebuilding the artifact.
- [x] Build Debug x86 and use `dumpbin /disasm /relocations` to require both `?Init@ClientMode@Hooks@@YAXXZ` and `?Init@InputAngles@Hooks@@YAXXZ` in `CGlobal_Hooks::Init()`: calls appear in order at object offsets `0x6A`, `0x6F`, `0x74` for ClientMode, InputAngles and ClientPrediction.
- [x] Run all seven focused runners, `git diff --check`, deploy matching DLL/EXE, and collect the same two-command runtime probe before enabling the A/B switch. Seven focused runners pass; Debug x86 build reports 0 warnings/0 errors for the incremental `Hooks.cpp` rebuild; source/game-root DLL length `1333760`, timestamp `2026-08-09 20:55:17`, SHA-256 `0965348A823E9D12762F194216F26E45B3847CD0269044F1F9A320D2FD7697C2` match. Runtime probe remains the next user gate.
