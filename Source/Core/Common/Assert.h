#pragma once

// Assertions guard internal invariants (programming errors), not runtime failures.
// Runtime failures are reported through Result/Status (see Error.h).
//
// SCHUIMM_ASSERT is active unless NDEBUG is defined or SCHUIMM_DISABLE_ASSERTS is set.
// SCHUIMM_VERIFY is always active.

namespace schuimm::detail {
[[noreturn]] void assertionFailed(const char* expression, const char* message, const char* file, int line) noexcept;
} // namespace schuimm::detail

#define SCHUIMM_VERIFY(condition, message)                                                          \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            ::schuimm::detail::assertionFailed(#condition, (message), __FILE__, __LINE__);          \
        }                                                                                           \
    } while (false)

#if defined(NDEBUG) || defined(SCHUIMM_DISABLE_ASSERTS)
#define SCHUIMM_ASSERT(condition, message) ((void)0)
#else
#define SCHUIMM_ASSERT(condition, message) SCHUIMM_VERIFY(condition, message)
#endif
