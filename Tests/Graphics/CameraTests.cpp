#include "Graphics/Common/Camera2D.h"
#include "TestFramework.h"

#include <cmath>

using namespace schuimm;

namespace {
void checkPoint(Vec2 actual, Vec2 expected, double tolerance = 1e-3)
{
    CHECK_NEAR(actual.x, expected.x, tolerance);
    CHECK_NEAR(actual.y, expected.y, tolerance);
}
} // namespace

SCHUIMM_TEST(Camera_CentresPositionInViewport)
{
    Camera2D camera({640, 360});
    camera.setPosition({1000, 500});
    checkPoint(camera.worldToScreen({1000, 500}), {320, 180});
    checkPoint(camera.worldToScreen({1010, 480}), {330, 160});
    const RectF visible = camera.visibleWorldRect();
    CHECK_NEAR(visible.x, 680, 1e-3);
    CHECK_NEAR(visible.y, 320, 1e-3);
    CHECK_NEAR(visible.width, 640, 1e-3);
    CHECK_NEAR(visible.height, 360, 1e-3);
}

SCHUIMM_TEST(Camera_ScreenToWorldInvertsWorldToScreen)
{
    Camera2D camera({640, 360});
    camera.setPosition({-37.5f, 812.25f});
    camera.setZoom(2.75f);
    for (const Vec2 p : {Vec2{0, 0}, Vec2{640, 360}, Vec2{123.5f, 17.0f}}) {
        checkPoint(camera.worldToScreen(camera.screenToWorld(p)), p);
    }
    checkPoint(camera.screenToWorld({320, 180}), {-37.5f, 812.25f});
}

SCHUIMM_TEST(Camera_ZoomScalesAroundCentre)
{
    Camera2D camera({640, 360});
    camera.setPosition({100, 100});
    camera.setZoom(2.0f);
    checkPoint(camera.worldToScreen({110, 100}), {340, 180});
    CHECK_NEAR(camera.visibleWorldRect().width, 320, 1e-3);
    camera.setZoomLimits(0.5f, 4.0f);
    camera.setZoom(10.0f);
    CHECK_NEAR(camera.zoom(), 4.0, 1e-6);
    camera.setZoom(0.1f);
    CHECK_NEAR(camera.zoom(), 0.5, 1e-6);
}

SCHUIMM_TEST(Camera_MovementAndViewportChanges)
{
    Camera2D camera({640, 360});
    camera.move({10, -5});
    camera.move({1, 1});
    checkPoint(camera.position(), {11, -4});
    camera.setViewport({320, 200});
    checkPoint(camera.worldToScreen({11, -4}), {160, 100});
    CHECK(camera.viewport() == (Extent2D{320, 200}));
}

SCHUIMM_TEST(Camera_BoundsKeepViewInside)
{
    Camera2D camera({640, 360});
    camera.setBounds(RectF{0, 0, 1600, 900});
    camera.setPosition({0, 0});
    checkPoint(camera.position(), {320, 180});
    camera.setPosition({5000, 5000});
    checkPoint(camera.position(), {1280, 720});
    // Zooming in shrinks the visible area, so the clamp margin shrinks too.
    camera.setZoom(2.0f);
    camera.setPosition({0, 0});
    checkPoint(camera.position(), {160, 90});
    // Bounds smaller than the view: centred.
    camera.setZoom(1.0f);
    camera.setBounds(RectF{100, 100, 200, 100});
    checkPoint(camera.position(), {200, 150});
    camera.setBounds(std::nullopt);
    camera.setPosition({-50, -50});
    checkPoint(camera.position(), {-50, -50});
}

SCHUIMM_TEST(Camera_FollowIsFrameRateIndependent)
{
    Camera2D coarse({640, 360});
    Camera2D fine({640, 360});
    const Vec2 target{100, 50};
    for (int i = 0; i < 6; ++i) {
        coarse.follow(target, 1.0 / 30.0, 5.0f);
    }
    for (int i = 0; i < 24; ++i) {
        fine.follow(target, 1.0 / 120.0, 5.0f);
    }
    checkPoint(coarse.position(), fine.position(), 1e-2);
    // After 0.2 s at rate 5 the remaining distance is exp(-1).
    const double remaining = std::exp(-1.0);
    CHECK_NEAR(coarse.position().x, 100.0 * (1.0 - remaining), 1e-2);

    for (int i = 0; i < 600; ++i) {
        coarse.follow(target, 1.0 / 60.0, 5.0f);
    }
    checkPoint(coarse.position(), target, 1e-2);

    Camera2D snapping({640, 360});
    snapping.follow(target, 0.016, 0.0f);
    checkPoint(snapping.position(), target);
}

SCHUIMM_TEST(Camera_DeadZoneIgnoresSmallOffsets)
{
    Camera2D camera({640, 360});
    camera.setDeadZone({20, 10});
    camera.follow({15, 5}, 0.1, 0.0f);
    checkPoint(camera.position(), {0, 0});
    camera.follow({50, -30}, 0.1, 0.0f);
    checkPoint(camera.position(), {30, -20}); // target ends up on the dead-zone edge
}

SCHUIMM_TEST(Camera_PixelSnapRoundsTranslation)
{
    Camera2D camera({640, 360});
    camera.setPosition({10.3f, 20.6f});
    camera.setPixelSnap(true);
    const Transform2D view = camera.viewTransform();
    CHECK_EQ(view.tx, std::round(view.tx));
    CHECK_EQ(view.ty, std::round(view.ty));
}
