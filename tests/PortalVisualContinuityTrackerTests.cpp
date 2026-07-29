#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/Portal/PortalVisualContinuityTracker.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }

    void ExpectNear(float actual, float expected, const char* message)
    {
        if (std::fabs(actual - expected) > 0.001f)
        {
            std::cerr << "FAIL: " << message
                << " expected=" << expected
                << " actual=" << actual << '\n';
            std::exit(1);
        }
    }

    PortalVisualContinuity::Sample MakeEyeSample(
        float eyeX,
        float yaw,
        int commandNumber,
        int frameCount)
    {
        PortalVisualContinuity::Sample sample;
        sample.hasEyeOrigin = true;
        sample.eyeOrigin = { eyeX, 0.0f, 64.0f };
        sample.hasEyeAngles = true;
        sample.eyeAngles = { 0.0f, yaw, 0.0f };
        sample.commandNumber = commandNumber;
        sample.frameCount = frameCount;
        return sample;
    }
}

int main()
{
    using namespace PortalVisualContinuity;

    Tracker tracker;

    Sample handoff = MakeEyeSample(10.0f, 359.0f, 41, 100);
    handoff.engineTime = 1.000f;
    handoff.eyeSemantics = SampleSemantics::ExitViewSpace;
    handoff.angleSemantics = SampleSemantics::ExitViewSpace;
    handoff.playerSemantics = SampleSemantics::EntryPhysical;
    tracker.CacheEntryHandoff(1, 7, handoff);

    Sample commit = MakeEyeSample(13.0f, 1.0f, 41, 101);
    commit.engineTime = 1.025f;
    commit.eyeSemantics = SampleSemantics::ExitViewSpace;
    commit.angleSemantics = SampleSemantics::ExitViewSpace;
    commit.hasPlayerOrigin = true;
    commit.playerOrigin = { 13.0f, 0.0f, 0.0f };
    commit.hasVelocity = true;
    commit.velocity = { 200.0f, 0.0f, 0.0f };
    commit.playerSemantics = SampleSemantics::ExitPhysical;
    commit.velocitySemantics = SampleSemantics::ExitPhysical;
    const BeginResult first = tracker.BeginTransaction(1, 7, 41, commit);
    Expect(first.accepted, "a successful commit starts a continuity transaction");
    Expect(first.transactionId == 1, "the first transaction receives id 1");
    Expect(first.hasEntryHandoff, "the matching last entry handoff is bound to the commit");
    Expect(first.commit.accepted && first.commit.hasPrevious,
        "the commit is compared with the last entry handoff");
    ExpectNear(first.commit.delta.eyePosition, 3.0f,
        "handoff-to-commit eye position delta is measured");
    ExpectNear(first.commit.delta.eyeAngle, 2.0f,
        "angle delta follows the shortest path across 360 degrees");

    Sample prediction = MakeEyeSample(13.0f, 1.0f, 41, 101);
    prediction.hasPlayerOrigin = true;
    prediction.playerOrigin = { 13.5f, 0.0f, 0.0f };
    prediction.hasVelocity = true;
    prediction.velocity = { 250.0f, 0.0f, 0.0f };
    prediction.hasEyeOrigin = false;
    prediction.eyeSemantics = SampleSemantics::None;
    prediction.angleSemantics = SampleSemantics::ExitViewSpace;
    prediction.playerSemantics = SampleSemantics::ExitPhysical;
    prediction.velocitySemantics = SampleSemantics::ExitPhysical;
    Expect(!tracker.RecordPredictionSync(40, prediction).accepted,
        "prediction from a different command cannot join the transaction");
    const AppendResult predictionResult =
        tracker.RecordPredictionSync(41, prediction);
    Expect(predictionResult.accepted,
        "prediction from the committed command joins the transaction");
    ExpectNear(predictionResult.delta.playerPosition, 0.5f,
        "commit target and observed prediction are comparable in exit physical space");
    ExpectNear(predictionResult.delta.velocity, 50.0f,
        "commit and prediction velocity changes remain visible across provenance");
    Expect(!tracker.RecordPredictionSync(41, prediction).accepted,
        "only the first committed prediction synchronization is recorded");

    AppendResult finalRender;
    for (int index = 0; index < Tracker::kMainViewSampleCount; ++index)
    {
        Sample render = MakeEyeSample(
            14.0f + static_cast<float>(index),
            1.0f,
            0,
            102 + index);
        render.hasFov = true;
        render.fov = 90.0f;
        render.eyeSemantics = SampleSemantics::ExitViewSpace;
        render.angleSemantics = SampleSemantics::ExitViewSpace;
        render.hasPlayerOrigin = true;
        render.playerOrigin = { 14.0f + static_cast<float>(index), 0.0f, 0.0f };
        render.hasVelocity = true;
        render.velocity = { 245.0f, 0.0f, 0.0f };
        render.hasPhysicalEye = true;
        render.physicalEye = {
            15.5f + static_cast<float>(index),
            0.0f,
            64.0f
        };
        render.playerSemantics = SampleSemantics::ExitPhysical;
        render.velocitySemantics = SampleSemantics::ExitPhysical;
        render.physicalEyeSemantics = SampleSemantics::ExitPhysical;

        const AppendResult result = tracker.RecordMainViewPreRender(render);
        finalRender = result;
        Expect(result.accepted, "each bounded post-render sample is accepted");
        Expect(result.current.renderIndex == index,
            "post-render samples receive a stable zero-based index");
        Expect(result.completed == (index == Tracker::kMainViewSampleCount - 1),
            "the transaction completes exactly on the fifth main-view sample");
        if (index == 0)
        {
            ExpectNear(result.delta.eyePosition, 1.0f,
                "the first main view compares with commit across an eye-less prediction event");
            Expect(result.delta.eyePositionFromStage == Stage::TeleportCommit,
                "eye delta source skips samples that did not observe an eye");
            ExpectNear(result.delta.playerPosition, 0.5f,
                "prediction and client entity positions compare in exit physical space");
            Expect(result.delta.playerPositionFromStage == Stage::PredictionSync,
                "physical delta preserves provenance through its source stage");
        }
    }

    ExpectNear(finalRender.maximum.eyePosition, 3.0f,
        "the summary retains the largest eye-position discontinuity");
    Expect(finalRender.maximum.hasViewToPhysicalEyeResidual,
        "the summary must distinguish final-view residual from temporal motion");
    ExpectNear(finalRender.maximum.viewToPhysicalEyeResidual, 1.5f,
        "the summary measures each final view against its current physical eye");
    Expect(
        finalRender.maximum.viewToPhysicalEyeResidualStage == Stage::MainViewPreRender,
        "the final-view residual preserves its source stage");
    Expect(
        finalRender.maximum.eyePositionFromStage == Stage::EntryHandoff
            && finalRender.maximum.eyePositionToStage == Stage::TeleportCommit,
        "the summary identifies the exact stage pair that produced the maximum");
    Expect(!tracker.IsActive(), "the tracker closes after the bounded render window");
    Expect(!tracker.RecordMainViewPreRender(MakeEyeSample(99.0f, 1.0f, 0, 200)).accepted,
        "render samples after completion are rejected");

    tracker.CacheEntryHandoff(1, 8, MakeEyeSample(20.0f, 10.0f, 50, 300));
    const BeginResult wrongSide =
        tracker.BeginTransaction(2, 8, 50, MakeEyeSample(21.0f, 10.0f, 50, 301));
    Expect(wrongSide.accepted && wrongSide.transactionId == 2,
        "each committed traversal receives a monotonically increasing id");
    Expect(!wrongSide.hasEntryHandoff,
        "a handoff from the other portal cannot be attached to the transaction");

    tracker.ResetCapture();
    Sample staleEpochHandoff = MakeEyeSample(30.0f, 20.0f, 60, 400);
    staleEpochHandoff.engineTime = 4.000f;
    tracker.CacheEntryHandoff(2, 9, staleEpochHandoff);
    Sample newEpochCommit = MakeEyeSample(31.0f, 20.0f, 60, 401);
    newEpochCommit.engineTime = 4.010f;
    const BeginResult afterReset =
        tracker.BeginTransaction(2, 10, 60, newEpochCommit);
    Expect(afterReset.transactionId == 3,
        "capture reset preserves transaction id uniqueness for the current log session");
    Expect(!afterReset.hasEntryHandoff
            && afterReset.handoffBinding == HandoffBinding::EpochMismatch,
        "an aborted same-side approach cannot bind its handoff to a later traversal epoch");

    tracker.ResetCapture();
    Sample staleTimeHandoff = MakeEyeSample(40.0f, 30.0f, 70, 500);
    staleTimeHandoff.engineTime = 5.000f;
    tracker.CacheEntryHandoff(1, 11, staleTimeHandoff);
    Sample staleTimeCommit = MakeEyeSample(41.0f, 30.0f, 70, 520);
    staleTimeCommit.engineTime = 5.500f;
    const BeginResult staleTime =
        tracker.BeginTransaction(1, 11, 70, staleTimeCommit);
    Expect(!staleTime.hasEntryHandoff
            && staleTime.handoffBinding == HandoffBinding::Stale,
        "an old handoff in the same traversal epoch fails closed");

    const BeginResult superseding =
        tracker.BeginTransaction(2, 12, 71, MakeEyeSample(50.0f, 40.0f, 71, 521));
    Expect(superseding.supersededTransactionId == staleTime.transactionId,
        "a new commit reports the incomplete transaction it superseded");

    Sample firstFrame = MakeEyeSample(51.0f, 40.0f, 71, 522);
    firstFrame.eyeSemantics = SampleSemantics::ExitViewSpace;
    Expect(tracker.RecordMainViewPreRender(firstFrame).accepted,
        "the first unique main-view frame is accepted");
    Expect(!tracker.RecordMainViewPreRender(firstFrame).accepted,
        "a repeated render callback in the same engine frame is rejected");

    tracker.ResetCapture();
    Sample zeroCommit = MakeEyeSample(60.0f, 45.0f, 80, 600);
    zeroCommit.eyeSemantics = SampleSemantics::ExitViewSpace;
    zeroCommit.angleSemantics = SampleSemantics::ExitViewSpace;
    tracker.BeginTransaction(1, 13, 80, zeroCommit);
    Sample zeroRender = MakeEyeSample(60.0f, 45.0f, 80, 601);
    zeroRender.eyeSemantics = SampleSemantics::ExitViewSpace;
    zeroRender.angleSemantics = SampleSemantics::ExitViewSpace;
    const AppendResult zeroResult =
        tracker.RecordMainViewPreRender(zeroRender);
    Expect(zeroResult.maximum.hasEyePosition,
        "a zero eye-position delta is still a comparable observation");
    Expect(zeroResult.maximum.eyePositionFromStage == Stage::TeleportCommit
            && zeroResult.maximum.eyePositionToStage == Stage::MainViewPreRender,
        "a perfectly continuous pair is distinct from no comparable samples");

    const AbortResult resetAbort = tracker.ResetCapture();
    Expect(resetAbort.aborted && resetAbort.transactionId == zeroResult.transactionId,
        "reset reports the active transaction it aborts");
    Expect(!tracker.IsActive(), "reset closes the active transaction");

    std::cout << "Portal visual continuity tracker tests passed\n";
    return 0;
}
