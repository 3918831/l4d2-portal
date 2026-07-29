#include "PortalVisualContinuityTracker.h"

#include <cmath>

namespace
{
    using namespace PortalVisualContinuity;

    float Length(const Vec3& from, const Vec3& to)
    {
        const float x = to.x - from.x;
        const float y = to.y - from.y;
        const float z = to.z - from.z;
        return std::sqrt(x * x + y * y + z * z);
    }

    float WrappedAngleDelta(float from, float to)
    {
        float delta = std::fmod(to - from, 360.0f);
        if (delta > 180.0f)
            delta -= 360.0f;
        else if (delta < -180.0f)
            delta += 360.0f;
        return delta;
    }

    float AngleLength(const Vec3& from, const Vec3& to)
    {
        const float x = WrappedAngleDelta(from.x, to.x);
        const float y = WrappedAngleDelta(from.y, to.y);
        const float z = WrappedAngleDelta(from.z, to.z);
        return std::sqrt(x * x + y * y + z * z);
    }

    bool Compatible(
        SampleSemantics from,
        SampleSemantics to)
    {
        return from != SampleSemantics::None && from == to;
    }
}

PortalVisualContinuity::AbortResult
PortalVisualContinuity::Tracker::ResetCapture()
{
    AbortResult result;
    if (m_active)
    {
        result.aborted = true;
        result.transactionId = m_activeTransactionId;
        result.sampleCount = m_sampleCount;
        result.mainViewSampleCount = m_mainViewSampleCount;
    }

    m_sampleCount = 0;
    m_mainViewSampleCount = 0;
    m_lastMainViewFrame = -1;
    m_activeEntrySide = 0;
    m_activeCommandNumber = 0;
    m_activeTransactionId = 0;
    m_maximum = {};
    m_pendingEntryHandoff = {};
    m_pendingEntrySide = 0;
    m_pendingTraversalEpoch = 0;
    m_hasPendingEntryHandoff = false;
    m_active = false;
    m_predictionRecorded = false;
    return result;
}

void PortalVisualContinuity::Tracker::CacheEntryHandoff(
    int entrySide,
    std::uint32_t traversalEpoch,
    const Sample& sample)
{
    m_pendingEntryHandoff = sample;
    m_pendingEntryHandoff.stage = Stage::EntryHandoff;
    m_pendingEntryHandoff.traversalEpoch = traversalEpoch;
    m_pendingEntrySide = entrySide;
    m_pendingTraversalEpoch = traversalEpoch;
    m_hasPendingEntryHandoff = entrySide != 0 && traversalEpoch != 0;
}

PortalVisualContinuity::BeginResult
PortalVisualContinuity::Tracker::BeginTransaction(
    int entrySide,
    std::uint32_t traversalEpoch,
    int commandNumber,
    const Sample& commitSample)
{
    BeginResult result;
    if (m_active)
    {
        result.supersededTransactionId = m_activeTransactionId;
        result.supersededSampleCount = m_sampleCount;
        result.supersededMainViewSampleCount = m_mainViewSampleCount;
    }

    m_sampleCount = 0;
    m_mainViewSampleCount = 0;
    m_lastMainViewFrame = -1;
    m_activeEntrySide = entrySide;
    m_activeCommandNumber = commandNumber;
    m_activeTransactionId = m_nextTransactionId++;
    m_maximum = {};
    m_active = true;
    m_predictionRecorded = false;

    result.accepted = true;
    result.transactionId = m_activeTransactionId;
    result.handoffBinding = HandoffBinding::Missing;

    if (m_hasPendingEntryHandoff)
    {
        if (m_pendingEntrySide != entrySide)
        {
            result.handoffBinding = HandoffBinding::SideMismatch;
        }
        else if (m_pendingTraversalEpoch != traversalEpoch
            || traversalEpoch == 0)
        {
            result.handoffBinding = HandoffBinding::EpochMismatch;
        }
        else
        {
            const int commandAge =
                commandNumber - m_pendingEntryHandoff.commandNumber;
            const bool commandComparable =
                commandNumber > 0
                && m_pendingEntryHandoff.commandNumber > 0;
            if (commandComparable && (commandAge < 0 || commandAge > 1))
            {
                result.handoffBinding = HandoffBinding::CommandMismatch;
            }
            else
            {
                const int frameAge =
                    commitSample.frameCount - m_pendingEntryHandoff.frameCount;
                const float timeAge =
                    commitSample.engineTime - m_pendingEntryHandoff.engineTime;
                const bool frameComparable =
                    commitSample.frameCount >= 0
                    && m_pendingEntryHandoff.frameCount >= 0;
                const bool timeComparable =
                    commitSample.engineTime > 0.0f
                    && m_pendingEntryHandoff.engineTime > 0.0f;
                const bool frameFresh =
                    frameComparable
                    && frameAge >= 0
                    && frameAge <= kMaxEntryHandoffAgeFrames;
                const bool timeFresh =
                    timeComparable
                    && timeAge >= 0.0f
                    && timeAge <= kMaxEntryHandoffAgeSeconds;

                if ((frameComparable && !frameFresh)
                    || (timeComparable && !timeFresh)
                    || (!frameComparable && !timeComparable))
                {
                    result.handoffBinding = HandoffBinding::Stale;
                }
                else
                {
                    result.handoffBinding = HandoffBinding::Matched;
                    result.hasEntryHandoff = true;
                    result.entryHandoff = m_pendingEntryHandoff;
                    Append(result.entryHandoff);
                }
            }
        }
    }

    m_pendingEntryHandoff = {};
    m_pendingEntrySide = 0;
    m_pendingTraversalEpoch = 0;
    m_hasPendingEntryHandoff = false;

    Sample commit = commitSample;
    commit.stage = Stage::TeleportCommit;
    commit.traversalEpoch = traversalEpoch;
    commit.commandNumber = commandNumber;
    result.commit = Append(commit);
    return result;
}

PortalVisualContinuity::AppendResult
PortalVisualContinuity::Tracker::RecordPredictionSync(
    int commandNumber,
    const Sample& sample)
{
    if (!m_active
        || m_predictionRecorded
        || commandNumber != m_activeCommandNumber)
    {
        return {};
    }

    Sample prediction = sample;
    prediction.stage = Stage::PredictionSync;
    prediction.commandNumber = commandNumber;
    m_predictionRecorded = true;
    return Append(prediction);
}

PortalVisualContinuity::AppendResult
PortalVisualContinuity::Tracker::RecordMainViewPreRender(
    const Sample& sample)
{
    if (!m_active
        || m_mainViewSampleCount >= kMainViewSampleCount
        || sample.frameCount < 0
        || sample.frameCount <= m_lastMainViewFrame)
    {
        return {};
    }

    Sample render = sample;
    render.stage = Stage::MainViewPreRender;
    render.commandNumber = m_activeCommandNumber;
    render.renderIndex = m_mainViewSampleCount++;
    m_lastMainViewFrame = sample.frameCount;

    AppendResult result = Append(render);
    if (m_mainViewSampleCount == kMainViewSampleCount)
    {
        result.completed = true;
        m_active = false;
    }
    return result;
}

PortalVisualContinuity::AppendResult
PortalVisualContinuity::Tracker::Append(const Sample& sample)
{
    if (!m_active || m_sampleCount >= kMaximumSamples)
        return {};

    AppendResult result;
    result.accepted = true;
    result.transactionId = m_activeTransactionId;
    result.ordinal = m_sampleCount;
    result.current = sample;

    if (m_sampleCount > 0)
    {
        result.hasPrevious = true;
        result.previous = m_samples[m_sampleCount - 1];
    }

    for (int index = m_sampleCount - 1; index >= 0; --index)
    {
        const Sample& candidate = m_samples[index];
        if (!result.delta.hasEyePosition
            && candidate.hasEyeOrigin
            && sample.hasEyeOrigin
            && Compatible(candidate.eyeSemantics, sample.eyeSemantics))
        {
            result.delta.hasEyePosition = true;
            result.delta.eyePosition = Length(candidate.eyeOrigin, sample.eyeOrigin);
            result.delta.eyePositionFromOrdinal = index;
            result.delta.eyePositionFromStage = candidate.stage;
        }
        if (!result.delta.hasEyeAngle
            && candidate.hasEyeAngles
            && sample.hasEyeAngles
            && Compatible(candidate.angleSemantics, sample.angleSemantics))
        {
            result.delta.hasEyeAngle = true;
            result.delta.eyeAngle = AngleLength(candidate.eyeAngles, sample.eyeAngles);
            result.delta.eyeAngleFromOrdinal = index;
            result.delta.eyeAngleFromStage = candidate.stage;
        }
        if (!result.delta.hasPlayerPosition
            && candidate.hasPlayerOrigin
            && sample.hasPlayerOrigin
            && Compatible(candidate.playerSemantics, sample.playerSemantics))
        {
            result.delta.hasPlayerPosition = true;
            result.delta.playerPosition =
                Length(candidate.playerOrigin, sample.playerOrigin);
            result.delta.playerPositionFromOrdinal = index;
            result.delta.playerPositionFromStage = candidate.stage;
        }
        if (!result.delta.hasVelocity
            && candidate.hasVelocity
            && sample.hasVelocity
            && Compatible(candidate.velocitySemantics, sample.velocitySemantics))
        {
            result.delta.hasVelocity = true;
            result.delta.velocity = Length(candidate.velocity, sample.velocity);
            result.delta.velocityFromOrdinal = index;
            result.delta.velocityFromStage = candidate.stage;
        }
        if (!result.delta.hasFov
            && candidate.hasFov
            && sample.hasFov)
        {
            result.delta.hasFov = true;
            result.delta.fov = std::fabs(sample.fov - candidate.fov);
            result.delta.fovFromOrdinal = index;
            result.delta.fovFromStage = candidate.stage;
        }
    }

    if (result.delta.hasEyePosition
        && (!m_maximum.hasEyePosition
            || result.delta.eyePosition > m_maximum.eyePosition))
    {
        m_maximum.hasEyePosition = true;
        m_maximum.eyePosition = result.delta.eyePosition;
        m_maximum.eyePositionFromOrdinal =
            result.delta.eyePositionFromOrdinal;
        m_maximum.eyePositionToOrdinal = m_sampleCount;
        m_maximum.eyePositionFromStage =
            result.delta.eyePositionFromStage;
        m_maximum.eyePositionToStage = sample.stage;
    }
    if (result.delta.hasEyeAngle
        && (!m_maximum.hasEyeAngle
            || result.delta.eyeAngle > m_maximum.eyeAngle))
    {
        m_maximum.hasEyeAngle = true;
        m_maximum.eyeAngle = result.delta.eyeAngle;
        m_maximum.eyeAngleFromOrdinal =
            result.delta.eyeAngleFromOrdinal;
        m_maximum.eyeAngleToOrdinal = m_sampleCount;
        m_maximum.eyeAngleFromStage =
            result.delta.eyeAngleFromStage;
        m_maximum.eyeAngleToStage = sample.stage;
    }
    if (sample.hasEyeOrigin && sample.hasPhysicalEye)
    {
        const float residual =
            Length(sample.eyeOrigin, sample.physicalEye);
        if (!m_maximum.hasViewToPhysicalEyeResidual
            || residual > m_maximum.viewToPhysicalEyeResidual)
        {
            m_maximum.hasViewToPhysicalEyeResidual = true;
            m_maximum.viewToPhysicalEyeResidual = residual;
            m_maximum.viewToPhysicalEyeResidualOrdinal = m_sampleCount;
            m_maximum.viewToPhysicalEyeResidualStage = sample.stage;
        }
    }

    m_samples[m_sampleCount++] = sample;
    result.maximum = m_maximum;
    return result;
}

const char* PortalVisualContinuity::StageName(Stage stage)
{
    switch (stage)
    {
    case Stage::EntryHandoff: return "entry-handoff-last";
    case Stage::TeleportCommit: return "teleport-commit";
    case Stage::PredictionSync: return "prediction-boundary";
    case Stage::MainViewPreRender: return "main-view-pre-render";
    case Stage::None:
    default:
        return "none";
    }
}

const char* PortalVisualContinuity::SemanticsName(
    SampleSemantics semantics)
{
    switch (semantics)
    {
    case SampleSemantics::ExitViewSpace: return "exit-view-space";
    case SampleSemantics::EntryPhysical: return "entry-physical";
    case SampleSemantics::ExitPhysical: return "exit-physical";
    case SampleSemantics::None:
    default:
        return "none";
    }
}

const char* PortalVisualContinuity::HandoffBindingName(
    HandoffBinding binding)
{
    switch (binding)
    {
    case HandoffBinding::Matched: return "matched";
    case HandoffBinding::SideMismatch: return "side-mismatch";
    case HandoffBinding::EpochMismatch: return "epoch-mismatch";
    case HandoffBinding::CommandMismatch: return "command-mismatch";
    case HandoffBinding::Stale: return "stale";
    case HandoffBinding::Missing:
    default:
        return "missing";
    }
}
