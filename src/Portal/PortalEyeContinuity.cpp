#include "PortalEyeContinuity.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kOfficialFollowUnitsPerSecond = 100.0f;
    constexpr float kOfficialMaximumLag = 150.0f;
    constexpr float kMinimumVectorLength = 0.001f;
    constexpr float kEngineCatchUpTolerance = 0.5f;
    constexpr float kExitPlaneTolerance = 0.001f;
    constexpr float kShadowRawAdvanceEpsilon = 0.001f;
    constexpr float kMaximumShadowPredictionHorizon = 0.05f;

    bool IsFiniteVector(const Vector& value)
    {
        return std::isfinite(value.x)
            && std::isfinite(value.y)
            && std::isfinite(value.z);
    }
}

void PortalEyeContinuity::ShadowPredictor::Reset()
{
    m_rawEyeAnchor = Vector();
    m_previousBaselineEye = Vector();
    m_shadowEye = Vector();
    m_predictionElapsed = 0.0f;
    m_releaseTailElapsed = 0.0f;
    m_initialized = false;
    m_releaseTailActive = false;
    m_releaseTailSawRawAdvance = false;
}

const char* PortalEyeContinuity::ShadowReleaseTailEndReasonName(
    ShadowReleaseTailEndReason reason)
{
    switch (reason)
    {
    case ShadowReleaseTailEndReason::None: return "none";
    case ShadowReleaseTailEndReason::Converged: return "converged";
    case ShadowReleaseTailEndReason::HorizonExpired: return "horizon-expired";
    }
    return "unknown";
}

PortalEyeContinuity::ShadowPredictionResult
PortalEyeContinuity::ShadowPredictor::Update(
    bool enabled,
    bool continuityActive,
    bool restart,
    const Vector& baselineEye,
    const Vector& rawPhysicalEye,
    const Vector& velocity,
    float frameTime,
    float predictionHorizon,
    bool advanceFrameClock,
    bool releaseTailEnabled)
{
    ShadowPredictionResult result;
    result.eyeOrigin = baselineEye;
    result.baselineEye = baselineEye;
    result.enabled = enabled;
    result.continuityActive = continuityActive;
    result.releaseTailEnabled = releaseTailEnabled;
    result.releaseTailActiveBefore = m_releaseTailActive;
    result.releaseTailElapsedBefore = m_releaseTailElapsed;
    result.initializedBefore = m_initialized;
    result.predictionElapsedBefore = m_predictionElapsed;
    result.shadowEyeBefore = m_shadowEye;

    if (!enabled
        || !IsFiniteVector(baselineEye)
        || !IsFiniteVector(rawPhysicalEye)
        || !IsFiniteVector(velocity))
    {
        Reset();
        result.initializedAfter = m_initialized;
        return result;
    }

    const float safeHorizon = std::isfinite(predictionHorizon)
        ? std::min(std::max(0.0f, predictionHorizon), kMaximumShadowPredictionHorizon)
        : 0.0f;
    result.predictionHorizon = safeHorizon;

    if (!continuityActive)
    {
        if (!releaseTailEnabled || !m_initialized || safeHorizon <= 0.0f)
        {
            Reset();
            result.initializedAfter = m_initialized;
            return result;
        }

        if (!m_releaseTailActive)
        {
            m_releaseTailActive = true;
            m_releaseTailElapsed = 0.0f;
            m_releaseTailSawRawAdvance = false;
            result.releaseTailStarted = true;
        }

        result.baselineDelta = baselineEye - m_previousBaselineEye;
        result.rawEyeDelta = rawPhysicalEye - m_rawEyeAnchor;
        if (result.rawEyeDelta.Lenght() > kShadowRawAdvanceEpsilon)
        {
            m_rawEyeAnchor = rawPhysicalEye;
            m_releaseTailSawRawAdvance = true;
            result.rawAdvanced = true;
        }

        result.predictionOffset = m_shadowEye - baselineEye;
        result.releaseTailResidual = result.predictionOffset.Lenght();
        result.releaseTailSawRawAdvance = m_releaseTailSawRawAdvance;
        result.releaseTailElapsed = m_releaseTailElapsed;
        if (m_releaseTailSawRawAdvance
            && result.releaseTailResidual <= kEngineCatchUpTolerance)
        {
            result.shadowEyeAfter = m_shadowEye;
            result.releaseTailEnded = true;
            result.releaseTailEndReason =
                ShadowReleaseTailEndReason::Converged;
            Reset();
            result.initializedAfter = false;
            return result;
        }

        if (advanceFrameClock)
        {
            const float safeFrameTime = std::isfinite(frameTime)
                ? std::max(0.0f, frameTime)
                : 0.0f;
            const float availableTailTime = std::max(
                0.0f,
                safeHorizon - m_releaseTailElapsed);
            const float appliedFrameTime = std::min(
                safeFrameTime,
                availableTailTime);
            result.shadowFrameDelta = velocity * appliedFrameTime;
            if (result.shadowFrameDelta.Lenght() > kMinimumVectorLength)
            {
                m_shadowEye += result.shadowFrameDelta;
                result.worldSpaceAdvanced = true;
            }
            m_releaseTailElapsed += appliedFrameTime;
            result.horizonClamped = safeFrameTime > availableTailTime;
        }

        m_previousBaselineEye = baselineEye;
        result.shadowEyeAfter = m_shadowEye;
        result.predictionOffset = m_shadowEye - baselineEye;
        result.releaseTailResidual = result.predictionOffset.Lenght();
        result.releaseTailSawRawAdvance = m_releaseTailSawRawAdvance;
        result.releaseTailElapsed = m_releaseTailElapsed;

        if (m_releaseTailSawRawAdvance
            && result.releaseTailResidual <= kEngineCatchUpTolerance)
        {
            result.releaseTailEnded = true;
            result.releaseTailEndReason =
                ShadowReleaseTailEndReason::Converged;
            Reset();
            result.initializedAfter = false;
            return result;
        }

        if (m_releaseTailElapsed >= safeHorizon)
        {
            result.releaseTailEnded = true;
            result.releaseTailEndReason =
                ShadowReleaseTailEndReason::HorizonExpired;
            Reset();
            result.initializedAfter = false;
            return result;
        }

        result.active = true;
        result.releaseTailActive = true;
        if (result.predictionOffset.Lenght() > kMinimumVectorLength)
        {
            result.eyeOrigin = m_shadowEye;
            result.applied = true;
        }
        result.initializedAfter = m_initialized;
        return result;
    }

    m_releaseTailActive = false;
    m_releaseTailElapsed = 0.0f;
    m_releaseTailSawRawAdvance = false;
    result.active = true;

    if (!m_initialized || restart)
    {
        m_rawEyeAnchor = rawPhysicalEye;
        m_previousBaselineEye = baselineEye;
        m_shadowEye = baselineEye;
        m_predictionElapsed = 0.0f;
        m_initialized = true;
        result.restarted = true;
        result.shadowEyeAfter = m_shadowEye;
        result.initializedAfter = true;
        return result;
    }

    result.baselineDelta = baselineEye - m_previousBaselineEye;
    result.rawEyeDelta = rawPhysicalEye - m_rawEyeAnchor;
    if (result.rawEyeDelta.Lenght() > kShadowRawAdvanceEpsilon)
    {
        m_rawEyeAnchor = rawPhysicalEye;
        result.rawAdvanced = true;
        // A new authoritative sample opens a fresh one-tick prediction
        // horizon. It must not retract the world-space eye motion that was
        // already rendered while the 30 Hz entity state was frozen.
        m_predictionElapsed = 0.0f;
    }

    if (advanceFrameClock)
    {
        const float safeFrameTime = std::isfinite(frameTime)
            ? std::max(0.0f, frameTime)
            : 0.0f;
        const float availablePredictionTime = std::max(
            0.0f,
            safeHorizon - m_predictionElapsed);
        const float appliedFrameTime = std::min(
            safeFrameTime,
            availablePredictionTime);
        result.shadowFrameDelta = velocity * appliedFrameTime;
        if (result.shadowFrameDelta.Lenght() > kMinimumVectorLength)
        {
            m_shadowEye += result.shadowFrameDelta;
            result.worldSpaceAdvanced = true;
        }
        m_predictionElapsed += appliedFrameTime;
        result.horizonClamped = safeFrameTime > availablePredictionTime;
    }

    m_previousBaselineEye = baselineEye;
    result.predictionElapsed = m_predictionElapsed;
    result.shadowEyeAfter = m_shadowEye;
    result.predictionOffset = m_shadowEye - baselineEye;
    if (result.predictionOffset.Lenght() > kMinimumVectorLength)
    {
        result.eyeOrigin = m_shadowEye;
        result.applied = true;
    }
    result.initializedAfter = m_initialized;
    return result;
}

void PortalEyeContinuity::ShadowDiagnosticLogGate::Reset()
{
    m_hasPrevious = false;
    m_previousInitialized = false;
    m_previousApplied = false;
    m_previousHorizonClamped = false;
    m_rawAdvanceLogged = false;
}

bool PortalEyeContinuity::ShadowDiagnosticLogGate::ShouldLog(
    const ShadowPredictionResult& result,
    bool consumedPendingTeleport,
    bool releasedOverride,
    bool runtimeSafetyFallback)
{
    if (result.restarted)
        m_rawAdvanceLogged = false;

    const bool firstRawAdvance = result.rawAdvanced && !m_rawAdvanceLogged;
    const bool lifecycleEdge = !m_hasPrevious
        || result.initializedAfter != m_previousInitialized
        || result.applied != m_previousApplied;
    const bool horizonClampEdge = result.horizonClamped
        && (!m_hasPrevious || !m_previousHorizonClamped);
    const bool shouldLog = lifecycleEdge
        || horizonClampEdge
        || result.restarted
        || firstRawAdvance
        || consumedPendingTeleport
        || releasedOverride
        || runtimeSafetyFallback;

    m_hasPrevious = true;
    m_previousInitialized = result.initializedAfter;
    m_previousApplied = result.applied;
    m_previousHorizonClamped = result.horizonClamped;
    if (result.rawAdvanced)
        m_rawAdvanceLogged = true;
    if (!result.initializedAfter)
        m_rawAdvanceLogged = false;
    return shouldLog;
}

void PortalEyeContinuity::State::Reset()
{
    m_previousRawEye = Vector();
    m_previousOriginalViewEye = Vector();
    m_continuityEye = Vector();
    m_pendingMatrix = matrix3x4_t();
    m_pendingExitPlaneOrigin = Vector();
    m_pendingExitPlaneNormal = Vector();
    m_pendingHistoryRebaseDelta = Vector();
    m_activeExitPlaneOrigin = Vector();
    m_activeExitPlaneNormal = Vector();
    m_pendingTraversalEpoch = 0;
    m_lastPublishedTraversalEpoch = 0;
    m_pendingCommandNumber = 0;
    m_activeCommandNumber = 0;
    m_initialized = false;
    m_hasPendingTeleport = false;
    m_pendingHistoryRebase = false;
    m_eyePositionIsInterpolating = false;
    m_overrideOriginalView = false;
}

bool PortalEyeContinuity::State::PublishTeleport(
    const matrix3x4_t& entryToExit,
    const Vector& exitPlaneOrigin,
    const Vector& exitPlaneNormal,
    std::uint32_t traversalEpoch,
    int commandNumber,
    bool rebaseHistoryBeforeTransform,
    const Vector& historyRebaseDelta)
{
    Vector normalizedExitNormal = exitPlaneNormal;
    const float exitNormalLength = normalizedExitNormal.Lenght();
    if (!m_initialized
        || traversalEpoch == 0
        || commandNumber <= 0
        || traversalEpoch == m_lastPublishedTraversalEpoch
        || !IsFiniteVector(exitPlaneOrigin)
        || !IsFiniteVector(exitPlaneNormal)
        || !IsFiniteVector(historyRebaseDelta)
        || exitNormalLength <= kMinimumVectorLength)
    {
        return false;
    }
    normalizedExitNormal *= 1.0f / exitNormalLength;

    m_pendingMatrix = entryToExit;
    m_pendingExitPlaneOrigin = exitPlaneOrigin;
    m_pendingExitPlaneNormal = normalizedExitNormal;
    m_pendingHistoryRebase = rebaseHistoryBeforeTransform;
    m_pendingHistoryRebaseDelta = historyRebaseDelta;
    m_pendingTraversalEpoch = traversalEpoch;
    m_pendingCommandNumber = commandNumber;
    m_lastPublishedTraversalEpoch = traversalEpoch;
    m_hasPendingTeleport = true;
    return true;
}

PortalEyeContinuity::UpdateResult PortalEyeContinuity::State::Update(
    bool enabled,
    const Vector& rawPhysicalEye,
    const Vector& originalViewEye,
    float frameTime)
{
    UpdateResult result;
    result.eyeOrigin = originalViewEye;
    result.previousRawEyeBefore = m_previousRawEye;
    result.continuityEyeBefore = m_continuityEye;
    result.previousRawEyeAfterTransform = m_previousRawEye;
    result.continuityEyeAfterTransform = m_continuityEye;
    result.previousRawEyeAfterRebase = m_previousRawEye;
    result.continuityEyeAfterRebase = m_continuityEye;
    result.previousRawEyeWithoutRebaseTransform = m_previousRawEye;
    result.continuityEyeWithoutRebaseTransform = m_continuityEye;
    result.previousRawEyeCandidateAfterTransform = m_previousRawEye;
    result.continuityEyeCandidateAfterTransform = m_continuityEye;
    result.initializedBefore = m_initialized;
    result.hadPendingTeleport = m_hasPendingTeleport;
    result.interpolatingBefore = m_eyePositionIsInterpolating;
    result.overridingBefore = m_overrideOriginalView;
    result.activeExitPlaneOrigin = m_activeExitPlaneOrigin;
    result.activeExitPlaneNormal = m_activeExitPlaneNormal;
    result.traversalEpoch = m_pendingTraversalEpoch;
    result.commandNumber = m_activeCommandNumber;

    const auto finishTrace = [this, &result]()
    {
        result.previousRawEyeAfter = m_previousRawEye;
        result.continuityEyeAfter = m_continuityEye;
        result.initializedAfter = m_initialized;
        result.interpolatingAfter = m_eyePositionIsInterpolating;
        result.overridingAfter = m_overrideOriginalView;
    };

    if (!enabled)
    {
        Reset();
        finishTrace();
        return result;
    }

    if (!m_initialized)
    {
        m_previousRawEye = rawPhysicalEye;
        m_previousOriginalViewEye = originalViewEye;
        m_continuityEye = originalViewEye;
        m_initialized = true;
        finishTrace();
        return result;
    }

    if (m_hasPendingTeleport)
    {
        result.historyRebaseDelta = m_pendingHistoryRebaseDelta;
        result.historyRebaseCandidate =
            m_pendingHistoryRebaseDelta.LenghtSqr()
                > kMinimumVectorLength * kMinimumVectorLength;
        result.previousRawEyeWithoutRebaseTransform =
            TransformPoint(m_pendingMatrix, m_previousRawEye);
        result.continuityEyeWithoutRebaseTransform =
            TransformPoint(m_pendingMatrix, m_continuityEye);
        result.previousRawEyeCandidateAfterTransform = TransformPoint(
            m_pendingMatrix,
            m_previousRawEye + m_pendingHistoryRebaseDelta);
        result.continuityEyeCandidateAfterTransform = TransformPoint(
            m_pendingMatrix,
            m_continuityEye + m_pendingHistoryRebaseDelta);
        if (m_pendingHistoryRebase)
        {
            m_previousRawEye += m_pendingHistoryRebaseDelta;
            m_previousOriginalViewEye += m_pendingHistoryRebaseDelta;
            m_continuityEye += m_pendingHistoryRebaseDelta;
            result.historyRebaseApplied = true;
        }
        result.previousRawEyeAfterRebase = m_previousRawEye;
        result.continuityEyeAfterRebase = m_continuityEye;
        m_previousRawEye = TransformPoint(m_pendingMatrix, m_previousRawEye);
        m_previousOriginalViewEye =
            TransformPoint(m_pendingMatrix, m_previousOriginalViewEye);
        m_continuityEye = TransformPoint(m_pendingMatrix, m_continuityEye);
        m_activeExitPlaneOrigin = m_pendingExitPlaneOrigin;
        m_activeExitPlaneNormal = m_pendingExitPlaneNormal;
        result.previousRawEyeAfterTransform = m_previousRawEye;
        result.continuityEyeAfterTransform = m_continuityEye;
        m_activeCommandNumber = m_pendingCommandNumber;
        m_hasPendingTeleport = false;
        m_pendingHistoryRebase = false;
        m_pendingHistoryRebaseDelta = Vector();
        m_eyePositionIsInterpolating = true;
        m_overrideOriginalView = true;
        result.consumedPendingTeleport = true;
    }

    result.traversalEpoch = m_pendingTraversalEpoch;
    result.commandNumber = m_activeCommandNumber;
    result.activeExitPlaneOrigin = m_activeExitPlaneOrigin;
    result.activeExitPlaneNormal = m_activeExitPlaneNormal;

    if (!m_overrideOriginalView)
    {
        m_previousRawEye = rawPhysicalEye;
        m_previousOriginalViewEye = originalViewEye;
        m_continuityEye = originalViewEye;
        finishTrace();
        return result;
    }

    // L4D2's original CalcPlayerView already contains its prediction smoothing.
    // Carry that view's motion after the commit frame instead of the stepped
    // physical eye. The transformed history remains authoritative on all three
    // axes while it is on the safe side of the exit plane; the compatibility
    // guard below only constrains candidates that would remain behind it.
    result.freeMovementDelta = rawPhysicalEye - m_previousRawEye;
    result.engineViewMovementDelta =
        originalViewEye - m_previousOriginalViewEye;
    if (!result.consumedPendingTeleport)
        m_continuityEye += result.engineViewMovementDelta;
    m_previousRawEye = rawPhysicalEye;
    m_previousOriginalViewEye = originalViewEye;

    result.originalExitDepth =
        (originalViewEye - m_activeExitPlaneOrigin).Dot(m_activeExitPlaneNormal);

    if (!std::isfinite(result.originalExitDepth)
        || result.originalExitDepth < -kExitPlaneTolerance)
    {
        m_continuityEye = originalViewEye;
        m_overrideOriginalView = false;
        m_eyePositionIsInterpolating = false;
        result.safetyFallbackToOriginal = true;
        result.releasedOverride = true;
        result.eyeOrigin = originalViewEye;
        result.outputExitDepth = result.originalExitDepth;
        result.rawResidual = (rawPhysicalEye - originalViewEye).Lenght();
        result.originalViewResidual = 0.0f;
        finishTrace();
        return result;
    }

    const float candidateExitDepth =
        (m_continuityEye - m_activeExitPlaneOrigin).Dot(m_activeExitPlaneNormal);
    result.unconstrainedEye = m_continuityEye;
    result.unconstrainedExitDepth = candidateExitDepth;
    result.unconstrainedOriginalResidual =
        (originalViewEye - m_continuityEye).Lenght();

    // Keep the Portal SDK's transformed eye history intact whenever it is
    // already on the exit side. L4D2 still needs a compatibility guard for a
    // candidate that would place the view behind the portal's BSP plane.
    if (!std::isfinite(candidateExitDepth))
    {
        m_continuityEye = originalViewEye;
        m_overrideOriginalView = false;
        m_eyePositionIsInterpolating = false;
        result.safetyFallbackToOriginal = true;
        result.releasedOverride = true;
        result.eyeOrigin = originalViewEye;
        result.outputExitDepth = result.originalExitDepth;
        result.rawResidual = (rawPhysicalEye - originalViewEye).Lenght();
        result.originalViewResidual = 0.0f;
        finishTrace();
        return result;
    }

    if (candidateExitDepth < -kExitPlaneTolerance)
    {
        result.normalCorrection = result.originalExitDepth - candidateExitDepth;
        m_continuityEye += m_activeExitPlaneNormal * result.normalCorrection;
        result.planeConstrained = true;
    }

    if (m_eyePositionIsInterpolating)
    {
        Vector difference = originalViewEye - m_continuityEye;
        if (result.planeConstrained)
        {
            difference -= m_activeExitPlaneNormal
                * difference.Dot(m_activeExitPlaneNormal);
        }
        const float length = difference.Lenght();
        float followDistance = std::max(0.0f, frameTime) * kOfficialFollowUnitsPerSecond;

        if (length > kOfficialMaximumLag)
        {
            followDistance = length - kOfficialMaximumLag;
        }
        else if (length < followDistance)
        {
            m_eyePositionIsInterpolating = false;
            m_continuityEye = originalViewEye;
        }
        else if (length > kMinimumVectorLength)
        {
            difference *= followDistance / length;
            m_continuityEye += difference;
        }
        else
        {
            m_eyePositionIsInterpolating = false;
            m_continuityEye = originalViewEye;
        }
    }

    result.rawResidual = (rawPhysicalEye - m_continuityEye).Lenght();
    result.originalViewResidual = (originalViewEye - m_continuityEye).Lenght();
    result.activeExitPlaneNormal = m_activeExitPlaneNormal;
    result.outputExitDepth =
        (m_continuityEye - m_activeExitPlaneOrigin).Dot(m_activeExitPlaneNormal);

    // L4D2 keeps its own view smoothing alive behind this adapter. Hand camera
    // ownership back only after that original view reaches the corrected eye.
    if (!result.consumedPendingTeleport
        && result.originalViewResidual <= kEngineCatchUpTolerance)
    {
        m_overrideOriginalView = false;
        m_eyePositionIsInterpolating = false;
        m_continuityEye = originalViewEye;
        result.eyeOrigin = originalViewEye;
        result.releasedOverride = true;
        finishTrace();
        return result;
    }

    result.eyeOrigin = m_continuityEye;
    result.overrideOriginalView = true;
    finishTrace();
    return result;
}

Vector PortalEyeContinuity::State::TransformPoint(
    const matrix3x4_t& matrix,
    const Vector& point) const
{
    return Vector(
        matrix[0][0] * point.x + matrix[0][1] * point.y + matrix[0][2] * point.z + matrix[0][3],
        matrix[1][0] * point.x + matrix[1][1] * point.y + matrix[1][2] * point.z + matrix[1][3],
        matrix[2][0] * point.x + matrix[2][1] * point.y + matrix[2][2] * point.z + matrix[2][3]);
}
