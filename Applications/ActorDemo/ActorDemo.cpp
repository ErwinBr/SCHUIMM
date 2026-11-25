#include "ActorDemo.h"

#include "Core/Application/Application.h"
#include "Core/Common/StringBuilder.h"
#include "Core/Input/InputState.h"
#include "Graphics/Interface/Color.h"
#include "Graphics/Interface/Sprite.h"
#include "Resource/Image/ImageLoader.h"
#include "RoomGraphics/RoomRenderer.h"

#include <algorithm>
#include <array>

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

std::string describeActor(const ActorInstance& actor)
{
    return concat("'", actor.id(), "' (", actor.resource().id, ", handle ", actor.handle().id, ")");
}

} // namespace

ActorDemoListener::ActorDemoListener(Options options, const AssetLocator& assets)
    : options_(std::move(options))
    , assets_(assets)
    , rooms_{AssetPath::parse("Rooms/Hall.room.json").value(), AssetPath::parse("Rooms/Garden.room.json").value()}
{
}

ActorDemoListener::~ActorDemoListener() = default;

Status ActorDemoListener::onInitialize(Application& app)
{
    if (app.scripts() == nullptr) return makeError(ErrorCode::InvalidArgument, "ActorDemo needs a ScriptHost");
    library_ = std::make_unique<room::RoomLibrary>(assets_, *app.scripts());
    images_ = std::make_unique<ResourceCache<Image>>(assets_, loadImage);
    textures_ = std::make_unique<TextureCache>(app.graphics().renderer(), *images_);
    enterRoom(app, 0);
    if (!room_) return makeError(ErrorCode::InvalidData, "the first room could not be entered (see the log)");
    app.logger().info(category, "Tab: next room   R: re-enter   1-9: select actor   arrows: facing   "
                                "W: wave   I: idle   Space: pause   V: visible   -/=: scale   Esc: quit");
    return {};
}

void ActorDemoListener::enterRoom(Application& app, std::size_t index)
{
    // Leave the current room first: its actors (and their handles) are gone before the next room
    // exists. Room destruction happens here, in onUpdate, never while scripts run.
    if (room_) app.logger().info(category, concat("Leaving room '", room_->resource().id, "'"));
    room_.reset();
    roomTextures_ = {};

    roomIndex_ = index % rooms_.size();
    const AssetPath& path = rooms_[roomIndex_];
    auto loaded = library_->load(path);
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
    auto instance = room::RoomInstance::create(*loaded, *app.scripts());
    if (!instance) {
        app.logger().error(category, describe(instance.error()));
        exitCode_ = 1;
        return;
    }
    roomTextures_ = std::move(*textures);
    room_ = std::move(*instance);
    selected_ = 0;
    app.logger().info(category, concat("Entered room '", room_->resource().id, "' with ", room_->actors().size(),
                                       " fresh actors (", roomTextures_.size(), " textures, ",
                                       library_->actors().actorCount(), " actor resources cached)"));
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
    case Action::ScaleUp: report(actor->setScale(std::min(actor->scale() * 1.25f, 8.0f)), "scale up"); break;
    case Action::ScaleDown: report(actor->setScale(std::max(actor->scale() / 1.25f, 0.25f)), "scale down"); break;
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
    // One step every 20 frames: every interaction of the manual controls, including leaving
    // and re-entering rooms.
    if (app.frameIndex() == 0 || app.frameIndex() % 20 != 0) return;
    switch (automaticStep_++ % 15) {
    case 0: setFacing(app, Facing::West); break;      // Another clip.
    case 1: setFacing(app, Facing::East); break;      // The same clip mirrored: playback continues.
    case 2: setFacing(app, Facing::North); break;
    case 3: setFacing(app, Facing::SouthEast); break; // No artwork: the fallback (south).
    case 4: perform(app, Action::Wave); break;        // Non-looping: holds its last frame.
    case 5: setFacing(app, Facing::West); break;      // Wave falls back to south: continues.
    case 6: perform(app, Action::ScaleUp); break;
    case 7: perform(app, Action::SelectNext); perform(app, Action::ToggleVisible); break;
    case 8: perform(app, Action::TogglePause); break;
    case 9: perform(app, Action::TogglePause); break;
    case 10: perform(app, Action::NextRoom); break;
    case 11: perform(app, Action::Wave); break;
    case 12: perform(app, Action::NextRoom); break; // Back: the authored placements again.
    case 13: perform(app, Action::ReenterRoom); break;
    case 14: perform(app, Action::Idle); break;
    }
}

void ActorDemoListener::onUpdate(Application& app, double deltaSeconds)
{
    handleKeys(app);
    if (options_.automatic) runAutomaticStep(app);
    // Animation advances here, before the ScriptHost update; drawing only reads.
    if (room_) room_->update(deltaSeconds);
    if (options_.frames != 0 && app.frameIndex() + 1 >= options_.frames) app.requestQuit();
}

void ActorDemoListener::onRender(Application&, Renderer2D& renderer)
{
    renderer.clear(Color::black());
    if (!room_) return;
    const Extent2D size = room_->resource().size;
    const RoomStyle style = styleOf(room_->resource().id);
    const auto width = static_cast<float>(size.width);
    const auto height = static_cast<float>(size.height);
    drawRectangle(renderer, {0, 0, width, style.horizon}, style.wall, -2);
    drawRectangle(renderer, {0, style.horizon, width, height - style.horizon}, style.floor, -2);

    // A marker on the selected actor's ground point.
    if (ActorInstance* actor = selected()) {
        const Vec2 p = actor->position();
        drawRectangle(renderer, {p.x - 10, p.y - 1, 20, 3}, {1.0f, 0.9f, 0.2f, actor->visible() ? 0.9f : 0.4f}, -1);
    }
    if (auto status = room::drawRoom(renderer, room_->drawList(), roomTextures_); !status) {
        exitCode_ = 1;
    }
}

void ActorDemoListener::updateTitle(Application& app)
{
    std::string title = "SCHUIMM ActorDemo";
    if (room_) {
        title += concat(" - room '", room_->resource().id, "'");
        if (const ActorInstance* actor = selected()) {
            title += concat(" - ", actor->id(), ": ", actor->animation(), ", ", room::facingName(actor->facing()),
                            ", x", actor->scale(), actor->visible() ? "" : ", invisible");
        }
    }
    app.window().setTitle(title);
}

void ActorDemoListener::onShutdown(Application&)
{
    // Textures before the graphics device; the room before the ScriptHost.
    room_.reset();
    roomTextures_ = {};
    textures_.reset();
    images_.reset();
    library_.reset();
}

} // namespace schuimm::actordemo
