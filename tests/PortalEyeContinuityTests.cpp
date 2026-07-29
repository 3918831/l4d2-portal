#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../src/Portal/PortalEyeContinuity.h"

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

    matrix3x4_t Translation(float x, float y, float z)
    {
        return matrix3x4_t(
            1.0f, 0.0f, 0.0f, x,
            0.0f, 1.0f, 0.0f, y,
            0.0f, 0.0f, 1.0f, z);
    }
}

int main()
{
    PortalEyeContinuity::State state;

    const PortalEyeContinuity::UpdateResult primed = state.Update(
        true,
        Vector(10.0f, 2.0f, 64.0f),
        Vector(8.0f, 4.0f, 64.0f),
        0.01f);
    Expect(!primed.overrideOriginalView, "ordinary frames must keep the engine camera before a teleport");
    Expect(!primed.initializedBefore && primed.initializedAfter,
        "the first update must expose eye-history initialization");
    ExpectNear(primed.previousRawEyeAfter.x, 10.0f,
        "the initialized raw history must be observable");
    ExpectNear(primed.continuityEyeAfter.x, 8.0f,
        "the initialized continuity history must be observable");

    Expect(
        state.PublishTeleport(
            Translation(100.0f, 0.0f, 0.0f),
            Vector(100.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            7u,
            42),
        "a new traversal epoch must publish one pending eye-history transform");
    Expect(
        !state.PublishTeleport(
            Translation(100.0f, 0.0f, 0.0f),
            Vector(100.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            7u,
            42),
        "the same traversal epoch must not publish the transform twice");

    const PortalEyeContinuity::UpdateResult consumed = state.Update(
        true,
        Vector(115.0f, 20.0f, 70.0f),
        Vector(100.25f, 10.0f, 66.0f),
        0.01f);
    Expect(consumed.consumedPendingTeleport, "the first post-teleport view must consume the pending matrix");
    Expect(consumed.hadPendingTeleport,
        "the update trace must report that pending state existed on entry");
    Expect(consumed.interpolatingAfter,
        "the consumed teleport must expose its active interpolation state");
    ExpectNear(consumed.previousRawEyeBefore.x, 10.0f,
        "the trace must preserve the entry-space raw history");
    ExpectNear(consumed.continuityEyeBefore.x, 8.0f,
        "the trace must preserve the entry-space continuity history");
    ExpectNear(consumed.previousRawEyeAfterTransform.x, 110.0f,
        "the trace must expose the transformed raw history before free movement");
    ExpectNear(consumed.continuityEyeAfterTransform.x, 108.0f,
        "the trace must expose the transformed continuity history before following");
    ExpectNear(consumed.previousRawEyeAfter.x, 115.0f,
        "the trace must expose the current exit-space raw history");
    Expect(consumed.planeConstrained,
        "the first post-commit update must constrain the camera to the engine exit plane");
    Expect(!consumed.safetyFallbackToOriginal,
        "a valid exit-side engine eye must not trigger the safety fallback");
    ExpectNear(consumed.freeMovementDelta.x, 5.0f,
        "the raw first-frame delta must remain observable for diagnostics");
    ExpectNear(consumed.engineViewMovementDelta.x, -7.75f,
        "the transformed engine-view delta must remain observable");
    ExpectNear(consumed.normalCorrection, -7.75f,
        "the normal correction must align transformed history with the engine exit depth");
    ExpectNear(consumed.originalExitDepth, 0.25f,
        "the engine view must be measured against the exit plane");
    ExpectNear(consumed.outputExitDepth, 0.25f,
        "the corrected camera must remain on the same safe exit depth");
    ExpectNear(consumed.continuityEyeAfter.x, 100.25f,
        "the exit-normal component must immediately follow the engine camera");
    ExpectNear(consumed.continuityEyeAfter.y, 4.948683f,
        "only the portal-tangent residual should receive the follow step");
    ExpectNear(consumed.continuityEyeAfter.z, 64.316228f,
        "the portal-up residual should remain smooth");
    Expect(consumed.overrideOriginalView, "the corrected eye must override the stale engine view");
    Expect(consumed.traversalEpoch == 7u, "the consumed update must retain the traversal epoch");
    Expect(consumed.commandNumber == 42, "the consumed update must retain the teleport command");
    ExpectNear(
        consumed.eyeOrigin.x,
        100.25f,
        "the constrained result must never remain behind the engine exit depth");

    const PortalEyeContinuity::UpdateResult held = state.Update(
        true,
        Vector(117.0f, 22.0f, 71.0f),
        Vector(102.25f, 12.0f, 67.0f),
        0.01f);
    Expect(!held.consumedPendingTeleport, "later views must not apply the portal matrix again");
    Expect(held.overrideOriginalView, "the corrected eye must remain active while the engine view is stale");
    ExpectNear(held.engineViewMovementDelta.x, 2.0f,
        "later frames must carry the engine camera's free movement");
    ExpectNear(held.eyeOrigin.x, 102.25f,
        "later movement must preserve the engine exit-normal coordinate");

    const PortalEyeContinuity::UpdateResult released = state.Update(
        true,
        Vector(117.0f, 22.0f, 71.0f),
        Vector(102.25f, 12.0f, 67.0f),
        1.0f);
    Expect(released.releasedOverride, "the adapter must release after the engine camera catches up");
    Expect(!released.overrideOriginalView, "release must return ownership to the engine camera");
    ExpectNear(released.eyeOrigin.x, 102.25f, "release must not introduce a final eye-position jump");

    Expect(
        state.PublishTeleport(
            Translation(50.0f, 0.0f, 0.0f),
            Vector(150.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            8u,
            55),
        "a later traversal epoch must be accepted");
    const PortalEyeContinuity::UpdateResult disabled = state.Update(
        false,
        Vector(170.0f, 0.0f, 64.0f),
        Vector(150.0f, 0.0f, 64.0f),
        0.01f);
    Expect(!disabled.overrideOriginalView, "the A/B off path must preserve the original engine view");
    ExpectNear(disabled.eyeOrigin.x, 150.0f, "the A/B off path must not modify the eye origin");

    PortalEyeContinuity::State unsafeState;
    unsafeState.Update(
        true,
        Vector(10.0f, 0.0f, 64.0f),
        Vector(8.0f, 0.0f, 64.0f),
        0.01f);
    Expect(
        unsafeState.PublishTeleport(
            Translation(100.0f, 0.0f, 0.0f),
            Vector(100.0f, 0.0f, 0.0f),
            Vector(1.0f, 0.0f, 0.0f),
            9u,
            60),
        "the safety case must publish a teleport");
    const PortalEyeContinuity::UpdateResult unsafe = unsafeState.Update(
        true,
        Vector(99.0f, 0.0f, 64.0f),
        Vector(99.75f, 0.0f, 64.0f),
        0.01f);
    Expect(unsafe.safetyFallbackToOriginal,
        "an engine eye behind the exit plane must disable the custom camera");
    Expect(!unsafe.overrideOriginalView,
        "the unsafe case must return camera ownership to the engine");
    ExpectNear(unsafe.eyeOrigin.x, 99.75f,
        "the unsafe case must preserve the baseline engine view");

    std::cout << "PortalEyeContinuity tests passed\n";
    return 0;
}
