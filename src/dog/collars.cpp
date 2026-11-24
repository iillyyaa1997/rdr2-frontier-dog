#include "dog/collars.h"
#include <cctype>
#include <map>
#include "core/fs.h"
#include "core/kvfile.h"
#include "core/log.h"
#include "mod/context.h"

namespace fd::collars {

struct Collar {
    Hash drawable = 0, albedo = 0, normal = 0, material = 0, palette = 0;
    int tint0 = 0, tint1 = 0, tint2 = 0;
};

static std::vector<std::string> g_ids;
static std::map<std::string, Collar> g_collars;
static bool g_loaded = false;

static void load() {
    if (g_loaded) return;
    g_loaded = true;
    KvFile kv;
    if (!kv.load(fs::join(ctx().dataDir, "Collars.dat"))) {
        FD_WARN("Collars.dat not found: no collars");
        return;
    }
    for (const auto& id : kv.getList("collars")) {
        auto v = kv.getList("collar." + id);
        if (v.size() != 8) {
            FD_WARN("collar %s: 8 values expected, %d found", id.c_str(), int(v.size()));
            continue;
        }
        // a number (0x...) or a name hashed by the game (palettes: metaped_tint_*)
        auto h = [&](int i) {
            const std::string& s = v[i];
            bool num = !s.empty() && (std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '-');
            return num ? Hash(std::stoul(s, nullptr, 0)) : Hash(MISC::GET_HASH_KEY(s.c_str()));
        };
        Collar c{h(0), h(1), h(2), h(3), h(4), std::atoi(v[5].c_str()), std::atoi(v[6].c_str()), std::atoi(v[7].c_str())};
        g_collars[id] = c;
        g_ids.push_back(id);
    }
    FD_INFO("collars: %d", int(g_ids.size()));
}

std::vector<std::string> ids() {
    load();
    return g_ids;
}

bool known(const std::string& id) {
    load();
    return g_collars.count(id) > 0;
}

std::string worn(const DogRecord& d) {
    load();
    if (g_collars.count(d.collar)) return d.collar;
    return g_ids.empty() ? "" : g_ids.front();  // every dog of Arthur's wears a collar (his wish, 17.11)
}

void dress(Ped ped, const DogRecord& d) {
    if (!ped) return;
    if (d.preset >= 0) PED::_EQUIP_META_PED_OUTFIT_PRESET(ped, d.preset, false);
    std::string id = worn(d);
    if (id.empty() || !ctx().ini.getBool("Collar", true)) return;  // Collar=0: no collar (Nexus 22.11, big on some coats)
    const Collar& c = g_collars[id];
    PED::_SET_META_PED_TAG(ped, c.drawable, c.albedo, c.normal, c.material, c.palette, c.tint0, c.tint1, c.tint2);
    PED::_UPDATE_PED_VARIATION(ped, false, true, true, true, false);
}

}  // namespace fd::collars
