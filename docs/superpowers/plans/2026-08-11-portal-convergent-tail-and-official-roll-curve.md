# Portal Convergent Tail and Official Roll Curve Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the delayed render-eye hard handoff by replacing the fixed one-tick shadow tail with an official-style moving-baseline convergence factor, while expressing roll recovery as the exact Portal SDK rate curve at the user-approved 5x time scale.

**Architecture:** `PortalEyeContinuity::ShadowPredictor` remains the only shadow owner, but gains an independent default-off convergent-tail mode that carries authoritative baseline motion and reduces only the remaining offset at 100 units/s. `PortalViewReorientation::Controller` keeps its public contract and render ownership, but its rate update is written using official 120/400/60/240 constants plus an explicit 5x time transform. Runtime safety remains in `PortalTransition` and is evaluated every output frame.

**Tech Stack:** C++17, existing pure C++ runners, x86 Source SDK interfaces, existing Portal transition/log/command infrastructure, VS2022 Debug x86 MSBuild.

## Global Constraints

- Do not enable old noclip, movement mutation, exit push, or `PortalMaskRepair`.
- Do not mutate player origin, velocity, base velocity, MoveType, ground entity, usercmd movement, brush state, or portal transforms.
- New eye behavior is default-off until game A/B passes; fixed-tail control remains selectable.
- Safe cross-leaf and all existing finite/depth/contents/outside/leaf/trace gates remain fail closed and are re-evaluated every candidate frame.
- Convergent mode must carry authoritative baseline delta and catch up at exactly 100 units/s with a 150-unit maximum lag; it must not use `velocity * dt` for the release phase and must not normally end on a fixed horizon.
- Roll recovery must use official 120 deg/s, 400 deg/s², 60 deg/s settled, 240 deg/s grounded constants with `officialDeltaTime = frameTime * 5.0` applied to both rate and roll integration; no new easing family is allowed. Do not preserve the current independently inflated 600/1200/2000/300 approximation.
- Same frame count advances each state machine at most once.
- Diagnostics must be lifecycle/bucket compressed and explicitly state no physics mutation.
- Existing dirty work belongs to this portal phase. Do not stage, commit, push, deploy, or launch from subagent tasks. Root deploys only after reviews and fresh verification.
- Every behavior change follows strict RED → observed expected failure → minimal GREEN.

---

## Task 1: Official-Style Convergent Shadow Tail

**Files:**

- Modify: `src/Portal/PortalEyeContinuity.h`
- Modify: `src/Portal/PortalEyeContinuity.cpp`
- Modify: `src/Portal/PortalTransitionDecision.h`
- Modify: `src/Portal/PortalTransitionDecision.cpp`
- Modify: `src/Portal/PortalTransition.h`
- Modify: `src/Portal/PortalTransition.cpp`
- Modify: `src/Portal/PortalBspCommands.cpp`
- Modify: `src/Portal/PortalBspPhase1.cpp`
- Modify: `src/Util/Logger/PortalFileLog.h`
- Modify: `tests/PortalEyeContinuityTests.cpp`
- Modify: `tests/PortalTransitionDecisionTests.cpp`
- Modify: `tests/PortalFileLogPolicyTests.cpp`
- Create: `tests/PortalVisualConvergentTailCommandRegistrationTests.ps1`
- Create: `tests/run_portal_visual_convergenttail_registration_tests.cmd`
- Modify: `docs/portal-bsp-physical-traversal-and-continuity-technical-summary.zh-CN.md`

**Interfaces:**

- Extend `ShadowPredictor::Update(...)` with a final `bool convergentTailEnabled` argument or an equivalent explicit options POD; existing callers and tests must choose the mode explicitly.
- Extend `ShadowPredictionResult` with mode, baseline-carry, catch-up, residual-before/after, maximum-lag, and terminal-jump diagnostics.
- Add `GetPortalEyeConvergentTailEnabled()` and `SetPortalEyeConvergentTailEnabled(bool)`; storage defaults to `false`.

- [x] **Step 1 — RED moving-baseline contract:** add literal tests where baseline moves every frame while the initial visual offset is 14.807 and 10.214 units. Assert that convergent mode preserves baseline movement, decreases residual monotonically by the hand-derived `100 * dt`, and never ends by horizon expiry. Run `tests\run_portal_eye_continuity_tests.cmd`; expected RED is a missing mode/result contract or fixed-horizon termination.
- [x] **Step 2 — RED maximum-lag and frame-rate contract:** add 30/60/300 FPS literal expectations plus a 160-unit lag case that becomes 150 units after the first update. Assert final snap is less than or equal to that frame's official follow distance. Re-run and observe the expected RED.
- [x] **Step 3 — GREEN pure implementation:** in convergent release mode, on the frame-clock owner carry `baselineEye - previousBaselineEye`, compute official follow distance, enforce maximum lag, move toward baseline, and finish only by convergence. Preserve the fixed mode verbatim for control. Reset both modes on invalid input/disable/explicit reset.
- [x] **Step 4 — RED/GREEN switch and command:** add default-off decision tests and command registration/effect tests for `portal_visual_convergenttail <0|1>`. Switching either direction resets current shadow/transaction visual state before applying the new mode. Status prints enabled state and active tail mode.
- [x] **Step 5 — Runtime integration and compressed diagnostics:** pass the factor into the runtime-consumed predictor. Add lifecycle/bucket-compressed `[PortalEyeConvergentTail]` and extend `[PortalEyeShadowPredictor]`. Keep the existing runtime geometry safety path unchanged and ensure unsafe candidates still reset/fallback immediately.
- [x] **Step 6 — Focused verification:** run eye continuity, transition decision, file-log policy, render cadence, leaf handoff diagnostics, cross-leaf command, convergent-tail command, commit-seed command, and portal transform runners. Build Debug x86 and run `git diff --check`.
- [x] **Step 7 — Independent task review:** generate a Task-1-only review package and review against this task/spec. Close every Critical/Important finding through scoped fix rounds. Do not deploy yet.

## Task 2: Auditable Portal SDK Roll Rate Curve at 5x Time Scale

**Files:**

- Modify: `src/Portal/PortalViewReorientation.h`
- Modify: `src/Portal/PortalViewReorientation.cpp`
- Modify: `src/Portal/PortalTransition.cpp`
- Modify: `tests/PortalViewReorientationTests.cpp`
- Modify: `docs/portal-bsp-physical-traversal-and-continuity-technical-summary.zh-CN.md`

**Interfaces:**

- Preserve `Controller::Arm`, `Controller::Advance`, `Controller::Reset`, and the existing `FrameResult` consumer contract.
- A small pure official-rate helper may be public to the test translation unit, but must not expose engine types.
- `FrameResult::rate` remains the effective world-time angular rate used by existing logs.

- [x] **Step 1 — RED official branch fixtures:** add hand-derived tests for below/above `upZ=0.75`, rate rising/falling toward 60, airborne cap 120, grounded cap 240, and exact 5x time-compressed integration. The key first-frame fixture is air, roll `+90`, rate 0, `dt=1/60`: official-time rate becomes `33.3333`, effective world-time rate `166.6667`, and roll becomes approximately `87.2222`; the current inflated-constant approximation produces approximately `89.4444` and must fail. Mutating any official constant or branch must fail at least one test.
- [x] **Step 2 — RED lifecycle fixtures:** assert positive/negative roll remain monotonic, exact zero completes immediately with zero rate, duplicate frames do not advance, near-180 degrees completes within 0.5 seconds at 30/60/300 FPS, and dormant controller rate/transaction state clears after completion/reset. Exact `+180` random direction and Portal pitch/air-toggle/upside-down input branches remain explicit non-goals rather than invented approximations.
- [x] **Step 3 — GREEN official-timescale implementation:** replace inflated constants with official constants and `officialDeltaTime = frameTime * 5.0`. Apply the official rate branch and roll step with that delta. Preserve pitch/yaw live values, roll-only mutation, render ownership, and unsupported pitch rejection.
- [x] **Step 4 — Runtime/log integration:** keep existing command and Hook behavior. Add `curve=portal-sdk-2013 timeScale=5.000` to begin/end or status diagnostics without increasing per-frame log volume.
- [x] **Step 5 — Focused verification and independent review:** run reorientation, input AdjustAngles registration, file-log policy, transition decision, and portal transform runners; build Debug x86; run `git diff --check`; generate a Task-2-only review package and close all Critical/Important findings.

## Task 3: Whole-Round Verification, Documentation, and Deployment

**Files:**

- Modify: `docs/portal-bsp-physical-traversal-and-continuity-technical-summary.zh-CN.md`
- Modify: this plan execution ledger

- [x] **Step 1 — Whole-round regression:** run all portal runners touched by Tasks 1–2 plus prior input/commit-seed/cross-leaf/leaf-diagnostics runners and count failures explicitly.
- [x] **Step 2 — Build:** run Debug x86 MSBuild and require exit 0, 0 errors, with warnings enumerated rather than ignored.
- [x] **Step 3 — Safety and diff audit:** run `git diff --check`; inspect scoped Task 1/2 hunks for prohibited physical mutation and stale proven-false diagnostics.
- [x] **Step 4 — Final independent review:** review the combined Task 1/2 package against the spec and this plan. Apply one fix wave if necessary and one scoped re-review.
- [x] **Step 5 — Deploy:** only after reviews pass, copy DLL and launcher to the game root and verify source/destination length, modification time, and SHA-256. Do not stage, commit, push, or launch.
- [x] **Step 6 — Game handoff:** provide a minimal A/B command sequence, exact expected log tags, marker bindings, floor-to-wall sample counts, and wall/high-speed regression counts.

## Execution Ledger

| Phase | Status | Evidence | Next gate |
|---|---|---|---|
| D1 | complete | User approved official convergence plus official roll-curve alignment on 2026-08-11; written spec saved beside this plan. | Task 1 RED |
| I1 | complete | RED: primary eye/decision missing-contract compiles exit 2; log/command behavior exit 1. Fix RED: missing trace-interface and per-frame trace decisions exit 2; active-duplicate anchor assertion exit 1. GREEN: 9/9 focused runners, post-review Debug x86 exit 0 with 0 warnings/0 errors, diff check exit 0; independent fix-round re-review PASS with no Critical/Important. | Task 2 RED |
| I2 | complete | Behavioral RED exit 1, missing-contract compile RED, metadata RED exit 1; 12/12 focused runners, Debug x86 0 warnings/0 errors, diff check exit 0; independent review PASS with no Critical/Important/Minor. | Task 3 combined gate |
| I3 | complete | Root fresh 12/12 regression, Debug x86 0 warnings/0 errors, diff check exit 0, scoped prohibited-mutation scan clean, combined independent review PASS with no Critical/Important, and source/destination SHA-256 match after game-root deployment. | in-game A/B |
