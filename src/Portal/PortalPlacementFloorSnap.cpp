#include "PortalPlacementFloorSnap.h"

#include <cmath>

namespace
{
    bool g_Enabled = true;

    bool IsFinite(float value)
    {
        return std::isfinite(value);
    }

    bool IsFinite(const Vector& value)
    {
        return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
    }

    bool TryNormalize(const Vector& value, Vector& normalized)
    {
        if (!IsFinite(value))
            return false;

        const float lengthSquared = value.LenghtSqr();
        if (!IsFinite(lengthSquared) || lengthSquared < 1e-6f)
            return false;

        normalized = value / std::sqrt(lengthSquared);
        return IsFinite(normalized);
    }
}

PortalPlacementFloorSnap::Result PortalPlacementFloorSnap::Resolve(const Input& input)
{
    Result result;
    result.surfaceOrigin = input.surfaceOrigin;

    if (!IsFinite(input.surfaceOrigin)
        || !IsFinite(input.halfHeight)
        || !IsFinite(input.maximumSnapDistance)
        || input.halfHeight <= 0.0f
        || input.maximumSnapDistance <= 0.0f)
    {
        return result;
    }

    Vector portalForward;
    Vector portalUp;
    if (!TryNormalize(input.portalForward, portalForward)
        || !TryNormalize(input.portalUp, portalUp)
        || portalUp.z <= 0.7f)
    {
        return result;
    }

    result.eligible = true;
    if (!input.floorHit || !IsFinite(input.floorPoint))
        return result;

    Vector floorNormal;
    if (!TryNormalize(input.floorNormal, floorNormal)
        || floorNormal.Dot(portalUp) <= 0.7f)
    {
        return result;
    }

    // The floor trace is offset slightly away from the portal wall to avoid
    // immediately re-hitting it. Project its hit back onto the portal surface
    // before measuring the bottom gap or producing the adjusted origin.
    const Vector floorPointOnSurface =
        input.floorPoint - portalForward * kFloorProbeSurfaceOffset;
    const Vector portalBottom = input.surfaceOrigin - portalUp * input.halfHeight;
    result.bottomGapBefore = (portalBottom - floorPointOnSurface).Dot(portalUp);

    if (!IsFinite(result.bottomGapBefore)
        || std::fabs(result.bottomGapBefore) > input.maximumSnapDistance)
    {
        return result;
    }

    result.surfaceOrigin = floorPointOnSurface + portalUp * input.halfHeight;
    result.snapDelta = result.surfaceOrigin - input.surfaceOrigin;
    result.bottomGapAfter =
        (result.surfaceOrigin - portalUp * input.halfHeight - floorPointOnSurface).Dot(portalUp);
    result.snapped = IsFinite(result.surfaceOrigin)
        && IsFinite(result.snapDelta)
        && IsFinite(result.bottomGapAfter);

    if (!result.snapped)
    {
        result.surfaceOrigin = input.surfaceOrigin;
        result.snapDelta = Vector();
        result.bottomGapAfter = result.bottomGapBefore;
    }

    return result;
}

bool PortalPlacementFloorSnap::GetEnabled()
{
    return g_Enabled;
}

void PortalPlacementFloorSnap::SetEnabled(bool enabled)
{
    g_Enabled = enabled;
}
