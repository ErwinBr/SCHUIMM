#include "Room/ObjectHandles.h"

#include <atomic>
#include <cstdint>

namespace schuimm::room::detail {

script::ObjectRef allocateObjectHandle() noexcept
{
    // Starts at 1: 0 is the null handle.
    static std::atomic<std::uint64_t> next{1};
    return script::ObjectRef{next.fetch_add(1, std::memory_order_relaxed)};
}

} // namespace schuimm::room::detail
