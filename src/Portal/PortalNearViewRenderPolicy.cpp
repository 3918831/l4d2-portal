#include "PortalNearViewRenderPolicy.h"

#include <atomic>
#include <cmath>

namespace
{
    std::atomic_bool g_viewmodelDepthClearEnabled{ true };
    std::atomic_bool g_nearFrameGuardEnabled{ true };

    bool IsNearClipConflict(
        const PortalNearViewRenderPolicy::FrameContext& context)
    {
        constexpr float kDepthEpsilon = 0.01f;
        return context.enabled
            && context.apertureParticipated
            && std::isfinite(context.eyeDepth)
            && std::isfinite(context.zNear)
            && context.zNear > 0.0f
            && std::fabs(context.eyeDepth) <= context.zNear + kDepthEpsilon;
    }
}

bool PortalNearViewRenderPolicy::ShouldClearDepthBeforeViewmodel(
    const FrameContext& context)
{
    return IsNearClipConflict(context);
}

bool PortalNearViewRenderPolicy::ShouldSuppressLegacyFrameRedraw(
    const FrameContext& context)
{
    return IsNearClipConflict(context);
}

void PortalNearViewRenderPolicy::SetViewmodelDepthClearEnabled(bool enabled)
{
    g_viewmodelDepthClearEnabled.store(enabled, std::memory_order_relaxed);
}

bool PortalNearViewRenderPolicy::GetViewmodelDepthClearEnabled()
{
    return g_viewmodelDepthClearEnabled.load(std::memory_order_relaxed);
}

void PortalNearViewRenderPolicy::SetNearFrameGuardEnabled(bool enabled)
{
    g_nearFrameGuardEnabled.store(enabled, std::memory_order_relaxed);
}

bool PortalNearViewRenderPolicy::GetNearFrameGuardEnabled()
{
    return g_nearFrameGuardEnabled.load(std::memory_order_relaxed);
}
