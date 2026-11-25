#pragma once

// ActorDemo: two rooms with actors, to see the actor and animation foundation at work.
//
// Rooms are entered the way a game will: the current room instance is destroyed first, then the
// next one is created from its (cached, immutable) resources, so its actors start from their
// authored placements every time. See Documentation/Architecture/ManualTesting.md for the keys.

#include "Core/Application/ApplicationListener.h"
#include "Core/Image/Image.h"
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

    std::size_t automaticStep_ = 0;
    int exitCode_ = 0;
};

} // namespace schuimm::actordemo
