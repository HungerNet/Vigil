#pragma once
// Vigil test framework — shared by every suite (docs/VALIDATION.md).
//
//   EXPECT(cond, fmt, ...)   records an expectation; prints file:line and the message on failure
//   runSuite(argc, argv, tests)
//       <suite>.exe                 runs every test
//       <suite>.exe <substring>     runs tests whose name contains the substring
//       <suite>.exe --list          lists test names
//       <suite>.exe --counts        runs every test and also prints, per test, "expectations <n>
//                                   <name>" and "frames <digest> <count> <name>" (every frame the
//                                   test fed to an engine through tests/sim.h): the Rust test
//                                   parity matrix (tools/ci/test_parity.py) requires both to match
//
// Output is stable and grep-friendly ("[ RUN  ]", "[  OK  ]", "[ FAIL ]", final summary line), and
// the exit code is non-zero when any expectation failed, so CTest and CI need nothing else.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace vigil::test {

inline int& failures() { static int n = 0; return n; }
inline int& expectations() { static int n = 0; return n; }

// The frames a test feeds to engines (World::frame in tests/sim.h): FNV-1a over their trace lines
// (src/detection/trace.h). A Rust mirror of the test with the same digest drove the engine with
// exactly the same input (P3B_PLAN.md §3.2).
inline std::uint64_t& frameDigest() { static std::uint64_t h = 1469598103934665603ull; return h; }
inline long long& framesFed() { static long long n = 0; return n; }
inline void feedFrameDigest(const std::string& line) {
    std::uint64_t& h = frameDigest();
    for (unsigned char c : line) { h ^= c; h *= 1099511628211ull; }
    h ^= static_cast<unsigned char>('\n');
    h *= 1099511628211ull;
    ++framesFed();
}

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
    const bool counts = filter && std::strcmp(filter, "--counts") == 0;
    if (counts) filter = nullptr;
    int ran = 0;
    for (const Test& t : tests) {
        if (filter && !std::strstr(t.name, filter)) continue;
        const int before = failures();
        const int expectedBefore = expectations();
        frameDigest() = 1469598103934665603ull;
        framesFed() = 0;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", failures() == before ? " OK " : "FAIL", t.name);
        if (counts) {
            std::printf("expectations %d %s\n", expectations() - expectedBefore, t.name);
            std::printf("frames %016llx %lld %s\n", static_cast<unsigned long long>(frameDigest()), framesFed(), t.name);
        }
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
