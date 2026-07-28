#include <cstdlib>
#include <iostream>

#include "../src/Portal/PortalNearViewRenderPolicy.h"

namespace
{
    void Expect(bool condition, const char* message)
    {
        if (condition)
            return;

        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using namespace PortalNearViewRenderPolicy;

    const FrameContext exitingNearClip{
        true,
        true,
        1.8264f,
        7.0f
    };
    Expect(ShouldClearDepthBeforeViewmodel(exitingNearClip),
        "a screen-edge proxy inside zNear must clear depth before the viewmodel");
    Expect(ShouldSuppressLegacyFrameRedraw(exitingNearClip),
        "a portal model inside zNear must not be redrawn as an unmasked frame");

    const FrameContext intersectingExtremeNear{
        true,
        true,
        0.0868f,
        1.0f
    };
    Expect(ShouldClearDepthBeforeViewmodel(intersectingExtremeNear),
        "an extreme-near proxy must clear its depth before the viewmodel");
    Expect(ShouldSuppressLegacyFrameRedraw(intersectingExtremeNear),
        "an extreme-near portal must suppress the legacy whole-model frame");

    const FrameContext ordinaryPortal{
        true,
        true,
        15.5f,
        1.0f
    };
    Expect(!ShouldClearDepthBeforeViewmodel(ordinaryPortal),
        "an ordinary portal beyond zNear must keep normal world depth");
    Expect(!ShouldSuppressLegacyFrameRedraw(ordinaryPortal),
        "an ordinary portal beyond zNear must keep its frame");

    const FrameContext noApertureParticipation{
        true,
        false,
        0.0868f,
        1.0f
    };
    Expect(!ShouldClearDepthBeforeViewmodel(noApertureParticipation),
        "depth must not be cleared when no near aperture participated");
    Expect(!ShouldSuppressLegacyFrameRedraw(noApertureParticipation),
        "the frame must not be hidden when the portal aperture was not processed");

    const FrameContext disabled{
        false,
        true,
        0.0868f,
        1.0f
    };
    Expect(!ShouldClearDepthBeforeViewmodel(disabled),
        "the viewmodel depth repair must support a disabled A/B branch");
    Expect(!ShouldSuppressLegacyFrameRedraw(disabled),
        "the near-frame guard must support a disabled A/B branch");

    std::cout << "Portal near-view render policy tests passed\n";
    return 0;
}
