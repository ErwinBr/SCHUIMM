#include "TestFramework.h"

#include "Core/Input/InputState.h"

using namespace schuimm;

// These tests act as the "mock input implementation": they feed synthetic native-style
// events into InputState exactly as a platform backend would, frame by frame.

SCHUIMM_TEST(Input_InitialStateIsUp)
{
    InputState input;
    input.update();
    CHECK(input.state(Key::A) == ButtonState::Up);
    CHECK(input.state(MouseButton::Left) == ButtonState::Up);
    CHECK(!input.isDown(Key::Space));
}

SCHUIMM_TEST(Input_PressHoldRelease)
{
    InputState input;
    input.keyDown(Key::A);
    input.update();
    CHECK(input.state(Key::A) == ButtonState::Pressed);
    CHECK(input.wasPressed(Key::A));
    CHECK(input.isDown(Key::A));

    input.update();
    CHECK(input.state(Key::A) == ButtonState::Held);
    CHECK(input.isHeld(Key::A));
    CHECK(!input.wasPressed(Key::A));

    input.update();
    CHECK(input.isHeld(Key::A));

    input.keyUp(Key::A);
    input.update();
    CHECK(input.state(Key::A) == ButtonState::Released);
    CHECK(input.wasReleased(Key::A));
    CHECK(!input.isDown(Key::A));

    input.update();
    CHECK(input.state(Key::A) == ButtonState::Up);
}

SCHUIMM_TEST(Input_AutoRepeatIsIgnored)
{
    InputState input;
    input.keyDown(Key::Left);
    input.update();
    input.keyDown(Key::Left); // OS auto-repeat
    input.keyDown(Key::Left);
    input.update();
    CHECK(input.state(Key::Left) == ButtonState::Held);
}

SCHUIMM_TEST(Input_TapWithinOneFrameIsNotLost)
{
    InputState input;
    input.keyDown(Key::Space);
    input.keyUp(Key::Space);
    input.update();
    CHECK(input.state(Key::Space) == ButtonState::Pressed);
    input.update();
    CHECK(input.state(Key::Space) == ButtonState::Released);
    input.update();
    CHECK(input.state(Key::Space) == ButtonState::Up);
}

SCHUIMM_TEST(Input_ReleaseAndRepressWithinOneFrameIsNewPress)
{
    InputState input;
    input.keyDown(Key::W);
    input.update();
    input.update();
    REQUIRE(input.isHeld(Key::W));
    input.keyUp(Key::W);
    input.keyDown(Key::W);
    input.update();
    CHECK(input.state(Key::W) == ButtonState::Pressed);
}

SCHUIMM_TEST(Input_TapFollowedByPressInNextFrame)
{
    InputState input;
    input.keyDown(Key::E);
    input.keyUp(Key::E);
    input.update();
    CHECK(input.wasPressed(Key::E));
    input.keyDown(Key::E); // pressed again before the pending release was reported
    input.update();
    CHECK(input.state(Key::E) == ButtonState::Held);
}

SCHUIMM_TEST(Input_UnknownKeysAreIgnored)
{
    InputState input;
    input.keyDown(Key::Unknown);
    input.keyDown(Key::Count);
    input.update();
    CHECK(input.state(Key::Unknown) == ButtonState::Up);
}

SCHUIMM_TEST(Input_MouseButtons)
{
    InputState input;
    input.mouseButtonDown(MouseButton::Right);
    input.update();
    CHECK(input.wasPressed(MouseButton::Right));
    CHECK(!input.isDown(MouseButton::Left));
    input.update();
    CHECK(input.isHeld(MouseButton::Right));
    input.mouseButtonUp(MouseButton::Right);
    input.update();
    CHECK(input.wasReleased(MouseButton::Right));
}

SCHUIMM_TEST(Input_MousePositionAndDelta)
{
    InputState input;
    CHECK(!input.hasMousePosition());
    input.mouseMoved({100.0f, 50.0f}); // first position: no delta
    input.update();
    CHECK(input.hasMousePosition());
    CHECK((input.mousePosition() == Vec2{100.0f, 50.0f}));
    CHECK((input.mouseDelta() == Vec2{0.0f, 0.0f}));

    input.mouseMoved({110.0f, 45.0f});
    input.mouseMoved({120.0f, 60.0f});
    input.update();
    CHECK((input.mousePosition() == Vec2{120.0f, 60.0f}));
    CHECK((input.mouseDelta() == Vec2{20.0f, 10.0f}));

    input.update(); // no movement this frame
    CHECK((input.mouseDelta() == Vec2{0.0f, 0.0f}));
}

SCHUIMM_TEST(Input_WheelAccumulatesPerFrame)
{
    InputState input;
    input.mouseWheel({0.0f, 1.0f});
    input.mouseWheel({0.5f, 2.0f});
    input.update();
    CHECK((input.wheelDelta() == Vec2{0.5f, 3.0f}));
    input.update();
    CHECK((input.wheelDelta() == Vec2{0.0f, 0.0f}));
}

SCHUIMM_TEST(Input_ReleaseAllOnFocusLoss)
{
    InputState input;
    input.keyDown(Key::LeftShift);
    input.mouseButtonDown(MouseButton::Left);
    input.update();
    input.releaseAll();
    input.update();
    CHECK(input.wasReleased(Key::LeftShift));
    CHECK(input.wasReleased(MouseButton::Left));
    input.update();
    CHECK(input.state(Key::LeftShift) == ButtonState::Up);
}

SCHUIMM_TEST(Input_NamesAreReadable)
{
    CHECK_EQ(std::string(toString(Key::F11)), std::string("F11"));
    CHECK_EQ(std::string(toString(Key::Digit7)), std::string("Digit7"));
    CHECK_EQ(std::string(toString(MouseButton::Middle)), std::string("Middle"));
    CHECK_EQ(std::string(toString(ButtonState::Held)), std::string("Held"));
}
