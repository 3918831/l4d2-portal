#pragma once

#include <cstddef>
#include <cstdint>

namespace PortalVisualContinuity
{
    enum class Stage
    {
        None,
        EntryHandoff,
        TeleportCommit,
        PredictionSync,
        MainViewPreRender,
    };

    enum class SampleSemantics
    {
        None,
        ExitViewSpace,
        EntryPhysical,
        ExitPhysical,
    };

    enum class HandoffBinding
    {
        Missing,
        Matched,
        SideMismatch,
        EpochMismatch,
        CommandMismatch,
        Stale,
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Sample
    {
        Stage stage = Stage::None;
        Vec3 eyeOrigin;
        Vec3 eyeAngles;
        Vec3 playerOrigin;
        Vec3 velocity;
        SampleSemantics eyeSemantics = SampleSemantics::None;
        SampleSemantics angleSemantics = SampleSemantics::None;
        SampleSemantics playerSemantics = SampleSemantics::None;
        SampleSemantics velocitySemantics = SampleSemantics::None;
        float fov = 0.0f;
        float engineTime = 0.0f;
        std::uint32_t traversalEpoch = 0;
        int commandNumber = 0;
        int frameCount = -1;
        int transitionPhase = -1;
        int renderIndex = -1;
        bool hasEyeOrigin = false;
        bool hasEyeAngles = false;
        bool hasPlayerOrigin = false;
        bool hasVelocity = false;
        bool hasFov = false;
    };

    struct Delta
    {
        float eyePosition = 0.0f;
        float eyeAngle = 0.0f;
        float playerPosition = 0.0f;
        float velocity = 0.0f;
        float fov = 0.0f;
        int eyePositionFromOrdinal = -1;
        int eyeAngleFromOrdinal = -1;
        int playerPositionFromOrdinal = -1;
        int velocityFromOrdinal = -1;
        int fovFromOrdinal = -1;
        Stage eyePositionFromStage = Stage::None;
        Stage eyeAngleFromStage = Stage::None;
        Stage playerPositionFromStage = Stage::None;
        Stage velocityFromStage = Stage::None;
        Stage fovFromStage = Stage::None;
        bool hasEyePosition = false;
        bool hasEyeAngle = false;
        bool hasPlayerPosition = false;
        bool hasVelocity = false;
        bool hasFov = false;
    };

    struct MaximumDelta
    {
        float eyePosition = 0.0f;
        float eyeAngle = 0.0f;
        int eyePositionFromOrdinal = -1;
        int eyePositionToOrdinal = -1;
        int eyeAngleFromOrdinal = -1;
        int eyeAngleToOrdinal = -1;
        Stage eyePositionFromStage = Stage::None;
        Stage eyePositionToStage = Stage::None;
        Stage eyeAngleFromStage = Stage::None;
        Stage eyeAngleToStage = Stage::None;
        bool hasEyePosition = false;
        bool hasEyeAngle = false;
    };

    struct AppendResult
    {
        bool accepted = false;
        bool completed = false;
        bool hasPrevious = false;
        std::uint32_t transactionId = 0;
        int ordinal = -1;
        Sample previous;
        Sample current;
        Delta delta;
        MaximumDelta maximum;
    };

    struct BeginResult
    {
        bool accepted = false;
        bool hasEntryHandoff = false;
        std::uint32_t transactionId = 0;
        std::uint32_t supersededTransactionId = 0;
        int supersededSampleCount = 0;
        int supersededMainViewSampleCount = 0;
        HandoffBinding handoffBinding = HandoffBinding::Missing;
        Sample entryHandoff;
        AppendResult commit;
    };

    struct AbortResult
    {
        bool aborted = false;
        std::uint32_t transactionId = 0;
        int sampleCount = 0;
        int mainViewSampleCount = 0;
    };

    class Tracker
    {
    public:
        static constexpr int kMainViewSampleCount = 5;
        static constexpr int kMaximumSamples =
            1 + 1 + 1 + kMainViewSampleCount;
        static constexpr int kMaxEntryHandoffAgeFrames = 8;
        static constexpr float kMaxEntryHandoffAgeSeconds = 0.20f;

        AbortResult ResetCapture();
        void CacheEntryHandoff(
            int entrySide,
            std::uint32_t traversalEpoch,
            const Sample& sample);

        BeginResult BeginTransaction(
            int entrySide,
            std::uint32_t traversalEpoch,
            int commandNumber,
            const Sample& commitSample);
        AppendResult RecordPredictionSync(
            int commandNumber,
            const Sample& sample);
        AppendResult RecordMainViewPreRender(const Sample& sample);

        bool IsActive() const { return m_active; }
        std::uint32_t GetActiveTransactionId() const
        {
            return m_active ? m_activeTransactionId : 0;
        }

    private:
        AppendResult Append(const Sample& sample);

        Sample m_samples[kMaximumSamples];
        int m_sampleCount = 0;
        int m_mainViewSampleCount = 0;
        int m_lastMainViewFrame = -1;
        int m_activeEntrySide = 0;
        int m_activeCommandNumber = 0;
        std::uint32_t m_nextTransactionId = 1;
        std::uint32_t m_activeTransactionId = 0;
        MaximumDelta m_maximum;
        Sample m_pendingEntryHandoff;
        int m_pendingEntrySide = 0;
        std::uint32_t m_pendingTraversalEpoch = 0;
        bool m_hasPendingEntryHandoff = false;
        bool m_active = false;
        bool m_predictionRecorded = false;
    };

    const char* StageName(Stage stage);
    const char* SemanticsName(SampleSemantics semantics);
    const char* HandoffBindingName(HandoffBinding binding);
}
