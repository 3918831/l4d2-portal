#include <cstdlib>
#include <iostream>
#include <limits>

#include "../src/Portal/PortalEyeLeafHandoffDiagnostics.h"

namespace
{
    using PortalEyeLeafHandoffDiagnostics::EvaluationInput;
    using PortalEyeLeafHandoffDiagnostics::Evaluate;
    using PortalEyeLeafHandoffDiagnostics::PointSnapshot;
    using PortalEyeLeafHandoffDiagnostics::TraceSnapshot;
    using PortalEyeLeafHandoffDiagnostics::Vec3;

    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    PointSnapshot ClearPoint(float x, int leaf)
    {
        PointSnapshot snapshot;
        snapshot.point = { x, 20.0f, 30.0f };
        snapshot.leaf = leaf;
        snapshot.worldContents = 0;
        snapshot.playerSolidContents = 0;
        snapshot.outsideWorld = false;
        snapshot.available = true;
        return snapshot;
    }

    TraceSnapshot ClearTrace(const Vec3& endPosition)
    {
        TraceSnapshot trace;
        trace.endPosition = endPosition;
        trace.fraction = 1.0f;
        trace.startSolid = false;
        trace.allSolid = false;
        trace.available = true;
        return trace;
    }

    EvaluationInput ClearAdjacentLeafInput()
    {
        EvaluationInput input;
        input.original = ClearPoint(10.0f, 101);
        input.candidate = ClearPoint(13.0f, 202);
        input.originalToCandidate = ClearTrace(input.candidate.point);
        input.candidateToOriginal = ClearTrace(input.original.point);
        return input;
    }

    void ExpectBlocked(const EvaluationInput& input, const char* message)
    {
        Expect(!Evaluate(input).geometryClear, message);
    }
}

int main()
{
    {
        const auto result = Evaluate(ClearAdjacentLeafInput());
        Expect(result.geometryClear,
            "clear bidirectional geometry between distinct adjacent leaves must be reported");
        Expect(result.fallbackDelta.x == -3.0f
                && result.fallbackDelta.y == 0.0f
                && result.fallbackDelta.z == 0.0f,
            "fallback delta must point from rejected candidate to original eye");
        Expect(result.candidateOriginalDistance == 3.0f
                && result.fallbackDeltaLength == 3.0f,
            "candidate/original distance and fallback length must be hand-derived diagnostics");
    }

    {
        auto input = ClearAdjacentLeafInput();
        input.candidate.point.x = std::numeric_limits<float>::quiet_NaN();
        ExpectBlocked(input, "a non-finite candidate point must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.original.point.z = std::numeric_limits<float>::infinity();
        ExpectBlocked(input, "a non-finite original point must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.candidate.available = false;
        ExpectBlocked(input, "an unavailable candidate snapshot must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.original.available = false;
        ExpectBlocked(input, "an unavailable original snapshot must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.candidate.outsideWorld = true;
        ExpectBlocked(input, "an outside-world candidate must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.original.outsideWorld = true;
        ExpectBlocked(input, "an outside-world original eye must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.candidate.playerSolidContents = 1;
        ExpectBlocked(input, "a player-solid candidate must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.original.playerSolidContents = 1;
        ExpectBlocked(input, "a player-solid original eye must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.candidate.leaf = 0;
        ExpectBlocked(input, "a non-positive candidate leaf must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.original.leaf = -1;
        ExpectBlocked(input, "a non-positive original leaf must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.original.leaf = input.candidate.leaf;
        ExpectBlocked(input, "equal leaves are not cross-leaf handoff evidence");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.originalToCandidate.available = false;
        ExpectBlocked(input, "an unavailable original-to-candidate trace must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.candidateToOriginal.available = false;
        ExpectBlocked(input, "an unavailable candidate-to-original trace must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.originalToCandidate.fraction = 0.5f;
        ExpectBlocked(input, "a partial original-to-candidate trace must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.candidateToOriginal.fraction = 0.5f;
        ExpectBlocked(input, "a partial candidate-to-original trace must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.originalToCandidate.fraction = 0.9995f;
        input.candidateToOriginal.fraction = 0.9995f;
        Expect(Evaluate(input).geometryClear,
            "fractions within the 0.001 full-trace tolerance must be accepted");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.originalToCandidate.fraction = 0.998f;
        ExpectBlocked(input, "a trace outside the 0.001 full-trace tolerance must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.originalToCandidate.fraction = 1.0005f;
        ExpectBlocked(input, "a trace fraction above 1.0 must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.candidateToOriginal.fraction =
            std::numeric_limits<float>::quiet_NaN();
        ExpectBlocked(input, "a non-finite trace fraction must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.originalToCandidate.endPosition.y =
            std::numeric_limits<float>::infinity();
        ExpectBlocked(input, "a non-finite trace end position must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.originalToCandidate.startSolid = true;
        ExpectBlocked(input, "original-to-candidate startsolid must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.candidateToOriginal.startSolid = true;
        ExpectBlocked(input, "candidate-to-original startsolid must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.originalToCandidate.allSolid = true;
        ExpectBlocked(input, "original-to-candidate allsolid must fail closed");
    }
    {
        auto input = ClearAdjacentLeafInput();
        input.candidateToOriginal.allSolid = true;
        ExpectBlocked(input, "candidate-to-original allsolid must fail closed");
    }

    std::cout << "Portal eye leaf handoff diagnostics tests passed\n";
    return 0;
}
