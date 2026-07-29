#pragma once

#include <cstdint>

#include "../Util/Math/Math.h"

namespace PortalEyeContinuity
{
    struct UpdateResult
    {
        Vector eyeOrigin;
        Vector previousRawEyeBefore;
        Vector continuityEyeBefore;
        Vector previousRawEyeAfterTransform;
        Vector continuityEyeAfterTransform;
        Vector freeMovementDelta;
        Vector engineViewMovementDelta;
        Vector previousRawEyeAfter;
        Vector continuityEyeAfter;
        bool overrideOriginalView = false;
        bool consumedPendingTeleport = false;
        bool releasedOverride = false;
        bool initializedBefore = false;
        bool initializedAfter = false;
        bool hadPendingTeleport = false;
        bool interpolatingBefore = false;
        bool interpolatingAfter = false;
        bool overridingBefore = false;
        bool overridingAfter = false;
        bool planeConstrained = false;
        bool safetyFallbackToOriginal = false;
        float normalCorrection = 0.0f;
        float originalExitDepth = 0.0f;
        float outputExitDepth = 0.0f;
        float rawResidual = 0.0f;
        float originalViewResidual = 0.0f;
        std::uint32_t traversalEpoch = 0;
        int commandNumber = 0;
    };

    class State
    {
    public:
        void Reset();
        bool PublishTeleport(
            const matrix3x4_t& entryToExit,
            const Vector& exitPlaneOrigin,
            const Vector& exitPlaneNormal,
            std::uint32_t traversalEpoch,
            int commandNumber);
        UpdateResult Update(
            bool enabled,
            const Vector& rawPhysicalEye,
            const Vector& originalViewEye,
            float frameTime);

        bool HasPendingTeleport() const { return m_hasPendingTeleport; }
        bool IsInitialized() const { return m_initialized; }
        bool IsInterpolating() const { return m_eyePositionIsInterpolating; }
        bool IsOverridingOriginalView() const { return m_overrideOriginalView; }

    private:
        Vector TransformPoint(const matrix3x4_t& matrix, const Vector& point) const;

        Vector m_previousRawEye;
        Vector m_previousOriginalViewEye;
        Vector m_continuityEye;
        matrix3x4_t m_pendingMatrix;
        Vector m_pendingExitPlaneOrigin;
        Vector m_pendingExitPlaneNormal;
        Vector m_activeExitPlaneOrigin;
        Vector m_activeExitPlaneNormal;
        std::uint32_t m_pendingTraversalEpoch = 0;
        std::uint32_t m_lastPublishedTraversalEpoch = 0;
        int m_pendingCommandNumber = 0;
        int m_activeCommandNumber = 0;
        bool m_initialized = false;
        bool m_hasPendingTeleport = false;
        bool m_eyePositionIsInterpolating = false;
        bool m_overrideOriginalView = false;
    };
}
