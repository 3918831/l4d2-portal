#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/Portal/PortalPlacementFloorSnap.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    void ExpectNear(float actual, float expected, const char* message)
    {
        Expect(std::fabs(actual - expected) < 0.001f, message);
    }

    PortalPlacementFloorSnap::Input MakeInput(float portalCenterZ)
    {
        PortalPlacementFloorSnap::Input input;
        input.surfaceOrigin = Vector(0.0f, 0.0f, portalCenterZ);
        input.portalForward = Vector(1.0f, 0.0f, 0.0f);
        input.portalUp = Vector(0.0f, 0.0f, 1.0f);
        input.halfHeight = 54.0f;
        input.maximumSnapDistance = PortalPlacementFloorSnap::kMaximumSnapDistance;
        input.floorHit = true;
        input.floorPoint = Vector(0.05f, 0.0f, 0.0f);
        input.floorNormal = Vector(0.0f, 0.0f, 1.0f);
        return input;
    }
}

int main()
{
    Expect(PortalPlacementFloorSnap::GetEnabled(),
        "the validated near-floor placement snap must default on for traversal testing");

    {
        const auto result = PortalPlacementFloorSnap::Resolve(MakeInput(54.625f));
        Expect(result.snapped,
            "a wall portal slightly above the floor should snap down");
        ExpectNear(result.surfaceOrigin.z, 54.0f,
            "the snapped portal bottom should match the floor exactly");
        ExpectNear(result.snapDelta.z, -0.625f,
            "the downward snap should preserve the measured floor gap");
        ExpectNear(result.bottomGapBefore, 0.625f,
            "positive bottom gap should mean the portal is above the floor");
        ExpectNear(result.bottomGapAfter, 0.0f,
            "the snapped portal should have no remaining floor gap");
    }

    {
        const auto result = PortalPlacementFloorSnap::Resolve(MakeInput(53.375f));
        Expect(result.snapped,
            "a wall portal slightly embedded in the floor should snap up");
        ExpectNear(result.surfaceOrigin.z, 54.0f,
            "the embedded portal should be lifted until its bottom matches the floor");
        ExpectNear(result.snapDelta.z, 0.625f,
            "the upward correction should equal the measured penetration");
        ExpectNear(result.bottomGapBefore, -0.625f,
            "negative bottom gap should mean the portal is embedded in the floor");
    }

    {
        auto input = MakeInput(74.0f);
        const auto result = PortalPlacementFloorSnap::Resolve(input);
        Expect(result.snapped,
            "a portal twenty units above the floor should snap in the diagnostic wide band");
        ExpectNear(result.surfaceOrigin.z, 54.0f,
            "the wide diagnostic band should still align the bottom exactly");
    }

    {
        auto input = MakeInput(85.0f);
        const auto result = PortalPlacementFloorSnap::Resolve(input);
        Expect(!result.snapped,
            "a portal beyond the thirty-unit diagnostic band must stay at the trace position");
        ExpectNear(result.surfaceOrigin.z, 85.0f,
            "an out-of-band portal must not be moved");
    }

    {
        auto input = MakeInput(54.5f);
        input.portalUp = Vector(0.8f, 0.0f, 0.6f);
        const auto result = PortalPlacementFloorSnap::Resolve(input);
        Expect(!result.eligible,
            "floor matching must only run for portals whose local up is mostly world-up");
        Expect(!result.snapped,
            "an ineligible portal must not be moved");
    }

    {
        auto input = MakeInput(54.5f);
        input.floorNormal = Vector(1.0f, 0.0f, 0.0f);
        const auto result = PortalPlacementFloorSnap::Resolve(input);
        Expect(!result.snapped,
            "a near point on a wall must not be mistaken for a floor");
    }

    {
        auto input = MakeInput(50.75f);
        input.halfHeight = 50.0f;
        const auto result = PortalPlacementFloorSnap::Resolve(input);
        Expect(result.snapped,
            "floor matching should accept a caller-provided model half-height");
        ExpectNear(result.surfaceOrigin.z, 50.0f,
            "the configured half-height should determine the snapped center");
    }

    std::cout << "PortalPlacementFloorSnap tests passed\n";
    return 0;
}
