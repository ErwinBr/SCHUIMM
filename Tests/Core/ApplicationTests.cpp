#include "TestFramework.h"

#include "ApplicationMocks.h"

using namespace schuimm;
using namespace schuimm::test;
using namespace std::chrono_literals;

SCHUIMM_TEST(Application_RunsFullLifecycle)
{
    ApplicationFixture f;
    f.listener.quitAfterUpdates = 3;
    auto app = f.makeApplication();
    CHECK(app->state() == LifecycleState::Created);

    const Status status = app->run();
    REQUIRE(status.has_value());
    CHECK(app->state() == LifecycleState::Stopped);
    CHECK_EQ(f.listener.initializes, 1);
    CHECK_EQ(f.listener.updates, 3);
    CHECK_EQ(f.listener.renders, 2); // quit requested during the 3rd update: that frame is not rendered
    CHECK_EQ(f.graphics.beginFrames, 2);
    CHECK_EQ(f.graphics.endFrames, 2);
    CHECK_EQ(f.graphics.presents, 2);
    CHECK_EQ(f.listener.shutdowns, 1);
    CHECK_EQ(f.platform.polls, 3);
    CHECK_EQ(app->frameIndex(), std::uint64_t{2});
    CHECK(!f.window.liveResize); // callback removed on shutdown
}

SCHUIMM_TEST(Application_RunCanOnlyBeCalledOnce)
{
    ApplicationFixture f;
    f.listener.quitAfterUpdates = 1;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    const Status second = app->run();
    REQUIRE(!second.has_value());
    CHECK(second.error().code == ErrorCode::InvalidState);
    CHECK_EQ(f.listener.initializes, 1);
}

SCHUIMM_TEST(Application_InitialisationFailureAbortsRun)
{
    ApplicationFixture f;
    f.listener.initResult = makeError(ErrorCode::IoFailure, "asset missing");
    auto app = f.makeApplication();
    const Status status = app->run();
    REQUIRE(!status.has_value());
    CHECK(status.error().code == ErrorCode::IoFailure);
    CHECK_EQ(f.listener.updates, 0);
    CHECK_EQ(f.listener.shutdowns, 0);
    CHECK_EQ(f.platform.polls, 0);
    CHECK(app->state() == LifecycleState::Stopped);
}

SCHUIMM_TEST(Application_CloseRequestStopsTheLoop)
{
    ApplicationFixture f;
    f.platform.script = [&](int pump) {
        if (pump == 2) {
            f.window.push(CloseRequestedEvent{});
        }
    };
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    CHECK_EQ(f.listener.closeRequests, 1);
    CHECK_EQ(f.listener.updates, 2); // frames 0 and 1; the close arrives with pump 2
    CHECK_EQ(f.listener.shutdowns, 1);
}

SCHUIMM_TEST(Application_CloseRequestCanBeVetoed)
{
    ApplicationFixture f;
    f.listener.vetoCloseRequests = 1;
    f.platform.script = [&](int pump) {
        if (pump == 1 || pump == 4) {
            f.window.push(CloseRequestedEvent{});
        }
    };
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    CHECK_EQ(f.listener.closeRequests, 2);
    CHECK_EQ(f.listener.updates, 4); // kept running after the veto
}

SCHUIMM_TEST(Application_MinimiseSuspendsAndResumeResetsDelta)
{
    ApplicationFixture f;
    f.platform.script = [&](int pump) {
        if (pump == 1) {
            f.window.push(MinimizedChangedEvent{true});
        }
        if (pump == 2 || pump == 3) {
            f.now += 30s; // a long time passes while minimised
        }
        if (pump == 4) {
            f.window.push(MinimizedChangedEvent{false});
        }
    };
    f.listener.quitAfterUpdates = 3;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());

    CHECK_EQ(f.listener.suspends, 1);
    CHECK_EQ(f.listener.resumes, 1);
    CHECK_EQ(f.platform.waits, 3);       // pumps 2, 3 and 4 block instead of spinning
    CHECK_EQ(f.listener.updates, 3);     // pump 0, then pumps 5 and 6 after resuming
    REQUIRE(f.listener.deltas.size() == 3);
    CHECK_EQ(f.listener.deltas[1], 0.0); // first frame after resume: no 30 s jump
    CHECK_NEAR(f.listener.deltas[2], 0.016667, 1e-6);
    for (const LifecycleState state : f.listener.statesDuringUpdate) {
        CHECK(state == LifecycleState::Running);
    }
}

SCHUIMM_TEST(Application_MinimiseCanBeIgnored)
{
    ApplicationFixture f;
    f.settings.suspendWhenMinimized = false;
    f.platform.script = [&](int pump) {
        if (pump == 0) {
            f.window.push(MinimizedChangedEvent{true});
        }
    };
    f.listener.quitAfterUpdates = 3;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    CHECK_EQ(f.listener.suspends, 0);
    CHECK_EQ(f.platform.waits, 0);
}

SCHUIMM_TEST(Application_PauseWhenUnfocused)
{
    ApplicationFixture f;
    f.settings.pauseWhenUnfocused = true;
    f.platform.script = [&](int pump) {
        if (pump == 1) f.window.push(FocusChangedEvent{false});
        if (pump == 3) f.window.push(FocusChangedEvent{true});
    };
    f.listener.quitAfterUpdates = 3;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    CHECK_EQ(f.listener.suspends, 1);
    CHECK_EQ(f.listener.resumes, 1);
    REQUIRE(f.listener.focusChanges.size() == 2);
    CHECK(!f.listener.focusChanges[0]);
    CHECK(f.listener.focusChanges[1]);
}

SCHUIMM_TEST(Application_FocusLossReleasesHeldKeys)
{
    ApplicationFixture f;
    std::vector<ButtonState> states;
    f.platform.script = [&](int pump) {
        if (pump == 0) f.window.push(KeyEvent{Key::A, true, false});
        if (pump == 2) f.window.push(FocusChangedEvent{false}); // key-up never arrives
    };
    f.listener.onEachUpdate = [&](Application& app) { states.push_back(app.input().state(Key::A)); };
    f.listener.quitAfterUpdates = 4;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    REQUIRE(states.size() == 4);
    CHECK(states[0] == ButtonState::Pressed);
    CHECK(states[1] == ButtonState::Held);
    CHECK(states[2] == ButtonState::Released);
    CHECK(states[3] == ButtonState::Up);
}

SCHUIMM_TEST(Application_RoutesInputEvents)
{
    ApplicationFixture f;
    bool checked = false;
    f.platform.script = [&](int pump) {
        if (pump == 0) {
            f.window.push(MouseMovedEvent{{10.0f, 20.0f}});
            f.window.push(MouseButtonEvent{MouseButton::Right, true, {12.0f, 22.0f}});
            f.window.push(MouseWheelEvent{{0.0f, 2.0f}});
            f.window.push(KeyEvent{Key::Space, true, false});
            f.window.push(KeyEvent{Key::Space, true, true}); // auto-repeat
        }
    };
    f.listener.onEachUpdate = [&](Application& app) {
        if (app.frameIndex() == 0) {
            const InputState& input = app.input();
            CHECK(input.wasPressed(MouseButton::Right));
            CHECK((input.mousePosition() == Vec2{12.0f, 22.0f}));
            CHECK((input.wheelDelta() == Vec2{0.0f, 2.0f}));
            CHECK(input.wasPressed(Key::Space));
            checked = true;
        }
    };
    f.listener.quitAfterUpdates = 1;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    CHECK(checked);
}

SCHUIMM_TEST(Application_ClampsDeltaAfterStall)
{
    ApplicationFixture f;
    f.platform.script = [&](int pump) {
        if (pump == 2) f.now += 10s; // e.g. the window was dragged
    };
    f.listener.quitAfterUpdates = 3;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    REQUIRE(f.listener.deltas.size() == 3);
    CHECK_EQ(f.listener.deltas[0], 0.0);
    CHECK_NEAR(f.listener.deltas[1], 0.016667, 1e-6);
    CHECK_NEAR(f.listener.deltas[2], f.settings.clock.maximumDeltaSeconds, 1e-12);
}

SCHUIMM_TEST(Application_SkipsRenderingForEmptyFramebuffer)
{
    ApplicationFixture f;
    f.window.pixelSize = {0, 0};
    f.listener.quitAfterUpdates = 3;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    CHECK_EQ(f.listener.updates, 3);
    CHECK_EQ(f.listener.renders, 0);
    CHECK_EQ(f.graphics.presents, 0);
}

SCHUIMM_TEST(Application_LiveResizeRendersInsideModalLoop)
{
    ApplicationFixture f;
    f.platform.script = [&](int pump) {
        if (pump == 1) {
            // Simulates the Win32/Cocoa modal resize loop running inside pollEvents().
            f.window.pixelSize = f.window.windowSize = {800, 600};
            f.window.push(WindowResizedEvent{{800, 600}, {800, 600}});
            REQUIRE(f.window.liveResize);
            f.window.liveResize();
            f.window.liveResize();
        }
    };
    f.listener.quitAfterUpdates = 5;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    CHECK_EQ(f.listener.resizes, 1);
    // Pumps 0, 1, 2 render normally, plus two extra frames from the live-resize callback.
    CHECK_EQ(f.listener.updates, 5);
    CHECK_EQ(f.platform.polls, 3);
}

SCHUIMM_TEST(Application_LiveResizeIgnoredOutsideEventPump)
{
    ApplicationFixture f;
    f.listener.onEachUpdate = [&](Application&) {
        if (f.listener.updates == 1 && f.window.liveResize) {
            f.window.liveResize(); // re-entrant call from game code must be ignored
        }
    };
    f.listener.quitAfterUpdates = 2;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    CHECK_EQ(f.listener.updates, 2);
}

SCHUIMM_TEST(Application_StateNames)
{
    CHECK_EQ(std::string(toString(LifecycleState::Suspended)), std::string("Suspended"));
    CHECK_EQ(std::string(toString(LifecycleState::Stopped)), std::string("Stopped"));
}

// --- Script host service ------------------------------------------------------------------------

#include "Script/Runtime/ScriptHost.h"

namespace {

// main(): while True: tick(); yield()   (hand-assembled, so CoreTests needs no compiler)
std::shared_ptr<const script::Program> tickProgram(script::ScriptHost& host, int& ticks)
{
    using namespace script;
    REQUIRE(host.natives()
                .add({"tick", {}, ScriptType::None,
                      [&ticks](NativeContext&, std::span<const Value>) {
                          ++ticks;
                          return NativeResult::returnValue();
                      },
                      {}})
                .has_value());
    Module module;
    module.strings = {"test.sch", "main", "tick", "yield"};
    module.sources = {{0, ScriptRole::Room}};
    module.imports = {{2, {}, ScriptType::None}, {3, {}, ScriptType::None}};
    Function main;
    main.nameIndex = 1;
    main.code = {{Opcode::CallNative, noRegister, 0, 0}, {Opcode::CallNative, noRegister, 1, 0}, {Opcode::Jump, 0, 0, 0}};
    main.positions = {{2, 2}, {3, 2}, {1, 1}};
    module.functions.push_back(main);
    auto program = Program::link(std::move(module), host.natives(), "test.sch");
    REQUIRE(program.has_value());
    return *program;
}

} // namespace

SCHUIMM_TEST(Application_UpdatesScriptsOncePerFrameWithTheFrameDelta)
{
    ApplicationFixture f;
    script::ScriptHost host(f.logger);
    int ticks = 0;
    auto program = tickProgram(host, ticks);
    REQUIRE(host.start(program).has_value());
    f.scripts = &host;
    f.listener.quitAfterUpdates = 4;
    auto app = f.makeApplication();
    CHECK(app->scripts() == &host);
    REQUIRE(app->run().has_value());
    // Updates 1-3 run the scripts; quit is requested during the 4th listener update.
    CHECK_EQ(ticks, 3);
    double total = 0.0;
    for (std::size_t i = 0; i + 1 < f.listener.deltas.size(); ++i) {
        total += f.listener.deltas[i];
    }
    CHECK_NEAR(static_cast<double>(host.scheduler().timeNanoseconds()) / 1e9, total, 1e-6);
    CHECK_EQ(host.activeCount(), std::size_t{1}); // Still looping: the game loop was never blocked.
}

SCHUIMM_TEST(Application_SuspendedApplicationDoesNotRunScripts)
{
    ApplicationFixture f;
    script::ScriptHost host(f.logger);
    int ticks = 0;
    auto program = tickProgram(host, ticks);
    REQUIRE(host.start(program).has_value());
    f.scripts = &host;
    f.platform.script = [&](int pump) {
        if (pump == 1) f.window.push(MinimizedChangedEvent{true});
        if (pump == 4) f.window.push(MinimizedChangedEvent{false});
    };
    f.listener.quitAfterUpdates = 3;
    auto app = f.makeApplication();
    REQUIRE(app->run().has_value());
    CHECK_EQ(f.listener.updates, 3);
    CHECK_EQ(ticks, 2); // Frames 1 and the first after resume; the quitting frame skips scripts.
}
