// RenderLift — minimal test harness (no external dependencies).
//
// Usage: RL_TEST("name") { RL_CHECK(expr); } and at the end of main,
// rl::test::summary(). Failed checks print file:line and don't abort, so one
// run reports every failure.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace rl::test {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) {
        registry().push_back({std::move(name), std::move(fn)});
    }
};

inline void check(bool condition, const char* expr, const char* file, int line) {
    if (!condition) {
        ++failureCount();
        std::fprintf(stderr, "  FAIL %s:%d — RL_CHECK(%s)\n", file, line, expr);
    }
}

inline void checkNear(double a, double b, double eps, const char* exprA, const char* exprB,
                      const char* file, int line) {
    if (std::fabs(a - b) > eps) {
        ++failureCount();
        std::fprintf(stderr, "  FAIL %s:%d — |%s - %s| = |%f - %f| > %f\n", file, line, exprA,
                     exprB, a, b, eps);
    }
}

template <typename Exception, typename Fn>
inline void checkThrows(Fn&& fn, const char* expr, const char* file, int line) {
    try {
        fn();
        ++failureCount();
        std::fprintf(stderr, "  FAIL %s:%d — RL_CHECK_THROWS(%s) did not throw\n", file, line, expr);
    } catch (const Exception&) {
        // expected
    } catch (...) {
        ++failureCount();
        std::fprintf(stderr, "  FAIL %s:%d — RL_CHECK_THROWS(%s) threw the wrong type\n", file, line,
                     expr);
    }
}

inline int runAll() {
    int passed = 0;
    for (const Case& c : registry()) {
        const int before = failureCount();
        std::fprintf(stdout, "[test] %s\n", c.name.c_str());
        c.fn();
        if (failureCount() == before) {
            ++passed;
            std::fprintf(stdout, "  ok\n");
        }
    }
    std::fprintf(stdout, "───\n%d/%zu test cases passed, %d failed check(s)\n", passed,
                 registry().size(), failureCount());
    return failureCount() == 0 ? 0 : 1;
}

}  // namespace rl::test

#define RL_TEST(NAME)                                                       \
    static void test_##NAME();                                              \
    static ::rl::test::Registrar registrar_##NAME(#NAME, &test_##NAME);     \
    static void test_##NAME()

#define RL_CHECK(EXPR) ::rl::test::check(static_cast<bool>(EXPR), #EXPR, __FILE__, __LINE__)
#define RL_CHECK_NEAR(A, B, EPS) \
    ::rl::test::checkNear((A), (B), (EPS), #A, #B, __FILE__, __LINE__)
#define RL_CHECK_THROWS(EXPR, EXCEPTION) \
    ::rl::test::checkThrows<EXCEPTION>([&]() { (void)(EXPR); }, #EXPR, __FILE__, __LINE__)
