#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>

void RegisterTest(std::wstring name, std::function<void()> body);

struct TestRegistrar {
    TestRegistrar(std::wstring name, std::function<void()> body) {
        RegisterTest(std::move(name), std::move(body));
    }
};

#define WIDEN_LITERAL2(value) L##value
#define WIDEN_LITERAL(value) WIDEN_LITERAL2(value)

#define TEST_CASE(name)                                                          \
    static void name();                                                          \
    static TestRegistrar name##_registrar{WIDEN_LITERAL(#name), name};           \
    static void name()

#define REQUIRE(condition)                                                       \
    do {                                                                         \
        if (!(condition)) {                                                       \
            std::ostringstream oss;                                               \
            oss << "requirement failed: " #condition << " at " << __FILE__      \
                << ":" << __LINE__;                                             \
            throw std::runtime_error(oss.str());                                  \
        }                                                                        \
    } while (false)

#define REQUIRE_EQ(actual, expected)                                             \
    do {                                                                         \
        const auto actual_value = (actual);                                       \
        const auto expected_value = (expected);                                   \
        if (!(actual_value == expected_value)) {                                  \
            std::ostringstream oss;                                               \
            oss << "expected equality at " << __FILE__ << ":" << __LINE__;      \
            throw std::runtime_error(oss.str());                                  \
        }                                                                        \
    } while (false)
