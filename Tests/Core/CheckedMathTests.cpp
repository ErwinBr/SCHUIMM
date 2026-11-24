#include "TestFramework.h"

#include "Core/Common/CheckedMath.h"

#include <cstdint>
#include <limits>

using namespace schuimm;

namespace {
constexpr std::size_t maxSize = std::numeric_limits<std::size_t>::max();
} // namespace

SCHUIMM_TEST(CheckedMath_AddDetectsOverflow)
{
    CHECK(checkedAdd(0, 0) == std::optional<std::size_t>(0));
    CHECK(checkedAdd(2, 3) == std::optional<std::size_t>(5));
    CHECK(checkedAdd(maxSize, 0) == std::optional<std::size_t>(maxSize));
    CHECK(checkedAdd(0, maxSize) == std::optional<std::size_t>(maxSize));
    CHECK(checkedAdd(maxSize - 1, 1) == std::optional<std::size_t>(maxSize));
    CHECK(!checkedAdd(maxSize, 1).has_value());
    CHECK(!checkedAdd(1, maxSize).has_value());
    CHECK(!checkedAdd(maxSize, maxSize).has_value());
    CHECK(!checkedAdd(maxSize / 2 + 1, maxSize / 2 + 1).has_value());
}

SCHUIMM_TEST(CheckedMath_MulDetectsOverflow)
{
    CHECK(checkedMul(0, maxSize) == std::optional<std::size_t>(0));
    CHECK(checkedMul(maxSize, 0) == std::optional<std::size_t>(0));
    CHECK(checkedMul(1, maxSize) == std::optional<std::size_t>(maxSize));
    CHECK(checkedMul(6, 7) == std::optional<std::size_t>(42));
    CHECK(checkedMul(maxSize / 2, 2) == std::optional<std::size_t>(maxSize - 1));
    CHECK(!checkedMul(maxSize / 2 + 1, 2).has_value());
    CHECK(!checkedMul(2, maxSize / 2 + 1).has_value());
    CHECK(!checkedMul(maxSize, maxSize).has_value());
    constexpr std::size_t half = std::size_t{1} << (std::numeric_limits<std::size_t>::digits / 2);
    CHECK(!checkedMul(half, half).has_value());
    CHECK(checkedMul(half - 1, half + 1) == std::optional<std::size_t>(half * half - 1));
}

SCHUIMM_TEST(CheckedMath_UsableInConstantExpressions)
{
    static_assert(checkedMul(4, 1024).value() == 4096);
    static_assert(!checkedAdd(maxSize, 1).has_value());
    CHECK(true);
}
