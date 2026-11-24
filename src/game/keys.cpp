#include "game/keys.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace fd::game {

static bool gameInFront() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

int digitJustPressed() {
    static bool down[10] = {};
    int hit = 0;
    bool front = gameInFront();
    for (int k = 1; k <= 9; ++k) {
        bool now = front && ((GetAsyncKeyState('0' + k) & 0x8000) || (GetAsyncKeyState(VK_NUMPAD0 + k) & 0x8000));
        if (now && !down[k] && !hit) hit = k;
        down[k] = now;
    }
    return hit;
}

}  // namespace fd::game
