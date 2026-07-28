#include "PortalVisualContinuityDiagnostics.h"

#include "../Util/Logger/PortalFileLog.h"

namespace
{
    const char* BoolText(bool value)
    {
        return value ? "true" : "false";
    }
}

void PortalVisualContinuity::Diagnostics::ResetCapture(const char* reason)
{
    const AbortResult abort = m_tracker.ResetCapture();
    if (!abort.aborted)
        return;

    U::PortalFileLog::WriteFormat(
        "[PortalVisualTxn] txn=%u stage=aborted reason=%s samples=%d mainViewSamples=%d.\n",
        abort.transactionId,
        reason ? reason : "reset",
        abort.sampleCount,
        abort.mainViewSampleCount);
}

void PortalVisualContinuity::Diagnostics::CaptureEntryHandoff(
    int entrySide,
    std::uint32_t traversalEpoch,
    const Sample& sample)
{
    m_tracker.CacheEntryHandoff(entrySide, traversalEpoch, sample);
}

std::uint32_t PortalVisualContinuity::Diagnostics::BeginCommittedTeleport(
    int entrySide,
    std::uint32_t traversalEpoch,
    int commandNumber,
    const Sample& commitSample,
    const TeleportCommitDetails& details)
{
    const BeginResult begin = m_tracker.BeginTransaction(
        entrySide,
        traversalEpoch,
        commandNumber,
        commitSample);
    if (!begin.accepted)
        return 0;

    if (begin.supersededTransactionId != 0)
    {
        U::PortalFileLog::WriteFormat(
            "[PortalVisualTxn] txn=%u stage=aborted reason=superseded-by-new-commit samples=%d mainViewSamples=%d nextTxn=%u.\n",
            begin.supersededTransactionId,
            begin.supersededSampleCount,
            begin.supersededMainViewSampleCount,
            begin.transactionId);
    }

    U::PortalFileLog::WriteFormat(
        "[PortalVisualTxn] txn=%u stage=begin entry=%s exit=%s epoch=%u cmd=%d time=%.6f frame=%d mainViewBudget=%d handoffBinding=%s.\n",
        begin.transactionId,
        details.entryName ? details.entryName : "None",
        details.exitName ? details.exitName : "None",
        traversalEpoch,
        commandNumber,
        commitSample.engineTime,
        commitSample.frameCount,
        Tracker::kMainViewSampleCount,
        HandoffBindingName(begin.handoffBinding));

    if (begin.hasEntryHandoff)
        LogSample(begin.transactionId, 0, begin.entryHandoff);
    else
    {
        U::PortalFileLog::WriteFormat(
            "[PortalVisualTxn] txn=%u stage=entry-handoff-last present=false binding=%s.\n",
            begin.transactionId,
            HandoffBindingName(begin.handoffBinding));
    }
    LogAppend(begin.commit);

    U::PortalFileLog::WriteFormat(
        "[PortalVisualTxn] txn=%u stage=teleport-detail cmd=%d oldOrigin=(%.3f %.3f %.3f) newOrigin=(%.3f %.3f %.3f) oldEye=(%.3f %.3f %.3f) newEye=(%.3f %.3f %.3f) velocity=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) angles=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) fovComparable=false fovReason=no-main-view-sample-at-server-commit.\n",
        begin.transactionId,
        commandNumber,
        details.oldOrigin.x, details.oldOrigin.y, details.oldOrigin.z,
        details.newOrigin.x, details.newOrigin.y, details.newOrigin.z,
        details.oldEye.x, details.oldEye.y, details.oldEye.z,
        details.newEye.x, details.newEye.y, details.newEye.z,
        details.oldVelocity.x, details.oldVelocity.y, details.oldVelocity.z,
        details.newVelocity.x, details.newVelocity.y, details.newVelocity.z,
        details.oldAngles.x, details.oldAngles.y, details.oldAngles.z,
        details.newAngles.x, details.newAngles.y, details.newAngles.z);
    return begin.transactionId;
}

std::uint32_t PortalVisualContinuity::Diagnostics::CapturePredictionBoundary(
    int commandNumber,
    const Sample& sample,
    const PredictionBoundaryDetails& details)
{
    const AppendResult result =
        m_tracker.RecordPredictionSync(commandNumber, sample);
    if (!result.accepted)
        return 0;

    LogAppend(result);
    U::PortalFileLog::WriteFormat(
        "[PortalVisualTxn] txn=%u stage=prediction-detail cmd=%d syncApplied=%s eyeObserved=false expectedOrigin=(%.3f %.3f %.3f) moveOrigin=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) expectedVelocity=(%.3f %.3f %.3f) moveVelocity=(%.3f %.3f %.3f)->(%.3f %.3f %.3f) expectedAngles=(%.3f %.3f %.3f) cmdAngles=(%.3f %.3f %.3f)->(%.3f %.3f %.3f).\n",
        result.transactionId,
        commandNumber,
        BoolText(details.syncApplied),
        details.expectedOrigin.x, details.expectedOrigin.y, details.expectedOrigin.z,
        details.beforeOrigin.x, details.beforeOrigin.y, details.beforeOrigin.z,
        details.afterOrigin.x, details.afterOrigin.y, details.afterOrigin.z,
        details.expectedVelocity.x, details.expectedVelocity.y, details.expectedVelocity.z,
        details.beforeVelocity.x, details.beforeVelocity.y, details.beforeVelocity.z,
        details.afterVelocity.x, details.afterVelocity.y, details.afterVelocity.z,
        details.expectedAngles.x, details.expectedAngles.y, details.expectedAngles.z,
        details.beforeCommandAngles.x, details.beforeCommandAngles.y, details.beforeCommandAngles.z,
        details.afterCommandAngles.x, details.afterCommandAngles.y, details.afterCommandAngles.z);
    return result.transactionId;
}

std::uint32_t PortalVisualContinuity::Diagnostics::CaptureMainViewPreRender(
    const Sample& sample)
{
    const AppendResult result =
        m_tracker.RecordMainViewPreRender(sample);
    if (!result.accepted)
        return 0;

    LogAppend(result);
    if (result.completed)
    {
        U::PortalFileLog::WriteFormat(
            "[PortalVisualTxn] txn=%u stage=summary samples=%d mainViewSamples=%d eyePositionComparable=%s maxEyePositionDelta=%.4f maxEyePositionPair=%s[%d]->%s[%d] eyeAngleComparable=%s maxEyeAngleDelta=%.4f maxEyeAnglePair=%s[%d]->%s[%d].\n",
            result.transactionId,
            result.ordinal + 1,
            Tracker::kMainViewSampleCount,
            BoolText(result.maximum.hasEyePosition),
            result.maximum.eyePosition,
            StageName(result.maximum.eyePositionFromStage),
            result.maximum.eyePositionFromOrdinal,
            StageName(result.maximum.eyePositionToStage),
            result.maximum.eyePositionToOrdinal,
            BoolText(result.maximum.hasEyeAngle),
            result.maximum.eyeAngle,
            StageName(result.maximum.eyeAngleFromStage),
            result.maximum.eyeAngleFromOrdinal,
            StageName(result.maximum.eyeAngleToStage),
            result.maximum.eyeAngleToOrdinal);
    }
    return result.transactionId;
}

void PortalVisualContinuity::Diagnostics::LogSample(
    std::uint32_t transactionId,
    int ordinal,
    const Sample& sample) const
{
    U::PortalFileLog::WriteFormat(
        "[PortalVisualTxn] txn=%u stage=%s ordinal=%d renderIndex=%d epoch=%u cmd=%d time=%.6f frame=%d transitionPhase=%d hasEye=%s eye=(%.3f %.3f %.3f) eyeSemantics=%s hasAngles=%s eyeAngles=(%.3f %.3f %.3f) angleSemantics=%s hasPlayer=%s playerOrigin=(%.3f %.3f %.3f) playerSemantics=%s hasVelocity=%s velocity=(%.3f %.3f %.3f) velocitySemantics=%s hasFov=%s fov=%.3f.\n",
        transactionId,
        StageName(sample.stage),
        ordinal,
        sample.renderIndex,
        sample.traversalEpoch,
        sample.commandNumber,
        sample.engineTime,
        sample.frameCount,
        sample.transitionPhase,
        BoolText(sample.hasEyeOrigin),
        sample.eyeOrigin.x, sample.eyeOrigin.y, sample.eyeOrigin.z,
        SemanticsName(sample.eyeSemantics),
        BoolText(sample.hasEyeAngles),
        sample.eyeAngles.x, sample.eyeAngles.y, sample.eyeAngles.z,
        SemanticsName(sample.angleSemantics),
        BoolText(sample.hasPlayerOrigin),
        sample.playerOrigin.x, sample.playerOrigin.y, sample.playerOrigin.z,
        SemanticsName(sample.playerSemantics),
        BoolText(sample.hasVelocity),
        sample.velocity.x, sample.velocity.y, sample.velocity.z,
        SemanticsName(sample.velocitySemantics),
        BoolText(sample.hasFov),
        sample.fov);
}

void PortalVisualContinuity::Diagnostics::LogAppend(
    const AppendResult& result) const
{
    LogSample(result.transactionId, result.ordinal, result.current);
    if (!result.hasPrevious)
        return;

    U::PortalFileLog::WriteFormat(
        "[PortalVisualTxn] txn=%u stage=delta to=%s[%d] hasEyePosition=%s eyePosition=%.4f eyePositionFrom=%s[%d] hasEyeAngle=%s eyeAngle=%.4f eyeAngleFrom=%s[%d] hasPlayerPosition=%s playerPosition=%.4f playerPositionFrom=%s[%d] hasVelocity=%s velocity=%.4f velocityFrom=%s[%d] hasFov=%s fov=%.4f fovFrom=%s[%d].\n",
        result.transactionId,
        StageName(result.current.stage),
        result.ordinal,
        BoolText(result.delta.hasEyePosition),
        result.delta.eyePosition,
        StageName(result.delta.eyePositionFromStage),
        result.delta.eyePositionFromOrdinal,
        BoolText(result.delta.hasEyeAngle),
        result.delta.eyeAngle,
        StageName(result.delta.eyeAngleFromStage),
        result.delta.eyeAngleFromOrdinal,
        BoolText(result.delta.hasPlayerPosition),
        result.delta.playerPosition,
        StageName(result.delta.playerPositionFromStage),
        result.delta.playerPositionFromOrdinal,
        BoolText(result.delta.hasVelocity),
        result.delta.velocity,
        StageName(result.delta.velocityFromStage),
        result.delta.velocityFromOrdinal,
        BoolText(result.delta.hasFov),
        result.delta.fov,
        StageName(result.delta.fovFromStage),
        result.delta.fovFromOrdinal);
}
