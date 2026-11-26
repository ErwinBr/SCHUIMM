#include "Script/Bytecode/Semantics.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>

namespace schuimm::script {

namespace {
constexpr std::int64_t minInt = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t maxInt = std::numeric_limits<std::int64_t>::max();

IntResult overflow() noexcept { return {0, ArithmeticError::Overflow}; }
} // namespace

const char* describe(ArithmeticError error) noexcept
{
    switch (error) {
    case ArithmeticError::None: return "no error";
    case ArithmeticError::Overflow: return "Integer overflow";
    case ArithmeticError::DivisionByZero: return "Division by zero";
    case ArithmeticError::InvalidConversion: return "Float value cannot be converted to int";
    }
    return "Arithmetic error";
}

IntResult addInt(std::int64_t a, std::int64_t b) noexcept
{
    if ((b > 0 && a > maxInt - b) || (b < 0 && a < minInt - b)) {
        return overflow();
    }
    return {a + b, ArithmeticError::None};
}

IntResult subInt(std::int64_t a, std::int64_t b) noexcept
{
    if ((b < 0 && a > maxInt + b) || (b > 0 && a < minInt + b)) {
        return overflow();
    }
    return {a - b, ArithmeticError::None};
}

IntResult mulInt(std::int64_t a, std::int64_t b) noexcept
{
    // Portable overflow check (MSVC has no __builtin_mul_overflow).
    if (a > 0) {
        if (b > 0) {
            if (a > maxInt / b) return overflow();
        } else if (b < minInt / a) {
            return overflow();
        }
    } else if (a < 0) {
        if (b > 0) {
            if (a < minInt / b) return overflow();
        } else if (b < 0 && a < maxInt / b) {
            return overflow();
        }
    }
    return {a * b, ArithmeticError::None};
}

IntResult negInt(std::int64_t a) noexcept
{
    if (a == minInt) {
        return overflow();
    }
    return {-a, ArithmeticError::None};
}

IntResult floorDivInt(std::int64_t a, std::int64_t b) noexcept
{
    if (b == 0) {
        return {0, ArithmeticError::DivisionByZero};
    }
    if (a == minInt && b == -1) {
        return overflow();
    }
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return {q, ArithmeticError::None};
}

IntResult floorModInt(std::int64_t a, std::int64_t b) noexcept
{
    if (b == 0) {
        return {0, ArithmeticError::DivisionByZero};
    }
    if (b == -1) {
        return {0, ArithmeticError::None}; // Avoids minInt % -1, which is undefined in C++.
    }
    std::int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) {
        r += b;
    }
    return {r, ArithmeticError::None};
}

std::int64_t addIntSaturating(std::int64_t a, std::int64_t b) noexcept
{
    if (b > 0 && a > maxInt - b) {
        return maxInt;
    }
    if (b < 0 && a < minInt - b) {
        return minInt;
    }
    return a + b;
}

FloatResult divFloat(double a, double b) noexcept
{
    if (b == 0.0) {
        return {0.0, ArithmeticError::DivisionByZero};
    }
    return {a / b, ArithmeticError::None};
}

FloatResult floorDivFloat(double a, double b) noexcept
{
    if (b == 0.0) {
        return {0.0, ArithmeticError::DivisionByZero};
    }
    // CPython's float floor division: consistent with floorModFloat (a == b * q + r).
    double mod = std::fmod(a, b);
    double div = (a - mod) / b;
    if (mod != 0.0 && ((b < 0.0) != (mod < 0.0))) {
        div -= 1.0;
    }
    double floorDiv = 0.0;
    if (div != 0.0) {
        floorDiv = std::floor(div);
        if (div - floorDiv > 0.5) {
            floorDiv += 1.0;
        }
    } else {
        floorDiv = std::copysign(0.0, a / b);
    }
    return {floorDiv, ArithmeticError::None};
}

FloatResult floorModFloat(double a, double b) noexcept
{
    if (b == 0.0) {
        return {0.0, ArithmeticError::DivisionByZero};
    }
    double mod = std::fmod(a, b);
    if (mod != 0.0) {
        if ((b < 0.0) != (mod < 0.0)) {
            mod += b;
        }
    } else {
        mod = std::copysign(0.0, b);
    }
    return {mod, ArithmeticError::None};
}

IntResult floatToInt(double value) noexcept
{
    // 2^63 is exactly representable; every double in [-2^63, 2^63) truncates into range.
    constexpr double limit = 9223372036854775808.0;
    if (!std::isfinite(value) || value >= limit || value < -limit) {
        return {0, ArithmeticError::InvalidConversion};
    }
    return {static_cast<std::int64_t>(value), ArithmeticError::None};
}

std::string formatInt(std::int64_t value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << value;
    return std::move(stream).str();
}

std::string formatFloat(double value)
{
    if (std::isnan(value)) {
        return "nan";
    }
    if (std::isinf(value)) {
        return value < 0.0 ? "-inf" : "inf";
    }
    // std::to_chars for floating point needs macOS 13.3, so: the shortest %g-style precision
    // that reads back exactly. Streams with the classic locale never use a ',' separator.
    std::string text;
    int digits = 17;
    for (int precision = 1; precision <= 17; ++precision) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out.precision(precision);
        out << value;
        text = std::move(out).str();
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        double parsed = 0.0;
        in >> parsed;
        if (parsed == value && std::signbit(parsed) == std::signbit(value)) {
            digits = precision;
            break;
        }
    }
    // %g switches to an exponent as soon as the exponent reaches the precision ("1e+01" for
    // 10.0). Like Python, use positional notation for decimal exponents from -4 to 15.
    if (const std::size_t e = text.find('e'); e != std::string::npos) {
        const int exponent = std::stoi(text.substr(e + 1));
        if (exponent >= -4 && exponent < 16) {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << std::fixed;
            out.precision(std::max(0, digits - 1 - exponent));
            out << value;
            text = std::move(out).str();
        }
    }
    if (text.find_first_of(".e") == std::string::npos) {
        text += ".0";
    }
    return text;
}

const char* formatBool(bool value) noexcept { return value ? "True" : "False"; }

int compareStrings(std::string_view a, std::string_view b) noexcept
{
    const int result = a.compare(b); // char_traits<char>::compare compares as unsigned char.
    return result < 0 ? -1 : (result > 0 ? 1 : 0);
}

bool sameFloatBits(double a, double b) noexcept
{
    std::uint64_t x = 0;
    std::uint64_t y = 0;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

} // namespace schuimm::script
