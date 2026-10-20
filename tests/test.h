// Minimal test harness: no external dependencies, one binary runs everything.
#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace ro_test {

struct Failure {
    std::string message;
};

void fail(const char *file, int line, const std::string &what);

struct Case {
    std::string name;
    std::function<void()> fn;
};

std::vector<Case> &cases();
int runAll();

struct Registrar {
    Registrar(const std::string &name, std::function<void()> fn) { cases().push_back({name, std::move(fn)}); }
};

template <class T> std::string describe(const T &value)
{
    if constexpr (requires(std::ostream &os) { os << value; }) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else {
        return "<unprintable>";
    }
}

}  // namespace ro_test

#define RO_TEST(name)                                                                              \
    static void name();                                                                            \
    static ::ro_test::Registrar ro_test_reg_##name(#name, name);                                  \
    static void name()

#define RO_CHECK(cond)                                                                             \
    do {                                                                                           \
        if (!(cond))                                                                               \
            ::ro_test::fail(__FILE__, __LINE__, "check failed: " #cond);                           \
    } while (0)

#define RO_CHECK_EQ(actual, expected)                                                               \
    do {                                                                                           \
        const auto &ro_a = (actual);                                                               \
        const auto &ro_e = (expected);                                                             \
        if (!(ro_a == ro_e))                                                                       \
            ::ro_test::fail(__FILE__, __LINE__,                                                    \
                            std::string(#actual " != " #expected "\n    actual: ")                 \
                                + ::ro_test::describe(ro_a) + "\n  expected: "                     \
                                + ::ro_test::describe(ro_e));                                      \
    } while (0)
