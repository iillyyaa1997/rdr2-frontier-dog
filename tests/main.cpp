#include "test.h"
int main() {
    for (auto& c : t::cases()) {
        int before = t::failures();
        c.fn();
        std::printf("%s %s\n", t::failures() == before ? "ok  " : "FAIL", c.name);
    }
    std::printf("%zu tests, %d failed checks\n", t::cases().size(), t::failures());
    return t::failures() ? 1 : 0;
}
