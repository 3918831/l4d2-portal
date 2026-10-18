#include "PortalViewReorientation.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kAirborneReorientationRate = 120.0f;
    constexpr float kGroundedReorientationRate = 240.0f;
    constexpr float kReorientationAccelerationRate = 400.0f;
    constexpr float kUprightThreshold = 0.75f;
    constexpr float kSettledRate = 60.0f;
    constexpr float kOfficialTimeScale = 5.0f;
    constexpr float kCompletionEpsilon = 0.001f;

    float ComputeUpZ(const PortalViewReorientation::Angles& angles)
    {
        const float pitch = angles.pitch * 0.01745329251994329577f;
        const float roll = angles.roll * 0.01745329251994329577f;
        return std::cos(pitch) * std::cos(roll);
    }

    float Approach(float target, float value, float speed)
    {
        if (value < target)
            return std::min(value + speed, target);
        if (value > target)
            return std::max(value - speed, target);
        return target;
    }

    bool IsFinite(const PortalViewReorientation::Angles& angles)
    {
        return std::isfinite(angles.pitch) && std::isfinite(angles.yaw) &&
            std::isfinite(angles.roll);
    }

    float NormalizeRoll(float roll)
    {
        roll = std::fmod(roll, 360.0f);
        if (roll > 180.0f)
            roll -= 360.0f;
        else if (roll < -180.0f)
            roll += 360.0f;
        return roll;
    }
}

namespace PortalViewReorientation
{
    float AdvanceOfficialRate(
        float currentOfficialRate,
        float upZ,
        bool onGround,
        float officialDeltaTime)
    {
        if (!std::isfinite(currentOfficialRate) || !std::isfinite(upZ) ||
            !std::isfinite(officialDeltaTime) || officialDeltaTime < 0.0f)
        {
            return currentOfficialRate;
        }

        if (upZ < kUprightThreshold)
        {
            currentOfficialRate +=
                officialDeltaTime * kReorientationAccelerationRate;
            return std::min(
                currentOfficialRate,
                onGround
                    ? kGroundedReorientationRate
                    : kAirborneReorientationRate);
        }

        return Approach(
            kSettledRate,
            currentOfficialRate,
            officialDeltaTime * kReorientationAccelerationRate);
    }

    const char* GetCurveName()
    {
        return "portal-sdk-2013";
    }

    float GetTimeScale()
    {
        return kOfficialTimeScale;
    }

    bool Controller::Arm(const ArmRequest& request)
    {
        if (request.transactionId == 0 || request.mode != Mode::Roll ||
            !IsFinite(request.committedAngles))
            return false;

        if (!m_active)
            m_rate = 0.0f;

        m_active = true;
        m_transactionId = request.transactionId;
        m_lastFrameCount = -1;
        m_roll = NormalizeRoll(request.committedAngles.roll);
        if (m_roll == 0.0f)
            Reset();
        return true;
    }

    FrameResult Controller::Advance(const FrameInput& input)
    {
        FrameResult result;
        result.angles = input.liveAngles;
        result.angles.roll = m_roll;
        result.rate = GetRate();
        if (IsFinite(result.angles))
            result.upZ = ComputeUpZ(result.angles);

        if (!m_active || input.frameCount < 0 || input.frameTime < 0.0f ||
            !std::isfinite(input.frameTime) || !IsFinite(input.liveAngles))
        {
            return result;
        }

        if (input.frameCount == m_lastFrameCount)
        {
            result.duplicateFrame = true;
            return result;
        }

        m_lastFrameCount = input.frameCount;
        result.advanced = true;

        Angles angles;
        angles.pitch = input.liveAngles.pitch;
        angles.yaw = input.liveAngles.yaw;
        angles.roll = m_roll;
        const float upZ = ComputeUpZ(angles);

        const float officialDeltaTime = input.frameTime * kOfficialTimeScale;
        m_rate = AdvanceOfficialRate(
            m_rate,
            upZ,
            input.onGround,
            officialDeltaTime);

        const float step = officialDeltaTime * m_rate;
        m_roll = Approach(0.0f, m_roll, step);
        angles.roll = m_roll;

        result.angles = angles;
        result.rate = GetRate();
        result.upZ = upZ;
        if (std::fabs(m_roll) <= kCompletionEpsilon)
        {
            m_roll = 0.0f;
            result.angles.roll = 0.0f;
            result.completed = true;
            m_active = false;
            m_transactionId = 0;
            m_lastFrameCount = -1;
            m_rate = 0.0f;
        }

        return result;
    }

    void Controller::Reset()
    {
        m_active = false;
        m_transactionId = 0;
        m_lastFrameCount = -1;
        m_roll = 0.0f;
        m_rate = 0.0f;
    }

    bool Controller::IsActive() const
    {
        return m_active;
    }

    std::uint32_t Controller::GetTransactionId() const
    {
        return m_transactionId;
    }

    float Controller::GetCurrentRoll() const
    {
        return m_roll;
    }

    float Controller::GetRate() const
    {
        return m_rate * kOfficialTimeScale;
    }

    bool ShouldRestoreInputRoll(
        bool featureEnabled,
        bool signatureValidated,
        bool abiValidated,
        bool localInputInstance,
        bool transactionActive)
    {
        return featureEnabled && signatureValidated && abiValidated &&
            localInputInstance && transactionActive;
    }
}
