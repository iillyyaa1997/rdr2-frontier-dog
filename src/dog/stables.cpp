#include "dog/stables.h"

#include <algorithm>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/strutil.h"
#include "dog/collars.h"
#include "dog/companion.h"
#include "game/game.h"
#include "game/prompt.h"
#include "mod/context.h"
#include "ui/card.h"

namespace fd::stables {

using namespace game;

struct Stable {
    std::string id;
    Vector3 at, dogs;
    std::vector<std::string> breeds;
};

struct ForSale {
    Ped ped = 0;
    StockDog stock;
};

static std::vector<Stable> g_stables;
static std::vector<ForSale> g_sale;

bool saleCard(Ped ped, DogRecord& out) {
    for (const auto& f : g_sale)
        if (f.ped == ped) {
            out = f.stock.dog;
            return true;
        }
    return false;
}
static int g_active = -1;
static long long g_period = -1;
static game::UiPrompt g_buyPrompts[3];
static game::UiPrompt g_menuNext, g_menuDo;
static int g_menuIndex = 0;
static int g_nextCheck = 0;
static int g_bringSlot = -1, g_bringAt = 0;
static bool g_keyboard = false;

static Vector3 vec(const std::vector<double>& v) { return v.size() == 3 ? Vector3(float(v[0]), float(v[1]), float(v[2])) : Vector3(); }

void start() {
    KvFile kv;
    kv.load(ctx().dataDir + "\\Stables.dat");
    g_stables.clear();  // the script starts again after a save load; the lists outlive it (16 stables on 16.11)
    for (const auto& id : kv.getList("stables")) {
        Stable s;
        s.id = id;
        s.at = vec(kv.getFloats(id + ".pos"));
        s.dogs = vec(kv.getFloats(id + ".dogs"));
        s.breeds = kv.getList(id + ".breeds");
        g_stables.push_back(s);
    }
    for (auto& p : g_buyPrompts) p.create("INPUT_INTERACT_OPTION1", "", 1000, 2);
    g_menuNext.create(ctx().ini.get("StableNextControl", "INPUT_CONTEXT_X"), ctx().t("stable.next"));
    g_menuDo.create(ctx().ini.get("StableDoControl", "INPUT_INTERACT_OPTION1"), "", 1200);
    FD_INFO("stables: %zu", g_stables.size());
}

struct PendingSale {
    SpawnRequest req;
    StockDog stock;
};
static std::vector<PendingSale> g_pending;

static void clearSale() {
    for (auto& s : g_sale) deletePed(s.ped);
    g_sale.clear();
    g_pending.clear();
    for (auto& p : g_buyPrompts) p.show(false);
}

// Queues the period's stock; the dogs appear over the next frames (stepSale), never blocking the script.
static void spawnSale(const Stable& st) {
    clearSale();
    auto& c = ctx();
    long long day = gameDay();
    auto stock = stableStock(st.id, st.breeds, day, c.breeds, c.econ, c.chances, c.disabledBreeds());
    for (size_t i = 0; i < stock.size() && i < 3; ++i) {
        PendingSale ps;
        ps.req.model = stock[i].dog.model;
        ps.req.fallback = "a_c_doghobo_01";  // a model that always loads (the mutt freeze of 15.11)
        ps.req.at = add(st.dogs, Vector3(float(i) * 1.6f, 0, 0));
        ps.req.heading = 180;
        ps.stock = stock[i];
        g_pending.push_back(ps);
    }
    FD_INFO("stable %s: %zu dogs for sale queued (period %lld)", st.id.c_str(), g_pending.size(), day / c.econ.rotationDays);
}

// All models stream in parallel; at most one dog is created per frame.
static void stepSale() {
    if (g_pending.empty()) return;
    auto& c = ctx();
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        Ped p = stepSpawn(it->req);
        if (!p) {
            it = it->req.done ? g_pending.erase(it) : it + 1;
            continue;
        }
        StockDog stock = it->stock;
        g_pending.erase(it);
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
        ENTITY::SET_ENTITY_INVINCIBLE(p, true);
        DECORATOR::DECOR_SET_INT(p, "FD_shop", 1);
        int presets = PED::GET_NUM_META_PED_OUTFITS(p);
        if (stock.dog.preset < 0 && presets > 0) stock.dog.preset = c.rng.range(0, presets - 1);
        if (stock.dog.preset >= 0) PED::_EQUIP_META_PED_OUTFIT_PRESET(p, stock.dog.preset, false);
        if (stock.dog.scale != 100) PED::_SET_PED_SCALE(p, stock.dog.scale / 100.0f);
        TASK::TASK_START_SCENARIO_IN_PLACE_HASH(p, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_SITTING"), -1, true, 0, -1.0f, false);
        PED::_SET_PED_PROMPT_NAME(p, literal(c.breedText(stock.dog)));  // the breed, not the model's species
        ForSale f;
        f.ped = p;
        f.stock = stock;
        g_sale.push_back(f);
        if (g_pending.empty()) FD_INFO("stable: %zu dogs for sale spawned", g_sale.size());
        return;
    }
}

// ---- the stable hand's menu for the dog with Arthur ----

struct MenuItem {
    std::string text;
    int price = 0;  // dollars
    int kind = 0;   // 1 train, 2 cure, 3 pedigree, 4 papers, 5 rename, 6 sell, 7 bring, 8 collar on, 9 collar off
    std::string arg;
    int slot = -1;
};

static std::vector<MenuItem> menu(DogRecord* d) {
    auto& c = ctx();
    std::vector<MenuItem> out;
    if (d) {
        // training: every skill below 3 (ailing dogs only to 3 anyway); price × trainability (old D61)
        for (const auto& sk : c.breeds.skills()) {
            int price = trainingPrice(*d, sk, c.econ);
            if (price < 0) continue;
            out.push_back({c.f("stable.train", {{"skill", c.t("skill." + sk)}, {"from", std::to_string(d->skill(sk))},
                                                 {"to", std::to_string(d->skill(sk) + 1)}}), price, 1, sk});
        }
        const BreedDef* b = c.breeds.find(d->breed);
        if (d->grade == Grade::Ailing) out.push_back({c.t("stable.cure"), c.econ.curePrice, 2});
        if (b && !b->mutt && d->grade == Grade::Ordinary) out.push_back({c.t("stable.pedigree"), c.econ.pedigreePrice, 3});
        if (!d->papers) out.push_back({c.t("stable.papers"), papersPrice(*d, c.econ), 4});
        out.push_back({c.t("stable.rename"), c.econ.renamePrice, 5});
        for (const auto& id : collars::ids())
            if (d->collar != id)
                out.push_back({c.f("stable.collar", {{"collar", c.t("collar." + id)}}), c.econ.collarPrice, 8, id});
        if (!d->collar.empty()) out.push_back({c.t("stable.collar_off"), 0, 9});
        out.push_back({c.f("stable.sell", {{"name", d->name}}, d->female), -sellPrice(*d, c.breeds, c.econ), 6});
    }
    for (int i = 0; i < DogStore::kSlots; ++i) {
        if (!c.store.used(i) || i == companion().slot()) continue;
        const DogRecord& h = c.store.at(i);
        if (h.where != Where::Home) continue;
        out.push_back({c.f("stable.bring", {{"name", h.name}}, h.female), c.econ.swapPrice, 7, "", i});
    }
    return out;
}

static bool pay(int dollars) {
    if (dollars <= 0) return true;
    if (!takeCash(dollars * 100)) {
        helpText(ctx().t("help.no_money"));
        return false;
    }
    return true;
}

static void doItem(const MenuItem& m, const Stable& st) {
    auto& c = ctx();
    auto& comp = companion();
    DogRecord* d = comp.record();
    if (m.kind != 6 && m.kind != 5 && !pay(m.price)) return;
    switch (m.kind) {
        case 1:
        case 2:
        case 3: {
            if (!d) return;
            int days = m.kind == 1 ? 1 : (m.kind == 2 ? c.econ.cureDays : c.econ.pedigreeDays);
            d->awayReason = m.kind == 1 ? "training:" + m.arg : (m.kind == 2 ? "cure" : "pedigree");
            d->awayUntil = gameMinutes() + days * 1440LL;
            d->awayStable = st.id;
            std::string name = d->name;
            bool fem = d->female;
            int s = comp.slot();
            comp.sendHome(false);  // led away by the stable hand
            if (s >= 0 && c.store.used(s)) {
                c.store.at(s).where = Where::Away;
                c.store.save(s);
            }
            helpText(c.f("help.away_days", {{"name", name}, {"days", std::to_string(days)}}, fem), 7000);
            FD_INFO("stable: %s away %d day(s) (%s)", name.c_str(), days, m.kind == 1 ? m.arg.c_str() : "vet");
            return;
        }
        case 4:
            if (d) { d->papers = true; comp.save(); helpText(c.t("help.papers_done")); }
            return;
        case 5:
            MISC::DISPLAY_ONSCREEN_KEYBOARD(0, "FMMC_KEY_TIP8", "", d ? d->name.c_str() : "", "", "", "", 24);
            g_keyboard = true;
            return;
        case 6: {
            if (!d) return;
            int price = sellPrice(*d, c.breeds, c.econ);
            giveCash(price * 100);
            helpText(c.f("help.sold", {{"name", d->name}, {"price", std::to_string(price)}}, d->female), 6000);
            FD_INFO("stable: sold %s for $%d", d->name.c_str(), price);
            comp.releaseForSale();
            return;
        }
        case 8:
        case 9:
            if (!d) return;
            d->collar = m.kind == 8 ? m.arg : "";
            comp.save();
            if (Ped p = comp.ped()) collars::dress(p, *d);
            helpText(c.f(m.kind == 8 ? "help.collar_on" : "help.collar_off", {{"name", d->name}}, d->female));
            FD_INFO("stable: %s collar %s", d->name.c_str(), m.kind == 8 ? m.arg.c_str() : "off");
            return;
        case 7:
            g_bringSlot = m.slot;
            g_bringAt = now() + c.econ.swapBringSeconds * 1000;
            helpText(c.f("help.bringing", {{"name", c.store.at(m.slot).name}, {"s", std::to_string(c.econ.swapBringSeconds)}},
                         c.store.at(m.slot).female));
            return;
    }
}

static void updateKeyboard() {
    if (!g_keyboard) return;
    int st = MISC::UPDATE_ONSCREEN_KEYBOARD();
    if (st == 0) return;  // still typing
    g_keyboard = false;
    if (st != 1) return;
    const char* r = MISC::GET_ONSCREEN_KEYBOARD_RESULT();
    std::string name = r ? str::trim(r) : "";
    DogRecord* d = companion().record();
    if (name.empty() || !d) return;
    if (!pay(ctx().econ.renamePrice)) return;
    d->name = name;
    companion().save();
    if (Ped p = companion().ped()) {
        MAP::_SET_BLIP_NAME(MAP::GET_BLIP_FROM_ENTITY(p), literal(name));
        PED::_SET_PED_PROMPT_NAME(p, literal(name));
    }
    helpText(ctx().f("help.renamed", {{"name", name}}, d->female));
}

void tick() {
    if (!ctx().feature("Stables") || g_stables.empty()) return;
    updateKeyboard();
    int t = now();
    if (g_bringSlot >= 0 && t >= g_bringAt) {
        int s = g_bringSlot;
        g_bringSlot = -1;
        if (g_active >= 0) companion().callFromHomeAt(s, g_stables[g_active].dogs);
        else companion().callFromHome(s);
    }
    if (t >= g_nextCheck) {
        g_nextCheck = t + 1000;
        Vector3 p = pos(player());
        int best = -1;
        float bd = 1e9f;
        for (size_t i = 0; i < g_stables.size(); ++i) {
            float d = dist(p, g_stables[i].at);
            if (d < bd) { bd = d; best = int(i); }
        }
        if (bd > 80.0f || onMission()) best = -1;
        if (best != g_active) {
            clearSale();
            g_active = best;
            g_period = -1;
        }
        if (g_active >= 0 && bd < 60.0f) {
            long long period = gameDay() / ctx().econ.rotationDays;
            if (period != g_period) {
                g_period = period;
                spawnSale(g_stables[g_active]);
            }
        }
    }
    stepSale();
    if (g_active < 0) {
        g_menuNext.show(false);
        g_menuDo.show(false);
        return;
    }
    const Stable& st = g_stables[g_active];
    auto& c = ctx();
    // dogs for sale: buy prompt on each, the card on focus
    Entity focus = 0;
    PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(PLAYER::PLAYER_ID(), &focus, false, false);
    for (size_t i = 0; i < g_sale.size(); ++i) {
        ForSale& f = g_sale[i];
        auto& pr = g_buyPrompts[i];
        if (!f.ped || !exists(f.ped)) { pr.show(false); continue; }
        bool closeTo = dist(f.ped, player()) < 3.0f;
        pr.setTarget(f.ped);
        pr.setText(c.f("stable.buy", {{"grade", c.gradeText(f.stock.dog)}, {"breed", c.breedText(f.stock.dog)},
                                       {"price", std::to_string(f.stock.price)}}, f.stock.dog.female));
        pr.show(closeTo);
        if (focus == f.ped && PAD::IS_CONTROL_JUST_PRESSED(0, MISC::GET_HASH_KEY("INPUT_INTERACT_LOCKON_DETAILS_ANIMAL")))
            ui::cardShow(f.stock.dog, f.ped);
        if (closeTo && pr.fired() && pay(f.stock.price)) {
            DogRecord d = f.stock.dog;
            d.uid += "-" + std::to_string(unixNow());
            d.name = c.t("dog.default_name");
            d.papers = true;
            Ped bought = f.ped;
            DECORATOR::DECOR_SET_INT(bought, "FD_shop", 0);
            f.ped = 0;
            pr.show(false);
            if (companion().adopt(bought, d)) {
                FD_INFO("stable: bought %s (%s) for $%d", d.breed.c_str(), c.gradeText(d).c_str(), f.stock.price);
                helpText(c.f("help.bought", {{"grade", c.gradeText(d)}, {"breed", c.breedText(d)}}, d.female), 6000);
            } else {
                giveCash(f.stock.price * 100);  // no room: money back
                f.ped = bought;
                DECORATOR::DECOR_SET_INT(bought, "FD_shop", 1);
            }
        }
    }
    // the stable hand's menu: near the stable, on foot
    bool atStable = dist(pos(player()), st.at) < 6.0f && PED::IS_PED_ON_FOOT(player()) && !PED::IS_PED_IN_COMBAT(player(), 0);
    auto items = menu(companion().record());
    if (!atStable || items.empty() || g_keyboard) {
        g_menuNext.show(false);
        g_menuDo.show(false);
        return;
    }
    g_menuIndex %= int(items.size());
    const MenuItem& m = items[g_menuIndex];
    std::string label = m.text + (m.price > 0 ? " — $" + std::to_string(m.price) : (m.price < 0 ? " — +$" + std::to_string(-m.price) : ""));
    g_menuDo.setText(label);
    g_menuNext.show(items.size() > 1);
    g_menuDo.show(true);
    if (g_menuNext.fired()) g_menuIndex = (g_menuIndex + 1) % int(items.size());
    if (g_menuDo.fired()) doItem(m, st);
}

}  // namespace fd::stables
