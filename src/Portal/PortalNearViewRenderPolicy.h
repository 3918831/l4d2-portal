#pragma once

namespace PortalNearViewRenderPolicy
{
    struct FrameContext
    {
        bool enabled = false;
        bool apertureParticipated = false;
        float eyeDepth = 0.0f;
        float zNear = 0.0f;
    };

    bool ShouldClearDepthBeforeViewmodel(const FrameContext& context);
    bool ShouldSuppressLegacyFrameRedraw(const FrameContext& context);

    void SetViewmodelDepthClearEnabled(bool enabled);
    bool GetViewmodelDepthClearEnabled();

    void SetNearFrameGuardEnabled(bool enabled);
    bool GetNearFrameGuardEnabled();
}
