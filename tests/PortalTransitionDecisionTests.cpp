#include <cstdlib>
#include <cmath>
#include <iostream>

#include "../src/Portal/PortalTransitionDecision.h"

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
}

int main()
{
    Expect(
        PortalTransitionDecision::GetExitClearanceMode() == PortalExitClearanceMode::ExactTransform,
        "the established BSP traversal baseline must default to the exact portal transform");
    Expect(
        !PortalTransitionDecision::GetExactExitVisualGuardEnabled(),
        "the established baseline must keep the disproven exit guard disabled");
    Expect(
        PortalTransitionDecision::GetPortalEyeShadowPredictorEnabled(),
        "the validated one-tick shadow predictor must be enabled by default");
    Expect(
        PortalTransitionDecision::GetPortalVisualCommitSeedEnabled(),
        "the verified first-render official command-end eye seed must default on");
    const PortalVisualCommitSeedEdgeAction unchangedCommitSeedEdge =
        PortalTransitionDecision::DecidePortalVisualCommitSeedEdgeAction(
            false,
            false);
    Expect(!unchangedCommitSeedEdge.changed
            && !unchangedCommitSeedEdge.clearPendingSeed
            && !unchangedCommitSeedEdge.clearSeedDerivedPredictor,
        "an unchanged commit-seed decision must not disturb pending or predictor state");
    const PortalVisualCommitSeedEdgeAction enabledCommitSeedEdge =
        PortalTransitionDecision::DecidePortalVisualCommitSeedEdgeAction(
            false,
            true);
    Expect(enabledCommitSeedEdge.changed
            && enabledCommitSeedEdge.clearPendingSeed
            && !enabledCommitSeedEdge.clearSeedDerivedPredictor,
        "an enable edge must discard stale pending metadata without resetting an unrelated predictor");
    const PortalVisualCommitSeedEdgeAction disabledCommitSeedEdge =
        PortalTransitionDecision::DecidePortalVisualCommitSeedEdgeAction(
            true,
            false);
    Expect(disabledCommitSeedEdge.changed
            && disabledCommitSeedEdge.clearPendingSeed
            && disabledCommitSeedEdge.clearSeedDerivedPredictor,
        "a disable edge must clear pending metadata and any seed-derived predictor state");
    Expect(
        PortalTransitionDecision::GetPortalEyeShadowReleaseTailEnabled(),
        "the validated shadow release tail must be enabled by default");
    Expect(
        !PortalTransitionDecision::GetPortalEyeSingleOwnerEnabled(),
        "single eye-history ownership must default off for controlled A/B testing");
    const PortalEyeSingleOwnerEdgeAction singleOwnerEnableEdge =
        PortalTransitionDecision::DecidePortalEyeSingleOwnerEdgeAction(
            false,
            true);
    Expect(singleOwnerEnableEdge.changed
            && singleOwnerEnableEdge.resetEyeHistoryState
            && singleOwnerEnableEdge.resetShadowState
            && singleOwnerEnableEdge.clearPendingSeed
            && singleOwnerEnableEdge.resetTransactionVisualState,
        "enabling single-owner mode must atomically clear both old owners and pending seed metadata");
    const PortalEyeSingleOwnerEdgeAction singleOwnerDisableEdge =
        PortalTransitionDecision::DecidePortalEyeSingleOwnerEdgeAction(
            true,
            false);
    Expect(singleOwnerDisableEdge.changed
            && singleOwnerDisableEdge.resetEyeHistoryState
            && singleOwnerDisableEdge.resetShadowState
            && singleOwnerDisableEdge.clearPendingSeed
            && singleOwnerDisableEdge.resetTransactionVisualState,
        "returning to the fixed-shadow control must clear the same visual ownership");
    const PortalEyeSingleOwnerEdgeAction singleOwnerNoChange =
        PortalTransitionDecision::DecidePortalEyeSingleOwnerEdgeAction(
            false,
            false);
    Expect(!singleOwnerNoChange.changed
            && !singleOwnerNoChange.resetEyeHistoryState
            && !singleOwnerNoChange.resetShadowState
            && !singleOwnerNoChange.clearPendingSeed
            && !singleOwnerNoChange.resetTransactionVisualState,
        "repeating the current single-owner setting must not reset visual state");
    PortalTransitionDecision::SetPortalEyeSingleOwnerEnabled(true);
    Expect(PortalTransitionDecision::GetPortalEyeSingleOwnerEnabled(),
        "the single-owner factor must expose an enabled experiment path");
    PortalTransitionDecision::SetPortalEyeSingleOwnerEnabled(false);
    Expect(!PortalTransitionDecision::GetPortalEyeSingleOwnerEnabled(),
        "the single-owner factor must restore its default-off control path");
    Expect(
        PortalTransitionDecision::ShouldFailClosedForMissingPortalEyeSafetyInterface(
            true,
            false),
        "an active custom eye candidate must fail closed when the trace safety interface is unavailable");
    Expect(
        !PortalTransitionDecision::ShouldFailClosedForMissingPortalEyeSafetyInterface(
            false,
            false),
        "an inactive custom eye path must not invent a safety fallback");
    Expect(
        !PortalTransitionDecision::ShouldFailClosedForMissingPortalEyeSafetyInterface(
            true,
            true),
        "an available trace interface must continue into the existing per-frame spatial gates");
    Expect(
        PortalTransitionDecision::ArePortalEyeBidirectionalSafetyTracesClear(
            true, 1.0f, false, false, true,
            true, 1.0f, false, false, true),
        "two available full finite traces must pass the per-frame eye-candidate trace gate");
    Expect(
        !PortalTransitionDecision::ArePortalEyeBidirectionalSafetyTracesClear(
            false, 1.0f, false, false, true,
            true, 1.0f, false, false, true),
        "a missing forward trace must fail closed even when candidate and original share a leaf");
    Expect(
        !PortalTransitionDecision::ArePortalEyeBidirectionalSafetyTracesClear(
            true, 0.5f, false, false, true,
            true, 1.0f, false, false, true),
        "a partial trace must reject the candidate on every output frame");
    Expect(
        !PortalTransitionDecision::ArePortalEyeBidirectionalSafetyTracesClear(
            true, 1.0f, true, false, true,
            true, 1.0f, false, false, true),
        "start-solid trace evidence must reject the candidate");
    Expect(
        !PortalTransitionDecision::ArePortalEyeBidirectionalSafetyTracesClear(
            true, 1.0f, false, false, false,
            true, 1.0f, false, false, true),
        "a non-finite trace end must fail closed");
    Expect(
        PortalTransitionDecision::GetProjectedEyeHistoryRebaseEnabled(),
        "the validated projected eye-history rebase must be enabled by default");
    Expect(
        PortalTransitionDecision::GetPortalTransactionDiagnosticsEnabled(),
        "focused transaction diagnostics must default on during continuity testing");
    Expect(PortalTransitionDecision::GetPortalViewReorientationEnabled(),
        "verified official view reorientation must default on");
    PortalTransitionDecision::SetPortalViewReorientationEnabled(false);
    Expect(!PortalTransitionDecision::GetPortalViewReorientationEnabled(),
        "official view reorientation must remain independently disableable");
    PortalTransitionDecision::SetPortalViewReorientationEnabled(true);
    Expect(PortalTransitionDecision::GetPortalViewReorientationEnabled(),
        "official view reorientation must round-trip to its verified default-on path");
    Expect(PortalTransitionDecision::GetPortalEyeCrossLeafHandoffEnabled(),
        "the game-validated safe cross-leaf eye handoff baseline must default on");
    for (int enabled = 0; enabled <= 1; ++enabled)
    {
        for (int leafMismatch = 0; leafMismatch <= 1; ++leafMismatch)
        {
            for (int geometryClear = 0; geometryClear <= 1; ++geometryClear)
            {
                const bool expected = enabled != 0
                    && leafMismatch != 0
                    && geometryClear != 0;
                Expect(
                    PortalTransitionDecision::ShouldAllowPortalEyeCrossLeafHandoff(
                        enabled != 0,
                        leafMismatch != 0,
                        geometryClear != 0) == expected,
                    "safe cross-leaf eye handoff must require all three gates");
            }
        }
    }
    PortalTransitionDecision::SetPortalEyeCrossLeafHandoffEnabled(true);
    Expect(PortalTransitionDecision::GetPortalEyeCrossLeafHandoffEnabled(),
        "safe cross-leaf eye handoff must expose an enabled experiment path");
    PortalTransitionDecision::SetPortalEyeCrossLeafHandoffEnabled(true);

    PortalTransitionDecision::PortalEyeCrossLeafHandoffController handoffController;
    const PortalTransitionDecision::PortalEyeCrossLeafHandoffFrameDecision firstSafeMismatch =
        handoffController.EvaluateFrame(true, true, true, false);
    Expect(firstSafeMismatch.allowCandidate
            && firstSafeMismatch.emitAllowEdge
            && !firstSafeMismatch.emitRejectEdge
            && handoffController.IsAuthorized(),
        "the first safe mismatch must authorize and emit one allow edge");

    const PortalTransitionDecision::PortalEyeCrossLeafHandoffFrameDecision repeatedSafeMismatch =
        handoffController.EvaluateFrame(true, true, true, false);
    Expect(repeatedSafeMismatch.allowCandidate
            && !repeatedSafeMismatch.emitAllowEdge
            && !repeatedSafeMismatch.emitRejectEdge
            && handoffController.IsAuthorized(),
        "a repeated safe mismatch must re-authorize without repeating the allow edge");

    Expect(handoffController.ConsumeHorizonExpiryProbeEdge(true, true, true),
        "a diagnostics-enabled horizon-expiry edge must emit one probe");
    Expect(!handoffController.ConsumeHorizonExpiryProbeEdge(true, true, true),
        "the same transaction must not repeat the horizon-expiry probe edge");
    Expect(handoffController.IsAuthorized(),
        "read-only horizon evidence must not change cross-leaf authorization");

    const PortalTransitionDecision::PortalEyeCrossLeafHandoffFrameDecision unsafeMismatch =
        handoffController.EvaluateFrame(true, true, false, false);
    Expect(!unsafeMismatch.allowCandidate
            && !unsafeMismatch.emitAllowEdge
            && unsafeMismatch.emitRejectEdge
            && !handoffController.IsAuthorized(),
        "an unsafe mismatch after authorization must reject once and clear authorization");
    const PortalTransitionDecision::PortalEyeCrossLeafHandoffFrameDecision repeatedUnsafeMismatch =
        handoffController.EvaluateFrame(true, true, false, false);
    Expect(!repeatedUnsafeMismatch.emitRejectEdge,
        "a cleared authorization must not repeat the rejection edge");

    handoffController.Reset();
    handoffController.EvaluateFrame(true, true, true, false);
    const PortalTransitionDecision::PortalEyeCrossLeafHandoffFrameDecision runtimeSafetyFailure =
        handoffController.EvaluateFrame(true, false, false, true);
    Expect(runtimeSafetyFailure.emitRejectEdge
            && !runtimeSafetyFailure.allowCandidate
            && !handoffController.IsAuthorized(),
        "any later runtime safety failure must reject and clear authorization");

    PortalTransitionDecision::PortalEyeCrossLeafHandoffController gatedProbeController;
    Expect(!gatedProbeController.ConsumeHorizonExpiryProbeEdge(false, true, true),
        "diagnostics-off horizon expiry must not emit a probe");
    Expect(!gatedProbeController.ConsumeHorizonExpiryProbeEdge(true, false, true),
        "a non-ended release tail must not emit a horizon probe");
    Expect(!gatedProbeController.ConsumeHorizonExpiryProbeEdge(true, true, false),
        "a non-horizon tail end must not emit a horizon probe");
    Expect(gatedProbeController.ConsumeHorizonExpiryProbeEdge(true, true, true),
        "failed probe gates must not consume the later valid horizon edge");
    gatedProbeController.Reset();
    Expect(gatedProbeController.ConsumeHorizonExpiryProbeEdge(true, true, true),
        "transaction cleanup must re-arm the next transaction's probe edge");

    PortalTransitionDecision::PortalEyeCrossLeafHandoffController cleanupController;
    cleanupController.EvaluateFrame(true, true, true, false);
    cleanupController.Reset();
    Expect(!cleanupController.IsAuthorized(),
        "transaction cleanup must clear cross-leaf authorization");
    Expect(
        PortalTransitionDecision::GetGroundLiveCommitEnabled(),
        "the validated ground live-commit path must be enabled by default");
    Expect(
        PortalTransitionDecision::ShouldUseProjectedPlaneCrossing(false, true),
        "the rollback control must retain the projected ground commit");
    Expect(
        !PortalTransitionDecision::ShouldUseProjectedPlaneCrossing(true, true),
        "the baseline must defer a grounded projected candidate to the live crossing");
    Expect(
        PortalTransitionDecision::ShouldUseProjectedPlaneCrossing(true, false),
        "the ground live-commit baseline must not alter the validated airborne crossing path");
    PortalTransitionDecision::SetGroundLiveCommitEnabled(false);
    Expect(
        !PortalTransitionDecision::GetGroundLiveCommitEnabled(),
        "the projected ground control path must remain available as a rollback switch");
    PortalTransitionDecision::SetGroundLiveCommitEnabled(true);
    Expect(
        PortalTransitionDecision::GetGroundLiveCommitEnabled(),
        "the rollback switch must restore the validated ground live-commit baseline");

    Expect(
        !PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::Idle),
        "idle movement must use the normal walk solver");
    Expect(
        !PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::ApproachingPortal),
        "approaching the portal must not enter noclip early");
    Expect(
        PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::IntersectingPortal),
        "portal intersection must use controlled noclip");
    Expect(
        PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::CommittingTeleport),
        "teleport commit must retain controlled noclip");
    Expect(
        PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::ExitingPortal),
        "exit movement must retain controlled noclip");
    Expect(
        !PortalTransitionDecision::RequiresControlledNoclip(PortalTransitionPhase::Cooldown),
        "cooldown must restore the normal walk solver");

    Expect(
        PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::Idle),
        "idle movement may still use legacy bridge helpers when no GMod traversal is active");
    Expect(
        !PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::ApproachingPortal),
        "GMod-style traversal must not let legacy bridge helpers alter approach movement");
    Expect(
        !PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::IntersectingPortal),
        "controlled noclip must disable legacy bridge movement writers");
    Expect(
        !PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::CommittingTeleport),
        "teleport commit must disable legacy bridge movement writers");
    Expect(
        !PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::ExitingPortal),
        "exit movement must disable legacy bridge movement writers");
    Expect(
        PortalTransitionDecision::ShouldApplyLegacyBridgeMovement(PortalTransitionPhase::Cooldown),
        "cooldown has restored walk movement so legacy bridge helpers are not blocked by this policy");

    Expect(
        PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::Idle),
        "idle must run the original player movement");
    Expect(
        PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::ApproachingPortal),
        "approach must run the original player movement before controlled noclip");
    Expect(
        !PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::IntersectingPortal),
        "controlled noclip must replace original player movement during intersection");
    Expect(
        !PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::CommittingTeleport),
        "controlled noclip must replace original player movement during teleport commit");
    Expect(
        !PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::ExitingPortal),
        "controlled noclip must replace original player movement while exiting");
    Expect(
        PortalTransitionDecision::ShouldRunOriginalPlayerMove(PortalTransitionPhase::Cooldown),
        "cooldown must return to the original player movement");

    Expect(
        !PortalTransitionDecision::ShouldReplaceOriginalPlayerMove(
            PortalTransitionPhase::IntersectingPortal,
            false),
        "intersection must retain original PlayerMove when movement mutation is disabled");
    Expect(
        !PortalTransitionDecision::ShouldReplaceOriginalPlayerMove(
            PortalTransitionPhase::CommittingTeleport,
            false),
        "teleport commit must retain original PlayerMove when movement mutation is disabled");
    Expect(
        PortalTransitionDecision::ShouldReplaceOriginalPlayerMove(
            PortalTransitionPhase::IntersectingPortal,
            true),
        "legacy controlled movement may replace original PlayerMove during intersection");
    Expect(
        !PortalTransitionDecision::ShouldReplaceOriginalPlayerMove(
            PortalTransitionPhase::Idle,
            true),
        "idle movement must never replace original PlayerMove");

    Expect(
        PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, true, true, true),
        "front-to-back crossing inside the aperture must commit");
    Expect(
        PortalTransitionDecision::ShouldCommitPlaneCrossing(0.0f, -0.25f, true, true, true),
        "leaving the exact portal plane toward the back must commit");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(3.0f, 0.25f, true, true, true),
        "remaining in front of the portal must not commit");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(-0.25f, 0.50f, true, true, true),
        "back-to-front movement must not commit the entry portal");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, true, false, true),
        "crossing outside the aperture must not commit");
    Expect(
        PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, false, true, true),
        "a first valid observation behind the portal plane must commit without waiting another command");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(-0.25f, -0.50f, true, true, true),
        "a player already behind the portal plane must not retrigger the same crossing");
    Expect(
        !PortalTransitionDecision::ShouldCommitPlaneCrossing(0.50f, -0.25f, true, true, false),
        "a crossing while moving away from the portal must not commit");

    Expect(
        PortalTransitionDecision::ShouldCommitExitRearmCatchup(
            false,
            59.827f,
            -14.841f,
            true,
            true,
            true),
        "a swept crossing of the opposite portal must commit on the command that releases the exit lock");
    Expect(
        !PortalTransitionDecision::ShouldCommitExitRearmCatchup(
            true,
            59.827f,
            -14.841f,
            true,
            true,
            true),
        "exit rearm catch-up must not immediately re-enter the portal that was just exited");
    Expect(
        !PortalTransitionDecision::ShouldCommitExitRearmCatchup(
            false,
            -67.111f,
            -14.841f,
            true,
            true,
            true),
        "exit rearm catch-up must reject stale entry-space history that never swept the next portal plane");
    Expect(
        !PortalTransitionDecision::ShouldCommitExitRearmCatchup(
            false,
            59.827f,
            -14.841f,
            false,
            true,
            true),
        "exit rearm catch-up must require a consecutive exit-space history sample");

    float crossingFraction = 0.0f;
    Expect(
        PortalTransitionDecision::TryComputePortalPlaneCrossingFraction(
            8.0f, -12.0f, &crossingFraction),
        "a front-to-back swept segment must report a portal-plane crossing");
    ExpectNear(
        crossingFraction,
        0.4f,
        "the swept portal-plane crossing fraction must locate the zero-depth point");
    Expect(
        !PortalTransitionDecision::TryComputePortalPlaneCrossingFraction(
            -2.0f, -12.0f, &crossingFraction),
        "a segment wholly behind the portal must not report a new crossing");
    Expect(
        !PortalTransitionDecision::TryComputePortalPlaneCrossingFraction(
            8.0f, 2.0f, &crossingFraction),
        "a segment wholly in front of the portal must not report a crossing");

    constexpr float predictedCommitDepth = -0.25f;
    const float predictedLeadTime = PortalTransitionDecision::ComputePredictedPortalCommitLeadTime(
        2.0f,
        -60.0f,
        0.05f,
        predictedCommitDepth);
    ExpectNear(
        predictedLeadTime,
        0.0375f,
        "predicted crossing must advance just behind the entry plane instead of stopping in front");
    ExpectNear(
        2.0f + (-60.0f * predictedLeadTime),
        predictedCommitDepth,
        "the predicted anchor must land in the entry portal's back half-space");
    Expect(
        PortalTransitionDecision::IsPortalCommitHalfSpaceValid(-0.25f, 0.25f),
        "a negative entry depth transformed to a positive exit depth is valid");
    Expect(
        !PortalTransitionDecision::IsPortalCommitHalfSpaceValid(1.5f, -1.5f),
        "the old projected front-to-back depth inversion must be rejected");

    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(4.23f, 16.0f, 2.0f),
        13.77f,
        "an intersecting exit hull must be pushed fully in front of the wall");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(20.0f, 16.0f, 2.0f),
        0.0f,
        "an already clear exit hull must not receive an extra push");

    Expect(
        PortalTransitionDecision::ExitClearanceModeName(PortalExitClearanceMode::FullHull) == std::string("FullHull"),
        "the full-hull clearance mode must have a stable diagnostic name");
    Expect(
        PortalTransitionDecision::ExitClearanceModeName(PortalExitClearanceMode::PlaneEpsilon) == std::string("PlaneEpsilon"),
        "the plane-epsilon clearance mode must have a stable diagnostic name");
    Expect(
        PortalTransitionDecision::ExitClearanceModeName(PortalExitClearanceMode::ExactTransform) == std::string("ExactTransform"),
        "the exact-transform clearance mode must have a stable diagnostic name");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(
            PortalExitClearanceMode::FullHull, -1.5f, 16.0f, 2.0f),
        19.5f,
        "full-hull mode must clear the complete player hull plus epsilon");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(
            PortalExitClearanceMode::PlaneEpsilon, -1.5f, 16.0f, 2.0f),
        3.5f,
        "plane-epsilon mode must only place the transformed center past the portal plane");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(
            PortalExitClearanceMode::PlaneEpsilon, 2.0f, 16.0f, 2.0f),
        0.0f,
        "plane-epsilon mode must not push a center already at the requested epsilon");
    ExpectNear(
        PortalTransitionDecision::ComputeExitClearancePush(
            PortalExitClearanceMode::ExactTransform, -1.5f, 16.0f, 2.0f),
        0.0f,
        "exact-transform mode must preserve the transformed position without any exit push");
    Expect(
        PortalTransitionDecision::TryParseExitClearanceMode("full", nullptr) == false,
        "mode parsing must reject a null output pointer");
    PortalExitClearanceMode parsedMode = PortalExitClearanceMode::FullHull;
    Expect(
        PortalTransitionDecision::TryParseExitClearanceMode("plane", &parsedMode)
            && parsedMode == PortalExitClearanceMode::PlaneEpsilon,
        "the short plane command value must select plane-epsilon mode");
    Expect(
        PortalTransitionDecision::TryParseExitClearanceMode("exact", &parsedMode)
            && parsedMode == PortalExitClearanceMode::ExactTransform,
        "the short exact command value must select exact-transform mode");
    Expect(
        !PortalTransitionDecision::TryParseExitClearanceMode("unknown", &parsedMode),
        "unknown clearance modes must be rejected");
    PortalTransitionDecision::SetExitClearanceMode(PortalExitClearanceMode::PlaneEpsilon);
    Expect(
        PortalTransitionDecision::GetExitClearanceMode() == PortalExitClearanceMode::PlaneEpsilon,
        "the runtime clearance selection must retain plane-epsilon mode");
    PortalTransitionDecision::SetExitClearanceMode(PortalExitClearanceMode::ExactTransform);
    Expect(
        PortalTransitionDecision::GetExitClearanceMode() == PortalExitClearanceMode::ExactTransform,
        "the runtime clearance selection must return to the exact-transform baseline");

    Expect(
        PortalTransitionDecision::IsViewNearPortalPlane(24.0f, 12.0f, -20.0f, 48.0f, 38.0f, 62.0f),
        "a view within the portal-local diagnostic prism must be logged");
    Expect(
        !PortalTransitionDecision::IsViewNearPortalPlane(48.1f, 0.0f, 0.0f, 48.0f, 38.0f, 62.0f),
        "a view beyond the diagnostic depth must not be logged");
    Expect(
        !PortalTransitionDecision::IsViewNearPortalPlane(1.0f, 38.1f, 0.0f, 48.0f, 38.0f, 62.0f),
        "a view beyond the diagnostic lateral boundary must not be logged");

    Expect(
        PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::Idle),
        "probe loss outside controlled traversal may clear back to idle");
    Expect(
        PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::ApproachingPortal),
        "probe loss while approaching may clear back to idle");
    Expect(
        !PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::IntersectingPortal),
        "probe loss during controlled intersection must not immediately restore WALK");
    Expect(
        !PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::CommittingTeleport),
        "probe loss during teleport commit must not immediately restore WALK");
    Expect(
        !PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::ExitingPortal),
        "probe loss during exit must not immediately restore WALK at an unverified position");
    Expect(
        PortalTransitionDecision::ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase::Cooldown),
        "probe loss during cooldown may remain normal walk");

    Expect(
        PortalTransitionDecision::ShouldReleaseExitControlledMove(16.0f, true),
        "GMod-style ipMove releases controlled noclip as soon as frontDist reaches 16");
    Expect(
        PortalTransitionDecision::ShouldReleaseExitControlledMove(15.99f, true) == false,
        "GMod-style ipMove keeps controlled noclip while frontDist is still inside 16");
    Expect(
        PortalTransitionDecision::ShouldReleaseExitControlledMove(8.0f, false),
        "leaving the portal aperture releases controlled noclip even before the plane distance threshold");

    Expect(
        PortalTransitionDecision::ShouldAllowTraversalEntry(PortalTransitionPhase::Cooldown, 1.00f, 1.20f) == false,
        "cooldown must block immediate re-entry after early exit release");
    Expect(
        PortalTransitionDecision::ShouldAllowTraversalEntry(PortalTransitionPhase::Cooldown, 1.20f, 1.20f),
        "cooldown allows entry again once the cooldown time has elapsed");
    Expect(
        PortalTransitionDecision::ShouldAllowTraversalEntry(PortalTransitionPhase::Idle, 1.00f, 1.20f),
        "idle entry is not blocked by a stale cooldown timestamp");
    Expect(
        !PortalTransitionDecision::ShouldAllowTraversalEntry(PortalTransitionPhase::CommittingTeleport, 2.00f, 1.20f),
        "a teleport commit must never admit another traversal entry");
    Expect(
        !PortalTransitionDecision::ShouldAllowTraversalEntry(PortalTransitionPhase::ExitingPortal, 2.00f, 1.20f),
        "the spatial exit-rearm lock must block entry even after the old cooldown timestamp expires");
    Expect(
        PortalTransitionDecision::ShouldSyncCommittedViewAnglesAtPredictionSetup(
            PortalTransitionPhase::ExitingPortal, 1521, 1521),
        "the committed command must use exit-facing angles while its movement is simulated");
    Expect(
        !PortalTransitionDecision::ShouldSyncCommittedViewAnglesAtPredictionSetup(
            PortalTransitionPhase::Cooldown, 1522, 1521),
        "the command after the teleport must keep its own view-angle input");
    Expect(
        !PortalTransitionDecision::ShouldSyncCommittedViewAnglesAtPredictionSetup(
            PortalTransitionPhase::ExitingPortal, 0, 0),
        "invalid command numbers must never qualify for a committed angle handoff");
    Expect(
        !PortalTransitionDecision::ShouldSyncCommittedViewAnglesAtPredictionSetup(
            PortalTransitionPhase::Idle, 1521, 1521),
        "normal movement must not rewrite user command view angles");
    const auto noCommitCommand =
        PortalTransitionDecision::ClassifyServerRunCommand(
            false, 1521, 0, 0.0f, 0.0f);
    Expect(
        noCommitCommand.timing == PortalServerCommandTiming::NoCommit,
        "a server command without a committed transaction must remain unclassified");
    Expect(
        noCommitCommand.angleSpace == PortalCommandAngleSpace::Unknown,
        "a server command without a committed transaction has no portal angle space");

    const auto commitEntryCommand =
        PortalTransitionDecision::ClassifyServerRunCommand(
            true, 1521, 1521, 0.0f, 90.0f);
    Expect(
        commitEntryCommand.timing == PortalServerCommandTiming::CommitCommand,
        "the authoritative command matching the teleport command must be identified");
    Expect(
        commitEntryCommand.angleSpace == PortalCommandAngleSpace::Entry,
        "an authoritative command nearest the pre-portal angles must be entry-space");

    const auto commitExitCommand =
        PortalTransitionDecision::ClassifyServerRunCommand(
            true, 1521, 1521, 90.0f, 0.0f);
    Expect(
        commitExitCommand.angleSpace == PortalCommandAngleSpace::Exit,
        "an authoritative command nearest the post-portal angles must be exit-space");

    const auto commandBeforeCommit =
        PortalTransitionDecision::ClassifyServerRunCommand(
            true, 1520, 1521, 1.0f, 89.0f);
    Expect(
        commandBeforeCommit.timing == PortalServerCommandTiming::BeforeCommit,
        "an older server command must be distinguished from the commit command");

    const auto commandAfterCommit =
        PortalTransitionDecision::ClassifyServerRunCommand(
            true, 1524, 1521, 2.0f, 88.0f);
    Expect(
        commandAfterCommit.timing == PortalServerCommandTiming::AfterCommitWindow,
        "nearby commands after commit must remain inside the bounded diagnostic window");

    const auto commandAfterWindow =
        PortalTransitionDecision::ClassifyServerRunCommand(
            true, 1530, 1521, 2.0f, 88.0f);
    Expect(
        commandAfterWindow.timing == PortalServerCommandTiming::AfterWindow,
        "commands beyond the diagnostic window must not be confused with the handoff");

    const auto ambiguousAngles =
        PortalTransitionDecision::ClassifyServerRunCommand(
            true, 1521, 1521, 45.0f, 45.0005f);
    Expect(
        ambiguousAngles.angleSpace == PortalCommandAngleSpace::Ambiguous,
        "nearly equal angle differences must be reported as ambiguous");
    Expect(
        PortalTransitionDecision::ShouldApplyServerPortalCommandAngleFix(
            true,
            PortalServerCommandTiming::CommitCommand,
            PortalCommandAngleSpace::Entry),
        "the official server adapter must transform only the committed entry-space command");
    Expect(
        !PortalTransitionDecision::ShouldApplyServerPortalCommandAngleFix(
            false,
            PortalServerCommandTiming::CommitCommand,
            PortalCommandAngleSpace::Entry),
        "the server command-angle adapter must support an isolated A/B off path");
    Expect(
        !PortalTransitionDecision::ShouldApplyServerPortalCommandAngleFix(
            true,
            PortalServerCommandTiming::CommitCommand,
            PortalCommandAngleSpace::Exit),
        "an already transformed exit-space command must not be transformed twice");
    Expect(
        !PortalTransitionDecision::ShouldApplyServerPortalCommandAngleFix(
            true,
            PortalServerCommandTiming::AfterCommitWindow,
            PortalCommandAngleSpace::Entry),
        "commands after the committed command must keep their own input angles");
    Expect(
        !PortalTransitionDecision::ShouldApplyServerPortalCommandAngleFix(
            true,
            PortalServerCommandTiming::CommitCommand,
            PortalCommandAngleSpace::Ambiguous),
        "an ambiguous committed command must not be mutated");
    PortalTransitionDecision::SetServerPortalCommandAngleFixEnabled(false);
    Expect(
        !PortalTransitionDecision::GetServerPortalCommandAngleFixEnabled(),
        "the server command-angle adapter must expose an isolated A/B off path");
    PortalTransitionDecision::SetServerPortalCommandAngleFixEnabled(true);
    Expect(
        PortalTransitionDecision::GetServerPortalCommandAngleFixEnabled(),
        "the server command-angle adapter must restore the default on path");
    PortalTransitionDecision::SetPortalEyeContinuityEnabled(false);
    Expect(
        !PortalTransitionDecision::GetPortalEyeContinuityEnabled(),
        "the eye-history continuity adapter must support an isolated A/B off path");
    PortalTransitionDecision::SetPortalEyeContinuityEnabled(true);
    Expect(
        PortalTransitionDecision::GetPortalEyeContinuityEnabled(),
        "the eye-history continuity adapter must support an isolated A/B on path");
    Expect(
        PortalTransitionDecision::GetPortalEyeShadowPredictorEnabled(),
        "the validated shadow predictor must retain the continuity baseline");
    PortalTransitionDecision::SetPortalEyeShadowPredictorEnabled(false);
    Expect(
        !PortalTransitionDecision::GetPortalEyeShadowPredictorEnabled(),
        "the shadow predictor must remain independently disableable");
    PortalTransitionDecision::SetPortalEyeShadowPredictorEnabled(true);
    Expect(
        PortalTransitionDecision::GetPortalEyeShadowPredictorEnabled(),
        "the shadow predictor control path must restore the continuity baseline");
    Expect(
        PortalTransitionDecision::GetPortalVisualCommitSeedEnabled(),
        "changing the shadow predictor must not disable the verified commit-seed factor");
    PortalTransitionDecision::SetPortalVisualCommitSeedEnabled(false);
    Expect(
        !PortalTransitionDecision::GetPortalVisualCommitSeedEnabled(),
        "the commit-seed factor must support an independent disabled path");
    Expect(
        PortalTransitionDecision::GetPortalEyeShadowPredictorEnabled(),
        "enabling the commit seed must not alter the shadow-predictor factor");
    PortalTransitionDecision::SetPortalVisualCommitSeedEnabled(true);
    Expect(
        PortalTransitionDecision::GetPortalVisualCommitSeedEnabled(),
        "the commit-seed factor must round-trip to its verified default-on path");

    Expect(
        PortalTransitionDecision::GetPortalEyeShadowReleaseTailEnabled(),
        "the validated ground handoff tail must retain the continuity baseline");
    PortalTransitionDecision::SetPortalEyeShadowReleaseTailEnabled(false);
    Expect(
        !PortalTransitionDecision::GetPortalEyeShadowReleaseTailEnabled(),
        "the release tail must remain independently disableable");
    PortalTransitionDecision::SetPortalEyeShadowReleaseTailEnabled(true);
    Expect(
        PortalTransitionDecision::GetPortalEyeShadowReleaseTailEnabled(),
        "the release-tail control path must restore the continuity baseline");
    Expect(
        PortalTransitionDecision::GetProjectedEyeHistoryRebaseEnabled(),
        "the validated projected eye-history rebase must retain the continuity baseline");
    PortalTransitionDecision::SetProjectedEyeHistoryRebaseEnabled(false);
    Expect(
        !PortalTransitionDecision::GetProjectedEyeHistoryRebaseEnabled(),
        "the projected eye-history rebase must remain independently disableable");
    PortalTransitionDecision::SetProjectedEyeHistoryRebaseEnabled(true);
    Expect(
        PortalTransitionDecision::GetProjectedEyeHistoryRebaseEnabled(),
        "the projected eye-history rebase control path must restore the continuity baseline");
    Expect(
        !PortalTransitionDecision::IsPortalPredictionDiagnosticCommandDelta(-1),
        "commands before the commit must stay outside the diagnostic window");
    Expect(
        PortalTransitionDecision::IsPortalPredictionDiagnosticCommandDelta(0)
            && PortalTransitionDecision::IsPortalPredictionDiagnosticCommandDelta(1),
        "the stabilized diagnostic window must retain commit and the first authoritative command");
    Expect(
        !PortalTransitionDecision::IsPortalPredictionDiagnosticCommandDelta(2),
        "commands beyond the first authoritative command must stay outside the stabilized window");
    Expect(
        PortalTransitionDecision::IsFloorToWallDiagnosticOrientation(0.99f, 0.01f),
        "a near-horizontal entry and near-vertical exit must arm the floor-to-wall diagnostic");
    Expect(
        !PortalTransitionDecision::IsFloorToWallDiagnosticOrientation(0.01f, 0.99f),
        "the reverse wall-to-floor direction must remain outside the focused diagnostic");
    Expect(
        !PortalTransitionDecision::IsFloorToWallDiagnosticOrientation(0.99f, 0.75f),
        "a sloped exit that is not wall-like must remain outside the focused diagnostic");
    Expect(
        PortalTransitionDecision::IsFloorToWallDiagnosticCommandDelta(0)
            && PortalTransitionDecision::IsFloorToWallDiagnosticCommandDelta(32),
        "the delayed floor-to-wall diagnostic must retain commit through command plus thirty-two");
    Expect(
        !PortalTransitionDecision::IsFloorToWallDiagnosticCommandDelta(-1)
            && !PortalTransitionDecision::IsFloorToWallDiagnosticCommandDelta(33),
        "the delayed floor-to-wall diagnostic must reject commands outside its bounded window");
    Expect(
        PortalTransitionDecision::IsWithinGModWallPortalBounds(16.5f, 0.0f, 0.0f),
        "GMod wall portal bounds accepts a centered player close to the portal plane");
    Expect(
        !PortalTransitionDecision::IsWithinGModWallPortalBounds(17.1f, 0.0f, 0.0f),
        "GMod wall portal bounds rejects players too far in front of the portal plane");
    Expect(
        !PortalTransitionDecision::IsWithinGModWallPortalBounds(8.0f, 20.1f, 0.0f),
        "GMod wall portal bounds rejects players outside the portal width");
    Expect(
        !PortalTransitionDecision::IsWithinGModWallPortalBounds(8.0f, 0.0f, 44.1f),
        "GMod wall portal bounds rejects players above the portal height");
    Expect(
        PortalTransitionDecision::IsWithinGModWallPortalBounds(8.0f, 0.0f, -57.0f),
        "L4D2-adapted wall portal bounds accepts the observed local player feet height");
    Expect(
        !PortalTransitionDecision::IsWithinGModWallPortalBounds(8.0f, 0.0f, -61.0f),
        "L4D2-adapted wall portal bounds still rejects players below the practical portal height");

    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(true, true, 6.25f, 7.0f, 9.0f, 1.0f) - 1.0f) < 0.001f,
        "portal-aware near clip reduces the main view near plane inside the confirmed clipping interval");
    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(false, true, 6.25f, 7.0f, 9.0f, 1.0f) - 7.0f) < 0.001f,
        "disabled portal-aware near clip preserves the original near plane");
    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(true, false, 6.25f, 7.0f, 9.0f, 1.0f) - 7.0f) < 0.001f,
        "invalid portal context preserves the original near plane");
    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(true, true, 9.01f, 7.0f, 9.0f, 1.0f) - 7.0f) < 0.001f,
        "a view outside the portal near-plane danger interval preserves the original near plane");
    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(true, true, -0.01f, 7.0f, 9.0f, 1.0f) - 7.0f) < 0.001f,
        "the pre-Teleport near-clip path does not alter a view behind the entry plane");
    Expect(
        std::fabs(PortalTransitionDecision::ComputePortalAwareNearClip(true, true, 0.5f, 0.5f, 9.0f, 1.0f) - 0.5f) < 0.001f,
        "portal-aware near clip never increases an already smaller near plane");

    ExpectNear(
        PortalTransitionDecision::ComputePortalAwareNearClipForPhase(
            PortalTransitionPhase::ExitingPortal, true, true, -1.5f, 7.0f, 18.0f, 1.0f),
        1.0f,
        "exact exit handoff must keep the near plane reduced while the camera is still behind the exit plane");
    ExpectNear(
        PortalTransitionDecision::ComputePortalAwareNearClipForPhase(
            PortalTransitionPhase::Cooldown, true, true, 16.82f, 7.0f, 18.0f, 1.0f),
        1.0f,
        "high-speed exit handoff must survive the immediate transition into cooldown");
    ExpectNear(
        PortalTransitionDecision::ComputePortalAwareNearClipForPhase(
            PortalTransitionPhase::Cooldown, true, true, 18.01f, 7.0f, 18.0f, 1.0f),
        7.0f,
        "exit near clip must stop after the camera has cleared the guarded interval");
    ExpectNear(
        PortalTransitionDecision::ComputePortalAwareNearClipForPhase(
            PortalTransitionPhase::Idle, true, true, 1.0f, 7.0f, 18.0f, 1.0f),
        7.0f,
        "normal gameplay must not inherit the exit handoff near clip");

    Expect(
        PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            true, PortalTransitionPhase::ExitingPortal, PortalExitClearanceMode::ExactTransform,
            true, true, -1.5f, 24.0f),
        "exact exit handoff must append a safe visibility origin while the view is behind the exit plane");
    Expect(
        PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            true, PortalTransitionPhase::Cooldown, PortalExitClearanceMode::ExactTransform,
            true, true, 16.82f, 24.0f),
        "the visibility guard must cover a high-speed handoff that reaches cooldown in one tick");
    Expect(
        !PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            true, PortalTransitionPhase::ExitingPortal, PortalExitClearanceMode::PlaneEpsilon,
            true, true, -1.5f, 24.0f),
        "non-exact clearance modes must remain unchanged for A/B comparison");
    Expect(
        !PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            true, PortalTransitionPhase::ExitingPortal, PortalExitClearanceMode::ExactTransform,
            false, true, -1.5f, 24.0f),
        "recursive portal views must not receive the main-view exit visibility guard");
    Expect(
        !PortalTransitionDecision::ShouldUseExactExitVisualGuard(
            true, PortalTransitionPhase::ExitingPortal, PortalExitClearanceMode::ExactTransform,
            true, true, 24.01f, 24.0f),
        "the visibility guard must stop outside its bounded exit interval");

    Expect(
        PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, true, true, true, -0.001f),
        "the local entry camera must hand off immediately after it crosses the portal plane");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, true, true, true, 0.0f),
        "the entry camera must remain in the entry world while it is still on the portal plane");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, true, true, true, 0.001f),
        "the entry camera must remain unchanged in front of the portal plane");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, true, true, false, -1.0f),
        "a camera outside the portal aperture must never be transformed");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::ExitingPortal,
            true, true, true, true, -1.0f),
        "the exit phase must not transform the already teleported camera a second time");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            false, PortalTransitionPhase::IntersectingPortal,
            true, true, true, true, -1.0f),
        "the runtime control must disable only the entry camera handoff path");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            false, true, true, true, -1.0f),
        "non-local player views must not receive the local camera handoff");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, false, true, true, -1.0f),
        "the entry camera handoff must not run without active BSP carving");
    Expect(
        !PortalTransitionDecision::ShouldApplyEntryCameraHandoff(
            true, PortalTransitionPhase::IntersectingPortal,
            true, true, false, true, -1.0f),
        "the entry camera handoff must not run until both portals are ready");

    ExpectNear(
        PortalTransitionDecision::ComputeOfficialPortalRemoteClipPlaneDistance(10.0f),
        9.5f,
        "the official remote clip plane must sit half a unit behind a positive portal plane");
    ExpectNear(
        PortalTransitionDecision::ComputeOfficialPortalRemoteClipPlaneDistance(-3.0f),
        -3.5f,
        "the official remote clip plane offset must be orientation-independent");
    ExpectNear(
        PortalTransitionDecision::ComputeOfficialPortalRemoteViewNormalPush(),
        0.0f,
        "the official remote view must preserve the exact transformed camera origin");

    PortalTransitionDecision::SetPortalNearClipFixEnabled(false);
    Expect(
        !PortalTransitionDecision::GetPortalNearClipFixEnabled(),
        "portal-aware near clip runtime control can disable the view adjustment");
    PortalTransitionDecision::SetPortalNearClipFixEnabled(true);
    Expect(
        PortalTransitionDecision::GetPortalNearClipFixEnabled(),
        "portal-aware near clip runtime control can restore the default enabled state");

    Expect(
        !PortalTransitionDecision::GetExactExitVisualGuardEnabled(),
        "the disproven exact-exit visual guard must remain off in the baseline");
    PortalTransitionDecision::SetExactExitVisualGuardEnabled(true);
    Expect(
        PortalTransitionDecision::GetExactExitVisualGuardEnabled(),
        "the exact exit visual guard control remains available for diagnostics");
    PortalTransitionDecision::SetExactExitVisualGuardEnabled(false);
    Expect(
        !PortalTransitionDecision::GetExactExitVisualGuardEnabled(),
        "the exact exit visual guard must restore the disabled continuity baseline");

    PortalTransitionDecision::SetEntryCameraHandoffEnabled(false);
    Expect(
        !PortalTransitionDecision::GetEntryCameraHandoffEnabled(),
        "the official-style entry camera handoff can be disabled for runtime A/B comparison");
    PortalTransitionDecision::SetEntryCameraHandoffEnabled(true);
    Expect(
        PortalTransitionDecision::GetEntryCameraHandoffEnabled(),
        "the official-style entry camera handoff defaults back to enabled for the next traversal");

    std::cout << "PortalTransitionDecision tests passed\n";
    return 0;
}
