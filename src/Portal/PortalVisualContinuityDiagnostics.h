#pragma once

#include <cstdint>

#include "PortalVisualContinuityTracker.h"

namespace PortalVisualContinuity
{
    struct TeleportCommitDetails
    {
        const char* entryName = "None";
        const char* exitName = "None";
        Vec3 oldOrigin;
        Vec3 newOrigin;
        Vec3 oldEye;
        Vec3 newEye;
        Vec3 oldVelocity;
        Vec3 newVelocity;
        Vec3 oldAngles;
        Vec3 newAngles;
    };

    struct PredictionBoundaryDetails
    {
        bool syncApplied = false;
        Vec3 expectedOrigin;
        Vec3 expectedVelocity;
        Vec3 expectedAngles;
        Vec3 beforeOrigin;
        Vec3 afterOrigin;
        Vec3 beforeVelocity;
        Vec3 afterVelocity;
        Vec3 beforeCommandAngles;
        Vec3 afterCommandAngles;
    };

    class Diagnostics
    {
    public:
        void ResetCapture(const char* reason = "reset");
        void CaptureEntryHandoff(
            int entrySide,
            std::uint32_t traversalEpoch,
            const Sample& sample);
        std::uint32_t BeginCommittedTeleport(
            int entrySide,
            std::uint32_t traversalEpoch,
            int commandNumber,
            const Sample& commitSample,
            const TeleportCommitDetails& details);
        std::uint32_t CapturePredictionBoundary(
            int commandNumber,
            const Sample& sample,
            const PredictionBoundaryDetails& details);
        std::uint32_t CaptureMainViewPreRender(const Sample& sample);
        std::uint32_t MarkUserEvent(
            const char* reason = "manual-visual-marker") const;

        bool IsCapturingMainView() const { return m_tracker.IsActive(); }
        std::uint32_t GetLastTransactionId() const
        {
            return m_tracker.GetLastTransactionId();
        }

    private:
        void LogSample(
            std::uint32_t transactionId,
            int ordinal,
            const Sample& sample) const;
        void LogAppend(const AppendResult& result) const;

        Tracker m_tracker;
    };
}
