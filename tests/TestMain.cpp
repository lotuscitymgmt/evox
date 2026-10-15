#include "TestFramework.h"

#include <cstring>

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedTests = 0;

    for (const auto& t : evox::test::registry())
    {
        if (filter != nullptr && std::strstr (t.name, filter) == nullptr)
            continue;

        const int before = evox::test::failures();
        std::printf ("[ RUN  ] %s\n", t.name);
        t.fn();
        ++run;
        const bool ok = evox::test::failures() == before;
        failedTests += ok ? 0 : 1;
        std::printf ("[ %s ] %s\n", ok ? " OK " : "FAIL", t.name);
    }

    std::printf ("\n%d tests, %d failed\n", run, failedTests);
    return failedTests == 0 ? 0 : 1;
}
