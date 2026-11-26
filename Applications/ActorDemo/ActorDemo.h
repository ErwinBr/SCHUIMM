#pragma once

// ActorDemo: rooms with actors, to see actors, animation, navigation and movement at work. The
// courtyard has click-to-walk navigation (walkable areas, an island, a disconnected balcony, a
// perspective scale map), a debug overlay, an animated fan, and a room script
// (Assets/Rooms/Courtyard/Courtyard.sch) that drives the gardener's rounds and a demonstration
// of the adventure API (P); the hall and garden show actors and animation.
//
// Frame order (Adventure.md): input -> room update -> adventure publish -> (Application) scripts
// -> render.
//
// Rooms are entered the way a game will: the current room instance is destroyed first, then the
// next one is created from its (cached, immutable) resources, so its actors start from their
// authored placements every time. See Documentation/Architecture/ManualTesting.md for the keys.

#include "Adventure/AdventureContext.h"
#include "Core/Application/ApplicationListener.h"
#include "Core/Image/Image.h"
#include "Graphics/Common/Camera2D.h"
#include "GraphicsResources/TextureCache.h"
#include "Resource/AssetLocator.h"
#include "Resource/ResourceCache.h"
#include "Room/RoomInstance.h"
#include "Room/RoomLibrary.h"
#include "RoomGraphics/RoomTextures.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace schuimm::actordemo {

struct Options {
    std::uint64_t frames = 0; // Quit after this many frames (0: run until closed).
    bool automatic = false;   // Drive the demo with a timed sequence of the same actions as the keys.
    std::string assetRoot;
    std::string screenshotPath; // With frames: the last frame as a PPM image.
    bool showHelp = false;
};

class ActorDemoListener final : public ApplicationListener {
public:
    ActorDemoListener(Options options, const AssetLocator& assets);
    ~ActorDemoListener() override;

    Status onInitialize(Application& app) override;
    void onUpdate(Application& app, double deltaSeconds) override;
    void onRender(Application& app, Renderer2D& renderer) override;
    void onShutdown(Application& app) override;

    [[nodiscard]] int exitCode() const noexcept { return exitCode_; }

private:
    enum class Action {
        NextRoom,
        ReenterRoom,
        SelectNext,
        Wave,
        Idle,
        TogglePause,
        ToggleVisible,
        ScaleUp,
        ScaleDown,
    };

    void enterRoom(Application& app, std::size_t index);
    void perform(Application& app, Action action);
    void setFacing(Application& app, room::Facing facing);
    void handleKeys(Application& app);
    void runAutomaticStep(Application& app);
    void updateTitle(Application& app);
    void handleMouse(Application& app);
    void walkHero(Application& app, Vec2 destination);
    void demonstrate(Application& app);
    void useFan(Application& app);
    void drawDebug(Renderer2D& renderer);
    [[nodiscard]] room::ActorInstance* selected();

    Options options_;
    const AssetLocator& assets_;
    std::vector<AssetPath> rooms_;
    std::size_t roomIndex_ = 0;
    std::size_t selected_ = 0;

    std::unique_ptr<room::RoomLibrary> library_;
    std::unique_ptr<ResourceCache<Image>> images_;
    std::unique_ptr<TextureCache> textures_;
    room::RoomTextures roomTextures_;
    std::unique_ptr<room::RoomInstance> room_;

    Camera2D camera_;
    // The adventure API's view of the game: the active room and the camera (bound to the
    // ScriptHost's natives in onInitialize, before any room script is loaded).
    adventure::AdventureContext adventure_;
    script::CoroutineHandle demonstration_;
    bool debug_ = false;

    // --auto: first the scripted demonstration, then the timed sequence of key actions.
    bool automaticScripted_ = true;
    std::uint64_t automaticSequenceStart_ = 0;
    std::size_t automaticStep_ = 0;
    bool captureRequested_ = false;
    bool captured_ = false;
    int exitCode_ = 0;
};

} // namespace schuimm::actordemo
