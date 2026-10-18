#pragma once

enum class PortalTransitionPhase
{
    Idle,
    ApproachingPortal,
    IntersectingPortal,
    CommittingTeleport,
    ExitingPortal,
    Cooldown,
};

enum class PortalExitClearanceMode
{
    FullHull,
    PlaneEpsilon,
    ExactTransform,
};

enum class PortalServerCommandTiming
{
    NoCommit,
    BeforeCommit,
    CommitCommand,
    AfterCommitWindow,
    AfterWindow,
};

enum class PortalCommandAngleSpace
{
    Unknown,
    Entry,
    Exit,
    Ambiguous,
};

struct PortalServerRunCommandClassification
{
    PortalServerCommandTiming timing = PortalServerCommandTiming::NoCommit;
    PortalCommandAngleSpace angleSpace = PortalCommandAngleSpace::Unknown;
    int commandDelta = 0;
};

struct PortalVisualCommitSeedEdgeAction
{
    bool changed = false;
    bool clearPendingSeed = false;
    bool clearSeedDerivedPredictor = false;
};

struct PortalEyeSingleOwnerEdgeAction
{
    bool changed = false;
    bool resetEyeHistoryState = false;
    bool resetShadowState = false;
    bool clearPendingSeed = false;
    bool resetTransactionVisualState = false;
};

namespace PortalTransitionDecision
{
    struct PortalEyeCrossLeafHandoffFrameDecision
    {
        bool allowCandidate = false;
        bool emitAllowEdge = false;
        bool emitRejectEdge = false;
    };

    class PortalEyeCrossLeafHandoffController
    {
    public:
        PortalEyeCrossLeafHandoffFrameDecision EvaluateFrame(
            bool enabled,
            bool leafMismatch,
            bool geometryClear,
            bool runtimeSafetyFailure);
        bool ConsumeHorizonExpiryProbeEdge(
            bool diagnosticsEnabled,
            bool releaseTailEnded,
            bool horizonExpired);
        void Reset();
        bool IsAuthorized() const;

    private:
        bool m_authorized = false;
        bool m_horizonExpiryProbeEmitted = false;
    };

    bool RequiresControlledNoclip(PortalTransitionPhase phase);
    bool ShouldApplyLegacyBridgeMovement(PortalTransitionPhase phase);
    bool ShouldRunOriginalPlayerMove(PortalTransitionPhase phase);
    bool ShouldReplaceOriginalPlayerMove(PortalTransitionPhase phase, bool movementMutationAllowed);
    bool ShouldRestoreWalkOnProbeLoss(PortalTransitionPhase phase);
    bool ShouldReleaseExitControlledMove(float frontDistance, bool insideAperture);
    bool ShouldAllowTraversalEntry(PortalTransitionPhase phase, float currentTime, float cooldownUntil);
    bool IsCommittedTeleportPredictionSyncPhase(PortalTransitionPhase phase);
    bool ShouldSyncCommittedViewAnglesAtPredictionSetup(
        PortalTransitionPhase phase,
        int commandNumber,
        int committedCommandNumber);
    PortalServerRunCommandClassification ClassifyServerRunCommand(
        bool hasCommittedTeleport,
        int commandNumber,
        int committedCommandNumber,
        float differenceFromPrePortalAngles,
        float differenceFromPostPortalAngles);
    bool ShouldApplyServerPortalCommandAngleFix(
        bool enabled,
        PortalServerCommandTiming timing,
        PortalCommandAngleSpace angleSpace);
    bool GetServerPortalCommandAngleFixEnabled();
    void SetServerPortalCommandAngleFixEnabled(bool enabled);
    bool GetPortalTransactionDiagnosticsEnabled();
    void SetPortalTransactionDiagnosticsEnabled(bool enabled);
    bool GetPortalViewReorientationEnabled();
    void SetPortalViewReorientationEnabled(bool enabled);
    bool GetPortalEyeCrossLeafHandoffEnabled();
    void SetPortalEyeCrossLeafHandoffEnabled(bool enabled);
    bool ShouldAllowPortalEyeCrossLeafHandoff(
        bool enabled,
        bool leafMismatch,
        bool geometryClear);
    bool GetGroundLiveCommitEnabled();
    void SetGroundLiveCommitEnabled(bool enabled);
    bool ShouldUseProjectedPlaneCrossing(bool groundLiveCommitEnabled, bool onGround);
    bool IsPortalPredictionDiagnosticCommandDelta(int commandDelta);
    bool IsFloorToWallDiagnosticOrientation(float entryNormalZ, float exitNormalZ);
    bool IsFloorToWallDiagnosticCommandDelta(int commandDelta);
    bool IsWithinGModWallPortalBounds(float frontDistance, float feetLocalRight, float feetLocalUp);
    bool IsViewNearPortalPlane(
        float forwardDistance,
        float localRight,
        float localUp,
        float maxDepth,
        float maxRight,
        float maxUp);

    bool GetPortalNearClipFixEnabled();
    void SetPortalNearClipFixEnabled(bool enabled);
    float ComputePortalAwareNearClip(
        bool enabled,
        bool portalContextValid,
        float signedDepth,
        float currentNearClip,
        float activationDepth,
        float targetNearClip);
    float ComputePortalAwareNearClipForPhase(
        PortalTransitionPhase phase,
        bool enabled,
        bool portalContextValid,
        float signedDepth,
        float currentNearClip,
        float activationDepth,
        float targetNearClip);

    bool GetExactExitVisualGuardEnabled();
    void SetExactExitVisualGuardEnabled(bool enabled);
    bool ShouldUseExactExitVisualGuard(
        bool enabled,
        PortalTransitionPhase phase,
        PortalExitClearanceMode clearanceMode,
        bool isMainView,
        bool portalContextValid,
        float signedDepth,
        float activationDepth);

    bool GetEntryCameraHandoffEnabled();
    void SetEntryCameraHandoffEnabled(bool enabled);
    bool GetPortalEyeContinuityEnabled();
    void SetPortalEyeContinuityEnabled(bool enabled);
    bool GetPortalEyeShadowPredictorEnabled();
    void SetPortalEyeShadowPredictorEnabled(bool enabled);
    bool GetPortalVisualCommitSeedEnabled();
    void SetPortalVisualCommitSeedEnabled(bool enabled);
    PortalVisualCommitSeedEdgeAction DecidePortalVisualCommitSeedEdgeAction(
        bool wasEnabled,
        bool enabled);
    bool GetPortalEyeShadowReleaseTailEnabled();
    void SetPortalEyeShadowReleaseTailEnabled(bool enabled);
    bool GetPortalEyeSingleOwnerEnabled();
    void SetPortalEyeSingleOwnerEnabled(bool enabled);
    PortalEyeSingleOwnerEdgeAction DecidePortalEyeSingleOwnerEdgeAction(
        bool wasEnabled,
        bool enabled);
    bool ShouldFailClosedForMissingPortalEyeSafetyInterface(
        bool shadowViewActive,
        bool engineTraceAvailable);
    bool ArePortalEyeBidirectionalSafetyTracesClear(
        bool forwardAvailable,
        float forwardFraction,
        bool forwardStartSolid,
        bool forwardAllSolid,
        bool forwardEndFinite,
        bool reverseAvailable,
        float reverseFraction,
        bool reverseStartSolid,
        bool reverseAllSolid,
        bool reverseEndFinite);
    bool GetProjectedEyeHistoryRebaseEnabled();
    void SetProjectedEyeHistoryRebaseEnabled(bool enabled);
    bool ShouldApplyEntryCameraHandoff(
        bool enabled,
        PortalTransitionPhase phase,
        bool isLocalPlayer,
        bool carvingActive,
        bool portalsReady,
        bool insideAperture,
        float signedDepth);

    float ComputeOfficialPortalRemoteClipPlaneDistance(float portalPlaneDistance);
    float ComputeOfficialPortalRemoteViewNormalPush();

    PortalExitClearanceMode GetExitClearanceMode();
    void SetExitClearanceMode(PortalExitClearanceMode mode);
    const char* ExitClearanceModeName(PortalExitClearanceMode mode);
    bool TryParseExitClearanceMode(const char* text, PortalExitClearanceMode* mode);

    bool ShouldCommitPlaneCrossing(
        float previousSignedDepth,
        float currentSignedDepth,
        bool hasPreviousDepth,
        bool insideAperture,
        bool movingIntoPortal);

    bool ShouldCommitExitRearmCatchup(
        bool candidateIsReleasedExitPortal,
        float previousSignedDepth,
        float currentSignedDepth,
        bool hasPreviousDepth,
        bool insideAperture,
        bool movingIntoPortal);

    bool TryComputePortalPlaneCrossingFraction(
        float previousSignedDepth,
        float currentSignedDepth,
        float* crossingFraction);

    float ComputePredictedPortalCommitLeadTime(
        float currentSignedDepth,
        float velocityAlongNormal,
        float interval,
        float targetSignedDepth);

    bool IsPortalCommitHalfSpaceValid(
        float entrySignedDepth,
        float transformedExitSignedDepth);

    float ComputeExitClearancePush(
        float transformedCenterDepth,
        float hullHalfExtentAlongNormal,
        float clearanceEpsilon);

    float ComputeExitClearancePush(
        PortalExitClearanceMode mode,
        float transformedCenterDepth,
        float hullHalfExtentAlongNormal,
        float clearanceEpsilon);
}
