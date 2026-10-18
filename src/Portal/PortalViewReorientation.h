#pragma once

#include <cstdint>

namespace PortalViewReorientation
{
    enum class Mode { Roll };

    float AdvanceOfficialRate(
        float currentOfficialRate,
        float upZ,
        bool onGround,
        float officialDeltaTime);
    const char* GetCurveName();
    float GetTimeScale();

    struct Angles
    {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
    };

    struct ArmRequest
    {
        std::uint32_t transactionId = 0;
        Angles committedAngles;
        Mode mode = Mode::Roll;
    };

    struct FrameInput
    {
        int frameCount = -1;
        float frameTime = 0.0f;
        bool onGround = false;
        Angles liveAngles;
    };

    struct FrameResult
    {
        bool advanced = false;
        bool completed = false;
        bool duplicateFrame = false;
        Angles angles;
        float rate = 0.0f;
        float upZ = 1.0f;
    };

    class Controller
    {
    public:
        bool Arm(const ArmRequest& request);
        FrameResult Advance(const FrameInput& input);
        void Reset();
        bool IsActive() const;
        std::uint32_t GetTransactionId() const;
        float GetCurrentRoll() const;
        float GetRate() const;

    private:
        bool m_active = false;
        std::uint32_t m_transactionId = 0;
        int m_lastFrameCount = -1;
        float m_roll = 0.0f;
        // Portal SDK state rate. Public results expose the effective world-time rate.
        float m_rate = 0.0f;
    };

    bool ShouldRestoreInputRoll(
        bool featureEnabled,
        bool signatureValidated,
        bool abiValidated,
        bool localInputInstance,
        bool transactionActive);
}
