// Entry point: loads settings and data, waits for the game, then runs every module once per frame.
#include <string>

#include "core/fs.h"
#include "core/log.h"
#include "core/version.h"
#include "dog/companion.h"
#include "game/game.h"
#include "mod/context.h"
#include "mod/testhooks.h"
#include "sdk/natives.gen.h"

namespace fd {

static HMODULE g_module = nullptr;

static std::string moduleDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_module, buf, MAX_PATH);
    std::wstring w(buf, n);
    size_t cut = w.find_last_of(L"\\/");
    return fs::narrow(cut == std::wstring::npos ? L"." : w.substr(0, cut));
}

static void loadData() {
    auto& c = ctx();
    c.asiDir = moduleDir();
    c.dataDir = fs::join(c.asiDir, "FrontierDog");
    c.ini.load(fs::join(c.asiDir, "FrontierDog.ini"));
    log::init(fs::join(c.asiDir, "FrontierDog.log"), c.ini.getBool("DebugLog", false) ? log::Level::Debug : log::Level::Info);
    FD_INFO("Frontier Dog %s%s", FD_VERSION,
#ifdef FD_TEST
            " (test build)"
#else
            ""
#endif
    );
    c.tuning.load(fs::join(c.dataDir, "Companion.dat"));
    KvFile breeds, econ;
    if (!breeds.load(fs::join(c.dataDir, "Breeds.dat")) || !c.breeds.load(breeds)) FD_ERROR("Breeds.dat missing or empty");
    if (econ.load(fs::join(c.dataDir, "Economy.dat"))) c.econ.load(econ);
    c.chances.pedigree = breeds.getFloat("roll.pedigree", c.chances.pedigree);
    c.chances.ailing = breeds.getFloat("roll.ailing", c.chances.ailing);
    c.chances.ownBreedTrait = breeds.getFloat("roll.own_breed_trait", c.chances.ownBreedTrait);
    c.rng = Rng(uint64_t(GetTickCount64()) * 0x9E3779B97F4A7C15ULL);
    FD_INFO("breeds %zu, traits %zu, deeds %zu", c.breeds.breeds().size(), c.breeds.traits().size(), c.breeds.deeds().size());
}

static void scriptMain() {
    loadData();
    auto& c = ctx();
    // another dog companion mod spawns its own dog: two would follow Arthur (D139)
    bool oldMod = GetModuleHandleW(L"DogComapnion.asi") || GetModuleHandleW(L"DogCompanion.asi");
    if (oldMod) FD_WARN("Dog Companion is installed too: Frontier Dog stays idle");
    DECORATOR::DECOR_REGISTER("FD_companion", 3);
    while (!PLAYER::IS_PLAYER_PLAYING(PLAYER::PLAYER_ID()) || CAMERA::IS_SCREEN_FADED_OUT()) scriptWait(500);
    scriptWait(3000);
    c.lang.load(c.dataDir, LOCALIZATION::GET_CURRENT_LANGUAGE(), c.ini.get("Language", "auto"));
    FD_INFO("language %s", c.lang.code().c_str());
    if (oldMod) {
        game::helpText(c.t("help.old_mod"), 10000);
        while (true) scriptWait(1000);
    }
    c.store.open(fs::join(c.dataDir, "Dogs"));
    companion().start();
    while (true) {
        int n = SCRIPT::GET_NUMBER_OF_EVENTS(0);
        for (int i = 0; i < n; ++i) companion().onEvent(SCRIPT::GET_EVENT_AT_INDEX(0, i), i);
        companion().tick();
#ifdef FD_TEST
        testhooks::tick();
#endif
        scriptWait(0);
    }
}

}  // namespace fd

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        fd::g_module = module;
        scriptRegister(module, fd::scriptMain);
    } else if (reason == DLL_PROCESS_DETACH) {
        scriptUnregister(module);
    }
    return TRUE;
}
