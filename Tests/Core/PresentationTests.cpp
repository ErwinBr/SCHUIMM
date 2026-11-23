#include "TestFramework.h"

#include "Graphics/Interface/Presentation.h"

using namespace schuimm;

SCHUIMM_TEST(Presentation_ExactFitUsesWholeFramebuffer)
{
    CHECK((computePresentationRect({640, 360}, {1280, 720}, false) == PixelRect{0, 0, 1280, 720}));
}

SCHUIMM_TEST(Presentation_PillarboxesWideFramebuffer)
{
    // 4:3 content in a 16:9 window: bars left and right.
    CHECK((computePresentationRect({320, 240}, {1920, 1080}, false) == PixelRect{240, 0, 1440, 1080}));
}

SCHUIMM_TEST(Presentation_LetterboxesTallFramebuffer)
{
    CHECK((computePresentationRect({640, 360}, {1000, 1000}, false) == PixelRect{0, 218, 1000, 563}));
}

SCHUIMM_TEST(Presentation_IntegerScaling)
{
    // 1920/640 = 3 exactly; 1000x700 would allow 1.5625 but integer scaling picks 1.
    CHECK((computePresentationRect({640, 360}, {1920, 1080}, true) == PixelRect{0, 0, 1920, 1080}));
    CHECK((computePresentationRect({640, 360}, {1000, 700}, true) == PixelRect{180, 170, 640, 360}));
    // Smaller than the virtual resolution: integer scaling cannot apply, fall back to fractional.
    CHECK((computePresentationRect({640, 360}, {320, 180}, true) == PixelRect{0, 0, 320, 180}));
}

SCHUIMM_TEST(Presentation_EmptyInputsGiveEmptyRect)
{
    CHECK((computePresentationRect({640, 360}, {0, 0}, false) == PixelRect{}));
    CHECK((computePresentationRect({0, 0}, {100, 100}, false) == PixelRect{}));
}

SCHUIMM_TEST(Presentation_WindowToVirtualHandlesHighDpiAndBars)
{
    // Retina: 960x540 points, 1920x1080 pixels, 640x360 virtual.
    const Vec2 centre = windowToVirtual({480.0f, 270.0f}, {960, 540}, {1920, 1080}, {640, 360}, false);
    CHECK_NEAR(centre.x, 320.0, 1e-4);
    CHECK_NEAR(centre.y, 180.0, 1e-4);

    // Pillarboxed: the left bar maps to negative x.
    const Vec2 inBar = windowToVirtual({10.0f, 540.0f}, {1920, 1080}, {1920, 1080}, {320, 240}, false);
    CHECK(inBar.x < 0.0f);
    const Vec2 topLeft = windowToVirtual({240.0f, 0.0f}, {1920, 1080}, {1920, 1080}, {320, 240}, false);
    CHECK_NEAR(topLeft.x, 0.0, 1e-4);
    CHECK_NEAR(topLeft.y, 0.0, 1e-4);
}
