#pragma once

#include "Core/Application/Application.h"
#include "DemoOptions.h"
#include "DemoScene.h"
#include "Resource/AssetLocator.h"
#include "Script/Runtime/ScriptHost.h"

#include <memory>
#include <string>
#include <vector>

namespace demo {

// Application callbacks of the EngineDemo: window controls (fullscreen, VSync, size), the
// Milestone 2 scene (DemoScene), the Milestone 3 scripts (precompiled Scripts/demo.schb, run by
// the Application's ScriptHost) and the optional scripted self-test.
class DemoListener final : public schuimm::ApplicationListener {
public:
    // `assets` must outlive the listener.
    DemoListener(DemoOptions options, const schuimm::AssetLocator& assets);

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

    void handleInput(schuimm::Application& app);
    void logInputEdges(schuimm::Application& app);
    void updateTitle(schuimm::Application& app);
    void toggleFullscreen(schuimm::Application& app);
    void toggleVSync(schuimm::Application& app);
    void runSelfTest(schuimm::Application& app);
    void beginStep(schuimm::Application& app, SelfTestStep step);
    void recordResult(schuimm::Application& app, const std::string& name, bool passed, const std::string& detail);
    void saveCapturedFrame(schuimm::Application& app);
    schuimm::Status startScripts(schuimm::Application& app, schuimm::script::ScriptHost& scripts);
    void checkScripts(schuimm::Application& app);

    DemoOptions options_;
    const schuimm::AssetLocator& assets_;
    std::unique_ptr<DemoScene> scene_;
    std::vector<std::string> scriptOutput_;
    bool scriptsStarted_ = false;

    double time_ = 0.0;
    double titleTimer_ = 0.0;
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
