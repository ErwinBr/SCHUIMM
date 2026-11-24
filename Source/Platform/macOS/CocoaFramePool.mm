// Compiled WITHOUT ARC (see Source/Platform/CMakeLists.txt): NSAutoreleasePool is unavailable
// under ARC, and only a manually managed pool can stay open between two function calls.
#if __has_feature(objc_arc)
#error "CocoaFramePool.mm must be compiled with -fno-objc-arc"
#endif

#include "Platform/macOS/CocoaFramePool.h"

#import <Foundation/Foundation.h>

namespace schuimm::cocoa {

FramePool::FramePool()
    : pool_([[NSAutoreleasePool alloc] init])
{
}

FramePool::~FramePool()
{
    [static_cast<NSAutoreleasePool*>(pool_) drain];
}

void FramePool::cycle()
{
    [static_cast<NSAutoreleasePool*>(pool_) drain];
    pool_ = [[NSAutoreleasePool alloc] init];
}

} // namespace schuimm::cocoa
