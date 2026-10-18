#pragma once

#include <cstdint>

namespace PortalRenderCadence
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Sample
    {
        Vec3 calcViewInputEye;
        Vec3 rawEye;
        Vec3 originalEye;
        Vec3 appliedEye;
        Vec3 finalViewEye;
        Vec3 playerOrigin;
        Vec3 renderOrigin;
        Vec3 networkOrigin;
        Vec3 oldOrigin;
        Vec3 velocity;
        Vec3 calcViewInputAngles;
        Vec3 originalViewAngles;
        Vec3 appliedViewAngles;
        Vec3 finalViewAngles;
        float simulationTime = 0.0f;
        float frameTime = 0.0f;
        int frameCount = -1;
        int tickCount = -1;
        int commandNumber = 0;
        bool eyeHistoryActive = false;
        bool eyeHistoryReleased = false;
        bool shadowPredictorApplied = false;
    };

    struct Summary
    {
        std::uint32_t transactionId = 0;
        int committedCommandNumber = 0;
        int frameCount = 0;
        int commandTransitions = 0;
        int tickTransitions = 0;
        int rawFrozenFrames = 0;
        int originalFrozenFrames = 0;
        int appliedFrozenFrames = 0;
        int calcViewInputFrozenFrames = 0;
        int finalViewFrozenFrames = 0;
        int originFrozenFrames = 0;
        int renderOriginFrozenFrames = 0;
        int networkOriginFrozenFrames = 0;
        int oldOriginFrozenFrames = 0;
        int simulationTimeFrozenFrames = 0;
        int finalViewAnglesFrozenFrames = 0;
        int maxRawConsecutiveFrozenFrames = 0;
        int maxOriginalConsecutiveFrozenFrames = 0;
        int maxAppliedConsecutiveFrozenFrames = 0;
        int maxCalcViewInputConsecutiveFrozenFrames = 0;
        int maxFinalViewConsecutiveFrozenFrames = 0;
        int maxOriginConsecutiveFrozenFrames = 0;
        int maxRenderOriginConsecutiveFrozenFrames = 0;
        int maxNetworkOriginConsecutiveFrozenFrames = 0;
        int maxOldOriginConsecutiveFrozenFrames = 0;
        int maxSimulationTimeConsecutiveFrozenFrames = 0;
        int maxFinalViewAnglesConsecutiveFrozenFrames = 0;
        float maxRawDeltaLength = 0.0f;
        float maxOriginalDeltaLength = 0.0f;
        float maxAppliedDeltaLength = 0.0f;
        float maxCalcViewInputDeltaLength = 0.0f;
        float maxFinalViewDeltaLength = 0.0f;
        float maxOriginDeltaLength = 0.0f;
        float maxRenderOriginDeltaLength = 0.0f;
        float maxNetworkOriginDeltaLength = 0.0f;
        float maxOldOriginDeltaLength = 0.0f;
        float maxSimulationTimeDelta = 0.0f;
        float maxFinalViewAngleDeltaLength = 0.0f;
        int speedSampleFrames = 0;
        int eyeHistoryActiveFrames = 0;
        int shadowPredictorAppliedFrames = 0;
        int postEyeHistoryFrames = 0;
        int finalViewUnderSpeedFrames = 0;
        int eyeHistoryUnderSpeedFrames = 0;
        int postEyeHistoryUnderSpeedFrames = 0;
        int maxFinalViewUnderSpeedRun = 0;
        float minFinalViewSpeedRatio = 0.0f;
        float averageFinalViewSpeedRatio = 0.0f;
        float maxFinalViewSpeedRatio = 0.0f;
        float minAppliedSpeedRatio = 0.0f;
        float averageAppliedSpeedRatio = 0.0f;
        float maxAppliedSpeedRatio = 0.0f;
    };

    struct BeginResult
    {
        bool accepted = false;
        bool superseded = false;
        std::uint32_t transactionId = 0;
        Summary supersededSummary;
    };

    struct RecordResult
    {
        bool accepted = false;
        bool completed = false;
        bool hasPrevious = false;
        bool commandAdvanced = false;
        bool tickAdvanced = false;
        bool rawFrozen = false;
        bool originalFrozen = false;
        bool appliedFrozen = false;
        bool calcViewInputFrozen = false;
        bool finalViewFrozen = false;
        bool originFrozen = false;
        bool renderOriginFrozen = false;
        bool networkOriginFrozen = false;
        bool oldOriginFrozen = false;
        bool simulationTimeFrozen = false;
        bool finalViewAnglesFrozen = false;
        bool eyeHistoryChanged = false;
        bool shadowPredictorChanged = false;
        bool postEyeHistory = false;
        bool finalViewUnderSpeed = false;
        bool finalViewUnderSpeedChanged = false;
        std::uint32_t transactionId = 0;
        int ordinal = -1;
        int rawConsecutiveFrozenFrames = 0;
        int originalConsecutiveFrozenFrames = 0;
        int appliedConsecutiveFrozenFrames = 0;
        int calcViewInputConsecutiveFrozenFrames = 0;
        int finalViewConsecutiveFrozenFrames = 0;
        int originConsecutiveFrozenFrames = 0;
        int renderOriginConsecutiveFrozenFrames = 0;
        int networkOriginConsecutiveFrozenFrames = 0;
        int oldOriginConsecutiveFrozenFrames = 0;
        int simulationTimeConsecutiveFrozenFrames = 0;
        int finalViewAnglesConsecutiveFrozenFrames = 0;
        float rawDeltaLength = 0.0f;
        float originalDeltaLength = 0.0f;
        float appliedDeltaLength = 0.0f;
        float calcViewInputDeltaLength = 0.0f;
        float finalViewDeltaLength = 0.0f;
        float originDeltaLength = 0.0f;
        float renderOriginDeltaLength = 0.0f;
        float networkOriginDeltaLength = 0.0f;
        float oldOriginDeltaLength = 0.0f;
        float simulationTimeDelta = 0.0f;
        float finalViewAngleDeltaLength = 0.0f;
        float appliedExitNormalDelta = 0.0f;
        float appliedExitTangentDelta = 0.0f;
        Vec3 expectedFrameDelta;
        float expectedFrameDeltaLength = 0.0f;
        float appliedVelocityProjectedDelta = 0.0f;
        float finalViewVelocityProjectedDelta = 0.0f;
        float appliedSpeedRatio = 0.0f;
        float finalViewSpeedRatio = 0.0f;
        Sample previous;
        Sample current;
        Summary summary;
    };

    class Tracker
    {
    public:
        static constexpr int kCaptureCommandTransitions = 6;
        static constexpr int kMaximumFrames = 192;
        static constexpr int kFloorWallCaptureCommandTransitions = 32;
        static constexpr int kFloorWallMaximumFrames = 1024;
        static constexpr float kFreezeEpsilon = 0.001f;
        static constexpr float kUnderSpeedRatio = 0.5f;

        void Reset();
        BeginResult BeginTransaction(
            std::uint32_t transactionId,
            int committedCommandNumber,
            const Vec3& exitNormal,
            int captureCommandTransitions = kCaptureCommandTransitions,
            int maximumFrames = kMaximumFrames);
        RecordResult RecordFrame(const Sample& sample);

        bool IsActive() const { return m_active; }
        std::uint32_t GetTransactionId() const
        {
            return m_active ? m_summary.transactionId : 0;
        }
        const Summary& GetSummary() const { return m_summary; }

    private:
        static Vec3 Delta(const Vec3& to, const Vec3& from);
        static float Dot(const Vec3& a, const Vec3& b);
        static float Length(const Vec3& value);
        static float AngleDeltaLength(const Vec3& to, const Vec3& from);

        Sample m_previous;
        Vec3 m_exitNormal;
        Summary m_summary;
        int m_lastFrameCount = -1;
        int m_rawConsecutiveFrozenFrames = 0;
        int m_originalConsecutiveFrozenFrames = 0;
        int m_appliedConsecutiveFrozenFrames = 0;
        int m_calcViewInputConsecutiveFrozenFrames = 0;
        int m_finalViewConsecutiveFrozenFrames = 0;
        int m_originConsecutiveFrozenFrames = 0;
        int m_renderOriginConsecutiveFrozenFrames = 0;
        int m_networkOriginConsecutiveFrozenFrames = 0;
        int m_oldOriginConsecutiveFrozenFrames = 0;
        int m_simulationTimeConsecutiveFrozenFrames = 0;
        int m_finalViewAnglesConsecutiveFrozenFrames = 0;
        int m_finalViewConsecutiveUnderSpeedFrames = 0;
        bool m_seenEyeHistoryActive = false;
        bool m_postEyeHistory = false;
        bool m_previousFinalViewUnderSpeed = false;
        bool m_hasPrevious = false;
        bool m_active = false;
        int m_captureCommandTransitions = kCaptureCommandTransitions;
        int m_maximumFrames = kMaximumFrames;
    };
}
