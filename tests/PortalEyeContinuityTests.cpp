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
            true,
            false);
    Expect(!shadowDisabled.applied,
        "the disabled shadow predictor must preserve the official eye-history output");
    ExpectNear(shadowDisabled.eyeOrigin.x, 100.0f,
        "the disabled shadow predictor must not move the camera");

    PortalEyeContinuity::ShadowRestartSeedInput restartSeed;
    restartSeed.enabled = true;
    restartSeed.candidate = true;
    restartSeed.transactionId = 17;
    restartSeed.pendingTransactionId = 17;
    restartSeed.source =
        PortalEyeContinuity::ShadowRestartSeedSource::OfficialCommandEnd;
    restartSeed.eye = Vector(110.0f, 20.0f, 64.0f);
    restartSeed.velocity = Vector(200.0f, -50.0f, 0.0f);
    restartSeed.publicationTime = 10.0f;
    restartSeed.renderTime = 10.02f;

    PortalEyeContinuity::ShadowPredictor restartSeedPredictor;
    const PortalEyeContinuity::ShadowPredictionResult seededRestart =
        restartSeedPredictor.Update(
            true,
            true,
            true,
            Vector(100.0f, 20.0f, 64.0f),
            Vector(100.0f, 20.0f, 64.0f),
            Vector(200.0f, -50.0f, 0.0f),
            0.01f,
            1.0f / 30.0f,
            true,
            false,
            restartSeed);
    Expect(seededRestart.restartSeedCandidate,
        "a published official command-end eye must be reported as a restart-seed candidate");
    Expect(seededRestart.restartSeedValid,
        "a finite current seed matching the pending transaction must validate");
    Expect(seededRestart.restartSeedApplied && seededRestart.applied,
        "a valid enabled restart seed must replace the baseline on the restart frame");
    ExpectNear(seededRestart.restartSeedElapsed, 0.02f,
        "publication-to-render elapsed must be retained when it is inside the horizon");
    ExpectNear(seededRestart.restartSeedEye.x, 110.0f,
        "restart diagnostics must expose the unadvanced official command-end eye");
    ExpectNear(seededRestart.advancedRestartSeedEye.x, 114.0f,
        "200 units per second for 20 ms must advance the restart seed by four units");
    ExpectNear(seededRestart.advancedRestartSeedEye.y, 19.0f,
        "the restart seed must advance independently on every velocity axis");
    ExpectNear(seededRestart.eyeOrigin.x, 114.0f,
        "the restart frame must render from the advanced command-end eye");

    const PortalEyeContinuity::ShadowPredictionResult seededDuplicate =
        restartSeedPredictor.Update(
            true,
            true,
            false,
            Vector(100.0f, 20.0f, 64.0f),
            Vector(100.0f, 20.0f, 64.0f),
            Vector(200.0f, -50.0f, 0.0f),
            0.01f,
            1.0f / 30.0f,
            false,
            false);
    Expect(seededDuplicate.restartSeedApplied && seededDuplicate.applied,
        "duplicate CalcView calls must retain the applied restart-seed identity");
    ExpectNear(seededDuplicate.eyeOrigin.x, 114.0f,
        "duplicate CalcView calls must reuse the same seeded eye without advancing");
    ExpectNear(seededDuplicate.restartSeedElapsed, 0.02f,
        "duplicate CalcView calls must reuse the same clamped publication elapsed");

    PortalEyeContinuity::ShadowRestartSeedInput disabledRestartSeed = restartSeed;
    disabledRestartSeed.enabled = false;
    PortalEyeContinuity::ShadowPredictor disabledRestartSeedPredictor;
    const PortalEyeContinuity::ShadowPredictionResult disabledRestart =
        disabledRestartSeedPredictor.Update(
            true, true, true,
            Vector(100.0f, 20.0f, 64.0f),
            Vector(100.0f, 20.0f, 64.0f),
            Vector(200.0f, -50.0f, 0.0f),
            0.01f, 1.0f / 30.0f, true, false,
            disabledRestartSeed);
    Expect(disabledRestart.restartSeedCandidate && !disabledRestart.restartSeedValid,
        "a candidate must fail closed while the independent restart-seed factor is disabled");
    Expect(!disabledRestart.restartSeedApplied && !disabledRestart.applied,
        "a disabled restart seed must preserve the existing restart behavior");
    ExpectNear(disabledRestart.eyeOrigin.x, 100.0f,
        "a disabled restart seed must leave the restart baseline unchanged");

    PortalEyeContinuity::ShadowRestartSeedInput nonFiniteRestartSeed = restartSeed;
    nonFiniteRestartSeed.eye.x = std::nanf("");
    PortalEyeContinuity::ShadowPredictor nonFiniteRestartSeedPredictor;
    const PortalEyeContinuity::ShadowPredictionResult nonFiniteRestart =
        nonFiniteRestartSeedPredictor.Update(
            true, true, true,
            Vector(100.0f, 20.0f, 64.0f),
            Vector(100.0f, 20.0f, 64.0f),
            Vector(200.0f, -50.0f, 0.0f),
            0.01f, 1.0f / 30.0f, true, false,
            nonFiniteRestartSeed);
    Expect(nonFiniteRestart.restartSeedCandidate && !nonFiniteRestart.restartSeedValid,
        "a non-finite restart eye must fail closed");
    Expect(!nonFiniteRestart.restartSeedApplied && !nonFiniteRestart.applied,
        "a non-finite restart seed must preserve the existing restart behavior");
    ExpectNear(nonFiniteRestart.eyeOrigin.x, 100.0f,
        "a non-finite restart seed must return the baseline eye");

    PortalEyeContinuity::ShadowRestartSeedInput mismatchedRestartSeed = restartSeed;
    mismatchedRestartSeed.pendingTransactionId = 18;
    PortalEyeContinuity::ShadowPredictor mismatchedRestartSeedPredictor;
    const PortalEyeContinuity::ShadowPredictionResult mismatchedRestart =
        mismatchedRestartSeedPredictor.Update(
            true, true, true,
            Vector(100.0f, 20.0f, 64.0f),
            Vector(100.0f, 20.0f, 64.0f),
            Vector(200.0f, -50.0f, 0.0f),
            0.01f, 1.0f / 30.0f, true, false,
            mismatchedRestartSeed);
    Expect(mismatchedRestart.restartSeedCandidate && !mismatchedRestart.restartSeedValid,
        "a restart seed from another transaction must fail closed");
    ExpectNear(mismatchedRestart.eyeOrigin.x, 100.0f,
        "a mismatched restart seed must preserve the baseline eye");

    PortalEyeContinuity::ShadowRestartSeedInput staleRestartSeed = restartSeed;
    staleRestartSeed.stale = true;
    PortalEyeContinuity::ShadowPredictor staleRestartSeedPredictor;
    const PortalEyeContinuity::ShadowPredictionResult staleRestart =
        staleRestartSeedPredictor.Update(
            true, true, true,
            Vector(100.0f, 20.0f, 64.0f),
            Vector(100.0f, 20.0f, 64.0f),
            Vector(200.0f, -50.0f, 0.0f),
            0.01f, 1.0f / 30.0f, true, false,
            staleRestartSeed);
    Expect(staleRestart.restartSeedCandidate && !staleRestart.restartSeedValid,
        "an explicitly stale restart seed must fail closed");
    ExpectNear(staleRestart.eyeOrigin.x, 100.0f,
        "a stale restart seed must preserve the baseline eye");

    PortalEyeContinuity::ShadowPredictor restartMetadataPredictor;
    const PortalEyeContinuity::ShadowPredictionResult metadataSeeded =
        restartMetadataPredictor.Update(
            true, true, true,
            Vector(100.0f, 20.0f, 64.0f),
            Vector(100.0f, 20.0f, 64.0f),
            Vector(200.0f, -50.0f, 0.0f),
            0.01f, 1.0f / 30.0f, true, false,
            restartSeed);
    Expect(metadataSeeded.restartSeedApplied,
        "the metadata isolation fixture must begin from a successful seeded restart");
    const PortalEyeContinuity::ShadowPredictionResult metadataMissingRestart =
        restartMetadataPredictor.Update(
            true, true, true,
            Vector(300.0f, 40.0f, 72.0f),
            Vector(300.0f, 40.0f, 72.0f),
            Vector(50.0f, 0.0f, 0.0f),
            0.01f, 1.0f / 30.0f, true,
            false);
    Expect(!metadataMissingRestart.restartSeedValid
            && !metadataMissingRestart.restartSeedApplied,
        "a missing seed on a new restart must not inherit the previous successful identity");
    Expect(
        metadataMissingRestart.restartSeedSource
            == PortalEyeContinuity::ShadowRestartSeedSource::None,
        "a missing seed restart must report source none instead of the previous source");
    ExpectNear(metadataMissingRestart.restartSeedEye.x, 0.0f,
        "a missing seed restart must clear the previous raw seed eye");
    ExpectNear(metadataMissingRestart.advancedRestartSeedEye.x, 0.0f,
        "a missing seed restart must clear the previous advanced seed eye");
    ExpectNear(metadataMissingRestart.eyeOrigin.x, 300.0f,
        "a missing seed restart must return its current baseline eye");

    PortalEyeContinuity::ShadowRestartSeedInput currentMismatchedSeed = restartSeed;
    currentMismatchedSeed.eye = Vector(210.0f, 30.0f, 64.0f);
    currentMismatchedSeed.transactionId = 21;
    currentMismatchedSeed.pendingTransactionId = 22;
    const PortalEyeContinuity::ShadowPredictionResult metadataMismatchedRestart =
        restartMetadataPredictor.Update(
            true, true, true,
            Vector(400.0f, 50.0f, 72.0f),
            Vector(400.0f, 50.0f, 72.0f),
            Vector(50.0f, 0.0f, 0.0f),
            0.01f, 1.0f / 30.0f, true, false,
            currentMismatchedSeed);
    Expect(!metadataMismatchedRestart.restartSeedValid
            && !metadataMismatchedRestart.restartSeedApplied,
        "a mismatched new restart must not inherit previous valid/applied metadata");
    ExpectNear(metadataMismatchedRestart.restartSeedEye.x, 210.0f,
        "a rejected restart must expose its current candidate rather than the previous seed");
    ExpectNear(metadataMismatchedRestart.advancedRestartSeedEye.x, 210.0f,
        "a rejected restart must not expose the previous advanced seed");
    ExpectNear(metadataMismatchedRestart.eyeOrigin.x, 400.0f,
        "a mismatched restart must return its current baseline eye");

    PortalEyeContinuity::ShadowRestartSeedInput negativeElapsedSeed = restartSeed;
    negativeElapsedSeed.publicationTime = 20.02f;
    negativeElapsedSeed.renderTime = 20.0f;
    PortalEyeContinuity::ShadowPredictor negativeElapsedPredictor;
    const PortalEyeContinuity::ShadowPredictionResult negativeElapsedRestart =
        negativeElapsedPredictor.Update(
            true, true, true,
            Vector(100.0f, 20.0f, 64.0f),
            Vector(100.0f, 20.0f, 64.0f),
            Vector(200.0f, -50.0f, 0.0f),
            0.01f, 1.0f / 30.0f, true, false,
            negativeElapsedSeed);
    ExpectNear(negativeElapsedRestart.restartSeedElapsed, 0.0f,
        "a publication timestamp after render time must clamp elapsed to zero");
    ExpectNear(negativeElapsedRestart.eyeOrigin.x, 110.0f,
        "a negative elapsed must not move the restart seed backward");

    PortalEyeContinuity::ShadowRestartSeedInput overHorizonSeed = restartSeed;
    overHorizonSeed.publicationTime = 30.0f;
    overHorizonSeed.renderTime = 30.2f;
    PortalEyeContinuity::ShadowPredictor overHorizonPredictor;
    const PortalEyeContinuity::ShadowPredictionResult overHorizonRestart =
        overHorizonPredictor.Update(
            true, true, true,
            Vector(100.0f, 20.0f, 64.0f),
            Vector(100.0f, 20.0f, 64.0f),
            Vector(200.0f, -50.0f, 0.0f),
            0.01f, 1.0f / 30.0f, true, false,
            overHorizonSeed);
    ExpectNear(overHorizonRestart.restartSeedElapsed, 1.0f / 30.0f,
        "publication-to-render elapsed must clamp to the one-tick prediction horizon");
    ExpectNear(overHorizonRestart.eyeOrigin.x, 116.666664f,
        "an over-horizon seed must advance by exactly one 30 Hz tick");

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
    Expect(!shadowLogGate.ShouldLog(
            rawAdvanceFrame,
            false,
            false,
            false),
        "repeated raw-eye advances in one predictor lifecycle must be summarized");

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
            true,
            false);
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
            true,
            false);
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
            false,
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
            true,
            false);
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
        true,
        false);
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
            true,
            false);
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
            true,
            false);
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
        true,
        false);
    partialRawAdvancePredictor.Update(
        true,
        true,
        false,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        Vector(600.0f, 0.0f, 0.0f),
        1.0f / 30.0f,
        1.0f / 30.0f,
        true,
        false);
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
            true,
            false);
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
            true,
            false);
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
            true,
            false);
    Expect(!shadowInactive.applied,
        "the predictor must not outlive the official eye-history override");
    ExpectNear(shadowInactive.eyeOrigin.x, 130.0f,
        "inactive eye history must return the engine-owned camera unchanged");

    PortalEyeContinuity::ShadowPredictor releaseTailPredictor;
    releaseTailPredictor.Update(
        true,
        true,
        true,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        Vector(220.0f, 0.0f, 0.0f),
        0.0f,
        1.0f / 30.0f,
        true,
        true);
    releaseTailPredictor.Update(
        true,
        true,
        false,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        Vector(220.0f, 0.0f, 0.0f),
        1.0f / 30.0f,
        1.0f / 30.0f,
        true,
        true);
    const PortalEyeContinuity::ShadowPredictionResult releaseTailStarted =
        releaseTailPredictor.Update(
            true,
            false,
            false,
            Vector(100.2f, 0.0f, 64.0f),
            Vector(100.0f, 0.0f, 64.0f),
            Vector(220.0f, 0.0f, 0.0f),
            0.005f,
            1.0f / 30.0f,
            true,
            true);
    Expect(releaseTailStarted.releaseTailStarted,
        "eye-history release must start the optional one-tick shadow handoff");
    Expect(releaseTailStarted.releaseTailActive,
        "the release frame must keep the render-only shadow handoff active");
    Expect(releaseTailStarted.applied,
        "the release frame must not snap immediately back to the frozen engine eye");
    ExpectNear(releaseTailStarted.eyeOrigin.x, 108.433334f,
        "the release tail must continue from the persistent world-space shadow eye");

    const PortalEyeContinuity::ShadowPredictionResult releaseTailDuplicate =
        releaseTailPredictor.Update(
            true,
            false,
            false,
            Vector(100.2f, 0.0f, 64.0f),
            Vector(100.0f, 0.0f, 64.0f),
            Vector(220.0f, 0.0f, 0.0f),
            0.005f,
            1.0f / 30.0f,
            false,
            true);
    ExpectNear(releaseTailDuplicate.eyeOrigin.x, 108.433334f,
        "duplicate CalcView calls must reuse the release-tail eye without advancing it");

    const PortalEyeContinuity::ShadowPredictionResult releaseTailConverged =
        releaseTailPredictor.Update(
            true,
            false,
            false,
            Vector(108.3f, 0.0f, 64.0f),
            Vector(108.0f, 0.0f, 64.0f),
            Vector(220.0f, 0.0f, 0.0f),
            0.005f,
            1.0f / 30.0f,
            true,
            true);
    Expect(releaseTailConverged.releaseTailEnded,
        "the first authoritative raw-eye catch-up must end a converged release tail");
    Expect(
        releaseTailConverged.releaseTailEndReason
            == PortalEyeContinuity::ShadowReleaseTailEndReason::Converged,
        "the release-tail handoff must distinguish convergence from timeout");
    Expect(!releaseTailConverged.applied,
        "a converged handoff must return camera ownership to the engine");
    ExpectNear(releaseTailConverged.eyeOrigin.x, 108.3f,
        "a converged handoff must use the authoritative engine eye");

    PortalEyeContinuity::ShadowPredictor releaseTailExpiryPredictor;
    releaseTailExpiryPredictor.Update(
        true,
        true,
        true,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        Vector(220.0f, 0.0f, 0.0f),
        0.0f,
        1.0f / 30.0f,
        true,
        true);
    releaseTailExpiryPredictor.Update(
        true,
        true,
        false,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        Vector(220.0f, 0.0f, 0.0f),
        0.005f,
        1.0f / 30.0f,
        true,
        true);
    const PortalEyeContinuity::ShadowPredictionResult releaseTailExpired =
        releaseTailExpiryPredictor.Update(
            true,
            false,
            false,
            Vector(100.0f, 0.0f, 64.0f),
            Vector(100.0f, 0.0f, 64.0f),
            Vector(220.0f, 0.0f, 0.0f),
            0.04f,
            1.0f / 30.0f,
            true,
            true);
    Expect(releaseTailExpired.releaseTailEnded,
        "a frozen engine eye must not keep the release tail alive indefinitely");
    Expect(
        releaseTailExpired.releaseTailEndReason
            == PortalEyeContinuity::ShadowReleaseTailEndReason::HorizonExpired,
        "the one-tick safety cap must be explicit in diagnostics");
    ExpectNear(releaseTailExpired.releaseTailElapsed, 1.0f / 30.0f,
        "the release tail must never exceed one runtime tick");
    Expect(!releaseTailExpired.applied,
        "an expired release tail must return ownership to the engine");

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
    ExpectNear(consumed.activeExitPlaneOrigin.x, 100.0f,
        "the consumed update must expose the active exit plane for runtime safety checks");
    ExpectNear(consumed.activeExitPlaneNormal.x, 1.0f,
        "the release tail must retain the normalized exit-plane direction");
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

    PortalEyeContinuity::State projectedRebaseState;
    projectedRebaseState.Update(
        true,
        Vector(-10.0f, 0.0f, 64.0f),
        Vector(-8.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        projectedRebaseState.PublishTeleport(
            Translation(100.0f, 0.0f, 0.0f),
            Vector(100.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            11u,
            62,
            true,
            Vector(8.25f, 0.0f, 0.0f)),
        "a projected teleport must accept an explicit pre-transform history rebase");
    const PortalEyeContinuity::UpdateResult projectedRebased =
        projectedRebaseState.Update(
            true,
            Vector(115.0f, 0.0f, 64.0f),
            Vector(100.25f, 0.0f, 64.0f),
            0.01f);
    Expect(projectedRebased.historyRebaseApplied,
        "the first post-teleport view must report the projected history rebase");
    ExpectNear(projectedRebased.historyRebaseDelta.x, 8.25f,
        "the history rebase trace must retain the projected lead delta");
    ExpectNear(projectedRebased.continuityEyeAfterRebase.x, 0.25f,
        "the projected lead delta must be applied in entry space before the portal matrix");
    ExpectNear(projectedRebased.continuityEyeAfterTransform.x, 100.25f,
        "the rebased history must transform onto the projected exit commit depth");
    Expect(!projectedRebased.planeConstrained,
        "a rebased projected history already on the exit side must not need the BSP safety constraint");
    ExpectNear(projectedRebased.unconstrainedExitDepth, 0.25f,
        "the rebased projected history must preserve the projected commit exit depth");

    PortalEyeContinuity::State projectedControlState;
    projectedControlState.Update(
        true,
        Vector(-10.0f, 0.0f, 64.0f),
        Vector(-8.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        projectedControlState.PublishTeleport(
            Translation(100.0f, 0.0f, 0.0f),
            Vector(100.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            12u,
            63,
            false,
            Vector(8.25f, 0.0f, 0.0f)),
        "the A/B control must retain the projected rebase candidate without applying it");
    const PortalEyeContinuity::UpdateResult projectedControl =
        projectedControlState.Update(
            true,
            Vector(115.0f, 0.0f, 64.0f),
            Vector(100.25f, 0.0f, 64.0f),
            0.01f);
    Expect(projectedControl.historyRebaseCandidate,
        "the control trace must expose that a projected rebase candidate existed");
    Expect(!projectedControl.historyRebaseApplied,
        "the control trace must not mutate the official-style history input");
    ExpectNear(projectedControl.continuityEyeCandidateAfterTransform.x, 100.25f,
        "the control trace must expose the hypothetical rebased exit eye");
    ExpectNear(projectedControl.continuityEyeAfterTransform.x, 92.0f,
        "the disabled path must preserve the unrevised transformed history");
    Expect(projectedControl.planeConstrained,
        "the disabled path must retain the existing BSP compatibility constraint");

    // Test 12 exposed two serial camera owners: State released below the
    // 0.5-unit tolerance while the independent shadow still carried as much
    // as 6.649 units.  The single-owner experiment must put the validated
    // command-end seed into State itself so no second post-State tail exists.
    PortalEyeContinuity::State singleOwnerState;
    singleOwnerState.Update(
        true,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        singleOwnerState.PublishTeleport(
            Translation(0.0f, 0.0f, 0.0f),
            Vector(0.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            17u,
            170),
        "the single-owner fixture must publish a pending teleport");

    PortalEyeContinuity::ShadowRestartSeedInput singleOwnerSeed;
    singleOwnerSeed.enabled = true;
    singleOwnerSeed.candidate = true;
    singleOwnerSeed.transactionId = 17u;
    singleOwnerSeed.pendingTransactionId = 17u;
    singleOwnerSeed.source =
        PortalEyeContinuity::ShadowRestartSeedSource::OfficialCommandEnd;
    singleOwnerSeed.eye = Vector(110.0f, 0.0f, 64.0f);
    singleOwnerSeed.velocity = Vector(200.0f, 0.0f, 0.0f);
    singleOwnerSeed.publicationTime = 10.0f;
    singleOwnerSeed.renderTime = 10.02f;

    const PortalEyeContinuity::UpdateResult singleOwnerConsumed =
        singleOwnerState.Update(
            true,
            Vector(105.0f, 0.0f, 64.0f),
            Vector(105.0f, 0.0f, 64.0f),
            0.01f,
            1.0f / 30.0f,
            singleOwnerSeed);
    Expect(singleOwnerConsumed.singleOwnerSeedCandidate,
        "the matching command-end seed must be observable at State ownership transfer");
    Expect(singleOwnerConsumed.singleOwnerSeedValid
            && singleOwnerConsumed.singleOwnerSeedApplied,
        "State must accept the finite current seed for the matching pending transaction");
    ExpectNear(singleOwnerConsumed.singleOwnerSeedElapsed, 0.02f,
        "State must retain publication-to-render elapsed inside one tick");
    ExpectNear(singleOwnerConsumed.singleOwnerSeedEye.x, 110.0f,
        "State diagnostics must retain the unadvanced command-end eye");
    ExpectNear(singleOwnerConsumed.singleOwnerAdvancedSeedEye.x, 114.0f,
        "State must advance the command-end eye across the render gap exactly once");
    ExpectNear(singleOwnerConsumed.eyeOrigin.x, 113.0f,
        "the seeded State owner must apply its official one-frame catch-up without a shadow owner");

    const PortalEyeContinuity::UpdateResult singleOwnerReleased =
        singleOwnerState.Update(
            true,
            Vector(106.0f, 0.0f, 64.0f),
            Vector(106.0f, 0.0f, 64.0f),
            1.0f);
    Expect(singleOwnerReleased.releasedOverride,
        "the seeded State owner must release directly to the engine eye");
    ExpectNear(singleOwnerReleased.eyeOrigin.x, 106.0f,
        "single-owner release must have no second-layer residual or tail");

    PortalEyeContinuity::State rejectedSingleOwnerState;
    rejectedSingleOwnerState.Update(
        true,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        rejectedSingleOwnerState.PublishTeleport(
            Translation(0.0f, 0.0f, 0.0f),
            Vector(0.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            18u,
            180),
        "the rejected single-owner fixture must publish a pending teleport");
    PortalEyeContinuity::ShadowRestartSeedInput mismatchedSingleOwnerSeed =
        singleOwnerSeed;
    mismatchedSingleOwnerSeed.transactionId = 18u;
    mismatchedSingleOwnerSeed.pendingTransactionId = 19u;
    const PortalEyeContinuity::UpdateResult mismatchedSingleOwner =
        rejectedSingleOwnerState.Update(
            true,
            Vector(105.0f, 0.0f, 64.0f),
            Vector(105.0f, 0.0f, 64.0f),
            0.01f,
            1.0f / 30.0f,
            mismatchedSingleOwnerSeed);
    Expect(mismatchedSingleOwner.singleOwnerSeedCandidate
            && !mismatchedSingleOwner.singleOwnerSeedValid
            && !mismatchedSingleOwner.singleOwnerSeedApplied,
        "a single-owner seed from another pending transaction must fail closed");

    PortalEyeContinuity::State staleSingleOwnerState;
    staleSingleOwnerState.Update(
        true,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        staleSingleOwnerState.PublishTeleport(
            Translation(0.0f, 0.0f, 0.0f),
            Vector(0.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            21u,
            210),
        "the stale single-owner fixture must publish a pending teleport");
    PortalEyeContinuity::ShadowRestartSeedInput staleSingleOwnerSeed =
        singleOwnerSeed;
    staleSingleOwnerSeed.transactionId = 21u;
    staleSingleOwnerSeed.pendingTransactionId = 21u;
    staleSingleOwnerSeed.stale = true;
    const PortalEyeContinuity::UpdateResult staleSingleOwner =
        staleSingleOwnerState.Update(
            true,
            Vector(105.0f, 0.0f, 64.0f),
            Vector(105.0f, 0.0f, 64.0f),
            0.01f,
            1.0f / 30.0f,
            staleSingleOwnerSeed);
    Expect(!staleSingleOwner.singleOwnerSeedValid
            && !staleSingleOwner.singleOwnerSeedApplied,
        "a stale single-owner seed must fail closed");

    PortalEyeContinuity::State nonFiniteSingleOwnerState;
    nonFiniteSingleOwnerState.Update(
        true,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        nonFiniteSingleOwnerState.PublishTeleport(
            Translation(0.0f, 0.0f, 0.0f),
            Vector(0.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            19u,
            190),
        "the non-finite single-owner fixture must publish a pending teleport");
    PortalEyeContinuity::ShadowRestartSeedInput nonFiniteSingleOwnerSeed =
        singleOwnerSeed;
    nonFiniteSingleOwnerSeed.transactionId = 19u;
    nonFiniteSingleOwnerSeed.pendingTransactionId = 19u;
    nonFiniteSingleOwnerSeed.eye.x = std::nanf("");
    const PortalEyeContinuity::UpdateResult nonFiniteSingleOwner =
        nonFiniteSingleOwnerState.Update(
            true,
            Vector(105.0f, 0.0f, 64.0f),
            Vector(105.0f, 0.0f, 64.0f),
            0.01f,
            1.0f / 30.0f,
            nonFiniteSingleOwnerSeed);
    Expect(!nonFiniteSingleOwner.singleOwnerSeedValid
            && !nonFiniteSingleOwner.singleOwnerSeedApplied,
        "a non-finite single-owner seed must fail closed");

    PortalEyeContinuity::State negativeElapsedSingleOwnerState;
    negativeElapsedSingleOwnerState.Update(
        true,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        negativeElapsedSingleOwnerState.PublishTeleport(
            Translation(0.0f, 0.0f, 0.0f),
            Vector(0.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            22u,
            220),
        "the negative-elapsed single-owner fixture must publish a pending teleport");
    PortalEyeContinuity::ShadowRestartSeedInput negativeElapsedSingleOwnerSeed =
        singleOwnerSeed;
    negativeElapsedSingleOwnerSeed.transactionId = 22u;
    negativeElapsedSingleOwnerSeed.pendingTransactionId = 22u;
    negativeElapsedSingleOwnerSeed.publicationTime = 30.02f;
    negativeElapsedSingleOwnerSeed.renderTime = 30.0f;
    const PortalEyeContinuity::UpdateResult negativeElapsedSingleOwner =
        negativeElapsedSingleOwnerState.Update(
            true,
            Vector(105.0f, 0.0f, 64.0f),
            Vector(105.0f, 0.0f, 64.0f),
            0.01f,
            1.0f / 30.0f,
            negativeElapsedSingleOwnerSeed);
    Expect(negativeElapsedSingleOwner.singleOwnerSeedApplied,
        "a finite negative-elapsed seed remains usable after clamping");
    ExpectNear(negativeElapsedSingleOwner.singleOwnerSeedElapsed, 0.0f,
        "publication after render time must not move the State seed backward");

    PortalEyeContinuity::State clampedSingleOwnerState;
    clampedSingleOwnerState.Update(
        true,
        Vector(100.0f, 0.0f, 64.0f),
        Vector(100.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        clampedSingleOwnerState.PublishTeleport(
            Translation(0.0f, 0.0f, 0.0f),
            Vector(0.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            20u,
            200),
        "the clamped single-owner fixture must publish a pending teleport");
    PortalEyeContinuity::ShadowRestartSeedInput clampedSingleOwnerSeed =
        singleOwnerSeed;
    clampedSingleOwnerSeed.transactionId = 20u;
    clampedSingleOwnerSeed.pendingTransactionId = 20u;
    clampedSingleOwnerSeed.publicationTime = 20.0f;
    clampedSingleOwnerSeed.renderTime = 20.2f;
    const PortalEyeContinuity::UpdateResult clampedSingleOwner =
        clampedSingleOwnerState.Update(
            true,
            Vector(105.0f, 0.0f, 64.0f),
            Vector(105.0f, 0.0f, 64.0f),
            0.01f,
            1.0f / 30.0f,
            clampedSingleOwnerSeed);
    ExpectNear(clampedSingleOwner.singleOwnerSeedElapsed, 1.0f / 30.0f,
        "single-owner publication-to-render advance must not exceed one command tick");
    const PortalEyeContinuity::UpdateResult clampedSingleOwnerDuplicate =
        clampedSingleOwnerState.Update(
            true,
            Vector(105.0f, 0.0f, 64.0f),
            Vector(105.0f, 0.0f, 64.0f),
            0.0f,
            1.0f / 30.0f,
            clampedSingleOwnerSeed);
    Expect(!clampedSingleOwnerDuplicate.consumedPendingTeleport
            && !clampedSingleOwnerDuplicate.singleOwnerSeedApplied,
        "duplicate CalcView consumption must not apply the command-end seed twice");

    std::cout << "PortalEyeContinuity tests passed\n";
    return 0;
}
