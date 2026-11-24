#pragma once

// Arithmetic, conversion and formatting rules of the scripting language, shared by the VM and
// the optimizer's constant folding so both always produce identical results
// (Documentation/Scripting/Language.md, "Arithmetic").

#include <cstdint>
#include <string>
#include <string_view>

namespace schuimm::script {

enum class ArithmeticError : std::uint8_t {
    None,
    Overflow,          // int64 result out of range.
    DivisionByZero,    // Integer or float division or modulo by zero.
    InvalidConversion, // float -> int of NaN, infinity or an out-of-range value.
};

[[nodiscard]] const char* describe(ArithmeticError error) noexcept;

struct IntResult {
    std::int64_t value = 0;
    ArithmeticError error = ArithmeticError::None;
};

struct FloatResult {
    double value = 0.0;
    ArithmeticError error = ArithmeticError::None;
};

[[nodiscard]] IntResult addInt(std::int64_t a, std::int64_t b) noexcept;
[[nodiscard]] IntResult subInt(std::int64_t a, std::int64_t b) noexcept;
[[nodiscard]] IntResult mulInt(std::int64_t a, std::int64_t b) noexcept;
[[nodiscard]] IntResult negInt(std::int64_t a) noexcept;
// Floored division and modulo (the result of % has the sign of the divisor), like Python.
[[nodiscard]] IntResult floorDivInt(std::int64_t a, std::int64_t b) noexcept;
[[nodiscard]] IntResult floorModInt(std::int64_t a, std::int64_t b) noexcept;
// a + b clamped to the int64 range: the range() loop counter step, which must not trap.
[[nodiscard]] std::int64_t addIntSaturating(std::int64_t a, std::int64_t b) noexcept;

[[nodiscard]] FloatResult divFloat(double a, double b) noexcept;
[[nodiscard]] FloatResult floorDivFloat(double a, double b) noexcept;
[[nodiscard]] FloatResult floorModFloat(double a, double b) noexcept;

// Truncation toward zero; NaN, infinities and values outside int64 fail.
[[nodiscard]] IntResult floatToInt(double value) noexcept;

// Text of values as str() and print() produce it.
//   int:   decimal, e.g. "-42".
//   float: the shortest decimal that reads back to the same double, always with a '.' or an
//          exponent ("1.0", "0.1", "1e+20", "inf", "-inf", "nan"). Independent of the C locale.
//   bool:  "True" / "False".
[[nodiscard]] std::string formatInt(std::int64_t value);
[[nodiscard]] std::string formatFloat(double value);
[[nodiscard]] const char* formatBool(bool value) noexcept;

// Byte-wise three-way comparison of UTF-8 strings: <0, 0 or >0.
[[nodiscard]] int compareStrings(std::string_view a, std::string_view b) noexcept;

// Bitwise identity of doubles (distinguishes -0.0 from 0.0 and treats equal NaN payloads as
// equal): used to deduplicate constants and compare folded values, not for script ==.
[[nodiscard]] bool sameFloatBits(double a, double b) noexcept;

} // namespace schuimm::script
