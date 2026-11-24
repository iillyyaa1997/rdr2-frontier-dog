#include "dog/stables.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/strutil.h"
#include "dog/collars.h"
#include "dog/companion.h"
#include "game/game.h"
#include "game/keys.h"
#include "game/prompt.h"
#include "mod/context.h"
#include "ui/card.h"
#include "ui/talk.h"

namespace fd::stables {

using namespace game;

struct Stable {
    std::string id;
    Vector3 at, dogs;
    bool measured = false;  // a .yard measured in game; otherwise .dogs is checked for being indoors
    std::vector<double> trader;  // .trader = x,y,z,heading placed by eye; empty = beside the yard
    std::vector<std::string> breeds;
};

struct ForSale {
    Ped ped = 0;
    StockDog stock;
    int nextAct = 0;  // when it picks its next thing to do in the yard
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
static bool g_talkMode = true;  // TraderTalk=0 in the ini: the old prompt list («Другое» leafs through it)
static bool g_talk = false;
static game::UiPrompt g_talkAccept, g_talkBack;  // the talk's buttons on a pad, as the game's prompts
// The dog trader (his wish, 17.11: «лучше если будет человек к которому можно подойти»): a man in the yard beside the
// dogs; walk up to him and his prompts show under his name, like the game's shops.
static SpawnRequest g_traderReq;
static bool g_traderPending = false;
static Ped g_trader = 0;
static int g_traderGroup = 0, g_nextFace = 0;
static std::string g_traderModel = "A_M_M_RANCHER_01";

bool isTrader(Ped p) { return p && p == g_trader; }
Ped trader() { return g_trader; }

static Vector3 vec(const std::vector<double>& v) { return v.size() == 3 ? Vector3(float(v[0]), float(v[1]), float(v[2])) : Vector3(); }

void start() {
    KvFile kv;
    kv.load(ctx().dataDir + "\\Stables.dat");
    g_stables.clear();  // the script starts again after a save load; the lists outlive it (16 stables on 16.11)
    for (const auto& id : kv.getList("stables")) {
        Stable s;
        s.id = id;
        s.at = vec(kv.getFloats(id + ".pos"));
        s.measured = kv.has(id + ".yard");
        s.dogs = vec(kv.getFloats(id + (s.measured ? ".yard" : ".dogs")));
        s.breeds = kv.getList(id + ".breeds");
        s.trader = kv.getFloats(id + ".trader");
        g_stables.push_back(s);
    }
    for (auto& p : g_buyPrompts) p.create("INPUT_INTERACT_OPTION1", "", 1000, 2);
    g_menuNext.create(ctx().ini.get("StableNextControl", "INPUT_CONTEXT_X"), ctx().t("stable.next"));
    g_talkMode = ctx().ini.getBool("TraderTalk", true);
    g_menuDo.create(ctx().ini.get("StableDoControl", "INPUT_INTERACT_OPTION1"), g_talkMode ? ctx().t("talk.prompt") : "",
                    g_talkMode ? 0 : 1200);
    g_talk = false;
    g_talkAccept.create("INPUT_FRONTEND_ACCEPT", ctx().t("talk.choose"));
    g_talkBack.create("INPUT_FRONTEND_CANCEL", ctx().t("talk.back"));
    g_traderModel = kv.get("trader_model", g_traderModel);
    if (!g_traderGroup) g_traderGroup = MISC::GET_RANDOM_INT_IN_RANGE(0x100000, 0x7FFFFF);
    g_menuNext.setGroup(g_traderGroup);  // shown under the trader's name (_UI_PROMPT_SET_ACTIVE_GROUP_THIS_FRAME)
    g_menuDo.setGroup(g_traderGroup);
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
    if (g_trader) deletePed(g_trader);
    g_trader = 0;
    g_traderPending = false;
    for (auto& p : g_buyPrompts) p.show(false);
}

// Queues the period's stock; the dogs appear over the next frames (stepSale), never blocking the script.
// The yard (.dogs) must be outdoors: inside the building the game opens its own stable menu and hides ours. Five yards
// were measured on 17.11 (where the game walks Arthur out after its menu, 3 m further out); for the others, if the
// spot is inside a building, the nearest outdoor navmesh point 10-18 m from the stable hand is used instead.
// A roof over the spot: the first surface from high above is well over it. Blackwater's stable is no interior to the
// game, so its .dogs spot passed as outdoors and the trader stood inside the stable (Nexus 22.11, Scottvdken: «i see the
// dog trader inside the stable, but i cant get close enough to him without triggering the Enter Stable animation»;
// seen 24.11: a teleport onto that spot landed on the stable's roof, 7 m over the trader).
static bool underRoof(const Vector3& p) {
    // a ray straight up from the spot (the ground probe from above did not see the roof, 24.11)
    BOOL hit = false;
    Vector3 end, normal;
    Entity e = 0;
    ScrHandle h = SHAPETEST::START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE(p.x, p.y, p.z + 1.0f, p.x, p.y, p.z + 25.0f, 1 | 16, 0, 7);
    if (SHAPETEST::GET_SHAPE_TEST_RESULT(h, &hit, &end, &normal, &e) == 2 && hit) return true;
    float top = 0;
    return MISC::GET_GROUND_Z_FOR_3D_COORD(p.x, p.y, p.z + 60.0f, &top, false) && top > p.z + 2.5f;
}

static void yardOutdoors(Stable& st) {
    // GET_INTERIOR_AT_COORDS goes by the building's box and also says "inside" right outside the door (Valentine's yard)
    if (st.measured) return;
    bool inside = INTERIOR::GET_INTERIOR_AT_COORDS(st.dogs.x, st.dogs.y, st.dogs.z) != 0;
    bool roofed = !inside && underRoof(st.dogs);
    FD_INFO("stable %s: yard %.1f %.1f %.1f, interior %d, roof %d", st.id.c_str(), st.dogs.x, st.dogs.y, st.dogs.z, int(inside), int(roofed));
    if (!inside && !roofed) return;
    Vector3 best;
    float bd = 1e9f;
    int noMesh = 0, height = 0, indoor = 0, roof = 0;
    for (float r = 10.0f; r <= 30.0f && bd > 1e8f; r += 4.0f)  // 10-18 m first, further out only if nothing fits
        for (int k = 0; k < 16; ++k) {
            float a = k * 3.14159265f / 8.0f;
            Vector3 safe;
            if (!safeCoord(Vector3(st.at.x + std::cos(a) * r, st.at.y + std::sin(a) * r, st.at.z), safe)) { ++noMesh; continue; }
            if (std::fabs(safe.z - st.at.z) > 3.0f) { ++height; continue; }
            if (INTERIOR::GET_INTERIOR_AT_COORDS(safe.x, safe.y, safe.z)) { ++indoor; continue; }
            if (underRoof(safe)) { ++roof; continue; }
            float d = dist(safe, st.dogs);
            if (d < bd) { bd = d; best = safe; }
        }
    if (bd > 1e8f)
        FD_INFO("stable %s: no outdoor spot found (no navmesh %d, height %d, indoors %d, roofed %d)", st.id.c_str(), noMesh,
                height, indoor, roof);
    if (bd < 1e8f) {
        FD_INFO("stable %s: the yard was %s, moved to %.1f %.1f %.1f", st.id.c_str(), inside ? "inside the building" : "under a roof",
                best.x, best.y, best.z);
        st.dogs = best;
    }
}

static void spawnSale(Stable& st) {
    yardOutdoors(st);
    clearSale();
    auto& c = ctx();
    long long day = gameDay();
    auto stock = stableStock(st.id, st.breeds, day, c.breeds, c.econ, c.chances, c.disabledBreeds());
    for (size_t i = 0; i < stock.size() && i < 3; ++i) {
        PendingSale ps;
        ps.req.model = stock[i].dog.model;
        ps.req.fallback = "a_c_doghobo_01";  // a model that always loads (the mutt freeze of 15.11)
        // in a row across the way out (same distance from the building), facing out to whoever comes to the stable
        Vector3 out(st.dogs.x - st.at.x, st.dogs.y - st.at.y, 0);
        float len = std::sqrt(out.x * out.x + out.y * out.y);
        if (len < 0.1f) out = Vector3(1, 0, 0), len = 1;
        Vector3 across(-out.y / len, out.x / len, 0);
        ps.req.at = add(st.dogs, scale(across, (float(i) - 1.0f) * 1.6f));
        ps.req.heading = std::atan2(-out.x, out.y) * 180.0f / 3.14159265f;
        ps.stock = stock[i];
        g_pending.push_back(ps);
    }
    // the trader stands in the open yard a little further out than the spot the game walks Arthur out to (3 m aside
    // put him in front of Valentine's doors), facing out like the dogs; a stable may place him by eye (.trader)
    {
        Vector3 out(st.dogs.x - st.at.x, st.dogs.y - st.at.y, 0);
        float len = std::sqrt(out.x * out.x + out.y * out.y);
        if (len < 0.1f) out = Vector3(1, 0, 0), len = 1;
        Vector3 across(-out.y / len, out.x / len, 0);
        g_traderReq = SpawnRequest{};
        g_traderReq.model = g_traderModel;
        g_traderReq.fallback = "A_M_M_RANCHER_01";
        g_traderReq.at = add(add(st.dogs, scale(across, float(c.tunef("stables.trader_aside", 0)))),
                             scale(Vector3(out.x / len, out.y / len, 0), float(c.tunef("stables.trader_out", 2))));
        g_traderReq.heading = std::atan2(-out.x, out.y) * 180.0f / 3.14159265f;
        if (st.trader.size() >= 3) g_traderReq.at = Vector3(float(st.trader[0]), float(st.trader[1]), float(st.trader[2]));
        if (st.trader.size() >= 4) g_traderReq.heading = float(st.trader[3]);
        g_traderPending = true;
    }
    FD_INFO("stable %s: %zu dogs for sale queued (period %lld)", st.id.c_str(), g_pending.size(), day / c.econ.rotationDays);
}

// All models stream in parallel; at most one dog is created per frame.
static void stepSale() {
    auto& c = ctx();
    if (g_traderPending) {
        if (Ped p = stepSpawn(g_traderReq)) {
            g_traderPending = false;
            g_trader = p;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
            ENTITY::SET_ENTITY_INVINCIBLE(p, true);
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p, true);
            PED::_SET_PED_PROMPT_NAME(p, literal(c.t("stable.trader")));
            TASK::TASK_STAND_STILL(p, -1);
            FD_INFO("stable: the dog trader is in the yard");
        } else if (g_traderReq.done) {
            g_traderPending = false;
        }
    }
    if (g_pending.empty()) return;
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
        PED::_SET_PED_PROMPT_NAME(p, literal(c.breedText(stock.dog)));  // the breed, not the model's species
        ForSale f;
        f.ped = p;
        f.stock = stock;
        f.nextAct = 0;  // starts doing something of its own at once
        g_sale.push_back(f);
        if (g_pending.empty()) FD_INFO("stable: %zu dogs for sale spawned", g_sale.size());
        return;
    }
}

// Buy dog i for sale: it becomes Arthur's dog, with papers. From the yard menu («Купить: …») or the prompt on the dog -
// on a dog for sale the game puts its own «Погладить» on the same key, which wins (17.11), so the menu is the sure way.
static bool pay(int dollars);

static void buy(size_t i) {
    auto& c = ctx();
    if (i >= g_sale.size()) return;
    ForSale& f = g_sale[i];
    if (!f.ped || !exists(f.ped) || !pay(f.stock.price)) return;
    DogRecord d = f.stock.dog;
    d.uid += "-" + std::to_string(unixNow());
    d.name = c.t("dog.default_name");
    d.papers = true;
    Ped bought = f.ped;
    DECORATOR::DECOR_SET_INT(bought, "FD_shop", 0);
    f.ped = 0;
    g_buyPrompts[i].show(false);
    if (companion().adopt(bought, d)) {
        FD_INFO("stable: bought %s (%s) for $%d", d.breed.c_str(), c.gradeText(d).c_str(), f.stock.price);
        helpText(c.f("help.bought", {{"grade", c.gradeText(d)}, {"breed", c.breedText(d)}}, d.female), 6000);
    } else {
        giveCash(f.stock.price * 100);  // no room: money back
        f.ped = bought;
        DECORATOR::DECOR_SET_INT(bought, "FD_shop", 1);
    }
}

// The dogs for sale live in the yard (his pick, 17.11: «бродят по двору» instead of sitting in a row): each on its own
// walks about within a few metres of the yard, sits, lies down, now and then trots up to Arthur; never while he focuses it.
static void yardLife(const Stable& st) {
    auto& c = ctx();
    int t = now();
    if (g_trader && alive(g_trader) && t >= g_nextFace) {
        g_nextFace = t + 3000;
        if (dist(g_trader, player()) < 10.0f) TASK::TASK_TURN_PED_TO_FACE_ENTITY(g_trader, player(), 2500, 0, 0, 0);
    }
    Entity focus = 0;
    PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(PLAYER::PLAYER_ID(), &focus, false, false);
    float radius = float(c.tunef("stables.yard_radius", 5));
    for (auto& f : g_sale) {
        if (!f.ped || !alive(f.ped) || t < f.nextAct || focus == f.ped) continue;
        Ped p = f.ped;
        float toArthur = dist(p, player());
        if (dist(pos(p), st.dogs) > radius + 3.0f) {  // wandered off: back to the yard
            TASK::TASK_GO_STRAIGHT_TO_COORD(p, st.dogs.x, st.dogs.y, st.dogs.z, 1.0f, -1, 0.0f, 1.5f, 0);
            f.nextAct = t + 6000;
            continue;
        }
        int r = c.rng.range(0, 99);
        if (toArthur < 10.0f && r < 20) {
            TASK::TASK_GO_TO_ENTITY(p, player(), 8000, 1.8f, 1.0f, 0, 0);
            f.nextAct = t + c.rng.range(8000, 12000);
        } else if (r < 60) {
            TASK::TASK_WANDER_IN_AREA(p, st.dogs.x, st.dogs.y, st.dogs.z, radius, 2.0f, 4.0f, 0);
            f.nextAct = t + c.rng.range(10000, 20000);
        } else if (r < 80) {
            TASK::TASK_START_SCENARIO_IN_PLACE_HASH(p, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_SITTING"), -1, true, 0, -1.0f, false);
            f.nextAct = t + c.rng.range(8000, 15000);
        } else {
            TASK::TASK_START_SCENARIO_IN_PLACE_HASH(p, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_RESTING"), -1, true, 0, -1.0f, false);
            f.nextAct = t + c.rng.range(15000, 30000);
        }
    }
}

// ---- the stable hand's menu for the dog with Arthur ----

struct MenuItem {
    std::string text;
    int price = 0;  // dollars
    int kind = 0;   // 1 train, 2 cure, 3 pedigree, 4 papers, 5 rename, 6 sell, 7 bring, 8 collar, 10 buy
    std::string arg;
    int slot = -1;
};

static std::vector<MenuItem> menu(DogRecord* d) {
    auto& c = ctx();
    std::vector<MenuItem> out;
    for (size_t i = 0; i < g_sale.size(); ++i)  // the dogs for sale first: buying is what most come for
        if (g_sale[i].ped && exists(g_sale[i].ped)) {
            const DogRecord& s = g_sale[i].stock.dog;
            out.push_back({c.f("stable.buy_item", {{"grade", c.gradeBeforeBreed(s)}, {"breed", c.breedText(s)}}, s.female),
                           g_sale[i].stock.price, 10, "", int(i)});
        }
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
        std::string worn = collars::worn(*d);  // a dog of Arthur's is never without one: changed, not taken off
        for (const auto& id : collars::ids())
            if (id != worn)
                out.push_back({c.f("stable.collar", {{"collar", c.t("collar." + id)}}), c.econ.collarPrice, 8, id});
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
    if (m.kind == 10) {
        buy(size_t(m.slot));
        return;
    }
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
            if (!d) return;
            d->collar = m.arg;
            comp.save();
            if (Ped p = comp.ped()) collars::dress(p, *d);
            helpText(c.f("help.collar_on", {{"name", d->name}}, d->female));
            FD_INFO("stable: %s collar %s", d->name.c_str(), m.arg.c_str());
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

// ---- the talk with the dog trader (24.11, his design 17 on the canvas): Arthur's dog's papers on the left (the
// paper of the game's own map), the trader's line and the answers on the right; up/down + accept or the number keys,
// back/«Бывай» closes. The deeds are the old menu's (doItem). TraderTalk=0 in the ini keeps the old prompt list. ----

enum class Screen { Root, Train, Sale, Collar, Sell, Bring };
static Screen g_screen = Screen::Root;
static int g_sel = 0, g_saleIdx = 0, g_talkSince = 0;
static std::string g_line;
static std::string g_testKey;                     // FD_TEST: a key pressed by the test hook

// kinds 1..10 = the old menu's deeds (doItem); the talk's own: 20 training list, 21 dogs for sale, 22 collars,
// 23 his price for the dog, 24 dogs at home, 26 the next dog for sale, 27 back, 28 goodbye
struct TalkOpt {
    std::string text;
    int kind = 0;
    int price = 0;
    std::string arg;
    int slot = -1;
};

static std::string priced(const std::string& text, int price) {
    return price > 0 ? text + ", $" + std::to_string(price) : text;
}

static std::vector<int> saleSlots() {
    std::vector<int> out;
    for (size_t i = 0; i < g_sale.size(); ++i)
        if (g_sale[i].ped && exists(g_sale[i].ped)) out.push_back(int(i));
    return out;
}

static std::vector<int> homeSlots() {
    auto& c = ctx();
    std::vector<int> out;
    for (int i = 0; i < DogStore::kSlots; ++i)
        if (c.store.used(i) && i != companion().slot() && c.store.at(i).where == Where::Home) out.push_back(i);
    return out;
}

static const char* romanNum(int n) {
    static const char* r[] = {"", "I", "II", "III", "IV", "V"};
    return r[std::clamp(n, 0, 5)];
}

static std::vector<TalkOpt> talkOptions(DogRecord* d) {
    auto& c = ctx();
    std::vector<TalkOpt> o;
    bool fem = d && d->female;
    switch (g_screen) {
        case Screen::Root: {
            if (d) {
                bool trainable = false;
                for (const auto& sk : c.breeds.skills())
                    if (trainingPrice(*d, sk, c.econ) >= 0) trainable = true;
                if (trainable) o.push_back({c.t("talk.opt.train", fem), 20});
            }
            if (!saleSlots().empty()) o.push_back({c.t("talk.opt.sale"), 21});
            if (d) {
                const BreedDef* b = c.breeds.find(d->breed);
                if (d->grade == Grade::Ailing) o.push_back({priced(c.t("talk.opt.cure", fem), c.econ.curePrice), 2, c.econ.curePrice});
                if (b && !b->mutt && d->grade == Grade::Ordinary)
                    o.push_back({priced(c.t("talk.opt.pedigree", fem), c.econ.pedigreePrice), 3, c.econ.pedigreePrice});
                if (!d->papers) {
                    int p = papersPrice(*d, c.econ);
                    o.push_back({priced(c.t("talk.opt.papers", fem), p), 4, p});
                }
                if (collars::ids().size() > 1) o.push_back({priced(c.t("talk.opt.collar"), c.econ.collarPrice), 22});
                o.push_back({priced(c.t("talk.opt.rename"), c.econ.renamePrice), 5, c.econ.renamePrice});
                o.push_back({c.t("talk.opt.sell", fem), 23});
            }
            if (!homeSlots().empty()) o.push_back({priced(c.t("talk.opt.bring"), c.econ.swapPrice), 24});
            o.push_back({c.t("talk.opt.bye"), 28});
            return o;
        }
        case Screen::Train:
            if (d)
                for (const auto& sk : c.breeds.skills()) {
                    int price = trainingPrice(*d, sk, c.econ);
                    if (price < 0) continue;
                    int lvl = d->skill(sk);
                    o.push_back({priced(c.f("talk.train_item", {{"skill", c.t("skill." + sk)}, {"from", romanNum(lvl)},
                                                                {"to", romanNum(lvl + 1)}}), price), 1, price, sk});
                }
            break;
        case Screen::Sale: {
            auto slots = saleSlots();
            if (g_saleIdx >= 0 && g_saleIdx < int(g_sale.size()) && g_sale[g_saleIdx].ped) {
                const ForSale& f = g_sale[g_saleIdx];
                o.push_back({priced(c.t("talk.opt.buy", f.stock.dog.female), f.stock.price), 10, f.stock.price, "", g_saleIdx});
            }
            if (slots.size() > 1) o.push_back({c.t("talk.opt.next_dog"), 26});
            break;
        }
        case Screen::Collar:
            if (d) {
                std::string worn = collars::worn(*d);
                for (const auto& id : collars::ids())
                    if (id != worn) o.push_back({priced(c.t("collar." + id), c.econ.collarPrice), 8, c.econ.collarPrice, id});
            }
            break;
        case Screen::Sell:
            if (d) {
                int price = sellPrice(*d, c.breeds, c.econ);
                o.push_back({c.f("talk.opt.sell_yes", {{"price", std::to_string(price)}}), 6, -price});
            }
            o.push_back({c.t("talk.opt.sell_no", fem), 27});
            return o;
        case Screen::Bring:
            for (int s : homeSlots())
                o.push_back({priced(c.store.at(s).name, c.econ.swapPrice), 7, c.econ.swapPrice, "", s});
            break;
    }
    o.push_back({c.t("talk.opt.back"), 27});
    return o;
}

// what the trader says on a screen
// «Американский фоксхаунд» inside a sentence: the first letter small (Latin and Cyrillic А-Я, Ё)
static std::string lowerFirst(std::string s) {
    if (s.empty()) return s;
    unsigned char a = (unsigned char)s[0];
    if (a < 0x80) { s[0] = char(std::tolower(a)); return s; }
    if (s.size() >= 2 && a == 0xD0) {
        unsigned char b = (unsigned char)s[1];
        if (b >= 0x90 && b <= 0x9F) s[1] = char(b + 0x20);                       // А-П -> а-п
        else if (b >= 0xA0 && b <= 0xAF) { s[0] = char(0xD1); s[1] = char(b - 0x20); }  // Р-Я -> р-я
        else if (b == 0x81) { s[0] = char(0xD1); s[1] = char(0x91); }               // Ё -> ё
    }
    return s;
}

static std::string screenLine(DogRecord* d) {
    auto& c = ctx();
    bool fem = d && d->female;
    switch (g_screen) {
        case Screen::Root:
            if (!d) return c.t("talk.hello.nodog");
            if (d->grade == Grade::Ailing) return c.t("talk.hello.ailing", fem);
            return c.t(d->papers ? "talk.hello.papers" : "talk.hello.nopapers", fem);
        case Screen::Train: return c.t("talk.say.train", fem);
        case Screen::Sale:
            if (g_saleIdx >= 0 && g_saleIdx < int(g_sale.size())) {
                const DogRecord& s = g_sale[g_saleIdx].stock.dog;
                return c.f("talk.say.sale", {{"grade", c.gradeBeforeBreed(s)}, {"breed", lowerFirst(c.breedText(s))},
                                             {"price", std::to_string(g_sale[g_saleIdx].stock.price)}}, s.female);
            }
            return c.t("talk.hello.nodog");
        case Screen::Collar: return c.t("talk.say.collar", fem);
        case Screen::Sell:
            if (!d) return "";
            return c.f(d->papers ? "talk.say.sell" : "talk.say.sell_nopapers",
                       {{"price", std::to_string(sellPrice(*d, c.breeds, c.econ))}}, fem);
        case Screen::Bring: return c.t("talk.say.bring");
    }
    return "";
}

static void goScreen(Screen s, DogRecord* d) {
    g_screen = s;
    g_sel = 0;
    if (s == Screen::Sale) {
        auto slots = saleSlots();
        g_saleIdx = slots.empty() ? -1 : slots[0];
    }
    g_line = screenLine(d);
}

static void talkOpen() {
    g_talk = true;
    g_talkSince = now();
    goScreen(Screen::Root, companion().record());
    if (g_trader && alive(g_trader)) TASK::TASK_TURN_PED_TO_FACE_ENTITY(g_trader, player(), 3000, 0, 0, 0);
    FD_INFO("stable: talk with the trader");
}

static void talkClose() {
    g_talk = false;
    g_talkAccept.show(false);
    g_talkBack.show(false);
}

static void talkAnswer(const TalkOpt& o, const Stable& st) {
    auto& c = ctx();
    DogRecord* d = companion().record();
    bool fem = d && d->female;
    switch (o.kind) {
        case 20: goScreen(Screen::Train, d); return;
        case 21: goScreen(Screen::Sale, d); return;
        case 22: goScreen(Screen::Collar, d); return;
        case 23: goScreen(Screen::Sell, d); return;
        case 24: goScreen(Screen::Bring, d); return;
        case 26: {
            auto slots = saleSlots();
            if (slots.empty()) return;
            auto it = std::find(slots.begin(), slots.end(), g_saleIdx);
            g_saleIdx = (it == slots.end() || it + 1 == slots.end()) ? slots[0] : *(it + 1);
            g_line = screenLine(d);
            g_sel = 0;
            return;
        }
        case 27:
            goScreen(Screen::Root, d);
            return;
        case 28:
            talkClose();
            return;
    }
    // a deed: money first (the line tells when it is short), then the old menu's doing of it
    if (o.price > 0 && o.kind != 5 && cash() < o.price * 100) {
        g_line = c.t("talk.no_money");
        return;
    }
    std::string name = d ? d->name : "";
    MenuItem m{o.text, o.price, o.kind, o.arg, o.slot};
    if (o.kind == 5) {  // the new name is typed on the game's keyboard: the talk closes for it
        talkClose();
        doItem(m, st);
        return;
    }
    doItem(m, st);
    DogRecord* now = companion().record();
    g_screen = Screen::Root;
    g_sel = 0;
    switch (o.kind) {
        case 1: case 2: case 3: g_line = c.f("talk.done.away", {{"name", name}}, fem); break;
        case 4: g_line = c.t("talk.done.papers", fem); break;
        case 6: g_line = c.f("talk.done.sold", {{"name", name}}, fem); break;
        case 7: g_line = c.f("talk.done.bring", {{"name", o.slot >= 0 && c.store.used(o.slot) ? c.store.at(o.slot).name : ""}}); break;
        case 8: g_line = c.t("talk.done.collar", fem); break;
        case 10: g_line = c.t("talk.done.bought", now && now->female); break;
        default: g_line = screenLine(now);
    }
}

static std::string townOf(const Stable& st) { return ctx().t("talk.town." + st.id); }

// one frame of the open talk; false when it closed
static bool talkTick(const Stable& st) {
    auto& c = ctx();
    bool trader = g_trader && alive(g_trader);
    if (!trader || dist(g_trader, player()) > 6.0f || PED::IS_PED_IN_COMBAT(player(), 0) || !PED::IS_PED_ON_FOOT(player()) ||
        g_keyboard) {
        talkClose();
        return false;
    }
    // Arthur stands still while they talk; the menu keys stay on (with every control off the pad's Select/Back prompts
    // did not show, 24.11)
    static const char* kKeys[] = {"INPUT_FRONTEND_UP", "INPUT_FRONTEND_DOWN", "INPUT_FRONTEND_ACCEPT", "INPUT_FRONTEND_CANCEL"};
    PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
    for (const char* k : kKeys) PAD::ENABLE_CONTROL_ACTION(0, MISC::GET_HASH_KEY(k), false);
    HUD::HIDE_HUD_AND_RADAR_THIS_FRAME();
    g_menuNext.show(false);
    g_menuDo.show(false);
    for (auto& p : g_buyPrompts) p.show(false);
    DogRecord* d = companion().record();
    auto opts = talkOptions(d);
    if (opts.empty()) return true;
    g_sel = std::clamp(g_sel, 0, int(opts.size()) - 1);
    bool kb = PAD::IS_USING_KEYBOARD_AND_MOUSE(0) && c.tune("talk.force_pad", 0) == 0;
    // keys, not in the first quarter second (the press that opened the talk)
    if (now() - g_talkSince > 250) {
        auto pressed = [](const char* ctl) {
            Hash h = MISC::GET_HASH_KEY(ctl);
            return PAD::IS_CONTROL_JUST_PRESSED(0, h) || PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, h);
        };
        std::string k = g_testKey;
        g_testKey.clear();
        bool up = pressed("INPUT_FRONTEND_UP") || k == "up", down = pressed("INPUT_FRONTEND_DOWN") || k == "down";
        bool accept = pressed("INPUT_FRONTEND_ACCEPT") || k == "accept";
        bool back = pressed("INPUT_FRONTEND_CANCEL") || k == "back";
        int digit = game::digitJustPressed();
        if (!k.empty() && k[0] >= '1' && k[0] <= '9') digit = k[0] - '0';
        if (up) g_sel = (g_sel + int(opts.size()) - 1) % int(opts.size());
        if (down) g_sel = (g_sel + 1) % int(opts.size());
        if (digit > 0 && digit < int(opts.size())) {  // the last answer (back/bye) is on Backspace, not on a number
            g_sel = digit - 1;
            accept = true;
        }
        if (back) {
            if (g_screen == Screen::Root) { talkClose(); return false; }
            goScreen(Screen::Root, d);
            return true;
        }
        if (accept) {
            talkAnswer(opts[g_sel], st);
            if (!g_talk) return false;
            d = companion().record();
            opts = talkOptions(d);
            g_sel = std::clamp(g_sel, 0, int(opts.size()) - 1);
        }
    }
    // the view
    ui::TalkView v;
    v.line = g_line;
    for (const auto& o : opts) v.options.push_back(o.text);
    v.selected = g_sel;
    v.keyboard = kb;
    const DogRecord* subject = nullptr;
    if (g_screen == Screen::Sale && g_saleIdx >= 0 && g_saleIdx < int(g_sale.size())) {
        subject = &g_sale[g_saleIdx].stock.dog;
        v.sheet.owner = c.t("talk.owner.trader");
        v.sheet.value = g_sale[g_saleIdx].stock.price;
        v.sheet.noName = true;
    } else if (g_screen == Screen::Bring && opts[g_sel].slot >= 0 && c.store.used(opts[g_sel].slot)) {
        subject = &c.store.at(opts[g_sel].slot);
        v.sheet.owner = c.t("talk.owner.arthur");
        v.sheet.value = sellPrice(*subject, c.breeds, c.econ);
    } else if (d) {
        subject = d;
        v.sheet.owner = c.t("talk.owner.arthur");
        v.sheet.value = sellPrice(*d, c.breeds, c.econ);
        if (Ped p = companion().ped())
            if (alive(p))
                v.sheet.healthPct = int(100.0f * ENTITY::GET_ENTITY_HEALTH(p) / std::max(1, ENTITY::GET_ENTITY_MAX_HEALTH(p, false)));
    } else {
        auto slots = saleSlots();  // no dog of his: the first dog in the yard
        if (!slots.empty()) {
            subject = &g_sale[slots[0]].stock.dog;
            v.sheet.owner = c.t("talk.owner.trader");
            v.sheet.value = g_sale[slots[0]].stock.price;
            v.sheet.noName = true;
        }
    }
    v.sheet.dog = subject;
    v.sheet.town = townOf(st);
    ui::talkDraw(v);
    g_talkAccept.show(!kb);
    g_talkBack.show(!kb);
    return true;
}

bool talking() { return g_talk; }

#ifdef FD_TEST
void testTalk(const std::string& what) {
    if (what == "open") {
        if (g_active >= 0) talkOpen();
    } else if (what == "close") {
        talkClose();
    } else {
        g_testKey = what;
    }
}
#endif

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
            if (g_talk) talkClose();
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
    if (g_active >= 0) yardLife(g_stables[g_active]);
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
        pr.setText(c.f("stable.buy", {{"grade", c.gradeBeforeBreed(f.stock.dog)}, {"breed", c.breedText(f.stock.dog)},
                                       {"price", std::to_string(f.stock.price)}}, f.stock.dog.female));
        pr.show(closeTo);
        if (focus == f.ped && PAD::IS_CONTROL_JUST_PRESSED(0, MISC::GET_HASH_KEY("INPUT_INTERACT_LOCKON_DETAILS_ANIMAL")))
            ui::cardShow(f.stock.dog, f.ped);
        if (closeTo && pr.fired()) buy(i);
    }
    // the stable hand's dog menu: in the yard by the dogs for sale, on foot. Never inside the building: the game opens
    // its own stable menu as soon as Arthur walks in and, on Back, walks him out to the yard (17.11, Valentine)
    bool trader = g_trader && alive(g_trader);
    if (g_talk && talkTick(st)) return;
    bool atStable = (trader ? dist(g_trader, player()) < float(c.tunef("stables.trader_reach", 3))
                            : dist(pos(player()), st.dogs) < float(c.tunef("stables.menu_radius", 5))) &&
                    PED::IS_PED_ON_FOOT(player()) && !PED::IS_PED_IN_COMBAT(player(), 0);
    auto items = menu(companion().record());
    if (!atStable || items.empty() || g_keyboard) {
        g_menuNext.show(false);
        g_menuDo.show(false);
        return;
    }
    if (g_talkMode && trader) {  // one prompt under his name: «Поговорить»
        HUD::_UI_PROMPT_SET_ACTIVE_GROUP_THIS_FRAME(g_traderGroup, literal(c.t("stable.trader")), 0, 0, 0, 0);
        g_menuNext.show(false);
        g_menuDo.setText(c.t("talk.prompt"));
        g_menuDo.show(true);
        if (g_menuDo.fired()) talkOpen();
        return;
    }
    HUD::_UI_PROMPT_SET_ACTIVE_GROUP_THIS_FRAME(g_traderGroup, literal(c.t("stable.trader")), 0, 0, 0, 0);
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
