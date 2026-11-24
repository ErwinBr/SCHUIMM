#pragma once

#include "Audio/AudioDevice.h"
#include "Core/Application/Application.h"
#include "Core/Audio/SoundData.h"
#include "Graphics/Common/Camera2D.h"
#include "GraphicsResources/Font.h"
#include "GraphicsResources/TextureCache.h"
#include "Resource/AssetLocator.h"
#include "Resource/Font/FontFace.h"
#include "Resource/ResourceCache.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace demo {

// The Milestone 2 demonstration scene: a small world larger than the screen with a background
// image, props and a walking player (sprites with transparency, y-sorted layers), a following
// camera with zoom, text in world space and in a screen-space UI, a render-target based
// "pixel dissolve" room transition and a minimap rendered from a second camera, plus sound
// effects, a looping ambience and volume control.
//
// All assets are loaded in create(); nothing is loaded or decoded while frames are drawn.
// The scene owns GPU resources (textures, fonts, render targets), so it must be destroyed
// before the graphics device (DemoListener does that in onShutdown()).
class DemoScene {
public:
    struct Status {
        int textures = 0;
        int renderTargets = 0;
        std::size_t glyphAtlasPages = 0;
        std::size_t missingGlyphs = 0;
        bool audioAvailable = false;
        std::string audioDescription;
    };

    // `assets` must outlive the scene. `audio` may be null (no audio device).
    [[nodiscard]] static schuimm::Result<std::unique_ptr<DemoScene>> create(schuimm::Renderer2D& renderer,
                                                                           const schuimm::AssetLocator& assets,
                                                                           schuimm::AudioDevice* audio,
                                                                           schuimm::Logger& logger);
    ~DemoScene();

    void update(schuimm::Application& app, double deltaSeconds);
    void render(schuimm::Application& app, schuimm::Renderer2D& renderer);

    // Scripted actions (keys or the self-test).
    void playChime();
    void toggleTransition();

    [[nodiscard]] Status status() const;

private:
    struct Prop {
        const schuimm::Texture* texture = nullptr;
        schuimm::Vec2 feet;  // bottom centre in world units
        float scale = 2.0f;  // world units per texel
        float sway = 0.0f;   // rotation amplitude (radians), around the feet
    };

    DemoScene(schuimm::Renderer2D& renderer, const schuimm::AssetLocator& assets, schuimm::AudioDevice* audio,
              schuimm::Logger& logger);
    schuimm::Status load();
    schuimm::Result<std::shared_ptr<const schuimm::Texture>> texture(const char* path, schuimm::TextureSettings settings);
    schuimm::Result<std::shared_ptr<const schuimm::Sound>> sound(const char* path);

    void handleInput(schuimm::Application& app, double deltaSeconds);
    void movePlayer(double deltaSeconds, schuimm::Vec2 direction);
    void drawWorld(schuimm::Renderer2D& renderer);
    void drawMinimap(schuimm::Renderer2D& renderer);
    void drawUi(schuimm::Application& app, schuimm::Renderer2D& renderer);
    void drawPanel(schuimm::Renderer2D& renderer, const schuimm::RectF& rect, int layer);

    schuimm::Renderer2D& renderer_;
    schuimm::AudioDevice* audio_;
    schuimm::Logger& logger_;

    // Resource caches (CPU) and the texture cache (GPU) on top of the image cache.
    schuimm::ResourceCache<schuimm::Image> images_;
    schuimm::TextureCache textures_;
    schuimm::ResourceCache<schuimm::FontFace> fonts_;
    schuimm::ResourceCache<schuimm::SoundData> sounds_;

    std::shared_ptr<const schuimm::Texture> background_;
    std::shared_ptr<const schuimm::Texture> path_;
    std::shared_ptr<const schuimm::Texture> player_;
    std::shared_ptr<const schuimm::Texture> tree_;
    std::shared_ptr<const schuimm::Texture> house_;
    std::shared_ptr<const schuimm::Texture> rock_;
    std::shared_ptr<const schuimm::Texture> flowers_;
    std::shared_ptr<const schuimm::Texture> panel_;
    std::shared_ptr<const schuimm::Texture> glow_;
    std::unique_ptr<schuimm::Font> uiFont_;
    std::unique_ptr<schuimm::Font> titleFont_;

    // The world is rendered into worldTarget_ every frame and then composited; during the
    // transition it is reduced through progressively smaller targets (nearest-filtered).
    std::unique_ptr<schuimm::RenderTarget> worldTarget_;
    std::array<std::unique_ptr<schuimm::RenderTarget>, 3> pixelTargets_;
    std::unique_ptr<schuimm::RenderTarget> minimapTarget_;

    std::shared_ptr<const schuimm::Sound> chime_;
    std::shared_ptr<const schuimm::Sound> step_;
    std::shared_ptr<const schuimm::Sound> ambience_;
    schuimm::SoundInstance ambienceInstance_;

    schuimm::Camera2D camera_;
    schuimm::Camera2D minimapCamera_;
    std::vector<Prop> props_;

    schuimm::Vec2 playerFeet_{800.0f, 620.0f};
    bool walkTargetActive_ = false;
    schuimm::Vec2 walkTarget_{};
    bool facingLeft_ = false;
    bool moving_ = false;
    double walkTime_ = 0.0;
    double stepTimer_ = 0.0;
    double time_ = 0.0;
    float zoomTarget_ = 1.0f;
    bool transitionOn_ = false;
    float transition_ = 0.0f; // 0 = clear, 1 = fully pixelated
    bool showHelp_ = true;
    std::string statusLine_;
    std::string helpText_;
};

} // namespace demo
