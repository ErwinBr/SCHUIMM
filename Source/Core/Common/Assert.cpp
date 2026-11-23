#include "Core/Common/Assert.h"

#include <cstdio>
#include <cstdlib>

namespace schuimm::detail {

void assertionFailed(const char* expression, const char* message, const char* file, int line) noexcept
{
    std::fprintf(stderr, "Assertion failed: %s\n  %s\n  at %s:%d\n", expression, message ? message : "", file, line);
    std::fflush(stderr);
    std::abort();
}

} // namespace schuimm::detail
