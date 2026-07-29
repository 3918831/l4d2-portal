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

    bool IsFiniteVector(const Vector& value)
    {
        return std::isfinite(value.x)
            && std::isfinite(value.y)
            && std::isfinite(value.z);
    }
}

void PortalEyeContinuity::State::Reset()
{
    m_previousRawEye = Vector();
    m_previousOriginalViewEye = Vector();
    m_continuityEye = Vector();
    m_pendingMatrix = matrix3x4_t();
    m_pendingExitPlaneOrigin = Vector();
    m_pendingExitPlaneNormal = Vector();
    m_activeExitPlaneOrigin = Vector();
    m_activeExitPlaneNormal = Vector();
    m_pendingTraversalEpoch = 0;
    m_lastPublishedTraversalEpoch = 0;
    m_pendingCommandNumber = 0;
    m_activeCommandNumber = 0;
    m_initialized = false;
    m_hasPendingTeleport = false;
    m_eyePositionIsInterpolating = false;
    m_overrideOriginalView = false;
}

bool PortalEyeContinuity::State::PublishTeleport(
    const matrix3x4_t& entryToExit,
    const Vector& exitPlaneOrigin,
    const Vector& exitPlaneNormal,
    std::uint32_t traversalEpoch,
    int commandNumber)
{
    Vector normalizedExitNormal = exitPlaneNormal;
    const float exitNormalLength = normalizedExitNormal.Lenght();
    if (!m_initialized
        || traversalEpoch == 0
        || commandNumber <= 0
        || traversalEpoch == m_lastPublishedTraversalEpoch
        || !IsFiniteVector(exitPlaneOrigin)
        || !IsFiniteVector(exitPlaneNormal)
        || exitNormalLength <= kMinimumVectorLength)
    {
        return false;
    }
    normalizedExitNormal *= 1.0f / exitNormalLength;

    m_pendingMatrix = entryToExit;
    m_pendingExitPlaneOrigin = exitPlaneOrigin;
    m_pendingExitPlaneNormal = normalizedExitNormal;
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
    result.initializedBefore = m_initialized;
    result.hadPendingTeleport = m_hasPendingTeleport;
    result.interpolatingBefore = m_eyePositionIsInterpolating;
    result.overridingBefore = m_overrideOriginalView;

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
        m_eyePositionIsInterpolating = true;
        m_overrideOriginalView = true;
        result.consumedPendingTeleport = true;
    }

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
    // physical eye. On the consumed frame, keep the transformed tangential/up
    // residual but align the camera's portal-normal coordinate immediately with
    // the engine view. This prevents interpolation from holding the camera in
    // the exit wall's BSP leaf while preserving a small visual residual.
    result.freeMovementDelta = rawPhysicalEye - m_previousRawEye;
    result.engineViewMovementDelta =
        originalViewEye - m_previousOriginalViewEye;
    if (!result.consumedPendingTeleport)
        m_continuityEye += result.engineViewMovementDelta;
    m_previousRawEye = rawPhysicalEye;
    m_previousOriginalViewEye = originalViewEye;

    result.traversalEpoch = m_pendingTraversalEpoch;
    result.commandNumber = m_activeCommandNumber;
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
    result.normalCorrection = result.originalExitDepth - candidateExitDepth;
    m_continuityEye += m_activeExitPlaneNormal * result.normalCorrection;
    result.planeConstrained = true;

    if (m_eyePositionIsInterpolating)
    {
        Vector difference = originalViewEye - m_continuityEye;
        difference -= m_activeExitPlaneNormal
            * difference.Dot(m_activeExitPlaneNormal);
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
