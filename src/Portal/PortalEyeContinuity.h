#pragma once

#include <cstdint>

#include "../Util/Math/Math.h"

namespace PortalEyeContinuity
{
    enum class ShadowRestartSeedSource
    {
        None,
        OfficialCommandEnd,
    };

    const char* ShadowRestartSeedSourceName(
        ShadowRestartSeedSource source);

    struct ShadowRestartSeedInput
    {
        Vector eye;
        Vector velocity;
        float publicationTime = 0.0f;
        float renderTime = 0.0f;
        std::uint32_t transactionId = 0;
        std::uint32_t pendingTransactionId = 0;
        ShadowRestartSeedSource source = ShadowRestartSeedSource::None;
        bool enabled = false;
        bool candidate = false;
        bool stale = false;
    };

    enum class ShadowReleaseTailEndReason
    {
        None,
        Converged,
        HorizonExpired,
    };

    const char* ShadowReleaseTailEndReasonName(
        ShadowReleaseTailEndReason reason);

    struct ShadowPredictionResult
    {
        Vector eyeOrigin;
        Vector baselineEye;
        Vector baselineDelta;
        Vector rawEyeDelta;
        Vector shadowEyeBefore;
        Vector shadowEyeAfter;
        Vector shadowFrameDelta;
        Vector predictionOffset;
        Vector restartSeedEye;
        Vector advancedRestartSeedEye;
        bool enabled = false;
        bool active = false;
        bool continuityActive = false;
        bool initializedBefore = false;
        bool initializedAfter = false;
        bool restarted = false;
        bool rawAdvanced = false;
        bool worldSpaceAdvanced = false;
        bool horizonClamped = false;
        bool applied = false;
        bool restartSeedEnabled = false;
        bool restartSeedCandidate = false;
        bool restartSeedValid = false;
        bool restartSeedApplied = false;
        bool releaseTailEnabled = false;
        bool releaseTailActiveBefore = false;
        bool releaseTailActive = false;
        bool releaseTailStarted = false;
        bool releaseTailEnded = false;
        bool releaseTailSawRawAdvance = false;
        ShadowReleaseTailEndReason releaseTailEndReason =
            ShadowReleaseTailEndReason::None;
        float predictionElapsedBefore = 0.0f;
        float predictionElapsed = 0.0f;
        float predictionHorizon = 0.0f;
        float releaseTailElapsedBefore = 0.0f;
        float releaseTailElapsed = 0.0f;
        float releaseTailResidual = 0.0f;
        float restartSeedElapsed = 0.0f;
        ShadowRestartSeedSource restartSeedSource =
            ShadowRestartSeedSource::None;
    };

    class ShadowPredictor
    {
    public:
        void Reset();
        ShadowPredictionResult Update(
            bool enabled,
            bool continuityActive,
            bool restart,
            const Vector& baselineEye,
            const Vector& rawPhysicalEye,
            const Vector& velocity,
            float frameTime,
            float predictionHorizon,
            bool advanceFrameClock,
            bool releaseTailEnabled,
            const ShadowRestartSeedInput& restartSeed =
                ShadowRestartSeedInput());

    private:
        Vector m_rawEyeAnchor;
        Vector m_previousBaselineEye;
        Vector m_shadowEye;
        float m_predictionElapsed = 0.0f;
        float m_releaseTailElapsed = 0.0f;
        bool m_initialized = false;
        bool m_releaseTailActive = false;
        bool m_releaseTailSawRawAdvance = false;
        Vector m_restartSeedEye;
        Vector m_advancedRestartSeedEye;
        float m_restartSeedElapsed = 0.0f;
        ShadowRestartSeedSource m_restartSeedSource =
            ShadowRestartSeedSource::None;
        bool m_restartSeedAppliedThisFrame = false;
    };

    class ShadowDiagnosticLogGate
    {
    public:
        void Reset();
        bool ShouldLog(
            const ShadowPredictionResult& result,
            bool consumedPendingTeleport,
            bool releasedOverride,
            bool runtimeSafetyFallback);

    private:
        bool m_hasPrevious = false;
        bool m_previousInitialized = false;
        bool m_previousApplied = false;
        bool m_previousHorizonClamped = false;
        bool m_rawAdvanceLogged = false;
    };

    struct UpdateResult
    {
        Vector eyeOrigin;
        Vector singleOwnerSeedEye;
        Vector singleOwnerAdvancedSeedEye;
        Vector singleOwnerStateEyeAfterSeed;
        Vector previousRawEyeBefore;
        Vector continuityEyeBefore;
        Vector previousRawEyeAfterTransform;
        Vector continuityEyeAfterTransform;
        Vector previousRawEyeAfterRebase;
        Vector continuityEyeAfterRebase;
        Vector previousRawEyeWithoutRebaseTransform;
        Vector continuityEyeWithoutRebaseTransform;
        Vector previousRawEyeCandidateAfterTransform;
        Vector continuityEyeCandidateAfterTransform;
        Vector historyRebaseDelta;
        Vector freeMovementDelta;
        Vector engineViewMovementDelta;
        Vector unconstrainedEye;
        Vector previousRawEyeAfter;
        Vector continuityEyeAfter;
        Vector activeExitPlaneOrigin;
        Vector activeExitPlaneNormal;
        bool overrideOriginalView = false;
        bool consumedPendingTeleport = false;
        bool releasedOverride = false;
        bool initializedBefore = false;
        bool initializedAfter = false;
        bool hadPendingTeleport = false;
        bool interpolatingBefore = false;
        bool interpolatingAfter = false;
        bool overridingBefore = false;
        bool overridingAfter = false;
        bool planeConstrained = false;
        bool safetyFallbackToOriginal = false;
        bool historyRebaseCandidate = false;
        bool historyRebaseApplied = false;
        bool singleOwnerSeedCandidate = false;
        bool singleOwnerSeedValid = false;
        bool singleOwnerSeedApplied = false;
        float normalCorrection = 0.0f;
        float unconstrainedExitDepth = 0.0f;
        float unconstrainedOriginalResidual = 0.0f;
        float originalExitDepth = 0.0f;
        float outputExitDepth = 0.0f;
        float rawResidual = 0.0f;
        float originalViewResidual = 0.0f;
        float singleOwnerSeedElapsed = 0.0f;
        std::uint32_t traversalEpoch = 0;
        int commandNumber = 0;
    };

    class State
    {
    public:
        void Reset();
        bool PublishTeleport(
            const matrix3x4_t& entryToExit,
            const Vector& exitPlaneOrigin,
            const Vector& exitPlaneNormal,
            std::uint32_t traversalEpoch,
            int commandNumber,
            bool rebaseHistoryBeforeTransform = false,
            const Vector& historyRebaseDelta = Vector());
        UpdateResult Update(
            bool enabled,
            const Vector& rawPhysicalEye,
            const Vector& originalViewEye,
            float frameTime,
            float predictionHorizon = 0.0f,
            const ShadowRestartSeedInput& singleOwnerSeed =
                ShadowRestartSeedInput());
        bool HasPendingTeleport() const { return m_hasPendingTeleport; }
        bool IsInitialized() const { return m_initialized; }
        bool IsInterpolating() const { return m_eyePositionIsInterpolating; }
        bool IsOverridingOriginalView() const { return m_overrideOriginalView; }

    private:
        Vector TransformPoint(const matrix3x4_t& matrix, const Vector& point) const;

        Vector m_previousRawEye;
        Vector m_previousOriginalViewEye;
        Vector m_continuityEye;
        matrix3x4_t m_pendingMatrix;
        Vector m_pendingExitPlaneOrigin;
        Vector m_pendingExitPlaneNormal;
        Vector m_pendingHistoryRebaseDelta;
        Vector m_activeExitPlaneOrigin;
        Vector m_activeExitPlaneNormal;
        std::uint32_t m_pendingTraversalEpoch = 0;
        std::uint32_t m_lastPublishedTraversalEpoch = 0;
        int m_pendingCommandNumber = 0;
        int m_activeCommandNumber = 0;
        bool m_initialized = false;
        bool m_hasPendingTeleport = false;
        bool m_pendingHistoryRebase = false;
        bool m_eyePositionIsInterpolating = false;
        bool m_overrideOriginalView = false;
    };
}
