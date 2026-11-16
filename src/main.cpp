#include "sdk/natives.gen.h"
#include "core/version.h"

static void scriptMain() {
    while (true) {
        PLAYER::PLAYER_PED_ID();
        scriptWait(0);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) scriptRegister(module, scriptMain);
    else if (reason == DLL_PROCESS_DETACH) scriptUnregister(module);
    return TRUE;
}
