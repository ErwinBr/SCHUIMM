#include "TestFramework.h"

#include "Core/Common/Error.h"
#include "Core/Common/Geometry.h"
#include "Core/Common/StringBuilder.h"

using namespace schuimm;

namespace {
Result<int> parsePositive(int value)
{
    if (value <= 0) {
        return makeError(ErrorCode::InvalidArgument, "value must be positive");
    }
    return value;
}
} // namespace

SCHUIMM_TEST(Error_ResultCarriesValueOrError)
{
    const Result<int> ok = parsePositive(3);
    REQUIRE(ok.has_value());
    CHECK_EQ(*ok, 3);

    const Result<int> failed = parsePositive(-1);
    REQUIRE(!failed.has_value());
    CHECK(failed.error().code == ErrorCode::InvalidArgument);
    CHECK_EQ(describe(failed.error()), std::string("InvalidArgument: value must be positive"));
}

SCHUIMM_TEST(Geometry_ExtentEmptiness)
{
    CHECK(Extent2D{}.isEmpty());
    CHECK((Extent2D{0, 10}.isEmpty()));
    CHECK(!(Extent2D{1, 1}.isEmpty()));
}

SCHUIMM_TEST(StringBuilder_ConcatAndFixed)
{
    CHECK_EQ(concat("size ", 640, "x", 360), std::string("size 640x360"));
    CHECK_EQ(formatFixed(16.66666, 2), std::string("16.67"));
    CHECK_EQ(formatFixed(-0.5, 0), std::string("-0"));
}
