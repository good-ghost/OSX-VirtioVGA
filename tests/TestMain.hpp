// 최소 단위 테스트 러너 (WI-0.006).
// 왜 직접 만드나: 커널 비의존 로직만 사용자 공간에서 확인하면 되므로 외부 테스트 프레임워크를 새 의존성으로 넣지 않는다.
// 필요해지면 그때 이유와 함께 프레임워크를 제안한다.
#pragma once

#include <cstdio>
#include <functional>
#include <vector>

namespace vvtest {

struct TestCase {
    const char *name;
    std::function<void()> body;
};

std::vector<TestCase> &registry();
void reportFailure(const char *file, int line, const char *expression);

struct Registrar {
    Registrar(const char *name, std::function<void()> body)
    {
        registry().push_back({name, std::move(body)});
    }
};

} // namespace vvtest

#define VV_TEST_CONCAT_INNER(a, b) a##b
#define VV_TEST_CONCAT(a, b) VV_TEST_CONCAT_INNER(a, b)

#define VV_TEST(name)                                                              \
    static void name();                                                            \
    static vvtest::Registrar VV_TEST_CONCAT(registrar_, name)(#name, name);        \
    static void name()

#define VV_EXPECT(expression)                                                      \
    do {                                                                           \
        if (!(expression)) {                                                       \
            vvtest::reportFailure(__FILE__, __LINE__, #expression);                \
        }                                                                          \
    } while (0)
