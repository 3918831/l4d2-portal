# Portal Official View Reorientation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变已验证 BSP、位置和速度连续性基线的前提下，保留 floor-to-wall 穿越产生的完整 roll，并按 Portal SDK 2013 的速率/加速度逐渲染帧回正，消除下一输入命令的 `+/-50` 瞬时截断。

**Architecture:** 新增一个无引擎依赖的 `PortalViewReorientation::Controller` 作为唯一角度状态机；穿越提交只负责 arm，`FRAME_RENDER_START` 每帧只推进一次；L4D2 优化构建中 `ClampAngles` 已内联，因此在其官方承载点 `CInput::AdjustAngles(int,float)` 调原函数后，只在通过运行态验证且事务活动时恢复调用前 roll。控制组保持原版 L4D2 行为，实验组由单一开关启用。

**Tech Stack:** C++17、x86 `__thiscall`/`__fastcall`、MinHook、Source SDK 2013 参考代码、现有 `PortalTransition`/`PortalTransitionSimulator`、MSVC `cl.exe` 独立测试脚本、VS2022 MSBuild。

## Global Constraints

- 不重新启用旧 noclip、movement mutation、出口 push 或 `PortalMaskRepair`。
- 不修改 origin、velocity、base velocity、move type、ground entity 或 brush 生命周期。
- 不用 `ClientMode::CreateMove` 事后恢复作为正式实现，不做 render-only 角度覆盖。
- Hook 解析或 ABI 验证失败时必须 fail closed：记录日志但绝不 mutation。
- 第一轮只实现完整 roll 保留与普通 roll reorientation；倒置鼠标和 Portal 特殊 pitch reorientation 是后续独立因子。
- 同一 `framecount` 最多推进一次，不在 `CalcPlayerView` 中推进。
- 所有逐帧日志必须按状态边沿、整数角区间或事务摘要压缩。
- 工作区已有本阶段前序未提交修改。实施过程中不得误提交/覆盖这些修改；每个任务先回填本文执行记录，待游戏 A/B 通过后再由用户授权统一提交。
- 每次修改先写 RED 测试、确认预期失败，再写最小 GREEN 实现。

---

## File Map

### New files

- `src/Portal/PortalViewReorientation.h`：纯状态、输入/输出结构和策略函数。
- `src/Portal/PortalViewReorientation.cpp`：Portal SDK 普通 roll 回正公式。
- `src/Hooks/Input/InputAngles.h`：`CInput::AdjustAngles` Hook 声明和验证状态。
- `src/Hooks/Input/InputAngles.cpp`：目标验证、ABI 首调验证和事务内 roll 恢复。
- `tests/PortalViewReorientationTests.cpp`：状态机与 mutation gate 的 RED/GREEN 测试。
- `tests/run_portal_view_reorientation_tests.cmd`：x86 独立测试编译/执行脚本。

### Modified files

- `src/Util/Offsets/Offsets.h/.cpp`：保存 `m_dwInputAdjustAngles`，使用已核实长签名解析。
- `src/Hooks/Hooks.h/.cpp`：注册新的输入角度 Hook。
- `src/Portal/PortalTransitionDecision.h/.cpp`：新增默认关闭的 A/B 开关。
- `src/Portal/PortalBspCommands.cpp`：新增 `portal_visual_reorientation <0|1>`。
- `src/Portal/PortalTransition.h/.cpp`：持有状态机、arm/reset、输入 gate 和 render-start 推进。
- `src/Portal/PortalTransitionSimulator.cpp`：成功提交并生成 transaction id 后 arm。
- `src/Hooks/BaseClient/BaseClient.cpp`：在 `FRAME_RENDER_START` 调用一次推进。
- `src/Util/Logger/PortalFileLog.h`：放行既有 `PortalFloorWall*` 及新角度标签。
- `tests/PortalFileLogPolicyTests.cpp`：覆盖日志路由。
- `tests/PortalTransitionDecisionTests.cpp`：覆盖默认值和 A/B 开关。
- `src/l4d2_base.vcxproj`、`src/l4d2_base.vcxproj.filters`：纳入新源码。
- `docs/superpowers/specs/2026-08-09-portal-official-view-reorientation-design.md`：记录二进制接缝复核。
- `docs/portal-bsp-physical-traversal-and-continuity-technical-summary.zh-CN.md`：游戏验证后记录结果与后续 I5。

---

## Task 1: Lock the Pure State-Machine Contract with RED Tests

**Files:**

- Create: `tests/PortalViewReorientationTests.cpp`
- Create: `tests/run_portal_view_reorientation_tests.cmd`
- Test target (not yet present): `src/Portal/PortalViewReorientation.h/.cpp`

**Interfaces to freeze:**

```cpp
namespace PortalViewReorientation
{
    enum class Mode { Roll };

    struct Angles
    {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
    };

    struct ArmRequest
    {
        std::uint32_t transactionId = 0;
        Angles committedAngles;
        Mode mode = Mode::Roll;
    };

    struct FrameInput
    {
        int frameCount = -1;
        float frameTime = 0.0f;
        bool onGround = false;
        Angles liveAngles;
    };

    struct FrameResult
    {
        bool advanced = false;
        bool completed = false;
        bool duplicateFrame = false;
        Angles angles;
        float rate = 0.0f;
        float upZ = 1.0f;
    };

    class Controller
    {
    public:
        bool Arm(const ArmRequest& request);
        FrameResult Advance(const FrameInput& input);
        void Reset();
        bool IsActive() const;
        std::uint32_t GetTransactionId() const;
        float GetCurrentRoll() const;
        float GetRate() const;
    };

    bool ShouldRestoreInputRoll(
        bool featureEnabled,
        bool signatureValidated,
        bool abiValidated,
        bool localInputInstance,
        bool transactionActive);
}
```

- [ ] Create the test runner with the established repository form:

```bat
@echo off
call "D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b %errorlevel%

if not exist tests\obj\portal_view_reorientation mkdir tests\obj\portal_view_reorientation

cl.exe /nologo /EHsc /std:c++17 tests\PortalViewReorientationTests.cpp src\Portal\PortalViewReorientation.cpp /Fo:tests\obj\portal_view_reorientation\ /Fe:tests\PortalViewReorientationTests.exe
if errorlevel 1 exit /b %errorlevel%

tests\PortalViewReorientationTests.exe
```

- [ ] Write tests for all of these exact behaviors:

  - `179.8 -> 0` and `-92.5 -> 0` are monotonic and never cross zero.
  - First airborne frame at `1/300 s` starts at `400/300 = 1.333... deg/s`; the roll step uses the newly accelerated rate.
  - While `upZ < 0.75`, airborne rate never exceeds `120 deg/s`; grounded rate never exceeds `240 deg/s`.
  - Once `upZ >= 0.75`, rate approaches `60 deg/s` by at most `400 * frameTime` per frame.
  - Duplicate `frameCount` returns `duplicateFrame=true` and does not change roll/rate.
  - Re-arm while active replaces canonical angles/transaction but retains the current official rate; arm after completion begins from rate zero.
  - `Reset()` disables the transaction and clears rate/frame identity.
  - `ShouldRestoreInputRoll` is true only when all five gates are true.

- [ ] Run `tests\run_portal_view_reorientation_tests.cmd`.

  Expected RED: compilation fails because `PortalViewReorientation.h/.cpp` do not exist.

- [ ] Update the execution ledger at the end of this plan with the RED command and first relevant compiler error.

**Checkpoint:** no product code and no commit.

---

## Task 2: Implement the Official Roll Reorientation Controller

**Files:**

- Create: `src/Portal/PortalViewReorientation.h`
- Create: `src/Portal/PortalViewReorientation.cpp`
- Test: `tests/PortalViewReorientationTests.cpp`

**Official constants and ordinary roll branch:**

```cpp
namespace
{
    constexpr float kReorientationRate = 120.0f;
    constexpr float kReorientationAccelerationRate = 400.0f;
    constexpr float kUprightThreshold = 0.75f;
    constexpr float kSettledRate = kReorientationRate * 0.5f;
    constexpr float kCompletionEpsilon = 0.001f;

    float ComputeUpZ(const PortalViewReorientation::Angles& angles)
    {
        const float pitch = angles.pitch * 0.01745329251994329577f;
        const float roll = angles.roll * 0.01745329251994329577f;
        return std::cos(pitch) * std::cos(roll);
    }
}
```

- [ ] Implement `Arm` with these validity rules:

  - reject `transactionId == 0` and non-finite angles;
  - preserve `m_rate` only when already active;
  - normalize roll into `[-180, 180]`;
  - arm even for small non-zero roll, but complete immediately on the next valid frame if within epsilon.

- [ ] Implement `Advance` in official order:

```cpp
angles.pitch = input.liveAngles.pitch;
angles.yaw = input.liveAngles.yaw;
angles.roll = m_roll;
const float upZ = ComputeUpZ(angles);

if (upZ < 0.75f)
{
    m_rate += input.frameTime * 400.0f;
    m_rate = std::min(m_rate, input.onGround ? 240.0f : 120.0f);
}
else
{
    m_rate = Approach(60.0f, m_rate, input.frameTime * 400.0f);
}

const float step = input.frameTime * m_rate;
m_roll = Approach(0.0f, m_roll, step);
angles.roll = m_roll;
```

- [ ] Do not implement pitch-axis rotation, random `180 -> -180`, air-reorientation cvar, vehicle checks or mouse inversion in this task. Record these as intentionally deferred, not silently approximated.

- [ ] Run `tests\run_portal_view_reorientation_tests.cmd`.

  Expected GREEN: `PortalViewReorientation tests passed`.

- [ ] Run `tests\run_portal_transform_tests.cmd` to ensure no transform regression.

- [ ] Record both outputs in the execution ledger.

**Checkpoint:** new pure files may be staged only if the user later authorizes a commit; do not commit now.

---

## Task 3: Add the Single-Factor A/B Switch and Focused Log Routing

**Files:**

- Modify: `tests/PortalTransitionDecisionTests.cpp`
- Modify: `tests/PortalFileLogPolicyTests.cpp`
- Modify: `src/Portal/PortalTransitionDecision.h/.cpp`
- Modify: `src/Portal/PortalBspCommands.cpp`
- Modify: `src/Util/Logger/PortalFileLog.h`

- [ ] Add RED assertions:

```cpp
Expect(!PortalTransitionDecision::GetPortalViewReorientationEnabled(),
    "official view reorientation must default off for a clean A/B control");
PortalTransitionDecision::SetPortalViewReorientationEnabled(true);
Expect(PortalTransitionDecision::GetPortalViewReorientationEnabled(),
    "the experiment switch must enable only official view reorientation");
PortalTransitionDecision::SetPortalViewReorientationEnabled(false);
```

```cpp
Expect(ShouldCapture("[PortalInputAdjustAnglesHook] signatureValidated=true."),
    "input-angle hook validation must reach the focused file log");
Expect(ShouldCapture("[PortalInputRollClamp] txn=4 restored=true."),
    "transaction roll restoration must reach the focused file log");
Expect(ShouldCapture("[PortalViewReorientationFrame] txn=4 roll=90."),
    "compressed reorientation frames must reach the focused file log");
Expect(ShouldCapture("[PortalFloorWallBegin] txn=4."),
    "floor-to-wall diagnostics must not be dropped by the allowlist");
```

- [ ] Run `tests\run_portal_transition_decision_tests.cmd` and `tests\run_portal_file_log_policy_tests.cmd`.

  Expected RED: missing decision symbols and log-routing assertions fail.

- [ ] Add `Get/SetPortalViewReorientationEnabled`; storage defaults to `false`.

- [ ] Add command:

```cpp
CON_COMMAND(portal_visual_reorientation,
    "Toggle official full-roll preservation and render-frame reorientation: portal_visual_reorientation <0|1>")
```

  Query logs must include `enabled`, `hookValidated`, `activeTxn`; mutation logs must state `scope=view-angles-only physicsMutation=false`.

- [ ] Add the four new needles to focused routing. Do not broadly enable all `PortalTransition`/`PortalSim` noise.

- [ ] Re-run both scripts.

  Expected GREEN: both test executables print their existing `... passed` line.

- [ ] Record RED/GREEN evidence in the execution ledger.

**Checkpoint:** no commit because all modified files were already dirty before this plan.

---

## Task 4: Resolve and Hook the Actual L4D2 Input Clamp Carrier

**Files:**

- Create: `src/Hooks/Input/InputAngles.h`
- Create: `src/Hooks/Input/InputAngles.cpp`
- Modify: `src/Util/Offsets/Offsets.h/.cpp`
- Modify: `src/Hooks/Hooks.h/.cpp`
- Modify: `src/l4d2_base.vcxproj`
- Modify: `src/l4d2_base.vcxproj.filters`

**Resolved target:** `CInput::AdjustAngles(int slot, float frametime)`, because current `client.dll` inlines `ClampAngles` into this function.

**Long signature:**

```text
55 8B EC 83 EC 0C D9 45 0C 56 57 8B 7D 08 51 D9 1C 24 57 8B F1
E8 ? ? ? ? D9 55 0C D9 EE DF F1 DD D8 0F 83 ? ? ? ?
8B 0D ? ? ? ? 8B 01 8B 40 4C 8D 55 F4 52 FF D0
```

The runtime validator must also verify the inlined roll sequence at fixed offsets:

- `target + 152`: `F3 0F 10 45 FC`;
- `target + 157`: `F3 0F 10 0D <absolute +50.0f>`;
- `target + 170`: `F3 0F 10 0D <absolute -50.0f>`;
- `target + 178`: `0F 2F C8 76 05 F3 0F 11 4D FC`;
- `target + 210`: `C2 08 00`.

- [ ] Add `DWORD m_dwInputAdjustAngles = 0x0;` and resolve it without `XASSERT`; absence must be logged and tolerated.

- [ ] Implement validation before `MH_CreateHook`:

  - target is inside `client.dll` `BaseOfCode..BaseOfCode+SizeOfCode`;
  - `VirtualQuery` reports an executable protection;
  - fixed opcode blocks match;
  - the two absolute constant operands point to readable floats equal to `+50.0f/-50.0f` within `0.001f`;
  - tail is `ret 8`.

- [ ] Implement x86 ABI exactly:

```cpp
using FN = void(__thiscall*)(void*, int, float);
void __fastcall Detour(void* ecx, void* edx, int slot, float frametime);
```

- [ ] On the first detour call, set `abiValidated=true` only when `ecx == I::IInput`; otherwise retain fail-closed behavior and emit one error edge.

- [ ] In every call:

```cpp
Vector before;
I::EngineClient->GetViewAngles(before);
Func.Original<FN>()(ecx, slot, frametime);
Vector after;
I::EngineClient->GetViewAngles(after);

if (ShouldRestoreInputRoll(...))
{
    const float clampedRoll = after.z;
    after.z = before.z;
    I::EngineClient->SetViewAngles(after);
    // edge-compressed transaction log
}
```

  The original function must always run exactly once. Only `after.z` may be replaced; pitch and yaw remain the original L4D2 results.

- [ ] Register `Hooks::InputAngles::Init()` after `ClientMode::Init()` and before global `MH_EnableHook`.

- [ ] Add project/filter entries and build Debug x86.

  Expected build result: `L4D2_Portal.dll` links with no unresolved symbol and the hook remains mutation-disabled by default.

- [ ] Record target pattern, validation fields and build output in the execution ledger.

**Fail-closed exit:** if any fixed offset/constant/ABI check fails, stop implementation at diagnostic mode; do not proceed with a fallback hook.

---

## Task 5: Arm at Teleport Commit and Advance Once per Render Frame

**Files:**

- Modify: `src/Portal/PortalTransition.h/.cpp`
- Modify: `src/Portal/PortalTransitionSimulator.cpp`
- Modify: `src/Hooks/BaseClient/BaseClient.cpp`
- Test: `tests/PortalViewReorientationTests.cpp`

**New `CPortalTransition` seam:**

```cpp
void ArmPortalViewReorientation(
    std::uint32_t transactionId,
    const QAngle& committedAngles,
    bool pitchReorientation);
void UpdatePortalViewReorientationAtRenderStart();
void ResetPortalViewReorientation(const char* reason);
bool IsPortalViewReorientationActive() const;
bool ShouldPreservePortalInputRoll() const;
std::uint32_t GetPortalViewReorientationTransactionId() const;
```

- [ ] Add RED tests for integration-facing state behavior: a floor-to-wall arm uses `Mode::Roll`, duplicate frame is ignored, reset returns the input gate to false.

- [ ] In `TryCommitTeleport`, call `ArmPortalViewReorientation` only after:

  - `PortalPlayerTeleport::Commit` succeeded;
  - `continuityTransactionId` is non-zero;
  - `newAngles` is finalized.

  For this first factor pass `pitchReorientation=false`; do not infer an unsupported special case.

- [ ] `ArmPortalViewReorientation` logs one begin line containing txn, full pitch/yaw/roll, mode, prior active/rate and `physicsMutation=false`.

- [ ] In `BaseClient::FrameStageNotify::Detour`, preserve the current original-call order and add:

```cpp
Table.Original<FN>(Index)(ecx, edx, curStage);
if (curStage == FRAME_RENDER_START)
    G::G_L4D2Portal.m_PortalTransition.UpdatePortalViewReorientationAtRenderStart();
```

- [ ] `UpdatePortalViewReorientationAtRenderStart` must:

  - return unless feature enabled, Hook signature+ABI validated, local living player and engine interfaces valid;
  - read live engine pitch/yaw;
  - use `(m_fFlags() & FL_ONGROUND) != 0` for the official grounded multiplier;
  - advance with `I::GlobalVars->frametime` and `framecount`;
  - write the returned complete angle once via `EngineClient->SetViewAngles`;
  - log only begin, integer-roll bucket/ground-state edges, completion and reset.

- [ ] `CPortalTransition::Reset()` must call `ResetPortalViewReorientation("portal-transition-reset")`; existing portal close, map shutdown and portal shutdown already flow through this reset.

- [ ] Re-run:

```text
tests\run_portal_view_reorientation_tests.cmd
tests\run_portal_transition_decision_tests.cmd
tests\run_portal_file_log_policy_tests.cmd
tests\run_portal_render_cadence_tracker_tests.cmd
tests\run_portal_eye_continuity_tests.cmd
tests\run_portal_transform_tests.cmd
```

  Expected: all six pass; no existing baseline default changes except the new default-off switch.

- [ ] Record test outputs in the execution ledger.

**Checkpoint:** implementation is ready for build/deploy, but not yet a validated baseline.

---

## Task 6: Build, Inspect, Deploy, and Hand Off the A/B Test

**Files:**

- Build output: `src/Debug/L4D2_Portal.dll`
- Deploy target: `J:\Games\Left 4 Dead 2 v2.2.4.3\L4D2_Portal.dll`
- Update: this plan's execution ledger

- [ ] Review `git diff --check` and targeted diffs. Confirm no old noclip/movement/exit-push/`PortalMaskRepair` path was enabled.

- [ ] Build:

```powershell
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'src\l4d2_base.sln' /p:Configuration=Debug /p:Platform=x86 /m
```

- [ ] Record DLL length, `LastWriteTime` and SHA-256.

- [ ] Copy the rebuilt DLL and launcher to the game root, then verify destination length/time/hash match the build outputs.

- [ ] Give the user this full test setup:

```text
portal_bsp_phase1 1
portal_bsp_clearance exact
portal_placement_floorsnap 1
portal_visual_nearclip 1
portal_visual_exitguard 0
portal_visual_entryhandoff 1
portal_visual_eyehistory 1
portal_visual_shadowpredictor 1
portal_visual_shadowtail 1
portal_visual_projectedhistoryrebase 1
portal_server_command_anglefix 1
portal_ground_livecommit 1
portal_visual_transactiondiag 1
portal_bsp_status
```

- [ ] Control group: `portal_visual_reorientation 0`; perform at least four floor-to-wall crossings and one wall-to-wall regression.

- [ ] Experimental group: `portal_visual_reorientation 1`; perform at least eight floor-to-wall crossings, then regress ground walk, high-speed airborne, vertical floor/ceiling loop and wall-to-wall once each.

- [ ] Flush before copying the log: `portal_log_flush`.

- [ ] Required log evidence:

  - `[PortalInputAdjustAnglesHook] signatureValidated=true ... mutation=false`;
  - `[PortalInputAdjustAnglesHook] abiValidated=true inputInstanceMatched=true`;
  - experimental txn has `[PortalInputRollClamp] ... restored=true` and no next-command `+/-50` loss;
  - `[PortalViewReorientationBegin]` followed by monotonic compressed frames and `[PortalViewReorientationEnd]`;
  - physical speed magnitude remains at the established baseline;
  - no black sky, portal body, viewmodel, wall stripe or airborne continuity regression.

- [ ] Record deployment identity and test instructions in the execution ledger.

**Stop condition:** hand control to the user for game testing. Do not implement mouse inversion or pitch reorientation in the same artifact.

---

## Task 7: Post-Test Baseline Decision

**Files:**

- Modify: `docs/portal-bsp-physical-traversal-and-continuity-technical-summary.zh-CN.md`
- Update: this plan's execution ledger

- [ ] Analyze the returned log by transaction tags, not by reading the entire file.

- [ ] If successful, record the official input seam, roll curve, regressions and DLL identity as a new baseline; then ask for commit/push authorization.

- [ ] If Hook validation failed, keep mutation disabled and plan only a signature/ABI correction round.

- [ ] If full roll is preserved but user input is wrong only while upside-down, open the separate I5 design addendum for Portal mouse inversion.

- [ ] If special wall/floor/wedge cases still require pitch-axis reorientation, open a second independent design/spec; do not fold it into the verified ordinary roll baseline.

---

## Execution Ledger

Update this table before moving to the next task. Paste concise command/result evidence; do not paste full build logs.

| Phase | Status | Evidence | Files/Artifact | Next gate |
|---|---|---|---|---|
| P1 detailed plan | complete | User confirmed design; binary seam verified as inlined `ClampAngles` inside `AdjustAngles` | this plan + design addendum | choose execution mode |
| I1 state machine RED | pending | — | tests only | expected compile failure recorded |
| I1 state machine GREEN | pending | — | `PortalViewReorientation.*` | unit tests pass |
| I2 switch/log routing | pending | — | decision/commands/logger | policy tests pass |
| I3 input Hook | pending | — | offsets + `Hooks/Input` | signature and ABI fail-closed validation |
| I4 transaction/render integration | pending | — | transition/simulator/base client | regression tests pass |
| I4 build/deploy | pending | — | Debug DLL/exe | destination identity matches |
| I4 game A/B | pending | — | user log/feedback | baseline decision |

## Plan Self-Review

- Spec coverage: root cause, official roll clamp difference, official rate/acceleration, transaction arm, render-frame cadence, fail-closed Hook, A/B and regressions are all mapped to tasks.
- Scope: first artifact contains only ordinary roll preservation/reorientation; mouse inversion and pitch-axis reorientation remain separate factors.
- Type/ABI consistency: pure controller uses owned POD angles; engine integration converts to/from `QAngle`; Hook uses verified x86 `__thiscall(this,int,float)` and `__fastcall` detour.
- Safety: no physical fields or BSP lifecycle are written; target/constant/instance validation gates mutation.
- Workspace safety: pre-existing dirty files are not treated as disposable and no implementation commit is authorized before game validation.
- Completeness scan: every task names concrete files, symbols, commands, expected results and exit gates; no unresolved implementation slot remains.
