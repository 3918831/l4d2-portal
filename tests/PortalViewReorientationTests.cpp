#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#include "../src/Portal/PortalViewReorientation.h"

namespace
{
    constexpr float kEpsilon = 0.001f;

    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    void ExpectNear(float actual, float expected, const char* message)
    {
        Expect(std::fabs(actual - expected) < kEpsilon, message);
    }

    PortalViewReorientation::ArmRequest RollArm(std::uint32_t transactionId, float roll)
    {
        PortalViewReorientation::ArmRequest request;
        request.transactionId = transactionId;
        request.committedAngles.roll = roll;
        request.mode = PortalViewReorientation::Mode::Roll;
        return request;
    }

    PortalViewReorientation::FrameInput Frame(
        int frameCount,
        float frameTime,
        bool onGround,
        float roll,
        float pitch = 0.0f,
        float yaw = 0.0f)
    {
        PortalViewReorientation::FrameInput input;
        input.frameCount = frameCount;
        input.frameTime = frameTime;
        input.onGround = onGround;
        input.liveAngles.pitch = pitch;
        input.liveAngles.yaw = yaw;
        input.liveAngles.roll = roll;
        return input;
    }

    void ExpectHalfSecondMonotonicReturn(
        float initialRoll,
        float framesPerSecond,
        std::uint32_t transactionId)
    {
        PortalViewReorientation::Controller controller;
        Expect(controller.Arm(RollArm(transactionId, initialRoll)),
            "a roll transaction must arm");

        const float frameTime = 1.0f / framesPerSecond;
        float previousRoll = initialRoll;
        float elapsed = 0.0f;
        bool completed = false;
        for (int frameCount = 1; frameCount <= 5000; ++frameCount)
        {
            const PortalViewReorientation::FrameResult result = controller.Advance(
                Frame(frameCount, frameTime, false, previousRoll));
            Expect(result.advanced, "each unique active frame must advance the controller");
            Expect(result.angles.roll * initialRoll >= -kEpsilon,
                "the reorientation must never cross zero");
            Expect(std::fabs(result.angles.roll) <= std::fabs(previousRoll) + kEpsilon,
                "the reorientation must be monotonic toward zero");
            previousRoll = result.angles.roll;
            elapsed += frameTime;
            if (result.completed)
            {
                ExpectNear(result.angles.roll, 0.0f,
                    "a completed reorientation must finish exactly level");
                completed = true;
                break;
            }
        }
        Expect(completed, "the reorientation must complete in bounded simulated time");
        Expect(elapsed <= 0.5f + kEpsilon,
            "a near-180-degree reorientation must complete within half a second");
    }
}

int main()
{
    std::uint32_t transactionId = 1u;
    for (const float framesPerSecond : { 30.0f, 60.0f, 300.0f })
    {
        ExpectHalfSecondMonotonicReturn(179.8f, framesPerSecond, transactionId++);
        ExpectHalfSecondMonotonicReturn(-179.8f, framesPerSecond, transactionId++);
    }

    PortalViewReorientation::Controller firstAirborneFrame;
    Expect(firstAirborneFrame.Arm(RollArm(3u, 90.0f)),
        "the first-airborne-frame case must arm");
    const PortalViewReorientation::FrameResult firstAirborne = firstAirborneFrame.Advance(
        Frame(10, 1.0f / 60.0f, false, 90.0f));
    ExpectNear(firstAirborne.rate, 166.666667f,
        "the first airborne frame must expose the five-times effective official rate");
    ExpectNear(firstAirborne.angles.roll, 87.222222f,
        "the first airborne roll step must advance both rate and roll on five-times official time");

    PortalViewReorientation::Controller firstNegativeAirborneFrame;
    Expect(firstNegativeAirborneFrame.Arm(RollArm(11u, -90.0f)),
        "the negative first-airborne-frame case must arm");
    const PortalViewReorientation::FrameResult firstNegativeAirborne =
        firstNegativeAirborneFrame.Advance(Frame(11, 1.0f / 60.0f, false, -90.0f));
    ExpectNear(firstNegativeAirborne.rate, 166.666667f,
        "positive and negative ordinary roll must expose the same effective rate");
    ExpectNear(firstNegativeAirborne.angles.roll, -87.222222f,
        "negative ordinary roll must mirror the official positive-roll step");

    const float officialDeltaTime = (1.0f / 60.0f) * 5.0f;
    ExpectNear(PortalViewReorientation::AdvanceOfficialRate(
            120.0f, 0.7500f, false, officialDeltaTime),
        86.666667f,
        "upZ exactly 0.75 must select the official upright branch");
    ExpectNear(PortalViewReorientation::AdvanceOfficialRate(
            120.0f, 0.7501f, false, officialDeltaTime),
        86.666667f,
        "upZ above 0.75 must select the official upright branch");
    ExpectNear(PortalViewReorientation::AdvanceOfficialRate(
            120.0f, 0.7499f, false, officialDeltaTime),
        120.0f,
        "upZ below 0.75 must select the strict official inverted branch");
    ExpectNear(PortalViewReorientation::AdvanceOfficialRate(
            110.0f, -1.0f, false, officialDeltaTime),
        120.0f,
        "the official airborne inverted branch must cap at 120 degrees per official second");
    ExpectNear(PortalViewReorientation::AdvanceOfficialRate(
            230.0f, -1.0f, true, officialDeltaTime),
        240.0f,
        "the official grounded inverted branch must cap at 240 degrees per official second");
    ExpectNear(PortalViewReorientation::AdvanceOfficialRate(
            0.0f, 1.0f, false, officialDeltaTime),
        33.333333f,
        "the official upright branch must approach 60 at 400 degrees per official second squared");
    ExpectNear(PortalViewReorientation::AdvanceOfficialRate(
            120.0f, 1.0f, false, officialDeltaTime),
        86.666667f,
        "the official upright branch must decelerate toward 60 at the same acceleration");

    PortalViewReorientation::Controller capController;
    Expect(capController.Arm(RollArm(4u, 179.8f)), "the cap case must arm");
    bool sawLowUpZ = false;
    bool sawHighUpZ = false;
    bool hasPriorRate = false;
    float priorRate = 0.0f;
    float maximumAirborneEffectiveRate = 0.0f;
    for (int frameCount = 1; frameCount <= 1000 && capController.IsActive(); ++frameCount)
    {
        const PortalViewReorientation::FrameResult result = capController.Advance(
            Frame(frameCount, 1.0f / 60.0f, false, capController.GetCurrentRoll()));
        if (result.upZ < 0.75f)
        {
            sawLowUpZ = true;
            maximumAirborneEffectiveRate =
                std::max(maximumAirborneEffectiveRate, result.rate);
            Expect(result.rate <= 600.0f + kEpsilon,
                "the airborne rate must not exceed 600 degrees per second while upZ is below 0.75");
        }
        else
        {
            if (hasPriorRate)
            {
                Expect(std::fabs(result.rate - priorRate) <= (10000.0f / 60.0f) + kEpsilon,
                    "once upZ reaches 0.75 the effective rate may approach 300 by at most the time-compressed official acceleration");
                Expect(std::fabs(result.rate - 300.0f) <= std::fabs(priorRate - 300.0f) + kEpsilon,
                    "once upZ reaches 0.75 the rate must approach 300 degrees per second");
            }
            sawHighUpZ = true;
        }
        priorRate = result.rate;
        hasPriorRate = true;
    }
    Expect(sawLowUpZ, "the roll trajectory must exercise the inverted-up rate cap");
    Expect(sawHighUpZ, "the roll trajectory must exercise the upright rate convergence");
    ExpectNear(maximumAirborneEffectiveRate, 600.0f,
        "the official airborne cap must be observable as 600 degrees per world second");

    PortalViewReorientation::Controller groundedCapController;
    Expect(groundedCapController.Arm(RollArm(5u, 179.8f)), "the grounded cap case must arm");
    bool sawGroundedLowUpZ = false;
    float maximumGroundedEffectiveRate = 0.0f;
    for (int frameCount = 1; frameCount <= 1000 && groundedCapController.IsActive(); ++frameCount)
    {
        const PortalViewReorientation::FrameResult result = groundedCapController.Advance(
            Frame(frameCount, 1.0f / 60.0f, true, groundedCapController.GetCurrentRoll()));
        if (result.upZ < 0.75f)
        {
            sawGroundedLowUpZ = true;
            maximumGroundedEffectiveRate =
                std::max(maximumGroundedEffectiveRate, result.rate);
            Expect(result.rate <= 1200.0f + kEpsilon,
                "the grounded rate must not exceed 1200 degrees per second while upZ is below 0.75");
        }
    }
    Expect(sawGroundedLowUpZ,
        "the grounded roll trajectory must exercise the inverted-up rate cap");
    ExpectNear(maximumGroundedEffectiveRate, 1200.0f,
        "the official grounded cap must be observable as 1200 degrees per world second");

    PortalViewReorientation::Controller uprightController;
    Expect(uprightController.Arm(RollArm(16u, 30.0f)),
        "the upright settled-rate case must arm");
    const PortalViewReorientation::FrameResult uprightFirst = uprightController.Advance(
        Frame(1, 1.0f / 60.0f, false, 30.0f));
    const PortalViewReorientation::FrameResult uprightSecond = uprightController.Advance(
        Frame(2, 1.0f / 60.0f, false, uprightFirst.angles.roll));
    ExpectNear(uprightFirst.rate, 166.666667f,
        "the upright branch must expose its first time-compressed acceleration step");
    ExpectNear(uprightSecond.rate, 300.0f,
        "the upright official settled rate must be observable as 300 degrees per world second");

    PortalViewReorientation::Controller exactZeroController;
    Expect(exactZeroController.Arm(RollArm(12u, 0.0f)),
        "exact zero remains a valid no-op arm request");
    Expect(!exactZeroController.IsActive(),
        "exact zero must complete immediately without an artificial active frame");
    Expect(exactZeroController.GetTransactionId() == 0u,
        "exact zero must not leave a dormant transaction identity");
    ExpectNear(exactZeroController.GetRate(), 0.0f,
        "exact zero must not create an artificial one-frame rate pulse");

    PortalViewReorientation::Controller duplicateController;
    Expect(duplicateController.Arm(RollArm(6u, -92.5f)), "the duplicate case must arm");
    const PortalViewReorientation::FrameResult original = duplicateController.Advance(
        Frame(20, 1.0f / 60.0f, false, -92.5f));
    const float rollBeforeDuplicate = duplicateController.GetCurrentRoll();
    const float rateBeforeDuplicate = duplicateController.GetRate();
    const PortalViewReorientation::FrameResult duplicate = duplicateController.Advance(
        Frame(20, 1.0f / 60.0f, false, original.angles.roll));
    Expect(duplicate.duplicateFrame, "an already-consumed frame count must be marked duplicate");
    ExpectNear(duplicate.angles.roll, original.angles.roll,
        "a duplicate frame must not change the roll");
    ExpectNear(duplicate.rate, original.rate, "a duplicate frame must not change the rate");
    ExpectNear(duplicateController.GetCurrentRoll(), rollBeforeDuplicate,
        "a duplicate frame must not mutate the controller roll state");
    ExpectNear(duplicateController.GetRate(), rateBeforeDuplicate,
        "a duplicate frame must not mutate the controller rate state");
    Expect(duplicateController.GetTransactionId() == 6u,
        "a duplicate frame must not change transaction identity");

    PortalViewReorientation::Controller liveAxesController;
    Expect(liveAxesController.Arm(RollArm(13u, 90.0f)),
        "the live-axis preservation case must arm");
    const PortalViewReorientation::FrameResult liveAxes = liveAxesController.Advance(
        Frame(25, 1.0f / 60.0f, false, 90.0f, 12.5f, -73.0f));
    ExpectNear(liveAxes.angles.pitch, 12.5f,
        "ordinary roll recovery must preserve live pitch");
    ExpectNear(liveAxes.angles.yaw, -73.0f,
        "ordinary roll recovery must preserve live yaw");

    PortalViewReorientation::Controller rearmController;
    Expect(rearmController.Arm(RollArm(7u, 179.8f)), "the re-arm case must initially arm");
    rearmController.Advance(Frame(30, 1.0f / 60.0f, false, 179.8f));
    const float activeRate = rearmController.GetRate();
    Expect(rearmController.Arm(RollArm(8u, -92.5f)),
        "re-arming while active must replace the active transaction");
    Expect(rearmController.GetTransactionId() == 8u,
        "re-arming must replace the transaction identity");
    ExpectNear(rearmController.GetCurrentRoll(), -92.5f,
        "re-arming must replace the canonical committed angles");
    ExpectNear(rearmController.GetRate(), activeRate,
        "re-arming while active must retain the official rate");

    for (int frameCount = 31; frameCount <= 5000 && rearmController.IsActive(); ++frameCount)
        rearmController.Advance(Frame(frameCount, 1.0f / 60.0f, false, rearmController.GetCurrentRoll()));
    Expect(!rearmController.IsActive(), "the re-armed transaction must complete");
    Expect(rearmController.GetTransactionId() == 0u,
        "completion must clear dormant transaction identity");
    ExpectNear(rearmController.GetRate(), 0.0f,
        "completion must clear dormant rate state");
    Expect(rearmController.Arm(RollArm(9u, 45.0f)),
        "a later transaction must arm after completion");
    ExpectNear(rearmController.GetRate(), 0.0f,
        "an arm after completion must begin from rate zero");

    rearmController.Advance(Frame(40, 1.0f / 60.0f, false, 45.0f));
    rearmController.Reset();
    Expect(!rearmController.IsActive(), "Reset must disable the active transaction");
    ExpectNear(rearmController.GetRate(), 0.0f, "Reset must clear the rate");
    Expect(rearmController.Arm(RollArm(10u, 45.0f)), "Reset must permit a new transaction");
    const PortalViewReorientation::FrameResult postReset = rearmController.Advance(
        Frame(40, 1.0f / 60.0f, false, 45.0f));
    Expect(!postReset.duplicateFrame, "Reset must clear frame identity");

    PortalViewReorientation::Controller invalidInputController;
    Expect(invalidInputController.Arm(RollArm(14u, 90.0f)),
        "the invalid-frame fail-safe case must arm");
    const float invalidRoll = invalidInputController.GetCurrentRoll();
    const float invalidRate = invalidInputController.GetRate();
    const PortalViewReorientation::FrameResult negativeTime = invalidInputController.Advance(
        Frame(50, -0.001f, false, invalidRoll));
    Expect(!negativeTime.advanced,
        "negative frame time must fail closed without advancing");
    const PortalViewReorientation::FrameResult nonFiniteTime = invalidInputController.Advance(
        Frame(51, std::numeric_limits<float>::infinity(), false, invalidRoll));
    Expect(!nonFiniteTime.advanced,
        "non-finite frame time must fail closed without advancing");
    PortalViewReorientation::FrameInput nonFiniteAngles =
        Frame(52, 1.0f / 60.0f, false, invalidRoll);
    nonFiniteAngles.liveAngles.yaw = std::numeric_limits<float>::quiet_NaN();
    Expect(!invalidInputController.Advance(nonFiniteAngles).advanced,
        "non-finite live angles must fail closed without advancing");
    ExpectNear(invalidInputController.GetCurrentRoll(), invalidRoll,
        "invalid frames must not mutate roll state");
    ExpectNear(invalidInputController.GetRate(), invalidRate,
        "invalid frames must not mutate rate state");
    Expect(invalidInputController.GetTransactionId() == 14u,
        "invalid frames must not mutate transaction identity");

    PortalViewReorientation::ArmRequest unsupportedMode = RollArm(15u, 90.0f);
    unsupportedMode.mode = static_cast<PortalViewReorientation::Mode>(1);
    PortalViewReorientation::Controller unsupportedModeController;
    Expect(!unsupportedModeController.Arm(unsupportedMode),
        "unsupported pitch-mode values must be rejected rather than approximated");
    Expect(!unsupportedModeController.IsActive(),
        "unsupported pitch mode must not create an active transaction");

    Expect(std::string(PortalViewReorientation::GetCurveName()) == "portal-sdk-2013",
        "lifecycle diagnostics must expose the selected official curve identity");
    ExpectNear(PortalViewReorientation::GetTimeScale(), 5.0f,
        "lifecycle diagnostics must expose the explicit five-times time scale");

    for (int disabledGate = 0; disabledGate < 5; ++disabledGate)
    {
        const bool featureEnabled = disabledGate != 0;
        const bool signatureValidated = disabledGate != 1;
        const bool abiValidated = disabledGate != 2;
        const bool localInputInstance = disabledGate != 3;
        const bool transactionActive = disabledGate != 4;
        Expect(!PortalViewReorientation::ShouldRestoreInputRoll(
                featureEnabled,
                signatureValidated,
                abiValidated,
                localInputInstance,
                transactionActive),
            "input-roll restoration must remain disabled when any gate is false");
    }
    Expect(PortalViewReorientation::ShouldRestoreInputRoll(true, true, true, true, true),
        "input-roll restoration must require all five gates");

    std::cout << "PortalViewReorientation tests passed\n";
    return 0;
}
