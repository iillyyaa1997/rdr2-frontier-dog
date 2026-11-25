// Entry point: loads settings and data, waits for the game, then runs every module once per frame.
#include <algorithm>
#include <string>

#include "core/fs.h"
#include "core/log.h"
#include "core/version.h"
#include "dog/abilities.h"
#include "dog/behaviour.h"
#include "dog/care.h"
#include "dog/companion.h"
#include "dog/delivery.h"
#include "dog/home.h"
#include "dog/hunting.h"
#include "dog/stables.h"
#include "dog/strays.h"
#include "game/game.h"
#include "mod/context.h"
#include "mod/testhooks.h"
#include "game/prompt.h"
#include "ui/card.h"
#include "ui/core.h"
#include "sdk/natives.gen.h"

namespace fd {

static HMODULE g_module = nullptr;
// watchdog: which module the script is in and since when (a plain thread, no natives; 16.11 the script stopped silently)
static volatile const char* g_module_name = "start";
static volatile ULONGLONG g_module_at = 0;

static DWORD WINAPI watchdog(LPVOID) {
    const char* reported = nullptr;
    for (;;) {
        Sleep(1000);
        ULONGLONG at = g_module_at;
        const char* name = (const char*)g_module_name;
        if (at && GetTickCount64() - at > 5000) {
            if (reported != name) {
                FD_WARN("watchdog: the script is in module %s for %llu ms (game paused or stuck)", name, GetTickCount64() - at);
                reported = name;
            }
        } else {
            reported = nullptr;
        }
    }
}

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
    c.econ.dogPriceScale = std::clamp(c.ini.getFloat("DogPricePercent", 100) / 100.0, 0.01, 100.0);
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
    // 25.11: breed cards changed (every breed has its 5) and the bond gives stat points - dogs saved before catch up
    for (int i = 0; i < DogStore::kSlots; ++i) {
        if (!c.store.used(i)) continue;
        DogRecord& d = c.store.at(i);
        bool card = refreshPedigreeCard(d, c.breeds);
        auto grown = applyBondGrowth(d);
        if (card || !grown.empty()) {
            FD_INFO("dog %d: stats brought up to date (%s%s) -> %d,%d,%d,%d,%d", i + 1, card ? "breed card" : "",
                    grown.empty() ? "" : (card ? ", bond points" : "bond points"), d.stats[0], d.stats[1], d.stats[2], d.stats[3], d.stats[4]);
            c.store.save(i);
        }
    }
    companion().start();
    behaviour::start();
    care::start();
    stables::start();
    home::start();
    delivery::start();
    abilities::start();
    ui::cardStart();
    DECORATOR::DECOR_REGISTER("FD_shop", 3);
    int heartbeat = 0;
    if (HANDLE w = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr)) CloseHandle(w);
    while (true) {
        // a module that takes long or never returns shows in the log (16.11: the script stopped after a teleport)
        auto step = [](const char* name, auto&& fn) {
            g_module_name = name;
            g_module_at = GetTickCount64();
            int t0 = game::now();
            fn();
            int dt = game::now() - t0;
            if (dt > 300) FD_WARN("slow module %s: %d ms", name, dt);
        };
        if (game::now() >= heartbeat) {
            heartbeat = game::now() + 30000;
            FD_INFO("alive");
        }
        step("events", [] {
            int n = SCRIPT::GET_NUMBER_OF_EVENTS(0);
            for (int i = 0; i < n; ++i) {
                Hash type = SCRIPT::GET_EVENT_AT_INDEX(0, i);
                companion().onEvent(type, i);
                hunting::onEvent(type, i);
            }
            int n1 = SCRIPT::GET_NUMBER_OF_EVENTS(1);
            for (int i = 0; i < n1; ++i) behaviour::onScriptEvent(i);
        });
        step("companion", [] { companion().tick(); });
        step("behaviour", [] { behaviour::tick(); });
        step("hunting", [] { hunting::tick(); });
        step("care", [] { care::tick(); });
        step("stables", [] { stables::tick(); });
        step("home", [] { home::tick(); });
        step("delivery", [] { delivery::tick(); });
        step("strays", [] { strays::tick(); });
        step("abilities", [] { abilities::tick(); });
        step("ui", [] { ui::coreTick(); ui::cardTick(); });
        step("prompts", [] { game::UiPrompt::applyFrame(); });
#ifdef FD_TEST
        step("test", [] { testhooks::tick(); });
#endif
        g_module_name = "wait";
        g_module_at = GetTickCount64();
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
