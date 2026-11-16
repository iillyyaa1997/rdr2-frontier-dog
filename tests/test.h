// Minimal test harness: TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
#pragma once
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace t {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { cases().push_back({n, f}); } };
template <typename A, typename B>
inline void eq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    if (!(a == b)) {
        std::ostringstream o; o << a << " != " << b;
        std::printf("  FAIL %s:%d: %s == %s (%s)\n", file, line, ea, eb, o.str().c_str());
        ++failures();
    }
}
}  // namespace t

#define TEST(name) static void name(); static t::Reg reg_##name(#name, name); static void name()
#define CHECK(c) do { if (!(c)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++t::failures(); } } while (0)
#define CHECK_EQ(a, b) t::eq((a), (b), #a, #b, __FILE__, __LINE__)
