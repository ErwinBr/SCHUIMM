#include "TestFramework.h"

#include <cstring>

namespace schuimm::test {

int runAll(int argc, char** argv)
{
    Registry& registry = Registry::instance();
    const std::string_view filter = argc > 1 ? std::string_view(argv[1]) : std::string_view();

    if (filter == "--list") {
        for (const TestCase& testCase : registry.cases()) {
            std::printf("%s\n", testCase.name);
        }
        return 0;
    }

    int run = 0;
    int failed = 0;
    for (const TestCase& testCase : registry.cases()) {
        if (!filter.empty() && std::string_view(testCase.name).find(filter) == std::string_view::npos) {
            continue;
        }
        ++run;
        registry.currentFailures_ = 0;
        std::printf("[ RUN  ] %s\n", testCase.name);
        std::fflush(stdout);
        try {
            testCase.function();
        } catch (const RequireFailure&) {
            // Already reported.
        } catch (const std::exception& exception) {
            registry.reportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + exception.what());
        } catch (...) {
            registry.reportFailure(__FILE__, __LINE__, "unexpected unknown exception");
        }
        const bool passed = registry.currentFailures_ == 0;
        failed += passed ? 0 : 1;
        std::printf("[ %s ] %s\n", passed ? " OK " : "FAIL", testCase.name);
    }

    std::printf("\n%d test(s) run, %d failed\n", run, failed);
    if (run == 0) {
        std::fprintf(stderr, "No tests matched filter '%.*s'\n", static_cast<int>(filter.size()), filter.data());
        return 1;
    }
    return failed == 0 ? 0 : 1;
}

} // namespace schuimm::test

int main(int argc, char** argv)
{
    return schuimm::test::runAll(argc, argv);
}
