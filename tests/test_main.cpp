#include "test.h"

#include <cstdio>

namespace ro_test {

void fail(const char *file, int line, const std::string &what)
{
    throw Failure{std::string(file) + ":" + std::to_string(line) + ": " + what};
}

std::vector<Case> &cases()
{
    static std::vector<Case> instance;
    return instance;
}

int runAll()
{
    int failed = 0;
    for (const auto &test : cases()) {
        try {
            test.fn();
            std::printf("[ ok ] %s\n", test.name.c_str());
        } catch (const Failure &f) {
            ++failed;
            std::printf("[FAIL] %s\n       %s\n", test.name.c_str(), f.message.c_str());
        } catch (const std::exception &e) {
            ++failed;
            std::printf("[FAIL] %s\n       unexpected exception: %s\n", test.name.c_str(), e.what());
        }
    }
    std::printf("\n%zu tests, %d failed\n", cases().size(), failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace ro_test

int main()
{
    return ro_test::runAll();
}
