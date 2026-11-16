#include "logic/breeds.h"

#include "core/strutil.h"

namespace fd {

int BreedDef::fiveStat() const {
    for (int i = 0; i < StatCount; ++i)
        if (stats[i] >= 5) return i;
    return -1;
}

static int statIndex(const std::string& s) {
    static const char* names[] = {"strength", "nose", "stamina", "courage", "trainability"};
    for (int i = 0; i < StatCount; ++i)
        if (s == names[i]) return i;
    return -1;
}

bool BreedTable::load(const KvFile& d) {
    raw_ = d;
    breeds_.clear();
    traits_.clear();
    deeds_.clear();
    skills_ = d.getList("skills");

    for (const auto& id : d.getList("traits")) {
        TraitDef t;
        t.id = id;
        t.price = int(d.getInt("trait." + id + ".price", 1));
        t.rollable = d.getBool("trait." + id + ".rollable", true);
        traits_.push_back(t);
    }
    for (const auto& id : d.getList("deeds")) {
        DeedDef x;
        x.id = id;
        for (const auto& s : d.getList("deed." + id + ".stats")) {
            int i = statIndex(s);
            if (i >= 0) x.stats.push_back(i);
        }
        x.traits = d.getList("deed." + id + ".traits");
        x.step = int(d.getInt("deed." + id + ".step", 10));
        deeds_.push_back(x);
    }
    for (const auto& id : d.getList("breeds")) {
        BreedDef b;
        b.id = id;
        const std::string p = id + ".";
        for (auto& m : str::split(d.get(p + "model"), '|'))
            if (!m.empty()) b.models.push_back(m);
        b.preset = int(d.getInt(p + "preset", -1));
        b.scale = int(d.getInt(p + "scale", 100));
        for (const auto& a : d.getList(p + "animal_type")) {
            long long v;
            if (str::parseInt(a, v)) b.animalTypes.push_back(unsigned(v));
        }
        auto st = d.getList(p + "stats");
        for (int i = 0; i < StatCount && i < int(st.size()); ++i) b.stats[i] = std::atoi(st[i].c_str());
        b.traits = d.getList(p + "traits");
        b.bond3Traits = d.getList(p + "bond3_traits");
        b.price = int(d.getInt(p + "price", 0));
        b.mutt = d.getBool(p + "mutt", false);
        b.iniSwitch = d.get(p + "ini");
        b.strayOnly = d.getBool(p + "stray_only", false);
        for (const auto& k : d.keysWithPrefix(p + "habit."))
            b.habits.set(k.substr(p.size() + 6), d.get(k));
        breeds_.push_back(b);
    }
    return !breeds_.empty();
}

const BreedDef* BreedTable::find(const std::string& id) const {
    for (const auto& b : breeds_)
        if (b.id == id) return &b;
    return nullptr;
}

const BreedDef* BreedTable::byAnimalType(unsigned hash) const {
    for (const auto& b : breeds_)
        for (unsigned a : b.animalTypes)
            if (a == hash) return &b;
    return nullptr;
}

const BreedDef* BreedTable::byModel(unsigned modelHash) const {
    for (const auto& b : breeds_)
        for (const auto& m : b.models)
            if (str::joaat(m.c_str()) == modelHash) return &b;
    return nullptr;
}

const TraitDef* BreedTable::trait(const std::string& id) const {
    for (const auto& t : traits_)
        if (t.id == id) return &t;
    return nullptr;
}

const DeedDef* BreedTable::deed(const std::string& id) const {
    for (const auto& x : deeds_)
        if (x.id == id) return &x;
    return nullptr;
}

int BreedTable::traitPoints(const std::vector<std::string>& ids) const {
    int sum = 0;
    for (const auto& id : ids) {
        const TraitDef* t = trait(id);
        sum += t ? t->price : 1;
    }
    return sum;
}

}  // namespace fd
