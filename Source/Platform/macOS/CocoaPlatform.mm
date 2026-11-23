#include "Platform/macOS/CocoaPlatform.h"

#include "Core/Common/StringBuilder.h"
#include "Platform/NativePlatform.h"
#include "Platform/macOS/CocoaWindow.h"

#include <os/log.h>

#include <algorithm>
#include <string>

@interface SchuimmApplicationDelegate : NSObject <NSApplicationDelegate>
- (instancetype)initWithOwner:(schuimm::cocoa::CocoaPlatform*)owner;
- (void)detach;
@end

@implementation SchuimmApplicationDelegate {
    schuimm::cocoa::CocoaPlatform* owner_;
}

- (instancetype)initWithOwner:(schuimm::cocoa::CocoaPlatform*)owner
{
    if ((self = [super init])) {
        owner_ = owner;
    }
    return self;
}

- (void)detach
{
    owner_ = nullptr;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    (void)sender;
    // Never let AppKit call exit(): shutdown must run through the engine so that
    // OpenGL contexts and windows are released deterministically.
    if (owner_ != nullptr) {
        owner_->handleTerminateRequest();
    }
    return NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    (void)sender;
    return NO;
}
@end

namespace schuimm::cocoa {

namespace {

// Forwards log lines to the unified logging system (Console.app / `log stream`).
class UnifiedLogSink final : public LogSink {
public:
    void write(const LogRecord& record, std::string_view formattedLine) override
    {
        const std::string line(formattedLine);
        os_log_type_t type = OS_LOG_TYPE_DEFAULT;
        switch (record.level) {
        case LogLevel::Trace:
        case LogLevel::Debug: type = OS_LOG_TYPE_DEBUG; break;
        case LogLevel::Info: type = OS_LOG_TYPE_INFO; break;
        case LogLevel::Warning: type = OS_LOG_TYPE_DEFAULT; break;
        case LogLevel::Error: type = OS_LOG_TYPE_ERROR; break;
        case LogLevel::Off: return;
        }
        os_log_with_type(OS_LOG_DEFAULT, type, "%{public}s", line.c_str());
    }
};

} // namespace

CocoaPlatform::CocoaPlatform(Logger& logger)
    : logger_(logger)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        applicationDelegate_ = [[SchuimmApplicationDelegate alloc] initWithOwner:this];
        [NSApp setDelegate:applicationDelegate_];

        createMenuBar();

        // AppKit does not deliver key-up events to the window while Command is held.
        // Forward them explicitly so the engine never sees a key stuck in the down state.
        keyUpMonitor_ = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp
                                                              handler:^NSEvent*(NSEvent* event) {
                                                                  if ((event.modifierFlags & NSEventModifierFlagCommand) != 0) {
                                                                      [[NSApp keyWindow] sendEvent:event];
                                                                  }
                                                                  return event;
                                                              }];

        // We drive the event loop ourselves instead of calling [NSApp run].
        [NSApp finishLaunching];
        [NSApp activateIgnoringOtherApps:YES];
    }
    logger_.info("Platform", "Cocoa platform initialised");
}

CocoaPlatform::~CocoaPlatform()
{
    @autoreleasepool {
        if (keyUpMonitor_ != nil) {
            [NSEvent removeMonitor:keyUpMonitor_];
            keyUpMonitor_ = nil;
        }
        [applicationDelegate_ detach];
        [NSApp setDelegate:nil];
        applicationDelegate_ = nil;
    }
}

void CocoaPlatform::createMenuBar()
{
    NSString* appName = [[NSProcessInfo processInfo] processName];

    NSMenu* menuBar = [[NSMenu alloc] init];
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    [menuBar addItem:appMenuItem];

    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:[@"Hide " stringByAppendingString:appName] action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:appName] action:@selector(terminate:) keyEquivalent:@"q"];
    appMenuItem.submenu = appMenu;

    NSMenuItem* windowMenuItem = [[NSMenuItem alloc] init];
    [menuBar addItem:windowMenuItem];
    NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    NSMenuItem* fullscreenItem = [windowMenu addItemWithTitle:@"Enter Full Screen"
                                                       action:@selector(toggleFullScreen:)
                                                keyEquivalent:@"f"];
    fullscreenItem.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
    windowMenuItem.submenu = windowMenu;
    [NSApp setWindowsMenu:windowMenu];

    [NSApp setMainMenu:menuBar];
}

Result<std::unique_ptr<Window>> CocoaPlatform::createWindow(const WindowDescription& description)
{
    if (description.size.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument, "window size must be positive");
    }
    @autoreleasepool {
        auto window = std::make_unique<CocoaWindow>(*this, logger_, description);
        return std::unique_ptr<Window>(std::move(window));
    }
}

void CocoaPlatform::pollEvents()
{
    @autoreleasepool {
        for (;;) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event == nil) {
                break;
            }
            [NSApp sendEvent:event];
        }
    }
}

void CocoaPlatform::waitEvents(double timeoutSeconds)
{
    @autoreleasepool {
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate dateWithTimeIntervalSinceNow:std::max(0.0, timeoutSeconds)]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event != nil) {
            [NSApp sendEvent:event];
        }
    }
    pollEvents();
}

std::unique_ptr<LogSink> CocoaPlatform::createDiagnosticSink()
{
    return std::make_unique<UnifiedLogSink>();
}

void CocoaPlatform::registerWindow(CocoaWindow* window)
{
    windows_.push_back(window);
}

void CocoaPlatform::unregisterWindow(CocoaWindow* window)
{
    windows_.erase(std::remove(windows_.begin(), windows_.end(), window), windows_.end());
}

void CocoaPlatform::handleTerminateRequest()
{
    logger_.info("Platform", "Application quit requested by the system");
    for (CocoaWindow* window : windows_) {
        window->pushEvent(CloseRequestedEvent{});
    }
}

} // namespace schuimm::cocoa

namespace schuimm {

Result<std::unique_ptr<Platform>> createNativePlatform(Logger& logger)
{
    if (![NSThread isMainThread]) {
        return makeError(ErrorCode::InvalidState, "the Cocoa platform must be created on the main thread");
    }
    return std::unique_ptr<Platform>(std::make_unique<cocoa::CocoaPlatform>(logger));
}

} // namespace schuimm
