#pragma once

// Tiny dependency-free test runner (keeps the repo free of third-party libraries).

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace evox::test
{

struct TestCase
{
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

struct Registrar
{
    Registrar (const char* name, std::function<void()> fn) { registry().push_back ({ name, std::move (fn) }); }
};

inline void check (bool ok, const char* expr, const char* file, int line, const std::string& msg = {})
{
    if (! ok)
    {
        ++failures();
        std::printf ("    FAILED %s:%d: %s %s\n", file, line, expr, msg.c_str());
    }
}

} // namespace evox::test

#define EVOX_CONCAT_INNER(a, b) a##b
#define EVOX_CONCAT(a, b) EVOX_CONCAT_INNER (a, b)

#define TEST_CASE(name)                                                                                     \
    static void EVOX_CONCAT (evoxTest_, __LINE__)();                                                        \
    static evox::test::Registrar EVOX_CONCAT (evoxReg_, __LINE__) (name, EVOX_CONCAT (evoxTest_, __LINE__)); \
    static void EVOX_CONCAT (evoxTest_, __LINE__)()

#define CHECK(expr) evox::test::check (static_cast<bool> (expr), #expr, __FILE__, __LINE__)
#define CHECK_MSG(expr, msg) evox::test::check (static_cast<bool> (expr), #expr, __FILE__, __LINE__, msg)
#define CHECK_NEAR(a, b, tol)                                                                               \
    evox::test::check (std::abs ((a) - (b)) <= (tol), #a " ~= " #b, __FILE__, __LINE__,                     \
                       "(" + std::to_string (a) + " vs " + std::to_string (b) + ", tol " + std::to_string (tol) + ")")
