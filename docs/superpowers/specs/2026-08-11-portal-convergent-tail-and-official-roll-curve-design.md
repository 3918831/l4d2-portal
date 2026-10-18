# Portal Convergent Eye Tail and Official Roll Curve Design

## 1. Approval and scope

The user approved this design direction on 2026-08-11: replace the delayed hard eye handoff with a Portal SDK-style convergence path and keep the rotation speed curve aligned with the official implementation. The same request requires implementation, build, deployment, and a game test handoff in this session; per the established branch workflow, design and product changes remain uncommitted until the game A/B passes.

This change is render-eye and view-angle only. It must not mutate player origin, velocity, base velocity, MoveType, ground entity, movement commands, brush state, portal transforms, or collision. It must not restore old noclip, movement mutation, exit push, or `PortalMaskRepair`.

## 2. Test 11 evidence

The explicit control group (transactions 8–11) hard-fell back on a geometry-clear leaf mismatch 0.200–0.233 seconds after commit. The candidate/original eye distances were 22.329, 24.420, 26.698, and 11.822 units.

The experiment group (transactions 12–15) correctly allowed every safe cross-leaf candidate, but all four transactions then ended at the fixed tail horizon after about 0.267 seconds with 5.421, 14.807, 10.214, and 5.622 units of residual. Two tails grew rather than converged. The terminal frame switched directly to the original eye.

Therefore:

- extending the existing `velocity * dt` tail is rejected because its residual is not monotonic;
- weakening spatial safety is rejected because Task 10 already proves the geometry gate works;
- the selected fix is to carry authoritative baseline movement and decay only the remaining visual offset, matching the semantics of `C_Portal_Player::UpdatePortalEyeInterpolation`.

## 3. Convergent-tail design

Add a default-off behavior factor:

```text
portal_visual_convergenttail <0|1>
```

When disabled, the current fixed-horizon tail remains the control. When enabled and `continuityActive` becomes false:

1. Preserve the previous applied visual offset relative to the moving authoritative baseline.
2. On the single frame-clock owner only, add `baselineEye - previousBaselineEye` to the shadow eye. This is the L4D2 adapter equivalent of the official free-movement carry.
3. Compute `difference = baselineEye - shadowEye`.
4. Compute official follow distance `frameTime * 100 units/s`.
5. If the residual exceeds 150 units, use `residual - 150` as the catch-up distance for that frame, matching the official maximum-lag rule.
6. If the remaining distance is smaller than the follow distance, output the authoritative baseline and finish as `converged`.
7. Otherwise move the shadow eye toward the baseline by exactly the follow distance.

The convergent mode has no normal `horizon-expired` termination. It ends only on convergence, explicit reset/transaction replacement, feature disable, invalid/non-finite input, or the existing runtime spatial safety fallback. Every produced candidate continues through positive exit depth, valid leaf, world/player-solid contents, outside-world, and safe cross-leaf geometry checks.

The old fixed mode remains available only for the A/B control. It is not promoted as a long-term fallback after the experiment is validated.

## 4. Terminal diagnostics

Diagnostics remain compressed and non-mutating:

- `[PortalEyeConvergentTail] event=start|progress|end` records transaction, frame, baseline delta, carried delta, catch-up delta, residual before/after, maximum-lag clamp, final visible jump, and `physicsMutation=false`.
- Progress logs fire only on residual buckets or lifecycle edges, never every render frame.
- The existing `[PortalEyeShadowPredictor]` line gains the tail mode and authoritative-carry/catch-up vectors.
- The existing safety fallback logs remain the authority for invalid or unsafe candidates.

## 5. Official roll curve

`C_Portal_Player::FixTeleportationRoll` uses these official constants and branches:

```text
REORIENTATION_RATE = 120 deg/s
REORIENTATION_ACCELERATION_RATE = 400 deg/s^2
upright settled rate = 60 deg/s
grounded maximum = 240 deg/s
upZ threshold = 0.75
```

The user still requires ordinary near-180-degree recovery within about half a second. Preserve the exact official curve in normalized time and apply an explicit `5.0x` time scale:

- use official constants verbatim in the state machine;
- evaluate the official rate update with `officialDeltaTime = frameTime * 5.0`;
- advance roll with the same `officialDeltaTime * officialRate`;
- keep the existing render-frame ownership, duplicate-frame suppression, roll-only write, input-roll preservation, and unsupported pitch fail-closed behavior;
- do not add cubic, exponential, spring, or bespoke easing;
- report effective world-time angular rate in logs so existing diagnostics remain comparable;
- clear dormant internal rate/transaction state after completion without changing the last applied angle.

This is mathematically the official curve played five times faster. The current implementation's independently inflated `600/1200/2000/300` constants are only equivalent to multiplying the official rate output by five at the original acceleration timeline; they are not equivalent to a five-times time-compressed trajectory. Exact time compression scales world-time angular speed by five and world-time acceleration by twenty-five, which follows naturally from using the official state and `officialDeltaTime` for both rate and roll integration.

The supported scope remains the official ordinary-roll branch. Exact `+180°` random direction selection, pitch reorientation around the right axis, `cl_reorient_in_air`, `g_bUpsideDown` mouse inversion, and direct access to Portal's private eye-angle interpolator are not silently invented in L4D2. Exact zero roll must immediately complete with zero rate; ordinary positive and negative roll must remain symmetric and monotonic. Ground state continues to use the verified L4D2 `FL_ONGROUND` source because `GetGroundEntity()` is not yet available through a validated client ABI.

## 6. Tests and game gate

Pure tests must first fail against the current implementation and then cover:

- moving-baseline carry while the visual offset monotonically closes;
- 100 units/s frame-rate independence at 30, 60, and 300 FPS;
- the 150-unit maximum-lag rule;
- Test 11 residuals, including the previously growing 14.807 and 10.214-unit cases, reaching convergence without a multi-unit terminal jump;
- fixed-mode control behavior remaining available;
- reset, disable, non-finite, duplicate frame, and safety integration behavior;
- official roll rate branches below/above `upZ=0.75`, airborne/grounded caps, positive/negative roll, duplicate frames, and near-180-degree completion within 0.5 seconds at 30/60/300 FPS.

Game A/B holds safe cross-leaf handoff on for both groups:

- control: `portal_visual_convergenttail 0`;
- experiment: `portal_visual_convergenttail 1`;
- both groups use the same official-timescale-5 roll curve;
- test floor-to-wall first, then wall-to-wall and high-speed-airborne regression;
- pass requires no delayed terminal eye jump, no regression in speed/physics, and no black sky, door body, viewmodel, wall strip, or outside-map frame.
