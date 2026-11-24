#include "DemoScene.h"

#include "Core/Common/StringBuilder.h"
#include "Graphics/Common/TextLayout.h"
#include "Graphics/Interface/Presentation.h"
#include "Resource/Audio/WavDecoder.h"
#include "Resource/Image/ImageLoader.h"

#include <algorithm>
#include <cmath>

namespace demo {

using namespace schuimm;

namespace {

constexpr const char* category = "Scene";
constexpr RectF worldBounds{0.0f, 0.0f, 1600.0f, 900.0f};
constexpr float horizonY = 300.0f; // the player stays on the meadow below it
constexpr float walkSpeed = 110.0f;
constexpr float minZoom = 0.75f;
constexpr float maxZoom = 3.0f;
constexpr double transitionSeconds = 0.45;

// Draw order (Sprite::layer). Props and the player use their feet y in between, so whatever
// stands lower on screen is drawn in front ("y-sorting").
constexpr int layerBackground = -100000;
constexpr int layerGround = -90000;
constexpr int layerMarker = -80000;
constexpr int layerOverlay = 100000;
constexpr int layerWorldText = 100001;

// UTF-8 source text (all compilers are set to UTF-8, see cmake/CompilerWarnings.cmake).
constexpr const char* sampleText = "Café crème, naïve Noël, Ñandú, Größe, garçon, €5";

constexpr const char* helpLines[] = {
    "WASD / arrows: walk (Shift: run)   Click: walk there",
    "Q / E or wheel: zoom   0: reset zoom",
    "Space: chime   N: pause ambience   - / =: volume",
    "Tab: pixel transition (render targets)",
    "F1: help   F11: fullscreen   V: VSync   Esc: quit",
};

float length(Vec2 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

} // namespace

DemoScene::DemoScene(Renderer2D& renderer, const AssetLocator& assets, AudioDevice* audio, Logger& logger)
    : renderer_(renderer)
    , audio_(audio)
    , logger_(logger)
    , images_(assets, loadImage)
    , textures_(renderer, images_)
    , fonts_(assets, loadFontFace)
    , sounds_(assets, loadSound)
{
}

DemoScene::~DemoScene()
{
    if (audio_ != nullptr) {
        audio_->stop(ambienceInstance_);
    }
}

Result<std::unique_ptr<DemoScene>> DemoScene::create(Renderer2D& renderer, const AssetLocator& assets, AudioDevice* audio,
                                                     Logger& logger)
{
    std::unique_ptr<DemoScene> scene(new DemoScene(renderer, assets, audio, logger));
    if (auto status = scene->load(); !status) {
        return std::unexpected(status.error());
    }
    return scene;
}

Result<std::shared_ptr<const Texture>> DemoScene::texture(const char* path, TextureSettings settings)
{
    auto id = ResourceId<Image>::parse(path);
    if (!id) {
        return std::unexpected(id.error());
    }
    return textures_.load(*id, settings);
}

Result<std::shared_ptr<const Sound>> DemoScene::sound(const char* path)
{
    auto id = ResourceId<SoundData>::parse(path);
    if (!id) {
        return std::unexpected(id.error());
    }
    // The data is decoded even without an audio device, so missing assets are always reported.
    auto data = sounds_.load(*id);
    if (!data) {
        return std::unexpected(data.error());
    }
    if (audio_ == nullptr) {
        return std::shared_ptr<const Sound>();
    }
    return audio_->createSound(**data);
}

Status DemoScene::load()
{
    constexpr TextureSettings pixelArt{TextureFilter::Nearest};
    constexpr TextureSettings tiled{TextureFilter::Nearest, TextureWrap::Repeat, TextureWrap::Repeat};
    constexpr TextureSettings smooth{TextureFilter::Linear};
    struct TextureEntry {
        std::shared_ptr<const Texture>* target;
        const char* path;
        TextureSettings settings;
    };
    const TextureEntry entries[] = {
        {&background_, "Images/background.png", pixelArt}, {&path_, "Images/path.png", tiled},
        {&player_, "Images/player.png", pixelArt},          {&tree_, "Images/tree.png", pixelArt},
        {&house_, "Images/house.png", pixelArt},            {&rock_, "Images/rock.png", pixelArt},
        {&flowers_, "Images/flowers.png", pixelArt},        {&panel_, "Images/panel.png", pixelArt},
        {&glow_, "Images/glow.png", smooth},
    };
    for (const TextureEntry& entry : entries) {
        auto loaded = texture(entry.path, entry.settings);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        *entry.target = std::move(*loaded);
    }
    // The decoded images are no longer needed once the textures exist.
    images_.unloadAll();

    auto face = fonts_.load(ResourceId<FontFace>::parse("Fonts/NotoSans-Regular.ttf").value());
    if (!face) {
        return std::unexpected(face.error());
    }
    auto uiFont = Font::create(renderer_, *face, {13, TextureFilter::Linear, {512, 512}, 4, true});
    auto titleFont = Font::create(renderer_, *face, {20, TextureFilter::Linear, {512, 512}, 4, true});
    if (!uiFont || !titleFont) {
        return std::unexpected(!uiFont ? uiFont.error() : titleFont.error());
    }
    uiFont_ = std::move(*uiFont);
    titleFont_ = std::move(*titleFont);

    const Extent2D resolution = renderer_.virtualResolution();
    auto world = renderer_.createRenderTarget({resolution, {TextureFilter::Linear}});
    if (!world) {
        return std::unexpected(world.error());
    }
    worldTarget_ = std::move(*world);
    for (std::size_t i = 0; i < pixelTargets_.size(); ++i) {
        const int divisor = 2 << i; // 2, 4, 8
        auto target = renderer_.createRenderTarget({{resolution.width / divisor, resolution.height / divisor}, {TextureFilter::Nearest}});
        if (!target) {
            return std::unexpected(target.error());
        }
        pixelTargets_[i] = std::move(*target);
    }
    auto minimap = renderer_.createRenderTarget({{160, 90}, {TextureFilter::Linear}});
    if (!minimap) {
        return std::unexpected(minimap.error());
    }
    minimapTarget_ = std::move(*minimap);

    auto chime = sound("Audio/chime.wav");
    auto step = sound("Audio/step.wav");
    auto ambience = sound("Audio/ambience.wav");
    if (!chime || !step || !ambience) {
        return std::unexpected(!chime ? chime.error() : !step ? step.error() : ambience.error());
    }
    chime_ = std::move(*chime);
    step_ = std::move(*step);
    ambience_ = std::move(*ambience);
    if (audio_ != nullptr) {
        ambienceInstance_ = audio_->play(ambience_, {0.35f, true});
    }

    // The world: two houses, trees, rocks and flowers on the meadow.
    props_ = {
        {house_.get(), {360, 470}, 2.0f, 0.0f},   {house_.get(), {1180, 420}, 2.0f, 0.0f},
        {tree_.get(), {180, 520}, 2.0f, 0.02f},  {tree_.get(), {560, 440}, 2.0f, 0.02f},
        {tree_.get(), {980, 700}, 2.5f, 0.02f},  {tree_.get(), {1420, 600}, 2.0f, 0.02f},
        {tree_.get(), {700, 820}, 2.2f, 0.02f},  {tree_.get(), {1500, 860}, 2.0f, 0.02f},
        {rock_.get(), {640, 640}, 2.0f, 0.0f},   {rock_.get(), {1060, 520}, 2.0f, 0.0f},
        {rock_.get(), {260, 780}, 2.0f, 0.0f},   {flowers_.get(), {480, 600}, 2.0f, 0.25f},
        {flowers_.get(), {870, 560}, 2.0f, 0.25f}, {flowers_.get(), {1320, 760}, 2.0f, 0.25f},
        {flowers_.get(), {120, 660}, 2.0f, 0.25f},
    };

    camera_.setViewport(resolution);
    camera_.setBounds(worldBounds);
    camera_.setZoomLimits(minZoom, maxZoom);
    camera_.setPixelSnap(true);
    camera_.setDeadZone({24.0f, 16.0f});
    camera_.setPosition(playerFeet_);
    minimapCamera_.setViewport(minimapTarget_->size());
    minimapCamera_.setZoom(static_cast<float>(minimapTarget_->size().width) / worldBounds.width);
    minimapCamera_.setPosition({worldBounds.width * 0.5f, worldBounds.height * 0.5f});

    for (const char* line : helpLines) {
        helpText_ += line;
        helpText_ += '\n';
    }

    const Status s = status();
    logger_.info(category, concat("Scene loaded: ", s.textures, " textures, ", s.renderTargets, " render targets, ",
                                  s.glyphAtlasPages, " glyph atlas pages, audio ", s.audioDescription));
    return {};
}

DemoScene::Status DemoScene::status() const
{
    Status s;
    s.textures = static_cast<int>(textures_.size());
    s.renderTargets = 2 + static_cast<int>(pixelTargets_.size());
    s.glyphAtlasPages = (uiFont_ ? uiFont_->pageCount() : 0) + (titleFont_ ? titleFont_->pageCount() : 0);
    s.missingGlyphs = (uiFont_ ? uiFont_->missingCodepoints().size() : 0) + (titleFont_ ? titleFont_->missingCodepoints().size() : 0);
    s.audioAvailable = audio_ != nullptr;
    s.audioDescription = audio_ != nullptr ? concat(audio_->backendName(), ", ", audio_->sampleRate(), " Hz") : "unavailable";
    return s;
}

// --- Update ------------------------------------------------------------------------------

void DemoScene::playChime()
{
    if (audio_ != nullptr && chime_) {
        // Pitch is fixed, so vary the volume a little; repeated presses overlap.
        const float volume = 0.6f + 0.2f * static_cast<float>(std::sin(time_ * 7.0));
        (void)audio_->play(chime_, {volume});
    }
}

void DemoScene::toggleTransition()
{
    transitionOn_ = !transitionOn_;
}

void DemoScene::handleInput(Application& app, double deltaSeconds)
{
    const InputState& input = app.input();
    Vec2 direction{};
    if (input.isDown(Key::A) || input.isDown(Key::Left)) direction.x -= 1.0f;
    if (input.isDown(Key::D) || input.isDown(Key::Right)) direction.x += 1.0f;
    if (input.isDown(Key::W) || input.isDown(Key::Up)) direction.y -= 1.0f;
    if (input.isDown(Key::S) || input.isDown(Key::Down)) direction.y += 1.0f;
    if (direction.x != 0.0f || direction.y != 0.0f) {
        walkTargetActive_ = false;
    }

    // Click to walk: window -> virtual resolution -> world.
    if (input.wasPressed(MouseButton::Left) && input.hasMousePosition()) {
        const PresentationSettings& presentation = app.graphics().presentation();
        const Vec2 screen = windowToVirtual(input.mousePosition(), app.window().size(), app.window().framebufferSize(),
                                            presentation.virtualResolution, presentation.integerScaling);
        walkTarget_ = camera_.screenToWorld(screen);
        walkTargetActive_ = true;
    }
    if (walkTargetActive_) {
        const Vec2 toTarget = walkTarget_ - playerFeet_;
        if (length(toTarget) < 3.0f) {
            walkTargetActive_ = false;
        } else {
            direction = toTarget;
        }
    }
    const float run = (input.isDown(Key::LeftShift) || input.isDown(Key::RightShift)) ? 2.0f : 1.0f;
    const float magnitude = length(direction);
    movePlayer(deltaSeconds, magnitude > 0.0f ? direction * (run / magnitude) : Vec2{});

    const auto dt = static_cast<float>(deltaSeconds);
    if (input.isDown(Key::Q)) zoomTarget_ *= std::exp(-1.2f * dt);
    if (input.isDown(Key::E)) zoomTarget_ *= std::exp(1.2f * dt);
    if (input.wheelDelta().y != 0.0f) zoomTarget_ *= std::pow(1.15f, input.wheelDelta().y);
    if (input.wasPressed(Key::Digit0)) zoomTarget_ = 1.0f;
    zoomTarget_ = std::clamp(zoomTarget_, minZoom, maxZoom);

    if (input.wasPressed(Key::Space)) playChime();
    if (input.wasPressed(Key::Tab)) toggleTransition();
    if (input.wasPressed(Key::F1)) showHelp_ = !showHelp_;
    if (audio_ != nullptr) {
        if (input.wasPressed(Key::N)) {
            if (audio_->state(ambienceInstance_) == PlaybackState::Paused) {
                audio_->resume(ambienceInstance_);
            } else {
                audio_->pause(ambienceInstance_);
            }
        }
        if (input.wasPressed(Key::Minus)) audio_->setMasterVolume(std::max(0.0f, audio_->masterVolume() - 0.1f));
        if (input.wasPressed(Key::Equals)) audio_->setMasterVolume(std::min(1.0f, audio_->masterVolume() + 0.1f));
    }
}

void DemoScene::movePlayer(double deltaSeconds, Vec2 velocity)
{
    moving_ = velocity.x != 0.0f || velocity.y != 0.0f;
    if (!moving_) {
        stepTimer_ = 0.0;
        return;
    }
    const auto dt = static_cast<float>(deltaSeconds);
    playerFeet_ += velocity * (walkSpeed * dt);
    playerFeet_.x = std::clamp(playerFeet_.x, worldBounds.x + 16.0f, worldBounds.x + worldBounds.width - 16.0f);
    playerFeet_.y = std::clamp(playerFeet_.y, horizonY + 40.0f, worldBounds.y + worldBounds.height - 4.0f);
    if (velocity.x != 0.0f) {
        facingLeft_ = velocity.x < 0.0f;
    }
    const auto speed = static_cast<double>(length(velocity));
    walkTime_ += deltaSeconds * speed;
    stepTimer_ -= deltaSeconds * speed;
    if (stepTimer_ <= 0.0) {
        stepTimer_ = 0.32;
        if (audio_ != nullptr && step_) {
            (void)audio_->play(step_, {0.45f});
        }
    }
}

void DemoScene::update(Application& app, double deltaSeconds)
{
    time_ += deltaSeconds;
    handleInput(app, deltaSeconds);

    // Smooth zoom and camera follow (both frame-rate independent).
    const float zoomBlend = 1.0f - std::exp(-10.0f * static_cast<float>(deltaSeconds));
    camera_.setZoom(camera_.zoom() + (zoomTarget_ - camera_.zoom()) * zoomBlend);
    camera_.follow(playerFeet_ - Vec2{0.0f, 24.0f}, deltaSeconds, 5.0f);

    const float step = static_cast<float>(deltaSeconds / transitionSeconds);
    transition_ = std::clamp(transition_ + (transitionOn_ ? step : -step), 0.0f, 1.0f);

    // The status text changes a few times per second; rebuilding it every frame is not needed.
    if (statusLine_.empty() || static_cast<int>(time_ * 4.0) != static_cast<int>((time_ - deltaSeconds) * 4.0)) {
        const RenderStatistics stats = app.graphics().renderer().lastFrameStatistics();
        statusLine_ = concat(formatFixed(app.clock().statistics().framesPerSecond, 0), " FPS   ", stats.quads, " quads, ",
                             stats.drawCalls, " draw calls, ", stats.renderTargetSwitches, " target switches   zoom ",
                             formatFixed(static_cast<double>(camera_.zoom()), 2), "x\n");
        if (audio_ != nullptr) {
            statusLine_ += concat("Audio: ", audio_->backendName(), ", voices ", audio_->activeVoiceCount(), ", volume ",
                                  static_cast<int>(std::lround(audio_->masterVolume() * 100.0f)), "%, ambience ",
                                  toString(audio_->state(ambienceInstance_)));
        } else {
            statusLine_ += "Audio: off (no output device, or --no-audio)";
        }
    }
}

// --- Rendering ---------------------------------------------------------------------------

void DemoScene::drawWorld(Renderer2D& renderer)
{
    Sprite background;
    background.texture = background_.get();
    background.destination = worldBounds;
    background.layer = layerBackground;
    renderer.drawSprite(background);

    // A cobblestone road: the 32x32 tile repeats (TextureWrap::Repeat) at 2 world units per texel.
    const auto road = [&](RectF area) {
        Sprite tiles;
        tiles.texture = path_.get();
        tiles.destination = area;
        tiles.source = RectF{area.x / 2.0f, area.y / 2.0f, area.width / 2.0f, area.height / 2.0f};
        tiles.layer = layerGround;
        renderer.drawSprite(tiles);
    };
    road({0, 560, 1600, 48});
    road({336, 470, 48, 90});
    road({1156, 420, 48, 140});

    if (walkTargetActive_) {
        Sprite marker;
        marker.texture = glow_.get();
        marker.destination = {walkTarget_.x - 10.0f, walkTarget_.y - 6.0f, 20.0f, 12.0f};
        marker.tint = {1.0f, 1.0f, 0.6f, 0.8f};
        marker.layer = layerMarker;
        renderer.drawSprite(marker);
    }

    for (const Prop& prop : props_) {
        const Extent2D size = prop.texture->size();
        const float w = static_cast<float>(size.width) * prop.scale;
        const float h = static_cast<float>(size.height) * prop.scale;
        Sprite sprite;
        sprite.texture = prop.texture;
        sprite.destination = {prop.feet.x - w * 0.5f, prop.feet.y - h, w, h};
        sprite.origin = {w * 0.5f, h}; // sway around the feet
        sprite.rotation = prop.sway * static_cast<float>(std::sin(time_ * 1.7 + static_cast<double>(prop.feet.x)));
        sprite.layer = static_cast<int>(prop.feet.y);
        renderer.drawSprite(sprite);
    }

    Sprite hero;
    hero.texture = player_.get();
    const int frame = moving_ ? static_cast<int>(walkTime_ * 8.0) % 4 : 0;
    hero.source = RectF{static_cast<float>(frame * 16), 0.0f, 16.0f, 24.0f};
    hero.destination = {playerFeet_.x - 16.0f, playerFeet_.y - 48.0f, 32.0f, 48.0f};
    hero.flipX = facingLeft_;
    hero.layer = static_cast<int>(playerFeet_.y);
    renderer.drawSprite(hero);

    // Fireflies: tinted, pulsing, half-transparent glows above everything.
    for (int i = 0; i < 12; ++i) {
        const double phase = i * 1.7;
        const float x = 240.0f + 110.0f * static_cast<float>(i) + 30.0f * static_cast<float>(std::sin(time_ * 0.8 + phase));
        const float y = 640.0f + 120.0f * static_cast<float>(std::sin(phase * 2.3)) + 20.0f * static_cast<float>(std::cos(time_ * 1.1 + phase));
        Sprite fly;
        fly.texture = glow_.get();
        fly.destination = {x - 12.0f, y - 12.0f, 24.0f, 24.0f};
        fly.tint = {1.0f, 0.95f, 0.5f, 0.35f + 0.25f * static_cast<float>(std::sin(time_ * 3.0 + phase))};
        fly.layer = layerOverlay;
        renderer.drawSprite(fly);
    }

    // World-space text: it moves and scales with the camera.
    drawText(renderer, *titleFont_, "Bäckerei «Zoë»", {360.0f, 290.0f},
             {Color{1.0f, 0.97f, 0.85f, 1.0f}, TextAlign::Center, layerWorldText});
    drawText(renderer, *titleFont_, "Café & Crêperie", {1180.0f, 240.0f},
             {Color{1.0f, 0.97f, 0.85f, 1.0f}, TextAlign::Center, layerWorldText});
}

void DemoScene::drawMinimap(Renderer2D& renderer)
{
    renderer.beginRenderTarget(*minimapTarget_);
    renderer.clear({0.05f, 0.07f, 0.1f, 1.0f});
    renderer.setView(minimapCamera_.viewTransform());
    Sprite background;
    background.texture = background_.get();
    background.destination = worldBounds;
    renderer.drawSprite(background);
    for (const Prop& prop : props_) {
        const Extent2D size = prop.texture->size();
        const float w = static_cast<float>(size.width) * prop.scale;
        const float h = static_cast<float>(size.height) * prop.scale;
        renderer.drawTexture(*prop.texture, {prop.feet.x - w * 0.5f, prop.feet.y - h, w, h});
    }
    renderer.fillRect({playerFeet_.x - 24.0f, playerFeet_.y - 48.0f, 48.0f, 48.0f}, {1.0f, 0.2f, 0.2f, 1.0f});
    // The main camera's view.
    const RectF view = camera_.visibleWorldRect();
    const float t = 10.0f;
    const Color frame{1.0f, 1.0f, 1.0f, 0.9f};
    renderer.fillRect({view.x, view.y, view.width, t}, frame);
    renderer.fillRect({view.x, view.y + view.height - t, view.width, t}, frame);
    renderer.fillRect({view.x, view.y, t, view.height}, frame);
    renderer.fillRect({view.x + view.width - t, view.y, t, view.height}, frame);
    renderer.endRenderTarget();
}

void DemoScene::drawPanel(Renderer2D& renderer, const RectF& r, int layer)
{
    // Nine-slice: 8-texel corners stay unscaled, edges and centre stretch.
    const float c = 8.0f;
    const float xs[4] = {r.x, r.x + c, r.x + r.width - c, r.x + r.width};
    const float ys[4] = {r.y, r.y + c, r.y + r.height - c, r.y + r.height};
    const float us[4] = {0.0f, c, 32.0f - c, 32.0f};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            Sprite piece;
            piece.texture = panel_.get();
            piece.destination = {xs[col], ys[row], xs[col + 1] - xs[col], ys[row + 1] - ys[row]};
            piece.source = RectF{us[col], us[row], us[col + 1] - us[col], us[row + 1] - us[row]};
            piece.layer = layer;
            renderer.drawSprite(piece);
        }
    }
}

void DemoScene::drawUi(Application& app, Renderer2D& renderer)
{
    (void)app;
    const Extent2D screen = renderer.targetSize();
    const auto width = static_cast<float>(screen.width);
    const auto height = static_cast<float>(screen.height);
    const TextStyle text{Color{0.95f, 0.93f, 0.85f, 1.0f}, TextAlign::Left, 1};

    if (showHelp_) {
        // The panel is sized from the measured text.
        float textWidth = measureText(*titleFont_, "SCHUIMM · Milestone 3").x;
        for (const char* line : helpLines) {
            textWidth = std::max(textWidth, measureText(*uiFont_, line).x);
        }
        const float lineAdvance = uiFont_->lineMetrics().lineHeight;
        drawPanel(renderer, {6, 6, textWidth + 22.0f, 36.0f + lineAdvance * static_cast<float>(std::size(helpLines))}, 0);
        drawText(renderer, *titleFont_, "SCHUIMM · Milestone 3", {16, 10}, {Color{1.0f, 0.85f, 0.45f, 1.0f}, TextAlign::Left, 1});
        drawText(renderer, *uiFont_, std::string_view(helpText_), {16, 38}, text);
    }

    // Minimap (a render target drawn as a texture), framed.
    const RectF minimap{width - 160.0f - 10.0f, 10.0f, 160.0f, 90.0f};
    drawPanel(renderer, {minimap.x - 4.0f, minimap.y - 4.0f, minimap.width + 8.0f, minimap.height + 8.0f}, 0);
    renderer.drawTexture(minimapTarget_->texture(), minimap);
    drawText(renderer, *uiFont_, "Minimap", {minimap.x + minimap.width * 0.5f, minimap.y + minimap.height + 4.0f},
             {Color{1, 1, 1, 0.85f}, TextAlign::Center, 1});

    drawScriptConsole(renderer, minimap.y + minimap.height + 22.0f);

    const float lineAdvance = uiFont_->lineMetrics().lineHeight;
    const float statusTop = height - 10.0f - lineAdvance * 3.0f;
    drawPanel(renderer, {6, statusTop - 4.0f, width - 12.0f, lineAdvance * 3.0f + 8.0f}, 0);
    drawText(renderer, *uiFont_, statusLine_, {16, statusTop}, text);
    drawText(renderer, *uiFont_, sampleText, {16, statusTop + lineAdvance * 2.0f},
             {Color{0.75f, 0.9f, 1.0f, 1.0f}, TextAlign::Left, 1});
    if (transition_ > 0.0f) {
        drawText(renderer, *titleFont_, "Pixel transition", {width * 0.5f, height * 0.5f - 12.0f},
                 {Color{1, 1, 1, transition_}, TextAlign::Center, 2});
    }
}

void DemoScene::addScriptOutput(std::string line)
{
    constexpr std::size_t maxLines = 7;
    scriptLines_.push_back(std::move(line));
    while (scriptLines_.size() > maxLines) {
        scriptLines_.pop_front();
    }
}

void DemoScene::drawScriptConsole(Renderer2D& renderer, float top)
{
    const float width = static_cast<float>(renderer.targetSize().width);
    const float lineAdvance = uiFont_->lineMetrics().lineHeight;
    constexpr float panelWidth = 200.0f;
    const float left = width - panelWidth - 6.0f;
    const auto lines = static_cast<float>(scriptLines_.size() + 1);
    drawPanel(renderer, {left, top, panelWidth, lineAdvance * lines + 10.0f}, 0);
    drawText(renderer, *uiFont_, scriptStatus_.empty() ? std::string_view("Scripts") : std::string_view(scriptStatus_),
             {left + 8.0f, top + 4.0f}, {Color{1.0f, 0.85f, 0.45f, 1.0f}, TextAlign::Left, 1});
    float y = top + 4.0f + lineAdvance;
    for (const std::string& line : scriptLines_) {
        drawText(renderer, *uiFont_, line, {left + 8.0f, y}, {Color{0.85f, 1.0f, 0.85f, 1.0f}, TextAlign::Left, 1});
        y += lineAdvance;
    }
}

void DemoScene::render(Application& app, Renderer2D& renderer)
{
    // 1. The world, through the camera, into an off-screen target.
    renderer.beginRenderTarget(*worldTarget_);
    renderer.clear({0.43f, 0.67f, 0.9f, 1.0f});
    renderer.setView(camera_.viewTransform());
    drawWorld(renderer);
    renderer.endRenderTarget();

    // 2. The minimap from a second camera into its own target.
    drawMinimap(renderer);

    // 3. Composite. During the transition the world is reduced through ever smaller targets
    //    and shown magnified with nearest filtering ("pixel dissolve"), and dimmed.
    const RenderTarget* source = worldTarget_.get();
    const int levels = static_cast<int>(std::lround(transition_ * static_cast<float>(pixelTargets_.size())));
    for (int i = 0; i < levels; ++i) {
        RenderTarget& target = *pixelTargets_[static_cast<std::size_t>(i)];
        renderer.beginRenderTarget(target);
        renderer.clear({0, 0, 0, 0});
        const Extent2D size = target.size();
        renderer.drawTexture(source->texture(), {0, 0, static_cast<float>(size.width), static_cast<float>(size.height)});
        renderer.endRenderTarget();
        source = &target;
    }
    renderer.clear(Color::black());
    const Extent2D screen = renderer.targetSize();
    const float dim = 1.0f - 0.35f * transition_;
    renderer.drawTexture(source->texture(), {0, 0, static_cast<float>(screen.width), static_cast<float>(screen.height)},
                         {0, 0, 1, 1}, {dim, dim, dim, 1.0f});

    // 4. Screen-space UI: unaffected by the camera.
    renderer.flush();
    drawUi(app, renderer);
}

} // namespace demo
