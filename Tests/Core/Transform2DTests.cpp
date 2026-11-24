#include "Core/Common/Transform2D.h"
#include "TestFramework.h"

#include <numbers>

using namespace schuimm;

namespace {
void checkPoint(Vec2 actual, Vec2 expected)
{
    CHECK_NEAR(actual.x, expected.x, 1e-4);
    CHECK_NEAR(actual.y, expected.y, 1e-4);
}
} // namespace

SCHUIMM_TEST(Transform2D_IdentityTranslationScale)
{
    checkPoint(Transform2D::identity().apply({3, 4}), {3, 4});
    checkPoint(Transform2D::translation({10, -2}).apply({3, 4}), {13, 2});
    checkPoint(Transform2D::scaling(2, 3).apply({3, 4}), {6, 12});
    checkPoint(Transform2D::translation({10, -2}).applyVector({3, 4}), {3, 4});
}

SCHUIMM_TEST(Transform2D_RotationIsClockwiseOnScreen)
{
    // y points down: a quarter turn moves +x onto +y (downwards), i.e. clockwise on screen.
    checkPoint(Transform2D::rotation(std::numbers::pi_v<float> / 2).apply({1, 0}), {0, 1});
    checkPoint(Transform2D::rotation(std::numbers::pi_v<float>).apply({1, 2}), {-1, -2});
}

SCHUIMM_TEST(Transform2D_CompositionReadsRightToLeft)
{
    const Transform2D t = Transform2D::translation({5, 0}) * Transform2D::scaling(2, 2);
    checkPoint(t.apply({1, 1}), {7, 2}); // scale first, then translate
    const Transform2D u = Transform2D::scaling(2, 2) * Transform2D::translation({5, 0});
    checkPoint(u.apply({1, 1}), {12, 2});
}

SCHUIMM_TEST(Transform2D_InverseRoundTrips)
{
    const Transform2D t = Transform2D::translation({-30, 12}) * Transform2D::rotation(0.7f) * Transform2D::scaling(2.5f, 0.5f);
    const auto inverse = t.inverse();
    REQUIRE(inverse.has_value());
    checkPoint(inverse->apply(t.apply({17, -4})), {17, -4});
    CHECK(approxEqual(*inverse * t, Transform2D::identity(), 1e-4f));
    CHECK(!Transform2D::scaling(0, 1).inverse().has_value());
}
