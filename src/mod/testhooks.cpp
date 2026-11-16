#ifdef FD_TEST
#include "mod/testhooks.h"

#include <string>
#include <vector>

#include "core/fs.h"
#include "core/log.h"
#include "core/strutil.h"
#include "dog/companion.h"
#include "game/game.h"
#include "mod/context.h"

namespace fd::testhooks {

using namespace game;

static std::vector<Ped> g_spawned;

static void run(const std::vector<std::string>& a) {
    auto& c = ctx();
    auto& comp = companion();
    const std::string& cmd = a[0];
    auto num = [&](size_t i, double def) { double v; return i < a.size() && str::parseFloat(a[i], v) ? v : def; };
    Ped pl = player();
    Vector3 p = pos(pl);
    Vector3 fwd = ENTITY::GET_ENTITY_FORWARD_VECTOR(pl);
    if (cmd == "stray" && a.size() >= 2) {
        const BreedDef* b = c.breeds.find(a[1]);
        if (!b) { FD_INFO("test: no breed %s", a[1].c_str()); return; }
        Ped s = spawnPed(b->models[0], add(p, scale(fwd, 5)), 0);
        if (s) { TASK::TASK_WANDER_STANDARD(s, 3.0f, 0); g_spawned.push_back(s); }
        FD_INFO("test: stray %s -> %d", a[1].c_str(), s);
    } else if (cmd == "give" && a.size() >= 2) {
        const BreedDef* b = c.breeds.find(a[1]);
        if (!b) return;
        Grade g = a.size() > 2 ? Grade(std::atoi(a[2].c_str())) : Grade::Pedigree;
        DogRecord d = makeDog(*b, c.breeds, g, c.rng, c.chances, {});
        d.name = a.size() > 3 ? a[3] : c.t("dog.default_name");
        d.where = Where::Home;
        int s = c.store.put(d);
        FD_INFO("test: give %s grade %d -> slot %d", a[1].c_str(), int(g), s + 1);
        if (s >= 0) comp.callFromHome(s);
    } else if (cmd == "dogs") {
        for (int i = 0; i < DogStore::kSlots; ++i) {
            if (!c.store.used(i)) continue;
            const auto& d = c.store.at(i);
            FD_INFO("test: slot %d %s %s grade %d where %d bond %d/%.1f stats %d,%d,%d,%d,%d traits %zu", i + 1,
                    d.name.c_str(), d.breed.c_str(), int(d.grade), int(d.where), d.bondLevel, d.bondPoints, d.stats[0],
                    d.stats[1], d.stats[2], d.stats[3], d.stats[4], d.traits.size());
        }
    } else if (cmd == "whistle") { comp.whistle();
    } else if (cmd == "stay") { comp.stay();
    } else if (cmd == "follow") { comp.follow();
    } else if (cmd == "far" && comp.present()) {
        Vector3 to = add(p, scale(fwd, -float(num(1, 40))));
        ENTITY::SET_ENTITY_COORDS(comp.ped(), to.x, to.y, to.z, false, false, false, true);
        TASK::CLEAR_PED_TASKS(comp.ped(), true, true);
        FD_INFO("test: dog moved %.0f m behind", num(1, 40));
    } else if (cmd == "jump" && comp.present()) {
        Vector3 to = add(p, Vector3(600, 0, 0));
        ENTITY::SET_ENTITY_COORDS(comp.ped(), to.x, to.y, to.z, false, false, false, true);
        FD_INFO("test: dog moved 600 m away");
    } else if (cmd == "kill" && comp.present()) {
        ENTITY::SET_ENTITY_HEALTH(comp.ped(), 0, 0);
    } else if (cmd == "bond") {
        comp.addBond(num(1, 10), "test", 0);
    } else if (cmd == "home") {
        comp.sendHome(true);
    } else if (cmd == "call") {
        comp.callFromHome(int(num(1, 1)) - 1);
    } else if (cmd == "money") {
        giveCash(int(num(1, 100) * 100));
    } else if (cmd == "tp" && a.size() >= 4) {
        ENTITY::SET_ENTITY_COORDS(pl, float(num(1, 0)), float(num(2, 0)), float(num(3, 0)), false, false, false, true);
    } else if (cmd == "time") {
        CLOCK::SET_CLOCK_TIME(int(num(1, 12)), 0, 0);
    } else if (cmd == "skipdays") {
        CLOCK::ADD_TO_CLOCK_TIME(int(num(1, 1)) * 24, 0, 0);
    } else if (cmd == "clean") {
        for (Ped& s : g_spawned) deletePed(s);
        g_spawned.clear();
    } else if (cmd == "pos") {
        FD_INFO("test: Arthur %.2f %.2f %.2f, district 0x%08X, town %d", p.x, p.y, p.z, districtHash(p), int(inTown(p)));
    } else {
        FD_INFO("test: unknown command %s", cmd.c_str());
        return;
    }
    FD_INFO("test: done %s", cmd.c_str());
}

void tick() {
    static int next = 0;
    if (now() < next) return;
    next = now() + 1000;
    const std::string path = fs::join(ctx().asiDir, "FrontierDog.test");
    std::string text;
    if (!fs::readFile(path, text)) return;
    fs::remove(path);
    for (auto& line : str::split(text, '\n')) {
        auto parts = str::split(str::trim(line), ' ');
        std::vector<std::string> args;
        for (auto& x : parts)
            if (!x.empty()) args.push_back(x);
        if (!args.empty()) {
            FD_INFO("test: > %s", str::trim(line).c_str());
            run(args);
        }
    }
}

}  // namespace fd::testhooks
#endif
