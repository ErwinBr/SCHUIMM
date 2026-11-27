#include "ActorDemo.h"

#include "Core/Application/Application.h"
#include "Core/Common/StringBuilder.h"
#include "Core/Input/InputState.h"
#include "Core/Common/GeometryPredicates.h"
#include "Graphics/Interface/Color.h"
#include "Graphics/Interface/Presentation.h"
#include "Graphics/Interface/Sprite.h"
#include "Resource/Image/ImageLoader.h"
#include "RoomGraphics/RoomRenderer.h"
#include "Screenshot.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace schuimm::actordemo {

using room::ActorInstance;
using room::Facing;

namespace {

constexpr const char* category = "ActorDemo";

struct RoomStyle {
    Color wall;
    Color floor;
    float horizon;
};

RoomStyle styleOf(std::string_view roomId)
{
    if (roomId == "garden") return {{0.55f, 0.78f, 0.95f, 1.0f}, {0.35f, 0.62f, 0.3f, 1.0f}, 170.0f};
    return {{0.45f, 0.35f, 0.3f, 1.0f}, {0.6f, 0.5f, 0.38f, 1.0f}, 180.0f};
}

void drawRectangle(Renderer2D& renderer, RectF rectangle, Color colour, int layer)
{
    Sprite sprite;
    sprite.destination = rectangle;
    sprite.tint = colour;
    sprite.layer = layer;
    renderer.drawSprite(sprite);
}

// A line as a thin rotated solid sprite.
void drawLine(Renderer2D& renderer, Vec2 a, Vec2 b, float thickness, Color colour, int layer)
{
    const auto l = static_cast<float>(distance(a, b));
    if (!(l > 0.0f)) return;
    Sprite sprite;
    sprite.destination = {a.x, a.y - thickness / 2, l, thickness};
    sprite.origin = {0, thickness / 2};
    sprite.rotation = std::atan2(b.y - a.y, b.x - a.x);
    sprite.tint = colour;
    sprite.layer = layer;
    renderer.drawSprite(sprite);
}

void drawCross(Renderer2D& renderer, Vec2 p, float size, Color colour, int layer)
{
    drawLine(renderer, {p.x - size, p.y - size}, {p.x + size, p.y + size}, 2, colour, layer);
    drawLine(renderer, {p.x - size, p.y + size}, {p.x + size, p.y - size}, 2, colour, layer);
}

std::string describePoint(Vec2 p)
{
    return concat("(", std::lround(p.x), ", ", std::lround(p.y), ")");
}

std::string describeActor(const ActorInstance& actor)
{
    return concat("'", actor.id(), "' (", actor.resource().id, ", handle ", actor.handle().id, ")");
}

} // namespace

ActorDemoListener::ActorDemoListener(Options options, const AssetLocator& assets)
    : options_(std::move(options))
    , assets_(assets)
{
}

ActorDemoListener::~ActorDemoListener() = default;

Status ActorDemoListener::onInitialize(Application& app)
{
    if (app.scripts() == nullptr) return makeError(ErrorCode::InvalidArgument, "ActorDemo needs a ScriptHost");
    // The adventure natives first: the game program is linked against them when it loads.
    auto natives = adventure::AdventureNatives::bind(app.scripts()->natives());
    if (!natives) return std::unexpected(natives.error());
    natives_ = std::move(*natives);
    auto runtime = adventure::AdventureRuntime::create(*app.scripts(), *natives_, assets_,
                                                       AssetPath::parse("ActorDemo.game.json").value(), &camera_);
    if (!runtime) return std::unexpected(runtime.error());
    runtime_ = std::move(*runtime);
    rooms_ = runtime_->game().rooms;
    images_ = std::make_unique<ResourceCache<Image>>(assets_, loadImage);
    textures_ = std::make_unique<TextureCache>(app.graphics().renderer(), *images_);
    camera_.setViewport(app.graphics().presentation().virtualResolution);
    camera_.setPixelSnap(true);
    enterRoom(app, 0);
    if (!room_) return makeError(ErrorCode::InvalidData, "the first room could not be entered (see the log)");
    app.logger().info(category, "Click: walk   Right-click/S: stop   G: debug overlay   Tab: next room   R: re-enter   "
                                "1-9: select actor   arrows: facing   W: wave   I: idle   Space: pause   V: visible   "
                                "-/=: scale   P: scripted demonstration   F: use the fan   Esc: quit");
    return {};
}

void ActorDemoListener::enterRoom(Application& app, std::size_t index)
{
    // Leave the current room first: its actors (and their handles) are gone before the next room
    // exists. Room destruction happens here, in onUpdate, never while scripts run.
    if (room_) app.logger().info(category, concat("Leaving room '", room_->resource().id, "'"));
    // The adventure API lets go of the room first (its pending walks and animations are
    // cancelled), then the room cancels its own and its actors' coroutines.
    runtime_->leaveRoom();
    room_ = nullptr;
    demonstration_ = {};
    roomTextures_ = {};

    roomIndex_ = index % rooms_.size();
    const AssetPath& path = rooms_[roomIndex_];
    auto loaded = runtime_->library().load(path);
    if (!loaded) {
        app.logger().error(category, describe(loaded.error()));
        exitCode_ = 1;
        return;
    }
    auto textures = room::RoomTextures::prepare(*textures_, *loaded, TextureSettings{TextureFilter::Nearest});
    if (!textures) {
        app.logger().error(category, describe(textures.error()));
        exitCode_ = 1;
        return;
    }
    images_->unloadAll(); // The textures exist; the decoded images are not needed any more.
    if (auto status = runtime_->enterRoom(path); !status) {
        app.logger().error(category, describe(status.error()));
        exitCode_ = 1;
        return;
    }
    roomTextures_ = std::move(*textures);
    room_ = runtime_->activeRoom();
    selected_ = 0;
    // A room with a "room:" declaration sets itself up in its ready() handler (the courtyard
    // starts the gardener's rounds); it runs in the next script update. Rooms without one ignore it.
    if (auto started = runtime_->dispatchRoom("ready"); !started) app.logger().error(category, describe(started.error()));
    const Extent2D size = room_->resource().size;
    camera_.setBounds(RectF{0, 0, static_cast<float>(size.width), static_cast<float>(size.height)});
    const ActorInstance* hero = room_->actor("hero");
    camera_.setPosition(hero != nullptr ? hero->position()
                                        : Vec2{static_cast<float>(size.width) / 2, static_cast<float>(size.height) / 2});
    app.logger().info(category, concat("Entered room '", room_->resource().id, "' with ", room_->actors().size(),
                                       " fresh actors (", roomTextures_.size(), " textures, ",
                                       runtime_->library().actors().actorCount(), " actor resources cached)"));
    for (const ActorInstance& actor : room_->actors()) app.logger().info(category, concat("  ", describeActor(actor)));
    updateTitle(app);
}

ActorInstance* ActorDemoListener::selected()
{
    if (!room_ || room_->actors().empty()) return nullptr;
    return &room_->actors()[selected_ % room_->actors().size()];
}

void ActorDemoListener::perform(Application& app, Action action)
{
    ActorInstance* actor = selected();
    const auto report = [&](const Status& status, std::string_view what) {
        if (!status) {
            app.logger().warning(category, concat(what, ": ", status.error().message));
        } else if (actor != nullptr) {
            app.logger().info(category, concat(describeActor(*actor), ": ", what));
        }
    };
    switch (action) {
    case Action::NextRoom: enterRoom(app, roomIndex_ + 1); return;
    case Action::ReenterRoom: enterRoom(app, roomIndex_); return;
    case Action::SelectNext:
        if (room_ && !room_->actors().empty()) selected_ = (selected_ + 1) % room_->actors().size();
        break;
    default: break;
    }
    if (actor == nullptr) return;
    switch (action) {
    case Action::Wave: report(actor->playAnimation("wave"), "wave"); break;
    case Action::Idle: report(actor->playAnimation("idle"), "idle"); break;
    case Action::TogglePause:
        if (actor->playbackStatus() == animation::PlaybackStatus::Paused) {
            actor->resumeAnimation();
            report({}, "resumed");
        } else {
            actor->pauseAnimation();
            report({}, "paused");
        }
        break;
    case Action::ToggleVisible:
        actor->setVisible(!actor->visible());
        report({}, actor->visible() ? "visible" : "invisible (still animating)");
        break;
    case Action::ScaleUp: report(actor->setBaseScale(std::min(actor->baseScale() * 1.25f, 8.0f)), "scale up"); break;
    case Action::ScaleDown: report(actor->setBaseScale(std::max(actor->baseScale() / 1.25f, 0.25f)), "scale down"); break;
    default: break;
    }
    updateTitle(app);
}

void ActorDemoListener::setFacing(Application& app, Facing facing)
{
    ActorInstance* actor = selected();
    if (actor == nullptr || actor->facing() == facing) return;
    const auto before = actor->player().clip();
    if (auto status = actor->setFacing(facing); !status) {
        app.logger().warning(category, status.error().message);
        return;
    }
    const bool restarted = !(before == actor->player().clip());
    app.logger().info(category, concat(describeActor(*actor), ": facing ", room::facingName(facing),
                                       restarted ? " (new clip)" : " (same clip, playback continues)",
                                       actor->flipX() ? ", mirrored" : ""));
    updateTitle(app);
}

void ActorDemoListener::handleKeys(Application& app)
{
    const InputState& input = app.input();
    if (input.wasPressed(Key::Escape)) app.requestQuit();
    if (input.wasPressed(Key::G)) {
        debug_ = !debug_;
        app.logger().info(category, debug_ ? "Debug overlay on" : "Debug overlay off");
    }
    if (input.wasPressed(Key::S) || input.wasPressed(MouseButton::Right)) {
        if (ActorInstance* hero = room_ ? room_->actor("hero") : nullptr; hero != nullptr && hero->isWalking()) {
            hero->stopWalking();
            app.logger().info(category, concat("hero: stopped at ", describePoint(hero->position()), " (request ",
                                               hero->movement().request, " cancelled)"));
        }
    }
    if (input.wasPressed(Key::P)) demonstrate(app);
    if (input.wasPressed(Key::F)) useFan(app);
    if (input.wasPressed(Key::Tab)) perform(app, Action::NextRoom);
    if (input.wasPressed(Key::R)) perform(app, Action::ReenterRoom);
    if (input.wasPressed(Key::W)) perform(app, Action::Wave);
    if (input.wasPressed(Key::I)) perform(app, Action::Idle);
    if (input.wasPressed(Key::Space)) perform(app, Action::TogglePause);
    if (input.wasPressed(Key::V)) perform(app, Action::ToggleVisible);
    if (input.wasPressed(Key::Equals)) perform(app, Action::ScaleUp);
    if (input.wasPressed(Key::Minus)) perform(app, Action::ScaleDown);
    static constexpr std::array<Key, 9> digits = {Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5,
                                                  Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9};
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (input.wasPressed(digits[i]) && room_ && i < room_->actors().size()) {
            selected_ = i;
            updateTitle(app);
        }
    }
    // Arrows: held together they give the diagonals.
    const bool any = input.wasPressed(Key::Up) || input.wasPressed(Key::Down) || input.wasPressed(Key::Left) ||
                     input.wasPressed(Key::Right);
    if (!any) return;
    const int x = (input.isDown(Key::Right) ? 1 : 0) - (input.isDown(Key::Left) ? 1 : 0);
    const int y = (input.isDown(Key::Down) ? 1 : 0) - (input.isDown(Key::Up) ? 1 : 0);
    static constexpr Facing table[3][3] = {
        {Facing::NorthWest, Facing::North, Facing::NorthEast},
        {Facing::West, Facing::South, Facing::East},
        {Facing::SouthWest, Facing::South, Facing::SouthEast},
    };
    if (x == 0 && y == 0) return;
    setFacing(app, table[y + 1][x + 1]);
}

void ActorDemoListener::runAutomaticStep(Application& app)
{
    // First 240 frames of the courtyard script: the demonstration (P) starts, and the fan is
    // switched (F) twice while it runs. Counted in frames, like the sequence, so the run does not
    // depend on the frame rate. The sequence then leaves the room, which cancels the rest of the
    // demonstration (press P to watch it to the end).
    if (automaticScripted_) {
        const std::uint64_t frame = app.frameIndex();
        if (frame == 1) demonstrate(app);
        if (frame == 60 || frame == 120) useFan(app);
        if (frame >= 240) {
            automaticScripted_ = false;
            automaticSequenceStart_ = frame;
        }
        return;
    }
    // One step every 20 frames: every interaction of the manual controls, including leaving
    // and re-entering rooms.
    const std::uint64_t frame = app.frameIndex() - automaticSequenceStart_;
    if (frame == 0 || frame % 20 != 0) return;
    switch (automaticStep_++ % 21) {
    // The courtyard: walking, around the fountain, replacement, stopping, an unreachable click.
    case 0: debug_ = true; walkHero(app, {880, 300}); break;
    case 1: walkHero(app, {500, 500}); break;   // Replaces the walk, from where the hero is.
    case 2: walkHero(app, {500, 330}); break;   // Around the fountain.
    case 3: walkHero(app, {800, 150}); break;   // The balcony: the nearest reachable point instead.
    case 4: perform(app, Action::NextRoom); break; // Leaving while walking.
    case 5: break;
    case 6: setFacing(app, Facing::West); break;      // Another clip.
    case 7: setFacing(app, Facing::East); break;      // The same clip mirrored: playback continues.
    case 8: setFacing(app, Facing::North); break;
    case 9: setFacing(app, Facing::SouthEast); break; // No artwork: the fallback (south).
    case 10: perform(app, Action::Wave); break;        // Non-looping: holds its last frame.
    case 11: setFacing(app, Facing::West); break;      // Wave falls back to south: continues.
    case 12: perform(app, Action::ScaleUp); break;
    case 13: perform(app, Action::SelectNext); perform(app, Action::ToggleVisible); break;
    case 14: perform(app, Action::TogglePause); break;
    case 15: perform(app, Action::TogglePause); break;
    case 16: perform(app, Action::NextRoom); break;
    case 17: perform(app, Action::Wave); break;
    case 18: perform(app, Action::NextRoom); break; // Back to the courtyard: fresh actors from the placements.
    case 19: perform(app, Action::ReenterRoom); break;
    case 20: perform(app, Action::NextRoom); break;
    }
}

void ActorDemoListener::walkHero(Application& app, Vec2 destination)
{
    ActorInstance* hero = room_ ? room_->actor("hero") : nullptr;
    if (hero == nullptr) return;
    auto request = room_->walkTo(*hero, destination);
    if (!request) {
        app.logger().warning(category, concat("hero: cannot walk to ", describePoint(destination), ": ",
                                              request.error().message));
        return;
    }
    app.logger().info(category, concat("hero: walk ", request->id, " to ", describePoint(request->requested),
                                       request->resolved == request->requested
                                           ? std::string()
                                           : concat(", resolved to ", describePoint(request->resolved)),
                                       request->completed ? " (already there)" : "", ", ",
                                       hero->movement().path.size(), " waypoints"));
}

void ActorDemoListener::handleMouse(Application& app)
{
    const InputState& input = app.input();
    if (!input.wasPressed(MouseButton::Left) || !input.hasMousePosition() || !room_) return;
    // Window points -> virtual screen (letterboxing and HiDPI) -> room coordinates (camera).
    const PresentationSettings& presentation = app.graphics().presentation();
    const Vec2 screen = windowToVirtual(input.mousePosition(), app.window().size(), app.window().framebufferSize(),
                                        presentation.virtualResolution, presentation.integerScaling);
    const Extent2D view = presentation.virtualResolution;
    if (screen.x < 0 || screen.y < 0 || screen.x >= static_cast<float>(view.width) ||
        screen.y >= static_cast<float>(view.height)) {
        return; // A click on the letterbox bars.
    }
    walkHero(app, camera_.screenToWorld(screen));
}

void ActorDemoListener::demonstrate(Application& app)
{
    // One demonstration at a time: a running one is cancelled (and stops where it is).
    if (demonstration_.isValid()) app.scripts()->cancel(demonstration_);
    auto started = runtime_->dispatchRoom("demonstrate");
    if (!started) {
        app.logger().error(category, describe(started.error()));
        return;
    }
    if (!started->handled) {
        app.logger().info(category, "P: this room has no demonstration (Tab to the courtyard)");
        return;
    }
    demonstration_ = started->handle;
    app.logger().info(category, "Started the scripted demonstration (clicking walks the hero and interrupts it)");
}

void ActorDemoListener::useFan(Application& app)
{
    if (!room_ || room_->object("fan") == nullptr) return;
    // The fan's "use" handler of the room script, with the fan as self.
    if (auto result = room_->dispatch("fan", "use"); !result) {
        app.logger().error(category, describe(result.error()));
    }
}

void ActorDemoListener::onUpdate(Application& app, double deltaSeconds)
{
    handleKeys(app);
    handleMouse(app);
    if (options_.automatic) runAutomaticStep(app);
    // Movement and animation advance here; then the adventure API publishes finished and
    // cancelled walks and animations, which the ScriptHost update (after onUpdate) resumes.
    // Drawing only reads.
    if (runtime_) runtime_->update(deltaSeconds);
    if (room_) {
        if (const ActorInstance* hero = room_->actor("hero")) camera_.follow(hero->position(), deltaSeconds, 4.0f);
    }
    if (captureRequested_) {
        if (std::optional<Image> frame = app.graphics().takeCapturedFrame()) {
            captureRequested_ = false;
            captured_ = true;
            if (demo::writePpm(options_.screenshotPath, *frame)) {
                app.logger().info(category, concat("Screenshot written to ", options_.screenshotPath));
            } else {
                app.logger().error(category, concat("Could not write ", options_.screenshotPath));
                exitCode_ = 1;
            }
        }
    }
    if (options_.frames != 0 && app.frameIndex() + 1 >= options_.frames) {
        if (!options_.screenshotPath.empty() && !captured_) {
            if (!captureRequested_) {
                app.graphics().requestFrameCapture();
                captureRequested_ = true;
            }
            return;
        }
        app.requestQuit();
    }
}

void ActorDemoListener::onRender(Application&, Renderer2D& renderer)
{
    renderer.clear(Color::black());
    if (!room_) return;
    renderer.setView(camera_.viewTransform());
    if (!room_->resource().background) {
        // Rooms without a background image get a plain wall and floor.
        const Extent2D size = room_->resource().size;
        const RoomStyle style = styleOf(room_->resource().id);
        const auto width = static_cast<float>(size.width);
        const auto height = static_cast<float>(size.height);
        drawRectangle(renderer, {0, 0, width, style.horizon}, style.wall, -2);
        drawRectangle(renderer, {0, style.horizon, width, height - style.horizon}, style.floor, -2);
    }

    // A marker on the selected actor's ground point.
    if (ActorInstance* actor = selected()) {
        const Vec2 p = actor->position();
        // Above the room (layer 1): the background is part of the room's drawing.
        drawRectangle(renderer, {p.x - 10, p.y - 1, 20, 3}, {1.0f, 0.9f, 0.2f, actor->visible() ? 0.9f : 0.4f}, 1);
    }
    if (auto status = room::drawRoom(renderer, room_->drawList(), roomTextures_); !status) {
        exitCode_ = 1;
    }
    if (debug_) drawDebug(renderer);
    renderer.resetView();
}

void ActorDemoListener::drawDebug(Renderer2D& renderer)
{
    constexpr int layer = 10;
    if (const navigation::NavigationMap* map = room_->navigation()) {
        // Visibility edges (faint), then the walkable polygons, then the graph's corner nodes.
        for (const auto& edge : map->edges()) {
            drawLine(renderer, map->nodes()[edge.from].point, map->nodes()[edge.to].point, 1, {0.3f, 0.6f, 1.0f, 0.25f},
                     layer);
        }
        for (const auto& polygon : map->polygons()) {
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                drawLine(renderer, polygon[i], polygon[(i + 1) % polygon.size()], 2, {0.2f, 1.0f, 0.3f, 0.8f}, layer);
            }
        }
        for (const auto& node : map->nodes()) {
            drawRectangle(renderer, {node.point.x - 2, node.point.y - 2, 4, 4}, {1.0f, 1.0f, 0.2f, 0.9f}, layer);
        }
    }
    for (const ActorInstance& actor : room_->actors()) {
        const Vec2 p = actor.position();
        drawRectangle(renderer, {p.x - 2, p.y - 2, 5, 5}, {1, 1, 1, 1}, layer + 1); // Ground point.
        const room::MovementState& movement = actor.movement();
        if (movement.status == room::MovementStatus::Idle) continue;
        if (actor.isWalking()) {
            Vec2 from = p;
            for (std::size_t i = movement.nextWaypoint; i < movement.path.size(); ++i) {
                drawLine(renderer, from, movement.path[i], 2, {1.0f, 0.5f, 0.1f, 0.9f}, layer + 1);
                from = movement.path[i];
            }
        }
        drawCross(renderer, movement.requested, 5, {1.0f, 0.2f, 0.2f, 0.9f}, layer + 1);
        drawRectangle(renderer, {movement.resolved.x - 3, movement.resolved.y - 3, 7, 7}, {0.2f, 1.0f, 0.4f, 0.9f},
                      layer + 1);
    }
}

void ActorDemoListener::updateTitle(Application& app)
{
    std::string title = "SCHUIMM ActorDemo";
    if (room_) {
        title += concat(" - room '", room_->resource().id, "'");
        if (const ActorInstance* actor = selected()) {
            title += concat(" - ", actor->id(), ": ", actor->animation(), ", ", room::facingName(actor->facing()),
                            ", x", actor->baseScale(), actor->visible() ? "" : ", invisible");
        }
        if (room_->navigation() != nullptr) {
            title += concat(" - click to walk, G: debug");
        }
    }
    app.window().setTitle(title);
}

void ActorDemoListener::onShutdown(Application& app)
{
    // Scripts stop before the adventure context their natives use goes away; textures before the
    // graphics device; the room before the ScriptHost.
    if (app.scripts() != nullptr) app.scripts()->stopAll();
    room_ = nullptr;
    runtime_.reset(); // Ends the game session: the room, the globals, the natives' session.
    roomTextures_ = {};
    textures_.reset();
    images_.reset();
}

} // namespace schuimm::actordemo
