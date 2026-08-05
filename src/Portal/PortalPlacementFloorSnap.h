#pragma once

#include "../Util/Math/Vector/Vector.h"

namespace PortalPlacementFloorSnap
{
    // Portal SDK 2013 uses PORTAL_HALF_HEIGHT == 54. The current mod models
    // are different, so callers pass this value explicitly and diagnostics
    // identify it as provisional until runtime geometry calibration replaces it.
    constexpr float kPortalSdk2013ProvisionalHalfHeight = 54.0f;
    // Deliberately wider than the Portal SDK production tolerance. It removes
    // manual aim variance while the current model's real half-height remains
    // provisional and is covered by a runtime rollback command.
    constexpr float kMaximumSnapDistance = 30.0f;
    constexpr float kFloorProbeSurfaceOffset = 0.05f;

    struct Input
    {
        Vector surfaceOrigin;
        Vector portalForward;
        Vector portalUp;
        float halfHeight = kPortalSdk2013ProvisionalHalfHeight;
        float maximumSnapDistance = kMaximumSnapDistance;
        bool floorHit = false;
        Vector floorPoint;
        Vector floorNormal;
    };

    struct Result
    {
        bool eligible = false;
        bool snapped = false;
        Vector surfaceOrigin;
        Vector snapDelta;
        float bottomGapBefore = 0.0f;
        float bottomGapAfter = 0.0f;
    };

    Result Resolve(const Input& input);

    bool GetEnabled();
    void SetEnabled(bool enabled);
}
