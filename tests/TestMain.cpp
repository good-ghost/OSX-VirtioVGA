#include "TestMain.hpp"

#include <cstdlib>

namespace vvtest {

namespace {
int gFailures = 0;
} // namespace

std::vector<TestCase> &registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

void reportFailure(const char *file, int line, const char *expression)
{
    ++gFailures;
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, expression);
}

} // namespace vvtest

int main()
{
    int failedTests = 0;
    for (const auto &test : vvtest::registry()) {
        const int before = vvtest::gFailures;
        test.body();
        const bool passed = vvtest::gFailures == before;
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.name);
        if (!passed) {
            ++failedTests;
        }
    }
    std::printf("%zu tests, %d failed\n", vvtest::registry().size(), failedTests);
    return failedTests == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
