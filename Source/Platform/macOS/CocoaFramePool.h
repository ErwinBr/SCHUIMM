#pragma once

// Internal to the macOS backend.

namespace schuimm::cocoa {

// An autorelease pool that spans one iteration of the engine's main loop.
//
// The engine drives the event loop itself instead of calling [NSApp run], so nothing drains
// the objects that AppKit and Apple's OpenGL implementation autorelease while a frame is
// updated and rendered (window titles, Metal command objects created by GL calls, ...).
// Without this pool they leak, roughly a dozen objects per frame. cycle() drains the pool of
// the previous frame and opens a new one; it is called at the start of every pollEvents() and
// waitEvents(). The pool is always the innermost one when cycled, because every other pool on
// the main thread is a scoped @autoreleasepool that has already been popped.
//
// Implemented with NSAutoreleasePool in a file compiled without ARC, because ARC code cannot
// keep a pool open across function boundaries.
class FramePool {
public:
    FramePool();
    ~FramePool();

    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;

    void cycle();

private:
    void* pool_ = nullptr;
};

} // namespace schuimm::cocoa
