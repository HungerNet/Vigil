#pragma once
// Vigil test framework — shared by every suite (docs/VALIDATION.md).
//
//   EXPECT(cond, fmt, ...)   records an expectation; prints file:line and the message on failure
//   runSuite(argc, argv, tests)
//       <suite>.exe                 runs every test
//       <suite>.exe <substring>     runs tests whose name contains the substring
//       <suite>.exe --list          lists test names
//
// Output is stable and grep-friendly ("[ RUN  ]", "[  OK  ]", "[ FAIL ]", final summary line), and
// the exit code is non-zero when any expectation failed, so CTest and CI need nothing else.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace vigil::test {

inline int& failures() { static int n = 0; return n; }
inline int& expectations() { static int n = 0; return n; }

struct Test {
    const char* name;
    void (*fn)();
};

template <std::size_t N>
int runSuite(int argc, char** argv, const Test (&tests)[N], const char* suite) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    if (filter && std::strcmp(filter, "--list") == 0) {
        for (const Test& t : tests) std::printf("%s\n", t.name);
        return 0;
    }
    int ran = 0;
    for (const Test& t : tests) {
        if (filter && !std::strstr(t.name, filter)) continue;
        const int before = failures();
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", failures() == before ? " OK " : "FAIL", t.name);
        ++ran;
    }
    std::printf("\n%s: %d tests, %d expectations, %d failed\n", suite, ran, expectations(), failures());
    if (ran == 0) {
        std::printf("no test matched \"%s\"\n", filter ? filter : "");
        return 2;
    }
    return failures() == 0 ? 0 : 1;
}

// A temporary file path under %TEMP% that is removed when the guard goes out of scope.
struct TempFile {
    std::string path;
    explicit TempFile(const char* name) {
        const char* dir = std::getenv("TEMP");
        path = std::string(dir ? dir : ".") + "\\" + name;
        std::remove(path.c_str());
    }
    ~TempFile() { std::remove(path.c_str()); }
};

}  // namespace vigil::test

#define EXPECT(cond, ...)                                                                  \
    do {                                                                                   \
        ++::vigil::test::expectations();                                                   \
        if (!(cond)) {                                                                     \
            ++::vigil::test::failures();                                                   \
            std::printf("    FAIL %s:%d  %s  - ", __FILE__, __LINE__, #cond);              \
            std::printf(__VA_ARGS__);                                                      \
            std::printf("\n");                                                             \
        }                                                                                  \
    } while (0)
