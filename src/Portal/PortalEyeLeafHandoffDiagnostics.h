#pragma once

namespace PortalEyeLeafHandoffDiagnostics
{
    inline constexpr float kFullTraceFractionTolerance = 0.001f;

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct PointSnapshot
    {
        Vec3 point;
        int leaf = -1;
        int worldContents = -1;
        int playerSolidContents = -1;
        bool outsideWorld = true;
        bool available = false;
    };

    struct TraceSnapshot
    {
        Vec3 endPosition;
        float fraction = 0.0f;
        bool startSolid = false;
        bool allSolid = false;
        bool available = false;
    };

    struct EvaluationInput
    {
        PointSnapshot candidate;
        PointSnapshot original;
        TraceSnapshot originalToCandidate;
        TraceSnapshot candidateToOriginal;
    };

    struct EvaluationResult
    {
        bool geometryClear = false;
        float candidateOriginalDistance = 0.0f;
        Vec3 fallbackDelta;
        float fallbackDeltaLength = 0.0f;
    };

    EvaluationResult Evaluate(const EvaluationInput& input);
}
