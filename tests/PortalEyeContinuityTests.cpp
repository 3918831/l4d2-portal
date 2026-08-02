#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/Portal/PortalEyeContinuity.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    void ExpectNear(float actual, float expected, const char* message)
    {
        Expect(std::fabs(actual - expected) < 0.001f, message);
    }

    matrix3x4_t Translation(float x, float y, float z)
    {
        return matrix3x4_t(
            1.0f, 0.0f, 0.0f, x,
            0.0f, 1.0f, 0.0f, y,
            0.0f, 0.0f, 1.0f, z);
    }
}

int main()
{
    PortalEyeContinuity::ShadowPredictor shadowPredictor;
    const PortalEyeContinuity::ShadowPredictionResult shadowDisabled =
        shadowPredictor.Update(
            false,
            true,
            true,
            Vector(100.0f, 0.0f, 64.0f),
            Vector(100.0f, 0.0f, 64.0f),
            Vector(600.0f, 0.0f, 0.0f),
            0.01f,
            1.0f / 30.0f,
            true);
    Expect(!shadowDisabled.applied,
        "the disabled shadow predictor must preserve the official eye-history output");
    ExpectNear(shadowDisabled.eyeOrigin.x, 100.0f,
        "the disabled shadow predictor must not move the camera");

    PortalEyeContinuity::ShadowDiagnosticLogGate shadowLogGate;
    PortalEyeContinuity::ShadowPredictionResult quietShadowFrame;
    quietShadowFrame.enabled = true;
    quietShadowFrame.active = true;
    quietShadowFrame.initializedBefore = true;
    quietShadowFrame.initializedAfter = true;
    quietShadowFrame.applied = true;
    Expect(shadowLogGate.ShouldLog(
            quietShadowFrame,
            false,
            false,
            false),
        "the first observed shadow-predictor state must establish a diagnostic baseline");
    Expect(!shadowLogGate.ShouldLog(
            quietShadowFrame,
            false,
            false,
            false),
        "unchanged render frames must not emit repeated shadow-predictor diagnostics");

    PortalEyeContinuity::ShadowPredictionResult rawAdvanceFrame = quietShadowFrame;
    rawAdvanceFrame.rawAdvanced = true;
    Expect(shadowLogGate.ShouldLog(
            rawAdvanceFrame,
            false,
            false,
            false),
        "an authoritative raw-eye advance must remain observable");

    PortalEyeContinuity::ShadowPredictionResult horizonFrame = quietShadowFrame;
    horizonFrame.horizonClamped = true;
    Expect(shadowLogGate.ShouldLog(
            horizonFrame,
            false,
            false,
            false),
        "the first prediction-horizon clamp must remain observable");
    Expect(!shadowLogGate.ShouldLog(
            horizonFrame,
            false,
            false,
            false),
        "a sustained prediction-horizon clamp must not log every render frame");
    Expect(shadowLogGate.ShouldLog(
            horizonFrame,
            false,
            false,
            true),
        "runtime safety fallback must always remain observable");

    const PortalEyeContinuity::ShadowPredictionResult shadowPrimed =
        shadowPredictor.Update(
            true,
            true,
            true,
            Vector(100.0f, 0.0f, 64.0f),
            Vector(100.0f, 0.0f, 64.0f),
            Vector(600.0f, 0.0f, 0.0f),
            0.01f,
            1.0f / 30.0f,
            true);
    Expect(shadowPrimed.restarted,
        "the teleport frame must establish a new exit-space prediction anchor");
    Expect(!shadowPrimed.applied,
        "the teleport frame must not predict from entry-space render time");

    const PortalEyeContinuity::ShadowPredictionResult shadowFrozen =
        shadowPredictor.Update(
            true,
            true,
            false,
            Vector(101.0f, 0.0f, 64.0f),
            Vector(100.0f, 0.0f, 64.0f),
            Vector(600.0f, 0.0f, 0.0f),
            0.01f,
            1.0f / 30.0f,
            true);
    Expect(shadowFrozen.applied,
        "a frozen 30 Hz raw eye must receive render-time extrapolation");
    ExpectNear(shadowFrozen.predictionElapsed, 0.01f,
        "the predictor must accumulate one render frame of time");
    ExpectNear(shadowFrozen.shadowFrameDelta.x, 6.0f,
        "600 units per second for 10 ms must advance the shadow eye by six units");
    ExpectNear(shadowFrozen.predictionOffset.x, 5.0f,
        "the reported offset must compare the world-space shadow eye with the current baseline");
    ExpectNear(shadowFrozen.eyeOrigin.x, 106.0f,
        "the shadow eye must advance from the previously rendered world-space position");

    const PortalEyeContinuity::ShadowPredictionResult shadowDuplicateView =
        shadowPredictor.Update(
            true,
            true,
            false,
            Vector(101.0f, 0.0f, 64.0f),
            Vector(100.0f, 0.0f, 64.0f),
            Vector(600.0f, 0.0f, 0.0f),
            0.01f,
            1.0f / 30.0f,
            false);
    ExpectNear(shadowDuplicateView.predictionElapsed, 0.01f,
        "additional CalcView calls in one frame must not advance the predictor clock");
    ExpectNear(shadowDuplicateView.eyeOrigin.x, 106.0f,
        "additional CalcView calls in one frame must reuse the same prediction");

    const PortalEyeContinuity::ShadowPredictionResult shadowClamped =
        shadowPredictor.Update(
            true,
            true,
            false,
            Vector(102.0f, 0.0f, 64.0f),
            Vector(100.0f, 0.0f, 64.0f),
            Vector(600.0f, 0.0f, 0.0f),
            0.10f,
            1.0f / 30.0f,
            true);
    Expect(shadowClamped.horizonClamped,
        "prediction must stop at one simulation tick when the raw eye stalls");
    ExpectNear(shadowClamped.eyeOrigin.x, 120.0f,
        "the 30 Hz horizon must cap world-space shadow travel at twenty units from its start");
    ExpectNear(shadowClamped.predictionOffset.x, 18.0f,
        "baseline movement must reduce residual without retracting the rendered shadow eye");

    PortalEyeContinuity::ShadowPredictor continuousCatchUpPredictor;
    continuousCatchUpPredictor.Update(
        true,
        true,
        true,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        Vector(600.0f, 0.0f, 0.0f),
        0.0f,
        1.0f / 30.0f,
        true);
    const PortalEyeContinuity::ShadowPredictionResult beforeRawCatchUp =
        continuousCatchUpPredictor.Update(
            true,
            true,
            false,
            Vector(100.0f, 0.0f, 64.0f),
            Vector(100.0f, 0.0f, 64.0f),
            Vector(600.0f, 0.0f, 0.0f),
            1.0f / 30.0f,
            1.0f / 30.0f,
            true);
    ExpectNear(beforeRawCatchUp.eyeOrigin.x, 120.0f,
        "one frozen simulation tick must advance the shadow eye by twenty units");

    const PortalEyeContinuity::ShadowPredictionResult duringRawCatchUp =
        continuousCatchUpPredictor.Update(
            true,
            true,
            false,
            Vector(100.14f, 0.0f, 64.0f),
            Vector(120.0f, 0.0f, 64.0f),
            Vector(600.0f, 0.0f, 0.0f),
            1.0f / 750.0f,
            1.0f / 30.0f,
            true);
    Expect(duringRawCatchUp.rawAdvanced,
        "the authoritative eye catch-up must remain observable");
    ExpectNear(duringRawCatchUp.eyeOrigin.x, 120.8f,
        "raw-eye catch-up must not retract motion that was already rendered");

    PortalEyeContinuity::ShadowPredictor partialRawAdvancePredictor;
    partialRawAdvancePredictor.Update(
        true,
        true,
        true,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        Vector(600.0f, 0.0f, 0.0f),
        0.0f,
        1.0f / 30.0f,
        true);
    partialRawAdvancePredictor.Update(
        true,
        true,
        false,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        Vector(600.0f, 0.0f, 0.0f),
        1.0f / 30.0f,
        1.0f / 30.0f,
        true);
    const PortalEyeContinuity::ShadowPredictionResult shadowPartialRawAdvance =
        partialRawAdvancePredictor.Update(
            true,
            true,
            false,
            Vector(102.0f, 0.0f, 64.0f),
            Vector(100.18f, 0.0f, 64.0f),
            Vector(600.0f, 0.0f, 0.0f),
            0.001f,
            1.0f / 30.0f,
            true);
    Expect(shadowPartialRawAdvance.rawAdvanced,
        "a small authoritative raw-eye change must remain observable");
    ExpectNear(shadowPartialRawAdvance.predictionElapsed, 0.001f,
        "a new authoritative sample must open a fresh prediction horizon");
    ExpectNear(shadowPartialRawAdvance.shadowFrameDelta.x, 0.6f,
        "the catch-up frame must still advance at physical render speed");
    ExpectNear(shadowPartialRawAdvance.predictionOffset.x, 18.6f,
        "baseline catch-up must reduce residual without subtracting world-space motion");
    ExpectNear(shadowPartialRawAdvance.eyeOrigin.x, 120.6f,
        "a partial raw advance must continue from the previously rendered shadow eye");

    const PortalEyeContinuity::ShadowPredictionResult shadowRebased =
        shadowPredictor.Update(
            true,
            true,
            false,
            Vector(122.0f, 0.0f, 64.0f),
            Vector(120.0f, 0.0f, 64.0f),
            Vector(600.0f, 0.0f, 0.0f),
            0.001f,
            1.0f / 30.0f,
            true);
    Expect(shadowRebased.rawAdvanced,
        "a new simulation position must rebase the predictor");
    Expect(shadowRebased.applied,
        "a new raw position must not discard the persistent world-space shadow eye");
    ExpectNear(shadowRebased.predictionElapsed, 0.001f,
        "the raw position update must start a fresh frame-time horizon");
    ExpectNear(shadowRebased.eyeOrigin.x, 120.6f,
        "the catch-up frame must advance from the previously displayed world-space eye");

    const PortalEyeContinuity::ShadowPredictionResult shadowInactive =
        shadowPredictor.Update(
            true,
            false,
            false,
            Vector(130.0f, 0.0f, 64.0f),
            Vector(120.0f, 0.0f, 64.0f),
            Vector(600.0f, 0.0f, 0.0f),
            0.01f,
            1.0f / 30.0f,
            true);
    Expect(!shadowInactive.applied,
        "the predictor must not outlive the official eye-history override");
    ExpectNear(shadowInactive.eyeOrigin.x, 130.0f,
        "inactive eye history must return the engine-owned camera unchanged");

    PortalEyeContinuity::State state;

    const PortalEyeContinuity::UpdateResult primed = state.Update(
        true,
        Vector(10.0f, 2.0f, 64.0f),
        Vector(8.0f, 4.0f, 64.0f),
        0.01f);
    Expect(!primed.overrideOriginalView, "ordinary frames must keep the engine camera before a teleport");
    Expect(!primed.initializedBefore && primed.initializedAfter,
        "the first update must expose eye-history initialization");
    ExpectNear(primed.previousRawEyeAfter.x, 10.0f,
        "the initialized raw history must be observable");
    ExpectNear(primed.continuityEyeAfter.x, 8.0f,
        "the initialized continuity history must be observable");

    Expect(
        state.PublishTeleport(
            Translation(100.0f, 0.0f, 0.0f),
            Vector(100.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            7u,
            42),
        "a new traversal epoch must publish one pending eye-history transform");
    Expect(
        !state.PublishTeleport(
            Translation(100.0f, 0.0f, 0.0f),
            Vector(100.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            7u,
            42),
        "the same traversal epoch must not publish the transform twice");

    const PortalEyeContinuity::UpdateResult consumed = state.Update(
        true,
        Vector(115.0f, 20.0f, 70.0f),
        Vector(100.25f, 10.0f, 66.0f),
        0.01f);
    Expect(consumed.consumedPendingTeleport, "the first post-teleport view must consume the pending matrix");
    Expect(consumed.hadPendingTeleport,
        "the update trace must report that pending state existed on entry");
    Expect(consumed.interpolatingAfter,
        "the consumed teleport must expose its active interpolation state");
    ExpectNear(consumed.previousRawEyeBefore.x, 10.0f,
        "the trace must preserve the entry-space raw history");
    ExpectNear(consumed.continuityEyeBefore.x, 8.0f,
        "the trace must preserve the entry-space continuity history");
    ExpectNear(consumed.previousRawEyeAfterTransform.x, 110.0f,
        "the trace must expose the transformed raw history before free movement");
    ExpectNear(consumed.continuityEyeAfterTransform.x, 108.0f,
        "the trace must expose the transformed continuity history before following");
    ExpectNear(consumed.previousRawEyeAfter.x, 115.0f,
        "the trace must expose the current exit-space raw history");
    Expect(!consumed.planeConstrained,
        "a positive-depth transformed eye must retain its official-style portal-normal history");
    Expect(!consumed.safetyFallbackToOriginal,
        "a valid exit-side engine eye must not trigger the safety fallback");
    ExpectNear(consumed.freeMovementDelta.x, 5.0f,
        "the raw first-frame delta must remain observable for diagnostics");
    ExpectNear(consumed.engineViewMovementDelta.x, -7.75f,
        "the transformed engine-view delta must remain observable");
    ExpectNear(consumed.unconstrainedEye.x, 108.0f,
        "the shadow diagnostic must preserve the official-style unconstrained candidate");
    ExpectNear(consumed.unconstrainedExitDepth, 8.0f,
        "the shadow diagnostic must expose the unconstrained exit depth");
    ExpectNear(consumed.normalCorrection, 0.0f,
        "a positive-depth transformed eye must not receive a normal correction");
    ExpectNear(consumed.originalExitDepth, 0.25f,
        "the engine view must be measured against the exit plane");
    ExpectNear(consumed.outputExitDepth, 7.225242f,
        "the safe transformed eye must advance by the official full-vector follow step");
    ExpectNear(consumed.continuityEyeAfter.x, 107.225242f,
        "the portal-normal residual must remain continuous instead of snapping to the engine camera");
    ExpectNear(consumed.continuityEyeAfter.y, 4.599813f,
        "the portal-tangent residual must receive the same full-vector follow step");
    ExpectNear(consumed.continuityEyeAfter.z, 64.199936f,
        "the portal-up residual must receive the same full-vector follow step");
    Expect(consumed.overrideOriginalView, "the corrected eye must override the stale engine view");
    Expect(consumed.traversalEpoch == 7u, "the consumed update must retain the traversal epoch");
    Expect(consumed.commandNumber == 42, "the consumed update must retain the teleport command");
    ExpectNear(
        consumed.eyeOrigin.x,
        107.225242f,
        "the safe result must retain portal-normal continuity");

    const PortalEyeContinuity::UpdateResult held = state.Update(
        true,
        Vector(117.0f, 22.0f, 71.0f),
        Vector(102.25f, 12.0f, 67.0f),
        0.01f);
    Expect(!held.consumedPendingTeleport, "later views must not apply the portal matrix again");
    Expect(held.overrideOriginalView, "the corrected eye must remain active while the engine view is stale");
    ExpectNear(held.engineViewMovementDelta.x, 2.0f,
        "later frames must carry the engine camera's free movement");
    ExpectNear(held.eyeOrigin.x, 108.450485f,
        "later movement must continue the official full-vector catch-up");

    const PortalEyeContinuity::UpdateResult released = state.Update(
        true,
        Vector(117.0f, 22.0f, 71.0f),
        Vector(102.25f, 12.0f, 67.0f),
        1.0f);
    Expect(released.releasedOverride, "the adapter must release after the engine camera catches up");
    Expect(!released.overrideOriginalView, "release must return ownership to the engine camera");
    ExpectNear(released.eyeOrigin.x, 102.25f, "release must not introduce a final eye-position jump");

    Expect(
        state.PublishTeleport(
            Translation(50.0f, 0.0f, 0.0f),
            Vector(150.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            8u,
            55),
        "a later traversal epoch must be accepted");
    const PortalEyeContinuity::UpdateResult disabled = state.Update(
        false,
        Vector(170.0f, 0.0f, 64.0f),
        Vector(150.0f, 0.0f, 64.0f),
        0.01f);
    Expect(!disabled.overrideOriginalView, "the A/B off path must preserve the original engine view");
    ExpectNear(disabled.eyeOrigin.x, 150.0f, "the A/B off path must not modify the eye origin");

    PortalEyeContinuity::State unsafeState;
    unsafeState.Update(
        true,
        Vector(10.0f, 0.0f, 64.0f),
        Vector(8.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        unsafeState.PublishTeleport(
            Translation(100.0f, 0.0f, 0.0f),
            Vector(100.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            9u,
            60),
        "the safety case must publish a teleport");
    const PortalEyeContinuity::UpdateResult unsafe = unsafeState.Update(
        true,
        Vector(99.0f, 0.0f, 64.0f),
        Vector(99.75f, 0.0f, 64.0f),
        0.01f);
    Expect(unsafe.safetyFallbackToOriginal,
        "an engine eye behind the exit plane must disable the custom camera");
    Expect(!unsafe.overrideOriginalView,
        "the unsafe case must return camera ownership to the engine");
    ExpectNear(unsafe.eyeOrigin.x, 99.75f,
        "the unsafe case must preserve the baseline engine view");

    PortalEyeContinuity::State negativeCandidateState;
    negativeCandidateState.Update(
        true,
        Vector(-10.0f, 0.0f, 64.0f),
        Vector(-8.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        negativeCandidateState.PublishTeleport(
            Translation(100.0f, 0.0f, 0.0f),
            Vector(100.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            10u,
            61),
        "the negative-candidate safety case must publish a teleport");
    const PortalEyeContinuity::UpdateResult negativeCandidate = negativeCandidateState.Update(
        true,
        Vector(115.0f, 0.0f, 64.0f),
        Vector(100.25f, 0.0f, 64.0f),
        0.01f);
    Expect(negativeCandidate.planeConstrained,
        "a transformed eye behind the exit plane must retain the compatibility safety constraint");
    ExpectNear(negativeCandidate.unconstrainedExitDepth, -8.0f,
        "the safety trace must expose the original negative candidate depth");
    ExpectNear(negativeCandidate.normalCorrection, 8.25f,
        "the compatibility correction must move a negative candidate to the safe engine depth");
    ExpectNear(negativeCandidate.outputExitDepth, 0.25f,
        "the protected candidate must remain on the safe side of the exit plane");

    std::cout << "PortalEyeContinuity tests passed\n";
    return 0;
}
