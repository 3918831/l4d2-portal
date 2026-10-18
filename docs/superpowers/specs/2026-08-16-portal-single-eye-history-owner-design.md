# Portal Single Eye-History Owner Design

## 1. Approval and evidence

The user approved this direction on 2026-08-16 after Test 12 disproved the
convergent shadow-tail hypothesis.  The control transactions 3-11 and
experiment transactions 12-18 were all marked `perceived_sticky`.  The
experiment reduced terminal shadow residuals to 0.023-0.192 units, but did not
change the symptom.  The later wall-to-wall test remained in experiment mode;
airborne transactions released the authoritative `State` at less than 0.5
units while the independent shadow still carried 2.945-6.649 units.  Walking
transactions carried only about 0.28-0.55 units and remained subjectively
continuous.

The convergent tail therefore applied the Portal SDK follow curve to the wrong
state layer.  It is rejected as a product behavior, not merely retuned.

## 2. Selected architecture

Portal SDK 2013 owns one interpolated/uninterpolated eye history.  The L4D2
adapter will follow the same ownership rule in a default-off A/B mode:

1. The validated command-end eye seed is advanced only across the
   publication-to-render gap, clamped to one simulation tick.
2. On the pending teleport consume frame, that advanced seed initializes the
   existing `PortalEyeContinuity::State` continuity eye.
3. `State` then carries L4D2's authoritative `CalcPlayerView` movement and
   performs the existing official 100 units/s, 150-unit maximum-lag catch-up.
4. The independent velocity shadow is disabled for that transaction.  There
   is no second post-`State` tail and no second camera owner.
5. Every State output continues through the existing exit-depth, contents,
   leaf, outside-world, and bidirectional trace safety checks.

The current fixed shadow path remains the control.  The disproved convergent
tail command, runtime branch, diagnostic gate, and tests are removed rather
than retained as dormant behavior.

The already game-validated, bidirectionally traced safe cross-leaf handoff is
the common default-on baseline for both groups.  It is not an A/B factor in
this test.  Phase 1 BSP mutation remains explicitly user-enabled.

## 3. A/B factor and diagnostics

Add one default-off factor:

```text
portal_visual_singleeyehistory <0|1>
```

Switching either direction resets current visual transaction state before the
new value takes effect.  A compressed `[PortalEyeSingleOwner]` lifecycle log
records candidate/valid/applied seed identity, raw and advanced seed eyes,
elapsed time, resulting State eye, and `physicsMutation=false`.  It emits on
consume, release, safety fallback, reset, or A/B edge, not every render frame.

## 4. Scope and safety

This factor changes render-eye history only.  It must not write player origin,
velocity, base velocity, MoveType, ground entity, user commands, brush state,
portal transforms, or collision.  It must not restore noclip, movement
mutation, exit push, or `PortalMaskRepair`.  The official 5x roll curve remains
unchanged so position ownership and rotation are not mixed in this A/B.

## 5. Game gate

Control uses `portal_visual_singleeyehistory 0`; experiment uses `1`.  Test
wall-to-wall walking and jumping first because roll is zero and isolates eye
position ownership, then floor-to-wall at ordinary and high speed, and finally
the already-good airborne high-speed regression.  Pass requires the airborne
wall-to-wall regression to disappear without degrading the established
high-speed path or introducing black sky, door body, viewmodel, wall strip, or
outside-map frames.
