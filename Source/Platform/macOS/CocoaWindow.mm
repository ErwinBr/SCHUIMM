#include "Platform/macOS/CocoaWindow.h"

#include "Core/Common/Assert.h"
#include "Core/Common/StringBuilder.h"
#include "Platform/macOS/CocoaKeyMap.h"
#include "Platform/macOS/CocoaOpenGLSurface.h"
#include "Platform/macOS/CocoaPlatform.h"

#include <cmath>

using schuimm::cocoa::CocoaWindow;

// ---------------------------------------------------------------------------------------------
// Content view: receives keyboard and mouse events. Flipped so that y grows downwards,
// matching the engine's window coordinate convention.
// ---------------------------------------------------------------------------------------------
@interface SchuimmContentView : NSView
- (instancetype)initWithFrame:(NSRect)frame owner:(CocoaWindow*)owner;
- (void)detach;
@end

@implementation SchuimmContentView {
    CocoaWindow* owner_;
    NSTrackingArea* trackingArea_;
}

- (instancetype)initWithFrame:(NSRect)frame owner:(CocoaWindow*)owner
{
    if ((self = [super initWithFrame:frame])) {
        owner_ = owner;
        // Render at native (Retina) resolution; framebufferSize() reports pixels.
        self.wantsBestResolutionOpenGLSurface = YES;
        [self updateTrackingAreas];
    }
    return self;
}

- (void)detach
{
    owner_ = nullptr;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { (void)event; return YES; }
- (BOOL)isOpaque { return YES; }

- (void)updateTrackingAreas
{
    if (trackingArea_ != nil) {
        [self removeTrackingArea:trackingArea_];
    }
    const NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                                          NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect |
                                          NSTrackingEnabledDuringMouseDrag;
    trackingArea_ = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:options owner:self userInfo:nil];
    [self addTrackingArea:trackingArea_];
    [super updateTrackingAreas];
}

- (void)viewDidChangeBackingProperties
{
    [super viewDidChangeBackingProperties];
    if (owner_ != nullptr) {
        owner_->handleGeometryChange();
    }
}

- (schuimm::Vec2)positionOf:(NSEvent*)event
{
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    return {static_cast<float>(point.x), static_cast<float>(point.y)};
}

- (void)pushButton:(NSEvent*)event button:(schuimm::MouseButton)button down:(bool)down
{
    if (owner_ != nullptr) {
        owner_->pushEvent(schuimm::MouseButtonEvent{button, down, [self positionOf:event]});
    }
}

- (void)pushMove:(NSEvent*)event
{
    if (owner_ != nullptr) {
        owner_->pushEvent(schuimm::MouseMovedEvent{[self positionOf:event]});
    }
}

static schuimm::MouseButton otherButton(NSInteger buttonNumber)
{
    switch (buttonNumber) {
    case 2: return schuimm::MouseButton::Middle;
    case 3: return schuimm::MouseButton::Extra1;
    case 4: return schuimm::MouseButton::Extra2;
    default: return schuimm::MouseButton::Count; // ignored by InputState
    }
}

- (void)mouseDown:(NSEvent*)event { [self pushButton:event button:schuimm::MouseButton::Left down:true]; }
- (void)mouseUp:(NSEvent*)event { [self pushButton:event button:schuimm::MouseButton::Left down:false]; }
- (void)rightMouseDown:(NSEvent*)event { [self pushButton:event button:schuimm::MouseButton::Right down:true]; }
- (void)rightMouseUp:(NSEvent*)event { [self pushButton:event button:schuimm::MouseButton::Right down:false]; }
- (void)otherMouseDown:(NSEvent*)event { [self pushButton:event button:otherButton(event.buttonNumber) down:true]; }
- (void)otherMouseUp:(NSEvent*)event { [self pushButton:event button:otherButton(event.buttonNumber) down:false]; }
- (void)mouseMoved:(NSEvent*)event { [self pushMove:event]; }
- (void)mouseDragged:(NSEvent*)event { [self pushMove:event]; }
- (void)rightMouseDragged:(NSEvent*)event { [self pushMove:event]; }
- (void)otherMouseDragged:(NSEvent*)event { [self pushMove:event]; }

- (void)scrollWheel:(NSEvent*)event
{
    if (owner_ == nullptr) {
        return;
    }
    double dx = event.scrollingDeltaX;
    double dy = event.scrollingDeltaY;
    if (event.hasPreciseScrollingDeltas) {
        // Trackpads report pixels; scale to approximately "lines" like a wheel notch.
        dx *= 0.1;
        dy *= 0.1;
    }
    if (dx != 0.0 || dy != 0.0) {
        owner_->pushEvent(schuimm::MouseWheelEvent{{static_cast<float>(dx), static_cast<float>(dy)}});
    }
}

- (void)keyDown:(NSEvent*)event
{
    // Deliberately not calling super: that would make AppKit beep for unhandled keys.
    if (owner_ != nullptr) {
        owner_->pushEvent(schuimm::KeyEvent{schuimm::cocoa::translateKeyCode(event.keyCode), true, event.ARepeat == YES});
    }
}

- (void)keyUp:(NSEvent*)event
{
    if (owner_ != nullptr) {
        owner_->pushEvent(schuimm::KeyEvent{schuimm::cocoa::translateKeyCode(event.keyCode), false, false});
    }
}

- (void)flagsChanged:(NSEvent*)event
{
    // Modifier keys only generate flagsChanged. The device-dependent bits distinguish left/right.
    if (owner_ == nullptr) {
        return;
    }
    const std::uint32_t mask = schuimm::cocoa::deviceModifierMask(event.keyCode);
    if (mask == 0) {
        return;
    }
    const bool down = (static_cast<std::uint64_t>(event.modifierFlags) & mask) != 0;
    owner_->pushEvent(schuimm::KeyEvent{schuimm::cocoa::translateKeyCode(event.keyCode), down, false});
}
@end

// ---------------------------------------------------------------------------------------------
// Window delegate: close requests, resizing, focus, minimise and fullscreen transitions.
// ---------------------------------------------------------------------------------------------
@interface SchuimmWindowDelegate : NSObject <NSWindowDelegate>
- (instancetype)initWithOwner:(CocoaWindow*)owner;
- (void)detach;
@end

@implementation SchuimmWindowDelegate {
    CocoaWindow* owner_;
}

- (instancetype)initWithOwner:(CocoaWindow*)owner
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

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    (void)sender;
    // The application decides; the window is destroyed by the engine, never by AppKit.
    if (owner_ != nullptr) {
        owner_->pushEvent(schuimm::CloseRequestedEvent{});
    }
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification
{
    (void)notification;
    if (owner_ != nullptr) {
        owner_->handleGeometryChange();
    }
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification
{
    (void)notification;
    if (owner_ != nullptr) {
        owner_->handleGeometryChange();
    }
}

- (void)windowDidBecomeKey:(NSNotification*)notification
{
    (void)notification;
    if (owner_ != nullptr) {
        owner_->pushEvent(schuimm::FocusChangedEvent{true});
    }
}

- (void)windowDidResignKey:(NSNotification*)notification
{
    (void)notification;
    if (owner_ != nullptr) {
        owner_->pushEvent(schuimm::FocusChangedEvent{false});
    }
}

- (void)windowDidMiniaturize:(NSNotification*)notification
{
    (void)notification;
    if (owner_ != nullptr) {
        owner_->handleVisibilityChange();
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)notification
{
    (void)notification;
    if (owner_ != nullptr) {
        owner_->handleVisibilityChange();
    }
}

- (void)windowDidChangeOcclusionState:(NSNotification*)notification
{
    (void)notification;
    if (owner_ != nullptr) {
        owner_->handleVisibilityChange();
    }
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification
{
    (void)notification;
    if (owner_ != nullptr) {
        owner_->handleFullscreenChange(true);
    }
}

- (void)windowDidExitFullScreen:(NSNotification*)notification
{
    (void)notification;
    if (owner_ != nullptr) {
        owner_->handleFullscreenChange(false);
    }
}
@end

// ---------------------------------------------------------------------------------------------
namespace schuimm::cocoa {

namespace {
Extent2D toExtent(NSSize size)
{
    return {static_cast<int>(std::lround(size.width)), static_cast<int>(std::lround(size.height))};
}
} // namespace

CocoaWindow::CocoaWindow(CocoaPlatform& platform, Logger& logger, const WindowDescription& description)
    : platform_(platform)
    , logger_(logger)
{
    const NSRect contentRect = NSMakeRect(0, 0, description.size.width, description.size.height);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    if (description.resizable) {
        style |= NSWindowStyleMaskResizable;
    }

    window_ = [[NSWindow alloc] initWithContentRect:contentRect styleMask:style backing:NSBackingStoreBuffered defer:NO];
    // Ownership stays with this object (ARC); AppKit must not release the window on close.
    window_.releasedWhenClosed = NO;
    window_.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    window_.acceptsMouseMovedEvents = YES;
    window_.title = [NSString stringWithUTF8String:description.title.c_str()];

    view_ = [[SchuimmContentView alloc] initWithFrame:contentRect owner:this];
    delegate_ = [[SchuimmWindowDelegate alloc] initWithOwner:this];
    window_.contentView = view_;
    window_.delegate = delegate_;
    [window_ makeFirstResponder:view_];
    [window_ center];
    [window_ makeKeyAndOrderFront:nil];

    platform_.registerWindow(this);
    reportedHidden_ = queryHidden();
    logger_.info("Platform", concat("Created NSWindow ", description.size.width, "x", description.size.height,
                                    " (framebuffer ", framebufferSize().width, "x", framebufferSize().height, ")"));

    if (description.fullscreen) {
        setFullscreen(true);
    }
}

CocoaWindow::~CocoaWindow()
{
    SCHUIMM_VERIFY(!hasSurface_, "the OpenGL surface must be destroyed before its window");
    @autoreleasepool {
        platform_.unregisterWindow(this);
        [view_ detach];
        [delegate_ detach];
        window_.delegate = nil;
        [window_ orderOut:nil];
        [window_ close];
        view_ = nil;
        delegate_ = nil;
        window_ = nil;
    }
}

Extent2D CocoaWindow::size() const
{
    return toExtent(view_.bounds.size);
}

Extent2D CocoaWindow::framebufferSize() const
{
    return toExtent([view_ convertRectToBacking:view_.bounds].size);
}

void CocoaWindow::setSize(Extent2D size)
{
    if (size.isEmpty() || isFullscreen()) {
        return;
    }
    [window_ setContentSize:NSMakeSize(size.width, size.height)];
}

void CocoaWindow::setTitle(const std::string& title)
{
    window_.title = [NSString stringWithUTF8String:title.c_str()];
}

void CocoaWindow::setFullscreen(bool fullscreen)
{
    if (fullscreen != isFullscreen()) {
        [window_ toggleFullScreen:nil];
    }
}

bool CocoaWindow::isFullscreen() const
{
    return (window_.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

bool CocoaWindow::isFocused() const
{
    return window_.keyWindow == YES;
}

bool CocoaWindow::isMinimized() const
{
    // The state last reported through MinimizedChangedEvent, so that queries and events never
    // disagree (see handleVisibilityChange).
    return reportedHidden_;
}

bool CocoaWindow::queryHidden() const
{
    // A fully occluded window counts as minimised: AppKit stops throttling buffer swaps for
    // windows that are not visible, so rendering one would spin the CPU at thousands of frames
    // per second even with VSync enabled. Right after creation the occlusion state may not yet
    // say "visible"; windowDidChangeOcclusionState: follows as soon as it does.
    const bool visible = (window_.occlusionState & NSWindowOcclusionStateVisible) != 0;
    return window_.miniaturized == YES || !visible;
}

void CocoaWindow::handleVisibilityChange()
{
    const bool hidden = queryHidden();
    if (hidden != reportedHidden_) {
        reportedHidden_ = hidden;
        events_.push(MinimizedChangedEvent{hidden});
    }
}

void CocoaWindow::drainEvents(std::vector<WindowEvent>& out)
{
    events_.drainInto(out);
}

void CocoaWindow::setLiveResizeCallback(std::function<void()> callback)
{
    liveResizeCallback_ = std::move(callback);
}

Result<std::unique_ptr<OpenGLSurface>> CocoaWindow::createOpenGLSurface(const OpenGLSurfaceDescription& description)
{
    if (hasSurface_) {
        return makeError(ErrorCode::InvalidState, "window already has an OpenGL surface");
    }
    return CocoaOpenGLSurface::create(*this, logger_, description);
}

void CocoaWindow::handleGeometryChange()
{
    // NSOpenGLContext must be told when its drawable changes size or backing scale.
    if (NSOpenGLContext* context = context_) {
        [context update];
    }
    events_.push(WindowResizedEvent{size(), framebufferSize()});
    if (view_.inLiveResize && liveResizeCallback_) {
        liveResizeCallback_();
    }
}

void CocoaWindow::handleFullscreenChange(bool fullscreen)
{
    events_.push(FullscreenChangedEvent{fullscreen});
    handleGeometryChange();
}

NSView* CocoaWindow::contentView() const
{
    return view_;
}

void CocoaWindow::attachOpenGLContext(NSOpenGLContext* context)
{
    context_ = context;
    hasSurface_ = true;
}

void CocoaWindow::detachOpenGLContext()
{
    context_ = nil;
    hasSurface_ = false;
}

} // namespace schuimm::cocoa
