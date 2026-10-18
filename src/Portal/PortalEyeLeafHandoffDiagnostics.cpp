#include "PortalEyeLeafHandoffDiagnostics.h"

#include <cmath>

namespace
{
    bool IsFinite(const PortalEyeLeafHandoffDiagnostics::Vec3& value)
    {
        return std::isfinite(value.x)
            && std::isfinite(value.y)
            && std::isfinite(value.z);
    }

    bool IsClearPoint(
        const PortalEyeLeafHandoffDiagnostics::PointSnapshot& snapshot)
    {
        return snapshot.available
            && IsFinite(snapshot.point)
            && snapshot.leaf > 0
            && !snapshot.outsideWorld
            && snapshot.playerSolidContents == 0;
    }

    bool IsFullClearTrace(
        const PortalEyeLeafHandoffDiagnostics::TraceSnapshot& trace)
    {
        return trace.available
            && std::isfinite(trace.fraction)
            && IsFinite(trace.endPosition)
            && !trace.startSolid
            && !trace.allSolid
            && trace.fraction >=
                1.0f - PortalEyeLeafHandoffDiagnostics::kFullTraceFractionTolerance
            && trace.fraction <= 1.0f;
    }
}

PortalEyeLeafHandoffDiagnostics::EvaluationResult
PortalEyeLeafHandoffDiagnostics::Evaluate(const EvaluationInput& input)
{
    EvaluationResult result;
    if (IsFinite(input.candidate.point) && IsFinite(input.original.point))
    {
        result.fallbackDelta = {
            input.original.point.x - input.candidate.point.x,
            input.original.point.y - input.candidate.point.y,
            input.original.point.z - input.candidate.point.z,
        };
        result.fallbackDeltaLength = std::sqrt(
            result.fallbackDelta.x * result.fallbackDelta.x
            + result.fallbackDelta.y * result.fallbackDelta.y
            + result.fallbackDelta.z * result.fallbackDelta.z);
        result.candidateOriginalDistance = result.fallbackDeltaLength;
    }

    result.geometryClear = IsClearPoint(input.candidate)
        && IsClearPoint(input.original)
        && input.candidate.leaf != input.original.leaf
        && IsFullClearTrace(input.originalToCandidate)
        && IsFullClearTrace(input.candidateToOriginal);
    return result;
}
