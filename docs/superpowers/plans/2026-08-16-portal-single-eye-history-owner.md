# Portal Single Eye-History Owner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development and verify every behavior change with an observed RED before the minimal GREEN.

**Goal:** Replace the disproved second-layer convergent tail with a default-off
single-owner eye-history A/B that seeds `State` directly and leaves no shadow
owner after the consume frame.

**Architecture:** Reuse the validated command-end seed and existing State
interpolation.  Resolve/advance the seed once, inject it into State on the
matching pending transaction, bypass ShadowPredictor for the experimental
transaction, and retain all runtime eye safety gates.

**Tech Stack:** C++17, existing pure C++ and PowerShell structural runners,
VS2022 Debug x86.

## Global constraints

- Render-eye history only; no physical or collision mutation.
- One new default-off A/B factor and compressed lifecycle diagnostics.
- Remove the disproved convergent-tail product behavior and its command/tests.
- Preserve the fixed shadow control and official roll curve unchanged.
- Strict RED -> observed expected failure -> minimal GREEN.

## Task 1: Pure ownership and seed contract

- [x] Add failing State tests for matching, stale, mismatched, non-finite,
      negative-elapsed, and over-horizon command-end seeds.
- [x] Add the airborne regression fixture where State residual is below 0.5
      while the old shadow residual is 6.649; assert single-owner release has
      no second tail.
- [x] Observe RED from the missing State seed/ownership contract.
- [x] Implement one-tick-clamped seed resolution and State initialization.
- [x] Run the focused eye-continuity runner to GREEN.

## Task 2: Runtime factor and cleanup

- [x] Add failing decision, command-registration, exact log-policy, and stale
      convergent-command-removal tests.
- [x] Add default-off `portal_visual_singleeyehistory`, edge-reset semantics,
      status output, and compressed `[PortalEyeSingleOwner]` diagnostics.
- [x] In experiment mode pass the resolved seed to State and disable the
      independent ShadowPredictor for that transaction.
- [x] Remove convergent-tail runtime branches, command, log gate, and runner.
- [x] Run focused decision/command/log/eye runners to GREEN.

## Task 3: Documentation, verification, and deployment

- [x] Record Test 12's falsified hypothesis, regression mechanism, new
      ownership seam, and deferred airborne-stall observation in the technical
      summary.
- [x] Run all portal runners affected by the current dirty phase.
- [x] Build Debug x86, run `git diff --check`, and audit scoped hunks for every
      prohibited mutation token.
- [x] Copy DLL and launcher to the game root and verify length, timestamp, and
      SHA-256.
- [x] Hand off one-factor control/experiment commands and exact test order.

## Execution ledger

| Phase | Status | Evidence | Next gate |
|---|---|---|---|
| D1 | complete | User approved the single-owner direction on 2026-08-16; design saved beside this plan. | Task 1 RED |
| I1 | complete | Eye-continuity RED failed on the missing State seed contract; matching, mismatch, non-finite, one-tick clamp, duplicate consumption and no-second-tail tests are GREEN. | Runtime factor |
| I2 | complete | Decision/command/log REDs observed; default-off factor, atomic reset, State-only latch, shadow fallback and convergent product cleanup are GREEN. | Verification/deploy |
| I3 | game gate | 26/26 portal runners passed; final Debug x86 build was 0 warning / 0 error; diff check and scoped safety scan passed; source/target DLL SHA-256 match at `554DE901...E99036`. | User game A/B |
