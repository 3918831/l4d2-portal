#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/Portal/PortalRenderCadenceTracker.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }

    void ExpectNear(float actual, float expected, const char* message)
    {
        Expect(std::fabs(actual - expected) < 0.001f, message);
    }

    PortalRenderCadence::Sample MakeSample(
        int frame,
        int tick,
        int command,
        float rawX,
        float originalX,
        float appliedX)
    {
        PortalRenderCadence::Sample sample;
        sample.frameCount = frame;
        sample.tickCount = tick;
        sample.commandNumber = command;
        sample.frameTime = 0.002f;
        sample.rawEye = { rawX, 0.0f, 64.0f };
        sample.originalEye = { originalX, 0.0f, 64.0f };
        sample.appliedEye = { appliedX, 0.0f, 64.0f };
        sample.calcViewInputEye = sample.originalEye;
        sample.finalViewEye = sample.appliedEye;
        sample.playerOrigin = { appliedX, 0.0f, 0.0f };
        sample.velocity = { 600.0f, 0.0f, 0.0f };
        return sample;
    }
}

int main()
{
    PortalRenderCadence::Tracker tracker;
    const PortalRenderCadence::BeginResult begin = tracker.BeginTransaction(
        7u,
        42,
        { 1.0f, 0.0f, 0.0f });
    Expect(begin.accepted, "a valid traversal must arm cadence capture");

    const PortalRenderCadence::RecordResult first = tracker.RecordFrame(
        MakeSample(100, 200, 42, 0.0f, 0.0f, 0.0f));
    Expect(first.accepted && !first.hasPrevious,
        "the first post-commit render frame must establish the cadence baseline");

    const PortalRenderCadence::RecordResult frozen = tracker.RecordFrame(
        MakeSample(101, 200, 42, 0.0f, 0.0f, 0.0f));
    Expect(frozen.accepted && frozen.hasPrevious,
        "the next unique render frame must be compared with the baseline");
    Expect(frozen.rawFrozen && frozen.originalFrozen && frozen.appliedFrozen
            && frozen.originFrozen,
        "unchanged eyes and entity origin inside one command must be classified as frozen");
    Expect(frozen.appliedConsecutiveFrozenFrames == 1,
        "the first frozen frame must begin the applied-eye freeze run");

    const PortalRenderCadence::RecordResult duplicate = tracker.RecordFrame(
        MakeSample(101, 200, 42, 1.0f, 1.0f, 1.0f));
    Expect(!duplicate.accepted,
        "duplicate CalcPlayerView calls in one render frame must not skew cadence");

    const PortalRenderCadence::RecordResult firstTick = tracker.RecordFrame(
        MakeSample(102, 201, 43, 20.0f, 5.0f, 10.0f));
    Expect(firstTick.commandAdvanced && firstTick.tickAdvanced,
        "a new prediction command and tick must be visible at the render seam");
    ExpectNear(firstTick.rawDeltaLength, 20.0f,
        "the raw-eye tick jump must be measured independently");
    ExpectNear(firstTick.originalDeltaLength, 5.0f,
        "native CalcPlayerView motion must be measured independently");
    ExpectNear(firstTick.appliedDeltaLength, 10.0f,
        "the actually rendered eye motion must be measured independently");
    ExpectNear(firstTick.originDeltaLength, 10.0f,
        "entity-origin motion must be measured independently at render cadence");
    ExpectNear(firstTick.appliedExitNormalDelta, 10.0f,
        "render motion must be projected onto the exit normal");
    ExpectNear(firstTick.appliedExitTangentDelta, 0.0f,
        "normal-only motion must have no exit-tangent component");

    tracker.RecordFrame(MakeSample(103, 201, 43, 20.0f, 5.0f, 10.0f));
    const PortalRenderCadence::RecordResult secondTransition = tracker.RecordFrame(
        MakeSample(104, 202, 44, 40.0f, 25.0f, 30.0f));
    Expect(!secondTransition.completed,
        "diagnostic capture must remain active beyond the old two-transition window");
    tracker.RecordFrame(MakeSample(105, 203, 45, 60.0f, 45.0f, 50.0f));
    tracker.RecordFrame(MakeSample(106, 204, 46, 80.0f, 65.0f, 70.0f));
    tracker.RecordFrame(MakeSample(107, 205, 47, 100.0f, 85.0f, 90.0f));
    const PortalRenderCadence::RecordResult completed = tracker.RecordFrame(
        MakeSample(108, 206, 48, 120.0f, 105.0f, 110.0f));
    Expect(completed.completed,
        "capture must span six post-commit command transitions");
    Expect(completed.summary.frameCount == 9,
        "duplicate calls must be excluded from the transaction summary");
    Expect(completed.summary.commandTransitions == 6,
        "the summary must count prediction command transitions");
    Expect(completed.summary.tickTransitions == 6,
        "the summary must count engine tick transitions");
    Expect(completed.summary.appliedFrozenFrames == 2,
        "the summary must count frozen render frames across command intervals");
    Expect(completed.summary.maxAppliedConsecutiveFrozenFrames == 1,
        "the summary must retain the longest applied-eye freeze run");
    ExpectNear(completed.summary.maxRawDeltaLength, 20.0f,
        "the summary must retain the largest raw-eye tick jump");
    ExpectNear(completed.summary.maxAppliedDeltaLength, 20.0f,
        "the summary must retain the largest rendered-eye jump");
    Expect(completed.summary.originFrozenFrames == 2,
        "the summary must reveal whether entity origin is tick-frozen");
    ExpectNear(completed.summary.maxOriginDeltaLength, 20.0f,
        "the summary must retain the largest entity-origin jump");
    Expect(!tracker.IsActive(), "completed capture must disarm itself");

    PortalRenderCadence::Tracker speedTracker;
    Expect(speedTracker.BeginTransaction(
        9u,
        70,
        { 1.0f, 0.0f, 0.0f }).accepted,
        "speed continuity diagnostics must arm for a valid traversal");
    PortalRenderCadence::Sample speedBaseline =
        MakeSample(300, 400, 70, 0.0f, 0.0f, 0.0f);
    speedBaseline.eyeHistoryActive = true;
    speedBaseline.shadowPredictorApplied = true;
    speedTracker.RecordFrame(speedBaseline);

    PortalRenderCadence::Sample slowHistory =
        MakeSample(301, 400, 70, 0.12f, 0.12f, 0.12f);
    slowHistory.eyeHistoryActive = true;
    slowHistory.shadowPredictorApplied = true;
    const PortalRenderCadence::RecordResult historySpeed =
        speedTracker.RecordFrame(slowHistory);
    ExpectNear(historySpeed.expectedFrameDeltaLength, 1.2f,
        "expected render displacement must use velocity times render frame time");
    ExpectNear(historySpeed.finalViewVelocityProjectedDelta, 0.12f,
        "final view displacement must be projected onto current velocity");
    ExpectNear(historySpeed.finalViewSpeedRatio, 0.1f,
        "the diagnostic must expose the rendered-to-expected speed ratio");
    Expect(historySpeed.finalViewUnderSpeed,
        "a final view moving below half of physical render speed must begin an under-speed run");
    Expect(historySpeed.finalViewUnderSpeedChanged,
        "the first under-speed frame must be emitted as a diagnostic edge");
    Expect(!historySpeed.postEyeHistory,
        "active eye history must not be classified as the post-release window");

    PortalRenderCadence::Sample slowRelease =
        MakeSample(302, 401, 71, 0.24f, 0.24f, 0.24f);
    slowRelease.eyeHistoryReleased = true;
    const PortalRenderCadence::RecordResult releaseSpeed =
        speedTracker.RecordFrame(slowRelease);
    Expect(releaseSpeed.eyeHistoryChanged,
        "eye-history release must be observable as a state edge");
    Expect(releaseSpeed.shadowPredictorChanged,
        "shadow-predictor release must be observable as a state edge");
    Expect(releaseSpeed.postEyeHistory,
        "the release frame must enter the post-eye-history diagnostic window");
    Expect(releaseSpeed.summary.eyeHistoryUnderSpeedFrames == 1,
        "the summary must separate under-speed frames while eye history is active");
    Expect(releaseSpeed.summary.postEyeHistoryUnderSpeedFrames == 1,
        "the summary must count under-speed frames after eye-history release");
    Expect(releaseSpeed.summary.maxFinalViewUnderSpeedRun == 2,
        "the summary must preserve a low-speed run across the release boundary");

    PortalRenderCadence::Sample recovered =
        MakeSample(303, 401, 71, 1.44f, 1.44f, 1.44f);
    const PortalRenderCadence::RecordResult recoveredSpeed =
        speedTracker.RecordFrame(recovered);
    ExpectNear(recoveredSpeed.finalViewSpeedRatio, 1.0f,
        "a view advancing by the physical render step must report unit speed ratio");
    Expect(!recoveredSpeed.finalViewUnderSpeed
            && recoveredSpeed.finalViewUnderSpeedChanged,
        "recovery from a low-speed band must emit its closing diagnostic edge");

    PortalRenderCadence::Tracker sourceTracker;
    Expect(sourceTracker.BeginTransaction(
        8u,
        50,
        { 1.0f, 0.0f, 0.0f }).accepted,
        "a second traversal must arm independent-source cadence capture");

    PortalRenderCadence::Sample sourceBaseline =
        MakeSample(200, 300, 50, 0.0f, 0.0f, 0.0f);
    sourceBaseline.calcViewInputEye = { -1.0f, 0.0f, 64.0f };
    sourceBaseline.finalViewEye = { 0.0f, 0.0f, 64.0f };
    sourceBaseline.renderOrigin = { 0.0f, 0.0f, 0.0f };
    sourceBaseline.networkOrigin = { 0.0f, 0.0f, 0.0f };
    sourceBaseline.oldOrigin = { 0.0f, 0.0f, 0.0f };
    sourceBaseline.simulationTime = 10.0f;
    sourceTracker.RecordFrame(sourceBaseline);

    PortalRenderCadence::Sample renderContinuous =
        MakeSample(201, 300, 50, 0.0f, 0.0f, 0.25f);
    renderContinuous.calcViewInputEye = { -0.75f, 0.0f, 64.0f };
    renderContinuous.finalViewEye = { 0.5f, 0.0f, 64.0f };
    renderContinuous.playerOrigin = { 0.0f, 0.0f, 0.0f };
    renderContinuous.renderOrigin = { 0.1f, 0.0f, 0.0f };
    renderContinuous.networkOrigin = { 0.0f, 0.0f, 0.0f };
    renderContinuous.oldOrigin = { 0.0f, 0.0f, 0.0f };
    renderContinuous.simulationTime = 10.0f;
    const PortalRenderCadence::RecordResult continuous =
        sourceTracker.RecordFrame(renderContinuous);
    ExpectNear(continuous.calcViewInputDeltaLength, 0.25f,
        "CalcPlayerView input motion must be measured independently");
    ExpectNear(continuous.finalViewDeltaLength, 0.5f,
        "final CViewSetup motion must be measured independently");
    ExpectNear(continuous.renderOriginDeltaLength, 0.1f,
        "render-origin motion must remain distinguishable from abs origin");
    Expect(continuous.originFrozen && !continuous.renderOriginFrozen,
        "a continuous render origin must be visible while abs origin is frozen");
    Expect(continuous.networkOriginFrozen && continuous.oldOriginFrozen
            && continuous.simulationTimeFrozen,
        "network, old-origin, and simulation-time cadence must be classified independently");

    PortalRenderCadence::Sample sourceTick =
        MakeSample(202, 301, 51, 20.0f, 20.0f, 20.0f);
    sourceTick.calcViewInputEye = { 19.0f, 0.0f, 64.0f };
    sourceTick.finalViewEye = { 20.5f, 0.0f, 64.0f };
    sourceTick.playerOrigin = { 20.0f, 0.0f, 0.0f };
    sourceTick.renderOrigin = { 1.0f, 0.0f, 0.0f };
    sourceTick.networkOrigin = { 20.0f, 0.0f, 0.0f };
    sourceTick.oldOrigin = { -3.0f, 0.0f, 0.0f };
    sourceTick.simulationTime = 10.033333f;
    const PortalRenderCadence::RecordResult sourceStep =
        sourceTracker.RecordFrame(sourceTick);
    ExpectNear(sourceStep.originDeltaLength, 20.0f,
        "abs-origin tick motion must retain its own measurement");
    ExpectNear(sourceStep.renderOriginDeltaLength, 0.9f,
        "render origin must expose whether it advances between abs-origin ticks");
    ExpectNear(sourceStep.networkOriginDeltaLength, 20.0f,
        "network-origin tick motion must be measured independently");
    ExpectNear(sourceStep.oldOriginDeltaLength, 3.0f,
        "old-origin motion must be measured independently");
    Expect(!sourceStep.simulationTimeFrozen,
        "a simulation-time advance must be observable at the render seam");

    std::cout << "PortalRenderCadence tracker tests passed\n";
    return 0;
}
