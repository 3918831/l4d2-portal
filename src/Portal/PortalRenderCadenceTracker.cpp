#include "PortalRenderCadenceTracker.h"

#include <algorithm>
#include <cmath>

void PortalRenderCadence::Tracker::Reset()
{
    m_previous = {};
    m_exitNormal = {};
    m_summary = {};
    m_lastFrameCount = -1;
    m_rawConsecutiveFrozenFrames = 0;
    m_originalConsecutiveFrozenFrames = 0;
    m_appliedConsecutiveFrozenFrames = 0;
    m_calcViewInputConsecutiveFrozenFrames = 0;
    m_finalViewConsecutiveFrozenFrames = 0;
    m_originConsecutiveFrozenFrames = 0;
    m_renderOriginConsecutiveFrozenFrames = 0;
    m_networkOriginConsecutiveFrozenFrames = 0;
    m_oldOriginConsecutiveFrozenFrames = 0;
    m_simulationTimeConsecutiveFrozenFrames = 0;
    m_finalViewAnglesConsecutiveFrozenFrames = 0;
    m_finalViewConsecutiveUnderSpeedFrames = 0;
    m_seenEyeHistoryActive = false;
    m_postEyeHistory = false;
    m_previousFinalViewUnderSpeed = false;
    m_hasPrevious = false;
    m_active = false;
    m_captureCommandTransitions = kCaptureCommandTransitions;
    m_maximumFrames = kMaximumFrames;
}

PortalRenderCadence::BeginResult PortalRenderCadence::Tracker::BeginTransaction(
    std::uint32_t transactionId,
    int committedCommandNumber,
    const Vec3& exitNormal,
    int captureCommandTransitions,
    int maximumFrames)
{
    BeginResult result;
    if (transactionId == 0
        || committedCommandNumber <= 0
        || captureCommandTransitions <= 0
        || maximumFrames <= 0)
        return result;

    const float normalLength = Length(exitNormal);
    if (!std::isfinite(normalLength) || normalLength <= kFreezeEpsilon)
        return result;

    if (m_active)
    {
        result.superseded = true;
        result.supersededSummary = m_summary;
    }

    Reset();
    m_exitNormal = {
        exitNormal.x / normalLength,
        exitNormal.y / normalLength,
        exitNormal.z / normalLength
    };
    m_summary.transactionId = transactionId;
    m_summary.committedCommandNumber = committedCommandNumber;
    m_captureCommandTransitions = captureCommandTransitions;
    m_maximumFrames = maximumFrames;
    m_active = true;
    result.accepted = true;
    result.transactionId = transactionId;
    return result;
}

PortalRenderCadence::RecordResult PortalRenderCadence::Tracker::RecordFrame(
    const Sample& sample)
{
    RecordResult result;
    if (!m_active || sample.frameCount < 0 || sample.frameCount == m_lastFrameCount)
        return result;

    result.accepted = true;
    result.transactionId = m_summary.transactionId;
    result.ordinal = m_summary.frameCount;
    result.current = sample;
    result.hasPrevious = m_hasPrevious;

    if (m_hasPrevious)
    {
        result.previous = m_previous;
        result.commandAdvanced = sample.commandNumber > 0
            && m_previous.commandNumber > 0
            && sample.commandNumber != m_previous.commandNumber;
        result.tickAdvanced = sample.tickCount >= 0
            && m_previous.tickCount >= 0
            && sample.tickCount != m_previous.tickCount;
        result.eyeHistoryChanged =
            sample.eyeHistoryActive != m_previous.eyeHistoryActive
            || sample.eyeHistoryReleased;
        result.shadowPredictorChanged =
            sample.shadowPredictorApplied != m_previous.shadowPredictorApplied;
        if (result.commandAdvanced)
            ++m_summary.commandTransitions;
        if (result.tickAdvanced)
            ++m_summary.tickTransitions;

        const Vec3 rawDelta = Delta(sample.rawEye, m_previous.rawEye);
        const Vec3 originalDelta = Delta(sample.originalEye, m_previous.originalEye);
        const Vec3 appliedDelta = Delta(sample.appliedEye, m_previous.appliedEye);
        const Vec3 calcViewInputDelta =
            Delta(sample.calcViewInputEye, m_previous.calcViewInputEye);
        const Vec3 finalViewDelta =
            Delta(sample.finalViewEye, m_previous.finalViewEye);
        const Vec3 originDelta = Delta(sample.playerOrigin, m_previous.playerOrigin);
        const Vec3 renderOriginDelta =
            Delta(sample.renderOrigin, m_previous.renderOrigin);
        const Vec3 networkOriginDelta =
            Delta(sample.networkOrigin, m_previous.networkOrigin);
        const Vec3 oldOriginDelta = Delta(sample.oldOrigin, m_previous.oldOrigin);
        result.rawDeltaLength = Length(rawDelta);
        result.originalDeltaLength = Length(originalDelta);
        result.appliedDeltaLength = Length(appliedDelta);
        result.calcViewInputDeltaLength = Length(calcViewInputDelta);
        result.finalViewDeltaLength = Length(finalViewDelta);
        result.originDeltaLength = Length(originDelta);
        result.renderOriginDeltaLength = Length(renderOriginDelta);
        result.networkOriginDeltaLength = Length(networkOriginDelta);
        result.oldOriginDeltaLength = Length(oldOriginDelta);
        result.simulationTimeDelta =
            sample.simulationTime - m_previous.simulationTime;
        result.finalViewAngleDeltaLength =
            AngleDeltaLength(sample.finalViewAngles, m_previous.finalViewAngles);
        result.appliedExitNormalDelta = Dot(appliedDelta, m_exitNormal);
        const float tangentLengthSqr = std::max(
            0.0f,
            result.appliedDeltaLength * result.appliedDeltaLength
                - result.appliedExitNormalDelta * result.appliedExitNormalDelta);
        result.appliedExitTangentDelta = std::sqrt(tangentLengthSqr);

        result.expectedFrameDelta = {
            sample.velocity.x * sample.frameTime,
            sample.velocity.y * sample.frameTime,
            sample.velocity.z * sample.frameTime
        };
        result.expectedFrameDeltaLength = Length(result.expectedFrameDelta);
        if (result.expectedFrameDeltaLength > kFreezeEpsilon)
        {
            const Vec3 velocityDirection = {
                result.expectedFrameDelta.x / result.expectedFrameDeltaLength,
                result.expectedFrameDelta.y / result.expectedFrameDeltaLength,
                result.expectedFrameDelta.z / result.expectedFrameDeltaLength
            };
            result.appliedVelocityProjectedDelta =
                Dot(appliedDelta, velocityDirection);
            result.finalViewVelocityProjectedDelta =
                Dot(finalViewDelta, velocityDirection);
            result.appliedSpeedRatio = result.appliedVelocityProjectedDelta
                / result.expectedFrameDeltaLength;
            result.finalViewSpeedRatio = result.finalViewVelocityProjectedDelta
                / result.expectedFrameDeltaLength;
            result.finalViewUnderSpeed =
                result.finalViewSpeedRatio < kUnderSpeedRatio;
            result.finalViewUnderSpeedChanged =
                result.finalViewUnderSpeed != m_previousFinalViewUnderSpeed;

            ++m_summary.speedSampleFrames;
            const float sampleCount =
                static_cast<float>(m_summary.speedSampleFrames);
            if (m_summary.speedSampleFrames == 1)
            {
                m_summary.minFinalViewSpeedRatio = result.finalViewSpeedRatio;
                m_summary.maxFinalViewSpeedRatio = result.finalViewSpeedRatio;
                m_summary.minAppliedSpeedRatio = result.appliedSpeedRatio;
                m_summary.maxAppliedSpeedRatio = result.appliedSpeedRatio;
            }
            else
            {
                m_summary.minFinalViewSpeedRatio = std::min(
                    m_summary.minFinalViewSpeedRatio,
                    result.finalViewSpeedRatio);
                m_summary.maxFinalViewSpeedRatio = std::max(
                    m_summary.maxFinalViewSpeedRatio,
                    result.finalViewSpeedRatio);
                m_summary.minAppliedSpeedRatio = std::min(
                    m_summary.minAppliedSpeedRatio,
                    result.appliedSpeedRatio);
                m_summary.maxAppliedSpeedRatio = std::max(
                    m_summary.maxAppliedSpeedRatio,
                    result.appliedSpeedRatio);
            }
            m_summary.averageFinalViewSpeedRatio +=
                (result.finalViewSpeedRatio
                    - m_summary.averageFinalViewSpeedRatio) / sampleCount;
            m_summary.averageAppliedSpeedRatio +=
                (result.appliedSpeedRatio
                    - m_summary.averageAppliedSpeedRatio) / sampleCount;
        }

        if (m_previous.eyeHistoryActive)
            m_seenEyeHistoryActive = true;
        if (sample.eyeHistoryActive)
            m_seenEyeHistoryActive = true;
        if (sample.eyeHistoryReleased
            || (m_seenEyeHistoryActive && !sample.eyeHistoryActive))
        {
            m_postEyeHistory = true;
        }
        result.postEyeHistory = m_postEyeHistory && !sample.eyeHistoryActive;
        if (sample.eyeHistoryActive)
            ++m_summary.eyeHistoryActiveFrames;
        if (sample.shadowPredictorApplied)
            ++m_summary.shadowPredictorAppliedFrames;
        if (result.postEyeHistory)
            ++m_summary.postEyeHistoryFrames;
        if (result.expectedFrameDeltaLength > kFreezeEpsilon)
        {
            m_finalViewConsecutiveUnderSpeedFrames = result.finalViewUnderSpeed
                ? m_finalViewConsecutiveUnderSpeedFrames + 1
                : 0;
            if (result.finalViewUnderSpeed)
            {
                ++m_summary.finalViewUnderSpeedFrames;
                if (sample.eyeHistoryActive)
                    ++m_summary.eyeHistoryUnderSpeedFrames;
                if (result.postEyeHistory)
                    ++m_summary.postEyeHistoryUnderSpeedFrames;
            }
            m_summary.maxFinalViewUnderSpeedRun = std::max(
                m_summary.maxFinalViewUnderSpeedRun,
                m_finalViewConsecutiveUnderSpeedFrames);
            m_previousFinalViewUnderSpeed = result.finalViewUnderSpeed;
        }

        result.rawFrozen = result.rawDeltaLength <= kFreezeEpsilon;
        result.originalFrozen = result.originalDeltaLength <= kFreezeEpsilon;
        result.appliedFrozen = result.appliedDeltaLength <= kFreezeEpsilon;
        result.calcViewInputFrozen =
            result.calcViewInputDeltaLength <= kFreezeEpsilon;
        result.finalViewFrozen = result.finalViewDeltaLength <= kFreezeEpsilon;
        result.originFrozen = result.originDeltaLength <= kFreezeEpsilon;
        result.renderOriginFrozen =
            result.renderOriginDeltaLength <= kFreezeEpsilon;
        result.networkOriginFrozen =
            result.networkOriginDeltaLength <= kFreezeEpsilon;
        result.oldOriginFrozen = result.oldOriginDeltaLength <= kFreezeEpsilon;
        result.simulationTimeFrozen =
            std::fabs(result.simulationTimeDelta) <= kFreezeEpsilon;
        result.finalViewAnglesFrozen =
            result.finalViewAngleDeltaLength <= kFreezeEpsilon;

        m_rawConsecutiveFrozenFrames = result.rawFrozen
            ? m_rawConsecutiveFrozenFrames + 1
            : 0;
        m_originalConsecutiveFrozenFrames = result.originalFrozen
            ? m_originalConsecutiveFrozenFrames + 1
            : 0;
        m_appliedConsecutiveFrozenFrames = result.appliedFrozen
            ? m_appliedConsecutiveFrozenFrames + 1
            : 0;
        m_calcViewInputConsecutiveFrozenFrames = result.calcViewInputFrozen
            ? m_calcViewInputConsecutiveFrozenFrames + 1
            : 0;
        m_finalViewConsecutiveFrozenFrames = result.finalViewFrozen
            ? m_finalViewConsecutiveFrozenFrames + 1
            : 0;
        m_originConsecutiveFrozenFrames = result.originFrozen
            ? m_originConsecutiveFrozenFrames + 1
            : 0;
        m_renderOriginConsecutiveFrozenFrames = result.renderOriginFrozen
            ? m_renderOriginConsecutiveFrozenFrames + 1
            : 0;
        m_networkOriginConsecutiveFrozenFrames = result.networkOriginFrozen
            ? m_networkOriginConsecutiveFrozenFrames + 1
            : 0;
        m_oldOriginConsecutiveFrozenFrames = result.oldOriginFrozen
            ? m_oldOriginConsecutiveFrozenFrames + 1
            : 0;
        m_simulationTimeConsecutiveFrozenFrames = result.simulationTimeFrozen
            ? m_simulationTimeConsecutiveFrozenFrames + 1
            : 0;
        m_finalViewAnglesConsecutiveFrozenFrames = result.finalViewAnglesFrozen
            ? m_finalViewAnglesConsecutiveFrozenFrames + 1
            : 0;
        result.rawConsecutiveFrozenFrames = m_rawConsecutiveFrozenFrames;
        result.originalConsecutiveFrozenFrames = m_originalConsecutiveFrozenFrames;
        result.appliedConsecutiveFrozenFrames = m_appliedConsecutiveFrozenFrames;
        result.calcViewInputConsecutiveFrozenFrames =
            m_calcViewInputConsecutiveFrozenFrames;
        result.finalViewConsecutiveFrozenFrames =
            m_finalViewConsecutiveFrozenFrames;
        result.originConsecutiveFrozenFrames = m_originConsecutiveFrozenFrames;
        result.renderOriginConsecutiveFrozenFrames =
            m_renderOriginConsecutiveFrozenFrames;
        result.networkOriginConsecutiveFrozenFrames =
            m_networkOriginConsecutiveFrozenFrames;
        result.oldOriginConsecutiveFrozenFrames =
            m_oldOriginConsecutiveFrozenFrames;
        result.simulationTimeConsecutiveFrozenFrames =
            m_simulationTimeConsecutiveFrozenFrames;
        result.finalViewAnglesConsecutiveFrozenFrames =
            m_finalViewAnglesConsecutiveFrozenFrames;

        if (result.rawFrozen)
            ++m_summary.rawFrozenFrames;
        if (result.originalFrozen)
            ++m_summary.originalFrozenFrames;
        if (result.appliedFrozen)
            ++m_summary.appliedFrozenFrames;
        if (result.calcViewInputFrozen)
            ++m_summary.calcViewInputFrozenFrames;
        if (result.finalViewFrozen)
            ++m_summary.finalViewFrozenFrames;
        if (result.originFrozen)
            ++m_summary.originFrozenFrames;
        if (result.renderOriginFrozen)
            ++m_summary.renderOriginFrozenFrames;
        if (result.networkOriginFrozen)
            ++m_summary.networkOriginFrozenFrames;
        if (result.oldOriginFrozen)
            ++m_summary.oldOriginFrozenFrames;
        if (result.simulationTimeFrozen)
            ++m_summary.simulationTimeFrozenFrames;
        if (result.finalViewAnglesFrozen)
            ++m_summary.finalViewAnglesFrozenFrames;
        m_summary.maxRawConsecutiveFrozenFrames = std::max(
            m_summary.maxRawConsecutiveFrozenFrames,
            m_rawConsecutiveFrozenFrames);
        m_summary.maxOriginalConsecutiveFrozenFrames = std::max(
            m_summary.maxOriginalConsecutiveFrozenFrames,
            m_originalConsecutiveFrozenFrames);
        m_summary.maxAppliedConsecutiveFrozenFrames = std::max(
            m_summary.maxAppliedConsecutiveFrozenFrames,
            m_appliedConsecutiveFrozenFrames);
        m_summary.maxCalcViewInputConsecutiveFrozenFrames = std::max(
            m_summary.maxCalcViewInputConsecutiveFrozenFrames,
            m_calcViewInputConsecutiveFrozenFrames);
        m_summary.maxFinalViewConsecutiveFrozenFrames = std::max(
            m_summary.maxFinalViewConsecutiveFrozenFrames,
            m_finalViewConsecutiveFrozenFrames);
        m_summary.maxOriginConsecutiveFrozenFrames = std::max(
            m_summary.maxOriginConsecutiveFrozenFrames,
            m_originConsecutiveFrozenFrames);
        m_summary.maxRenderOriginConsecutiveFrozenFrames = std::max(
            m_summary.maxRenderOriginConsecutiveFrozenFrames,
            m_renderOriginConsecutiveFrozenFrames);
        m_summary.maxNetworkOriginConsecutiveFrozenFrames = std::max(
            m_summary.maxNetworkOriginConsecutiveFrozenFrames,
            m_networkOriginConsecutiveFrozenFrames);
        m_summary.maxOldOriginConsecutiveFrozenFrames = std::max(
            m_summary.maxOldOriginConsecutiveFrozenFrames,
            m_oldOriginConsecutiveFrozenFrames);
        m_summary.maxSimulationTimeConsecutiveFrozenFrames = std::max(
            m_summary.maxSimulationTimeConsecutiveFrozenFrames,
            m_simulationTimeConsecutiveFrozenFrames);
        m_summary.maxFinalViewAnglesConsecutiveFrozenFrames = std::max(
            m_summary.maxFinalViewAnglesConsecutiveFrozenFrames,
            m_finalViewAnglesConsecutiveFrozenFrames);
        m_summary.maxRawDeltaLength = std::max(
            m_summary.maxRawDeltaLength,
            result.rawDeltaLength);
        m_summary.maxOriginalDeltaLength = std::max(
            m_summary.maxOriginalDeltaLength,
            result.originalDeltaLength);
        m_summary.maxAppliedDeltaLength = std::max(
            m_summary.maxAppliedDeltaLength,
            result.appliedDeltaLength);
        m_summary.maxCalcViewInputDeltaLength = std::max(
            m_summary.maxCalcViewInputDeltaLength,
            result.calcViewInputDeltaLength);
        m_summary.maxFinalViewDeltaLength = std::max(
            m_summary.maxFinalViewDeltaLength,
            result.finalViewDeltaLength);
        m_summary.maxOriginDeltaLength = std::max(
            m_summary.maxOriginDeltaLength,
            result.originDeltaLength);
        m_summary.maxRenderOriginDeltaLength = std::max(
            m_summary.maxRenderOriginDeltaLength,
            result.renderOriginDeltaLength);
        m_summary.maxNetworkOriginDeltaLength = std::max(
            m_summary.maxNetworkOriginDeltaLength,
            result.networkOriginDeltaLength);
        m_summary.maxOldOriginDeltaLength = std::max(
            m_summary.maxOldOriginDeltaLength,
            result.oldOriginDeltaLength);
        m_summary.maxSimulationTimeDelta = std::max(
            m_summary.maxSimulationTimeDelta,
            std::fabs(result.simulationTimeDelta));
        m_summary.maxFinalViewAngleDeltaLength = std::max(
            m_summary.maxFinalViewAngleDeltaLength,
            result.finalViewAngleDeltaLength);
    }

    m_previous = sample;
    m_lastFrameCount = sample.frameCount;
    m_hasPrevious = true;
    ++m_summary.frameCount;
    result.summary = m_summary;

    const int completedTransitions = std::max(
        m_summary.commandTransitions,
        m_summary.tickTransitions);
    if (completedTransitions >= m_captureCommandTransitions
        || m_summary.frameCount >= m_maximumFrames)
    {
        result.completed = true;
        m_active = false;
    }
    return result;
}

PortalRenderCadence::Vec3 PortalRenderCadence::Tracker::Delta(
    const Vec3& to,
    const Vec3& from)
{
    return { to.x - from.x, to.y - from.y, to.z - from.z };
}

float PortalRenderCadence::Tracker::Dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float PortalRenderCadence::Tracker::Length(const Vec3& value)
{
    return std::sqrt(Dot(value, value));
}

float PortalRenderCadence::Tracker::AngleDeltaLength(
    const Vec3& to,
    const Vec3& from)
{
    const auto normalizedDelta = [](float value)
    {
        while (value > 180.0f)
            value -= 360.0f;
        while (value < -180.0f)
            value += 360.0f;
        return value;
    };
    const Vec3 delta = {
        normalizedDelta(to.x - from.x),
        normalizedDelta(to.y - from.y),
        normalizedDelta(to.z - from.z)
    };
    return Length(delta);
}
