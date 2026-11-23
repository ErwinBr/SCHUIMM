#pragma once

// Minimal self-contained test harness (no external dependency).
//
//   SCHUIMM_TEST(SuiteName_CaseName) { CHECK(x == 1); REQUIRE(ptr != nullptr); }
//
// CHECK records a failure and continues; REQUIRE aborts the current test case.
// Link the test executable with TestMain.cpp, which runs every registered case.
// Command line: <exe> [substring-filter] | --list

#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace schuimm::test {

struct TestCase {
    const char* name;
    void (*function)();
};

struct RequireFailure {};

class Registry {
public:
    static Registry& instance()
    {
        static Registry registry;
        return registry;
    }

    void add(const char* name, void (*function)()) { cases_.push_back({name, function}); }
    [[nodiscard]] const std::vector<TestCase>& cases() const { return cases_; }

    void reportFailure(const char* file, int line, const std::string& message)
    {
        ++currentFailures_;
        std::fprintf(stderr, "  %s:%d: FAILED: %s\n", file, line, message.c_str());
    }

    int currentFailures_ = 0;

private:
    std::vector<TestCase> cases_;
};

struct Registrar {
    Registrar(const char* name, void (*function)()) { Registry::instance().add(name, function); }
};

template <typename T>
std::string stringify(const T& value)
{
    if constexpr (requires(std::ostream& os, const T& v) { os << v; }) {
        std::ostringstream stream;
        stream << value;
        return stream.str();
    } else {
        return "<unprintable>";
    }
}

int runAll(int argc, char** argv);

} // namespace schuimm::test

#define SCHUIMM_TEST(name)                                                                          \
    static void name();                                                                             \
    static const ::schuimm::test::Registrar name##_registrar(#name, &name);                         \
    static void name()

#define SCHUIMM_TEST_FAIL_(message, fatal)                                                          \
    do {                                                                                            \
        ::schuimm::test::Registry::instance().reportFailure(__FILE__, __LINE__, (message));         \
        if (fatal) {                                                                                \
            throw ::schuimm::test::RequireFailure{};                                                \
        }                                                                                           \
    } while (false)

#define CHECK(expression)                                                                           \
    do {                                                                                            \
        if (!(expression)) SCHUIMM_TEST_FAIL_(std::string("CHECK(") + #expression + ")", false);     \
    } while (false)

#define REQUIRE(expression)                                                                         \
    do {                                                                                            \
        if (!(expression)) SCHUIMM_TEST_FAIL_(std::string("REQUIRE(") + #expression + ")", true);    \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                  \
    do {                                                                                            \
        const auto& schuimmActual_ = (actual);                                                      \
        const auto& schuimmExpected_ = (expected);                                                  \
        if (!(schuimmActual_ == schuimmExpected_)) {                                                \
            SCHUIMM_TEST_FAIL_(std::string("CHECK_EQ(") + #actual + ", " + #expected + "): got '" + \
                                   ::schuimm::test::stringify(schuimmActual_) + "', expected '" +   \
                                   ::schuimm::test::stringify(schuimmExpected_) + "'",              \
                               false);                                                              \
        }                                                                                           \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                                     \
    do {                                                                                            \
        const double schuimmActual_ = static_cast<double>(actual);                                  \
        const double schuimmExpected_ = static_cast<double>(expected);                              \
        if (std::fabs(schuimmActual_ - schuimmExpected_) > static_cast<double>(tolerance)) {        \
            SCHUIMM_TEST_FAIL_(std::string("CHECK_NEAR(") + #actual + ", " + #expected + "): got " + \
                                   ::schuimm::test::stringify(schuimmActual_) + ", expected " +     \
                                   ::schuimm::test::stringify(schuimmExpected_),                    \
                               false);                                                              \
        }                                                                                           \
    } while (false)
