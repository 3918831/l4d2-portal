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

## Task 8: Seed the First Post-Commit Render Eye from the Official Command-End Eye

**Files:**

- Modify: `tests/PortalEyeContinuityTests.cpp`
- Modify: `tests/PortalTransitionDecisionTests.cpp`
- Modify: `src/Portal/PortalEyeContinuity.h/.cpp`
- Modify: `src/Portal/PortalTransitionDecision.h/.cpp`
- Modify: `src/Portal/PortalBspCommands.cpp`
- Modify: `src/Portal/PortalTransition.h/.cpp`
- Modify: `src/Portal/PortalTransitionSimulator.cpp`
- Update: this plan's execution ledger

**Single-factor contract:**

- Keep the verified view-reorientation factor unchanged and enabled during both game groups.
- Add a separate default-off `portal_visual_commitseed <0|1>` switch. Its only behavior mutation is the render-eye candidate on the predictor restart frame.
- At successful teleport commit, derive the safe exit-space seed as `officialExitCommandEndOrigin + (newEye - newOrigin)`. Retain the publication time, transaction id and source name `official-command-end` with that seed.
- Consume the seed once, only with the matching pending eye-continuity transaction. Clamp publication-to-first-render elapsed time to `[0, interval_per_tick]` before advancing the seed by exit velocity.
- When the new factor is enabled and the finite seed is valid, the shadow predictor restart frame must return `applied=true` and use the advanced seed instead of the already-full-tick-advanced first CalcView baseline. Disabled, missing, stale or non-finite seed input must preserve the current restart behavior exactly (`applied=false`, baseline eye unchanged).
- Reuse the existing candidate exit-depth, BSP leaf, contents and outside-world checks after the pure predictor result. Any failure must reset both continuity and predictor state and return the original engine view, as today.
- Add edge-only transaction diagnostics to the existing `[PortalEyeShadowPredictor]` line: factor enabled, seed candidate/valid/applied, source, elapsed, seed eye and advanced seed eye. Do not add per-frame uncompressed logging.
- Reset the pending commit seed on portal transition reset, failed publication, A/B disable edge, safety fallback and after its one matching consumption.
- Do not change origin, velocity, base velocity, movement, move type, ground entity, collision, BSP lifecycle, near clip, reorientation rate, input angles or any old prohibited path.

- [x] RED: extend the pure eye-continuity runner so a valid restart seed is required to apply on the restart frame, advance by a hand-derived elapsed-time fixture, and fail closed for disabled/non-finite/stale seed input. Run `tests\run_portal_eye_continuity_tests.cmd` and record the expected contract failure before product edits.
- [x] RED: require the decision switch to default off and round-trip independently. Run `tests\run_portal_transition_decision_tests.cmd` and record the missing-symbol failure.
- [x] GREEN: implement the minimum pure restart-seed contract and decision switch; both focused runners must exit 0.
- [x] Integrate commit publication, one-shot matching consumption, command/status output and compressed diagnostics without changing the disabled path.
- [x] Run the existing reorientation, eye-continuity, decision, render-cadence, file-log policy and transform runners, then Debug x86 MSBuild and `git diff --check`.
- [x] Review only the Task 8 diff against this contract. Do not stage, commit, push or deploy from a subagent. Root deploys the verified DLL for user A/B.

**Game gate:** keep `portal_visual_reorientation 1` for both groups; compare only `portal_visual_commitseed 0/1`. Success requires marked floor-to-wall transactions to retain the verified sub-degree first-view angle delta while materially reducing the current `18.390..31.530` unit teleport-commit-to-first-main-view eye delta, with no wall-to-wall or airborne regression.

---

## Task 9: Diagnose Delayed Cross-Leaf Eye-Handoff Fallback

**Files:**

- Create: `src/Portal/PortalEyeLeafHandoffDiagnostics.h`
- Create: `src/Portal/PortalEyeLeafHandoffDiagnostics.cpp`
- Create: `tests/PortalEyeLeafHandoffDiagnosticsTests.cpp`
- Create: `tests/run_portal_eye_leaf_handoff_diagnostics_tests.cmd`
- Modify: `src/Portal/PortalTransition.cpp`
- Modify: `src/Util/Logger/PortalFileLog.h`
- Modify: `tests/PortalFileLogPolicyTests.cpp`
- Modify: `src/l4d2_base.vcxproj`
- Modify: `src/l4d2_base.vcxproj.filters`
- Update: `docs/portal-bsp-physical-traversal-and-continuity-technical-summary.zh-CN.md`
- Update: this plan's execution ledger

**Interfaces:**

- Consumes: the existing `EyeSafetySnapshot`, `CTraceFilterWorldAndPropsOnly`, `MASK_PLAYERSOLID`, shadow-predictor candidate eye, original engine eye, transaction id, render frame/call ordinal, candidate exit depth, and current `candidate-leaf-mismatch` fallback reason.
- Produces: a pure `PortalEyeLeafHandoffDiagnostics::Evaluate(...)` result and one edge-only `[PortalEyeLeafHandoffProbe]` record. The result is diagnostic evidence only and must not participate in the safety decision.

**Single-factor contract:**

- Preserve the current fail-closed order and result exactly: `candidate-leaf-mismatch` must still reset eye history, shadow predictor and pending commit seed, then return the original engine view.
- Run the new probe only when the existing decision has reached `candidate-leaf-mismatch`; do not add a per-render-frame trace or uncompressed log stream.
- Capture both candidate and original point snapshots: BSP leaf, world contents, `MASK_PLAYERSOLID` contents, outside-world state and availability.
- Trace `originalEye -> candidateEye` and `candidateEye -> originalEye` through world and props using `MASK_PLAYERSOLID`. Record for both directions: availability, fraction, `startsolid`, `allsolid`, and trace end position.
- The pure diagnostic evaluation reports `geometryClear=true` only when both snapshots are available, finite and non-solid/non-outside with positive leaves, the leaves differ, both traces are available, neither direction starts/all-solid, and both fractions reach the full segment within the existing trace fraction tolerance. This value is observational; it must not authorize the candidate.
- Record candidate/original distance, fallback delta vector/length, candidate exit depth, current reason, transaction/frame/call identity, and explicit `diagnosticMutation=false viewMutation=false physicsMutation=false` fields.
- Route the exact `[PortalEyeLeafHandoffProbe]` tag through the focused portal log policy and cover the exact runtime spelling in its test.
- Do not add a user-facing switch. Existing `portal_visual_transactiondiag 1` gates the edge record; all stable runtime factors and defaults remain unchanged.
- Do not modify origin, velocity, base velocity, movement, move type, ground entity, collision, BSP lifecycle, near clip, reorientation, input angles, or any prohibited legacy path.

- [ ] **Step 1 — RED pure contract:** write literal-fixture tests proving a safe adjacent-leaf/bidirectionally clear segment reports `geometryClear=true`, while non-finite input, unavailable/outside/solid/invalid-leaf snapshots, partial traces, and `startsolid`/`allsolid` in either direction fail closed. Run `tests\run_portal_eye_leaf_handoff_diagnostics_tests.cmd`; it must fail because the product contract does not exist.
- [ ] **Step 2 — GREEN pure contract:** implement only the pure snapshot/trace/evaluation POD contract required by Step 1, then rerun the focused runner to exit 0.
- [ ] **Step 3 — RED/GREEN log routing:** require the exact runtime tag `[PortalEyeLeafHandoffProbe]` in `PortalFileLogPolicyTests.cpp`, observe the focused policy runner fail, then add the exact allowlist entry and rerun it to exit 0.
- [ ] **Step 4 — Runtime integration:** at the existing `candidate-leaf-mismatch` edge, preserve the rejected candidate and both safety snapshots, issue the two read-only world/prop traces, evaluate and emit one compressed transaction record before the unchanged reset/fallback path.
- [ ] **Step 5 — Project/build integration:** add product and test files to the Visual Studio project/filters without changing unrelated project settings.
- [ ] **Step 6 — Verification:** run the new focused runner plus reorientation, eye continuity, transition decision, render cadence, file-log policy and portal transform runners; build Debug x86; run `git diff --check`; independently review only Task 9's hunks.
- [ ] **Step 7 — Deploy:** root copies the verified Debug DLL to the game root and verifies source/destination length, timestamp and SHA-256. No subagent stages, commits, pushes or launches the game.

**Game gate:** keep `portal_visual_reorientation 1`, `portal_visual_commitseed 1` and transaction diagnostics enabled. Exercise at least eight floor-to-wall crossings plus wall-to-wall and high-speed-airborne regressions. The log must contain one `[PortalEyeLeafHandoffProbe]` for every delayed `candidate-leaf-mismatch`, while all existing physical/view decisions and prior regression behavior remain unchanged.

---

## Task 10: Safely Permit Geometry-Clear Cross-Leaf Eye Handoff

**Files:**

- Modify: `src/Portal/PortalTransitionDecision.h/.cpp`
- Modify: `src/Portal/PortalBspCommands.cpp`
- Modify: `src/Portal/PortalBspPhase1.cpp`
- Modify: `src/Portal/PortalTransition.cpp`
- Modify: `src/Portal/PortalTransition.h`
- Modify: `src/Portal/PortalViewReorientation.cpp`
- Modify: `src/Util/Logger/PortalFileLog.h`
- Modify: `tests/PortalTransitionDecisionTests.cpp`
- Modify: `tests/PortalViewReorientationTests.cpp`
- Modify: `tests/PortalFileLogPolicyTests.cpp`
- Modify: `docs/portal-bsp-physical-traversal-and-continuity-technical-summary.zh-CN.md`

**Interfaces:**

- Add `bool GetPortalEyeCrossLeafHandoffEnabled()` and `void SetPortalEyeCrossLeafHandoffEnabled(bool enabled)` with a default value of `false`.
- Add `bool ShouldAllowPortalEyeCrossLeafHandoff(bool enabled, bool leafMismatch, bool geometryClear)`, returning true only for `enabled && leafMismatch && geometryClear`.
- Add the command `portal_visual_crossleafhandoff <0|1>` and expose the current value in `PortalBspPhase1::LogStatus`.
- Keep the existing `PortalEyeLeafHandoffDiagnostics::Evaluate(...)` as the single geometry contract; do not duplicate its tolerance or safety truth table in transition code.
- Change the verified defaults of `g_portalViewReorientationEnabled` and `g_portalVisualCommitSeedEnabled` to `true`; their Hook/runtime validation remains fail closed. Keep Phase 1 itself user-controlled and keep the new cross-leaf factor default-off.
- Route exact edge tags `[PortalEyeLeafHandoffDecision]` and `[PortalEyeReleaseTailProbe]` through the focused file log.
- Tune `PortalViewReorientation` to the approved constants: airborne cap `600`, grounded cap `1200`, acceleration `2000`, settled rate `300` degrees/second.

**Single-factor contract:**

- Add a separate default-off `portal_visual_crossleafhandoff <0|1>` A/B switch. Keep reorientation, commit seed and all verified physical factors unchanged in both groups.
- Preserve the existing fail-closed checks for invalid/negative exit depth, unavailable or non-finite snapshots, outside-world points, player-solid contents, invalid leaves, incomplete traces, partial fractions, and `startsolid`/`allsolid`.
- Only when the current rejection reason would be `candidate-leaf-mismatch` and the already-tested bidirectional evaluator returns `geometryClear=true`, the enabled factor may keep the current predictor candidate instead of performing the mismatch reset. Log one edge-only decision with `decision=allow-safe-cross-leaf` and explicit no-physics-mutation fields.
- Disabled mode and every non-clear result must preserve the current mismatch fallback byte-for-behavior: reset eye history, shadow predictor and pending seed, then return the original engine view.
- In the same build, add read-only geometry/residual evidence to `horizon-expired` release-tail edges. It must not change tail lifetime or output, so the A/B behavior factor remains cross-leaf authorization only.
- The cross-leaf branch must not modify origin, velocity, base velocity, movement, move type, ground entity, collision, BSP lifecycle, near clip, input angles, or any prohibited legacy path. The only reorientation change in this artifact is the separately authorized four-constant speed tuning above; its state machine, Hook and lifecycle remain unchanged.

- [x] **Step 1 — RED cross-leaf decision:** add literal truth-table tests proving the new switch defaults off and authorization is false unless all three inputs are true. Run `tests\run_portal_transition_decision_tests.cmd`; expect failure because the API is absent.
- [x] **Step 2 — GREEN decision/defaults:** implement only the new decision/getter/setter and change the two already-verified defaults to on. Rerun the decision runner to exit 0.
- [x] **Step 3 — RED half-second reorientation:** add real controller tests for `179.8` and `-179.8` degrees at 30, 60 and 300 FPS. Each must remain monotonic, never cross zero and complete in at most `0.5s`. Run `tests\run_portal_view_reorientation_tests.cmd`; expect the existing `120/240/400/60` implementation to fail the duration assertion.
- [x] **Step 4 — GREEN reorientation tuning:** change only the four approved constants to `600/1200/2000/300`, update existing literal rate/cap assertions, and rerun the focused runner to exit 0.
- [x] **Step 5 — RED/GREEN command and log surfaces:** add exact command registration/runtime behavior coverage following the existing commit-seed command pattern; add exact focused-log tests for `[PortalEyeLeafHandoffDecision]` and `[PortalEyeReleaseTailProbe]`; observe failures before registering/routing them, then rerun to exit 0.
- [x] **Step 6 — Runtime safe-cross-leaf integration:** on a leaf mismatch, capture the original snapshot and bidirectional world/props traces whenever the factor is enabled or transaction diagnostics are enabled. Evaluate once through `PortalEyeLeafHandoffDiagnostics::Evaluate`. If and only if `ShouldAllowPortalEyeCrossLeafHandoff(...)` returns true, clear the mismatch fallback for that frame and retain the predictor candidate. Continue to re-evaluate safety on every later mismatching output frame. Log the first allow edge for the transaction as `[PortalEyeLeafHandoffDecision] decision=allow-safe-cross-leaf`; log any later safety rejection before the unchanged reset/fallback.
- [x] **Step 7 — Read-only horizon probe:** when `releaseTailEnded=true` with `reason=horizon-expired`, record candidate/original safety snapshots, residual, distance, leaves, contents/outside state and bidirectional trace availability/fraction/startsolid/allsolid/end in one `[PortalEyeReleaseTailProbe]` edge. It must not alter `ShadowPredictor`, tail lifetime, candidate selection or reset behavior.
- [x] **Step 8 — Status/default usability:** add the new factor and the now-default-on verified factors to status output. The test setup must require only Phase 1 enable, cross-leaf group selection, status and optional marker binds; exact clearance/nearclip/entry handoff/history/predictor/tail/rebase/server-angle/reorientation/commit-seed/diagnostics/ground-live settings must already be at their verified defaults.
- [x] **Step 9 — Full verification:** run leaf-handoff diagnostics, transition decision, view reorientation, eye continuity, render cadence, file-log policy, portal transform and command-registration runners; build Debug x86; run `git diff --check`; inspect the scoped diff for prohibited physical mutations.
- [x] **Step 10 — Independent review and deployment:** review only Task 10 hunks against this brief. After all Important findings are closed, root copies the rebuilt DLL and launcher to the game root and verifies destination length/time/SHA-256. Do not stage, commit, push or launch the game.

**Game gate:** run at least eight stable floor-to-wall crossings with `crossleafhandoff=0`, then eight with `crossleafhandoff=1`, followed by wall-to-wall and high-speed-airborne regressions. Expected partial success is removal of geometry-clear leaf-mismatch hard drops; fixed-horizon release-tail transactions may remain sticky and must be retained for the next independent factor.

**Deferred Task 11:** after Task 10 is verified, replace the fixed one-tick tail with an official-style continuous catch-up contract: preserve free-move delta every render frame, catch up at the Portal SDK rate equivalent to `frametime * 100`, cap lag at 150 units, and end only on convergence, reset or safety failure. This must receive its own default-off A/B and pure-state tests rather than being combined with Task 10.

---

## Execution Ledger

Update this table before moving to the next task. Paste concise command/result evidence; do not paste full build logs.

| Phase | Status | Evidence | Files/Artifact | Next gate |
|---|---|---|---|---|
| P1 detailed plan | complete | User confirmed design; binary seam verified as inlined `ClampAngles` inside `AdjustAngles` | this plan + design addendum | choose execution mode |
| I1 state machine RED | complete | `run_portal_view_reorientation_tests.cmd` exited 2 before the new product files existed; expected missing `PortalViewReorientation.h/.cpp` compiler errors recorded | tests only | controller implementation |
| I1 state machine GREEN | complete | `run_portal_view_reorientation_tests.cmd` and `run_portal_transform_tests.cmd` exited 0; transform C4819 warnings were pre-existing | `PortalViewReorientation.*` | switch/log routing |
| I2 switch/log routing | complete | decision and focused-log policy runners observed RED then GREEN; default-off switch, narrow routing, and final runtime-backed `portal_visual_reorientation` query/mutation command are recorded | decision/commands/logger | Hook and integration |
| I3 input Hook | complete | exact `CInput::AdjustAngles` carrier, PE/opcode/constant validator, and first-call ABI check implemented fail-closed; all six runners and Debug x86 build exited 0 | offsets + `Hooks/Input` | runtime signature/ABI observation |
| I4 transaction/render integration | complete | commit-time roll arm, living-player gate/reset, original-first `FRAME_RENDER_START` advance, and duplicate-frame guard implemented; all six regression runners exited 0 | transition/simulator/base client | build/deploy |
| I4 build/deploy | complete | 2026-08-09: `git diff --check` exit 0; focused static review found no newly enabled prohibited physical/legacy paths; all six plan runners exit 0; Debug x86 MSBuild exit 0 (0 warnings/0 errors); DLL/EXE copied to game root and source/destination length, LastWriteTime, SHA-256 match | Debug DLL/exe deployed: DLL `694464952B9C5EC2EA0D053A25B8EAADF2BCA192DAA45811F160AEAA07E702AA`; EXE `48EB5FCCEAFAD50F1A7B0F04C612F20E899680C2226351D6450036D5DEBED744` | user game A/B test |
| I4 runtime validation diagnosis | deployed, awaiting log | 2026-08-09: user query reported all validation gates false and no initialization tag; disk target/signature remained valid. Added query-time read-only probe for init lifecycle, initial/stored/late targets, complete PE/opcode/constant validation and hook-create state. UTF-8 raw-source registration test proves `InputAngles::Init()` is active; seven focused runners pass; Debug x86 build exits 0 with 5 existing C4355 warnings and 0 errors; no default switch changed. Deployed DLL length `1333760`, SHA-256 `37B00AEA88AF73FB629383866098833EB338FBC8C7460904FF5E2AB3ACB018BB` | `Hooks/Input/InputAngles.*`, query hook, focused log policy, registration test, probe design/plan | collect two-command runtime probe |
| I4 CP936 registration fix | deployed, awaiting log | Runtime probe showed stored/late target fully valid but `initAttempted=false`. `Hooks.obj` proved `InputAngles::Init()` absent between ClientMode and ClientPrediction. Root cause was a UTF-8 Chinese trailing comment ending in byte `0xAE` directly before LF under the non-`/utf-8` CP936 MSVC frontend. Test observed RED, comment-only ASCII fix produced GREEN, and rebuilt object now contains ClientMode/InputAngles/ClientPrediction calls at `0x6A/0x6F/0x74`. Seven focused runners and `git diff --check` pass; no ClientMode/CreateMove behavior changed. Deployed DLL SHA-256 `0965348A823E9D12762F194216F26E45B3847CD0269044F1F9A320D2FD7697C2` | `Hooks.cpp`, compiled-artifact registration test, design/plan addendum | collect two-command runtime probe |
| I4 runtime Hook gate | complete | `高速穿越连续性修复/7`: build `pe-6A7878B5`; init completed, hook created, stored target fully validated, and first-call `this` exactly matched `I::IInput`; command reported `hookValidated=true signatureValidated=true abiValidated=true`. Post-install late-rescan miss is expected because MinHook patches the original prologue; stored fixed-offset validation remains true. No hook/input errors recorded | user runtime log + read-only probe | floor-to-wall reorientation 0/1 A/B |
| I4 game A/B | angle factor verified; position factor still red | `高速穿越连续性修复/8`: seven marked control txns had average/max first-view angle deltas `77.051/129.558 deg`; seven marked experimental txns fell to `0.093/0.364 deg`, all Hook/ABI gates remained valid, and every experimental roll completed monotonically. Subjective stickiness nevertheless remained. The same experimental txns retained `18.390..31.530` unit teleport-commit-to-first-main-view eye deltas (average `26.479`). At restart the eye shadow predictor logged `applied=false` and only applied from the next render frame. | user runtime log + transaction/marker summaries | isolate commit-seeded first-render eye continuity |
| I5 commit-seeded first-render eye | deployed, awaiting game A/B | Strict RED/GREEN completed; two independent review findings were fixed with consecutive-restart metadata isolation and atomic enable/disable edge handling. Fresh root verification: seven runners exit 0, Debug x86 MSBuild exits 0 with 0 warnings/0 errors, and `git diff --check` is clean. The default-off render-eye-only factor was deployed with matching source/destination DLL length `1342464` and SHA-256 `1C09035A8D487DD628C0CF3F927884A0DACF898C7A1798E47CB69D121331DCA4`. | `PortalEyeContinuity.*`, transition/decision/command/status integration, focused tests, Task 8 report, deployed Debug DLL | compare `portal_visual_commitseed 0/1` in game |
| I5 game A/B | first-frame factor verified; delayed handoff still red | `高速穿越连续性修复/9`: experimental txns 11～18 all applied the official command-end seed on restart with no immediate fallback; marked first-sample eye delta average fell from control `27.727` to `6.992`. Seven of eight experimental floor-to-wall txns later failed only on `candidate-leaf-mismatch` after `29..187` render frames, producing cadence handoff deltas `5.688..26.389` units versus expected `0.787..2.602`; txn 12 instead ended release tail with a `4.628` versus `1.995` unit handoff. Wall-to-wall txns 19～23 remained normal. | user log, transaction/marker/predictor/cadence correlation, technical summary §12.5 | add read-only candidate trace/contents diagnostics before changing the leaf safety rule |
| I6 delayed leaf-handoff probe | deployed, awaiting game diagnostics | Pure-contract and exact-tag policy RED/GREEN completed. Independent review found two Important issues: over-one fractions were not fail-closed and the first review artifact was truncated. Fix Round 1 added a `1.0005f` RED regression, enforced `fraction <= 1.0f`, rebuilt a complete 596-line scoped review package, and re-review passed. Fresh root verification: seven runners exit 0; Debug x86 MSBuild exits 0 with 0 warnings/0 errors; `git diff --check` exits 0. Source/deployed DLL length `1349632`, SHA-256 `C58A28633ED010880CEF8E2B1BC753C36A0E53221954A5402999DE2A65D6AA1F`. The evaluator remains log-only and the original mismatch reset/fallback path is unchanged. | `PortalEyeLeafHandoffDiagnostics.*`, transition adapter/probe, focused tests/runner, logger policy, project metadata, summary §12.5, deployed Debug DLL | collect floor-to-wall `[PortalEyeLeafHandoffProbe]` evidence plus wall/high-speed regressions |
| I6 game diagnostics | complete; two delayed exit factors isolated | `高速穿越连续性修复/10`: all eight stable floor-to-wall marks remained sticky, but they split exactly 4/4. Txns 6/9/10/11 hit leaf mismatch; all four were finite, non-solid, in-world, positive-depth, valid-leaf and bidirectionally unobstructed (`fraction=1`, no start/all-solid), so `geometryClear=true`. Txns 5/7/8/12 instead reached one-tick `horizon-expired` with `2.746..10.699` units residual. All 15 commits retained `moveType=2` and speed magnitude, and marked max angle delta stayed below `0.084` degrees. | user log, deterministic transaction replay assertion, technical summary §12.5 Test 10 | implement Task 10 safe-cross-leaf default-off A/B; defer continuous tail to Task 11 |
| I7 safe cross-leaf handoff | deployed; awaiting game A/B | 2026-08-11: strict RED/GREEN completed for decision/defaults, half-second reorientation and command/log surfaces. Independent review found one Important coverage gap; Fix Round 1 extracted the runtime-consumed authorization/probe-edge controller, added allow→recheck→reject/reset and diagnostics-gated one-shot horizon tests, and scoped re-review returned `ADDRESSED`, task quality Approved. Root reran all 10 runners (`10/10`, failed `0`), Debug x86 MSBuild (`0` warnings, `0` errors) and `git diff --check` (exit `0`). Deployed source/destination identities match: DLL length `1355776`, SHA-256 `297188F42DF3EA09589640374641330DACA5033D7959FEFBEF1A012CF35F339B`; EXE length `58880`, SHA-256 `48EB5FCCEAFAD50F1A7B0F04C612F20E899680C2226351D6450036D5DEBED744`. | Task 10 product/tests/docs/report, initial/fix scoped review packages, verified game-root DLL/EXE | floor-to-wall `crossleafhandoff 0/1` game A/B plus wall/high-speed regressions |

### 2026-08-09 A/B conclusion and next single factor

- The official roll preservation/reorientation factor is working. It removes the abrupt angle seam without changing physical position or velocity, but it does not by itself remove the reported floor-to-wall stickiness.
- The common remaining signal is positional: all marked experimental transactions jump `18.390..31.530` units from teleport commit to the first main-view sample even though their angle delta is at most `0.364` degrees.
- In projected transaction 10, the physical command path advances a full tick from the committed exit origin, while the official remaining-command estimate advances only part of that tick. The first main-view eye therefore appears `31.4285` units from the commit eye. The current eye-history adapter consumes the pending transaction, but the shadow predictor uses that already-advanced first main view as its restart baseline and reports `applied=false` on the restart frame.
- Portal SDK 2013 transforms both interpolated and uninterpolated eye positions when `PlayerPortalled` is consumed, then carries those transformed eye histories into `UpdatePortalEyeInterpolation`. This supports seeding the first post-commit render eye from the transaction's transformed/official-command-end eye instead of waiting for the first already-advanced CalcView result.
- Next factor: keep reorientation enabled in both groups and add a separate default-off, render-eye-only commit seed A/B. On predictor restart, use a validated exit-space transaction eye advanced only by commit-to-render elapsed time; apply it on the restart frame, retain the existing leaf/contents/depth fail-closed checks, and log `seedSource`, elapsed time, first-frame application and residual. Do not mutate origin, velocity, movement, ground or BSP state.
- Defer roll-coupled `EyeFootPosition` replication until this first-frame positional seam is independently tested. It is an official difference, but it is not the universal discriminator in this log because sticky transactions also occurred with low view/physical-eye residuals.

## Plan Self-Review

- Spec coverage: root cause, official roll clamp difference, official rate/acceleration, transaction arm, render-frame cadence, fail-closed Hook, A/B and regressions are all mapped to tasks.
- Scope: first artifact contains only ordinary roll preservation/reorientation; mouse inversion and pitch-axis reorientation remain separate factors.
- Type/ABI consistency: pure controller uses owned POD angles; engine integration converts to/from `QAngle`; Hook uses verified x86 `__thiscall(this,int,float)` and `__fastcall` detour.
- Safety: no physical fields or BSP lifecycle are written; target/constant/instance validation gates mutation.
- Workspace safety: pre-existing dirty files are not treated as disposable and no implementation commit is authorized before game validation.
- Completeness scan: every task names concrete files, symbols, commands, expected results and exit gates; no unresolved implementation slot remains.
