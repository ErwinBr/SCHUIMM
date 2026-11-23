#pragma once

#include "Core/Application/Application.h"
#include "DemoOptions.h"
#include "Graphics/Interface/Texture.h"

#include <memory>
#include <string>
#include <vector>

namespace demo {

// The manual integration test scene: a coloured triangle, a textured quad and alpha-blended
// sprites, plus keyboard/mouse interaction and an optional scripted self-test.
class DemoListener final : public schuimm::ApplicationListener {
public:
    explicit DemoListener(DemoOptions options);

    schuimm::Status onInitialize(schuimm::Application& app) override;
    void onUpdate(schuimm::Application& app, double deltaSeconds) override;
    void onRender(schuimm::Application& app, schuimm::Renderer2D& renderer) override;
    void onResized(schuimm::Application& app, schuimm::Extent2D windowSize, schuimm::Extent2D framebufferSize) override;
    void onFullscreenChanged(schuimm::Application& app, bool fullscreen) override;
    void onFocusChanged(schuimm::Application& app, bool focused) override;
    void onSuspend(schuimm::Application& app) override;
    void onResume(schuimm::Application& app) override;
    void onShutdown(schuimm::Application& app) override;

    [[nodiscard]] int exitCode() const noexcept { return exitCode_; }

private:
    enum class SelfTestStep { Start, Resize, FullscreenOn, FullscreenOff, VSyncOff, VSyncOn, Screenshot, Done };

    void handleInput(schuimm::Application& app, double deltaSeconds);
    void logInputEdges(schuimm::Application& app);
    void updateTitle(schuimm::Application& app);
    void toggleFullscreen(schuimm::Application& app);
    void toggleVSync(schuimm::Application& app);
    void runSelfTest(schuimm::Application& app);
    void beginStep(schuimm::Application& app, SelfTestStep step);
    void recordResult(schuimm::Application& app, const std::string& name, bool passed, const std::string& detail);
    void saveCapturedFrame(schuimm::Application& app);

    DemoOptions options_;
    std::unique_ptr<schuimm::Texture> checkerboard_;
    std::unique_ptr<schuimm::Texture> sprite_;

    double time_ = 0.0;
    double titleTimer_ = 0.0;
    schuimm::Vec2 trianglePosition_{150.0f, 190.0f};
    schuimm::Vec2 spritePosition_{320.0f, 180.0f};
    float spriteScale_ = 1.0f;
    bool captureRequested_ = false;
    int exitCode_ = 0;

    // Self-test state.
    SelfTestStep step_ = SelfTestStep::Start;
    double stepStartTime_ = 0.0;
    std::uint64_t stepStartFrame_ = 0;
    bool sawFullscreenEvent_ = false;
    bool sawResizeEvent_ = false;
    bool leavingInitialFullscreen_ = false;
    double fpsVSyncOff_ = 0.0;
    double fpsVSyncOn_ = 0.0;
    std::vector<std::string> failures_;
};

} // namespace demo
