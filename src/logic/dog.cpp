#include "logic/dog.h"

#include <algorithm>
#include <cstdio>

#include "core/strutil.h"

namespace fd {

static const char* kStatNames[StatCount] = {"strength", "nose", "stamina", "courage", "trainability"};

// ---- record ----

int DogRecord::statSum() const {
    int s = 0;
    for (int v : stats) s += v;
    return s;
}

int DogRecord::skill(const std::string& id) const {
    auto it = skillLevel.find(id);
    return it == skillLevel.end() ? 1 : it->second;
}

bool DogRecord::hasTrait(const std::string& id) const {
    return std::find(traits.begin(), traits.end(), id) != traits.end();
}

bool DogRecord::isStreetHardened(const BreedTable& t) const {
    const BreedDef* b = t.find(breed);
    return b && b->mutt && grade == Grade::Pedigree;
}

static void readMap(const KvFile& kv, const std::string& prefix, std::map<std::string, int>& m) {
    m.clear();
    for (const auto& k : kv.keysWithPrefix(prefix)) m[k.substr(prefix.size())] = int(kv.getInt(k));
}

static void writeMap(KvFile& kv, const std::string& prefix, const std::map<std::string, int>& m) {
    for (const auto& k : kv.keysWithPrefix(prefix)) kv.erase(k);
    for (const auto& [k, v] : m) kv.setInt(prefix + k, v);
}

void DogRecord::fromKv(const KvFile& kv) {
    extra = kv;
    uid = kv.get("uid");
    name = kv.get("name");
    breed = kv.get("breed");
    female = kv.getBool("female");
    model = kv.get("model");
    preset = int(kv.getInt("preset", -1));
    scale = int(kv.getInt("scale", 100));
    grade = Grade(std::clamp<long long>(kv.getInt("grade", 1), 0, 2));
    papers = kv.getBool("papers");
    auto st = kv.getList("stats");
    for (int i = 0; i < StatCount; ++i) stats[i] = i < int(st.size()) ? std::atoi(st[i].c_str()) : 1;
    fiveStat = int(kv.getInt("five", -1));
    traits = kv.getList("traits");
    bondLevel = int(std::clamp<long long>(kv.getInt("bond.level", 1), 1, 3));
    bondPoints = kv.getFloat("bond.points", 0);
    readMap(kv, "skill.", skillLevel);
    readMap(kv, "skilluse.", skillUses);
    healthFrac = kv.getFloat("health", 1.0);
    hunger = kv.getFloat("hunger", 0);
    dirt = kv.getFloat("dirt", 0);
    limp = kv.getBool("limp");
    limpUntil = kv.getInt("limp.until", 0);
    readMap(kv, "deed.", deeds);
    readMap(kv, "deedsteps.", deedSteps);
    readMap(kv, "deedbank.", deedTraitBank);
    hardenStats = int(kv.getInt("harden.stats", 0));
    hardenTraits = int(kv.getInt("harden.traits", 0));
    where = Where(std::clamp<long long>(kv.getInt("where", 1), 0, 2));
    awayUntil = kv.getInt("away.until", 0);
    awayReason = kv.get("away.reason");
    awayStable = kv.get("away.stable");
    woundedUntil = kv.getInt("wounded.until", 0);
    auto wp = kv.getFloats("wounded.pos");
    if (wp.size() == 3) { woundedX = float(wp[0]); woundedY = float(wp[1]); woundedZ = float(wp[2]); }
}

KvFile DogRecord::toKv() const {
    KvFile kv = extra;
    kv.setInt("schema", kSchema);
    kv.set("uid", uid);
    kv.set("name", name);
    kv.set("breed", breed);
    kv.setBool("female", female);
    kv.set("model", model);
    kv.setInt("preset", preset);
    kv.setInt("scale", scale);
    kv.setInt("grade", int(grade));
    kv.setBool("papers", papers);
    std::vector<std::string> st;
    for (int v : stats) st.push_back(std::to_string(v));
    kv.setList("stats", st);
    kv.setInt("five", fiveStat);
    kv.setList("traits", traits);
    kv.setInt("bond.level", bondLevel);
    kv.setFloat("bond.points", bondPoints, 1);
    writeMap(kv, "skill.", skillLevel);
    writeMap(kv, "skilluse.", skillUses);
    kv.setFloat("health", healthFrac, 3);
    kv.setFloat("hunger", hunger, 1);
    kv.setFloat("dirt", dirt, 1);
    kv.setBool("limp", limp);
    kv.setInt("limp.until", limpUntil);
    writeMap(kv, "deed.", deeds);
    writeMap(kv, "deedsteps.", deedSteps);
    writeMap(kv, "deedbank.", deedTraitBank);
    kv.setInt("harden.stats", hardenStats);
    kv.setInt("harden.traits", hardenTraits);
    kv.setInt("where", int(where));
    kv.setInt("away.until", awayUntil);
    kv.set("away.reason", awayReason);
    kv.set("away.stable", awayStable);
    kv.setInt("wounded.until", woundedUntil);
    char pos[96];
    std::snprintf(pos, sizeof pos, "%.2f,%.2f,%.2f", woundedX, woundedY, woundedZ);
    kv.set("wounded.pos", pos);
    return kv;
}

// ---- stat helpers ----

static int argMax(const Stats& s, Rng& rng) {
    int best = -1, n = 0;
    for (int i = 0; i < StatCount; ++i) {
        if (best < 0 || s[i] > s[best]) { best = i; n = 1; }
        else if (s[i] == s[best] && rng.range(0, n++) == 0) best = i;  // fair tie-break
    }
    return best;
}

static void removePoints(Stats& s, int points, int floor, Rng& rng) {
    for (int k = 0; k < points; ++k) {
        int i = argMax(s, rng);
        if (s[i] <= floor) return;
        --s[i];
    }
}

// keep the sum, move points down from stats above cap onto the lowest stats below cap
static void capStats(Stats& s, int cap) {
    for (int guard = 0; guard < 50; ++guard) {
        int hi = -1, lo = -1;
        for (int i = 0; i < StatCount; ++i) {
            if (s[i] > cap && (hi < 0 || s[i] > s[hi])) hi = i;
            if (s[i] < cap && (lo < 0 || s[i] < s[lo])) lo = i;
        }
        if (hi < 0) return;
        --s[hi];
        if (lo >= 0) ++s[lo];
    }
}

Stats ordinaryFromBreed(const BreedDef& b, Rng& rng) {
    Stats s = b.stats;
    removePoints(s, 4, 1, rng);  // the natural 5 is the highest, so it goes first
    capStats(s, 4);
    // a small spread (D99): one random +1/-1 swap inside 1..4
    std::vector<int> up, down;
    for (int i = 0; i < StatCount; ++i) {
        if (s[i] < 4) up.push_back(i);
        if (s[i] > 1) down.push_back(i);
    }
    if (!up.empty() && !down.empty()) {
        int i = up[rng.range(0, int(up.size()) - 1)];
        int j = down[rng.range(0, int(down.size()) - 1)];
        if (i != j) { ++s[i]; --s[j]; }
    }
    return s;
}

Stats randomStats(int sum, int lo, int hi, int maxFives, Rng& rng, int* fiveIndex) {
    Stats s;
    s.fill(lo);
    int fives = 0;
    if (fiveIndex) *fiveIndex = -1;
    for (int left = sum - lo * StatCount; left > 0; --left) {
        std::vector<int> can;
        for (int i = 0; i < StatCount; ++i) {
            int cap = (hi >= 5 && fives < maxFives) ? 5 : std::min(hi, 4);
            if (s[i] < cap) can.push_back(i);
        }
        if (can.empty()) break;
        int i = can[rng.range(0, int(can.size()) - 1)];
        if (++s[i] == 5) {
            ++fives;
            if (fiveIndex) *fiveIndex = i;
        }
    }
    return s;
}

static Stats ailingFrom(Stats s, Rng& rng) {
    removePoints(s, 4, 1, rng);
    capStats(s, 3);
    return s;
}

// ---- traits ----

std::vector<std::string> rollTraits(int points, const BreedDef& own, const BreedTable& t, Rng& rng, double ownShare,
                                    const TraitBias& bias, bool allowSignatures, const std::vector<std::string>& have) {
    std::vector<std::string> out;
    auto owned = [&](const std::string& id) {
        return std::find(have.begin(), have.end(), id) != have.end() || std::find(out.begin(), out.end(), id) != out.end();
    };
    auto weightOf = [&](const std::string& id) {
        auto it = bias.find(id);
        return it == bias.end() ? 1.0 : it->second;
    };
    int left = points;
    while (left > 0) {
        std::vector<std::string> ownPool, anyPool;
        for (const auto& id : own.traits) {
            const TraitDef* d = t.trait(id);
            if (d && d->price <= std::min(left, 2) && !owned(id)) ownPool.push_back(id);
        }
        for (const auto& d : t.traits()) {
            if (!d.rollable || owned(d.id) || d.price > left) continue;
            if (d.price >= 3 && !allowSignatures) continue;
            anyPool.push_back(d.id);
        }
        bool useOwn = !ownPool.empty() && (anyPool.empty() || rng.chance(ownShare));
        const auto& pool = useOwn ? ownPool : anyPool;
        if (pool.empty()) break;
        std::vector<double> w;
        for (const auto& id : pool) w.push_back(weightOf(id));
        int i = rng.weighted(w);
        if (i < 0) break;
        out.push_back(pool[i]);
        left -= t.trait(pool[i])->price;
    }
    return out;
}

// ---- new dogs ----

static std::string newUid(Rng& rng) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)rng.next());
    return buf;
}

DogRecord makeDog(const BreedDef& b, const BreedTable& t, Grade g, Rng& rng, const RollChances& c, const TraitBias& bias) {
    DogRecord d;
    d.uid = newUid(rng);
    d.breed = b.id;
    d.female = rng.chance(0.5);
    d.model = b.models.empty() ? "" : b.models[rng.range(0, int(b.models.size()) - 1)];
    d.preset = b.preset;
    d.scale = b.mutt ? rng.range(85, 110) : b.scale;  // every mutt its own size (D106)
    d.grade = g;
    if (b.mutt) {
        if (g == Grade::Pedigree) {
            d.stats = randomStats(16, 1, 5, 1, rng, &d.fiveStat);
            d.traits = rollTraits(5, b, t, rng, 0.0, bias, true, {});
        } else {
            d.stats = randomStats(g == Grade::Ailing ? 8 : 12, 1, g == Grade::Ailing ? 3 : 4, 0, rng, nullptr);
            if (g == Grade::Ordinary) d.traits = rollTraits(2, b, t, rng, c.ownBreedTrait, bias, false, {});
        }
    } else if (g == Grade::Pedigree) {
        d.stats = b.stats;
        d.fiveStat = b.fiveStat();
        d.traits = b.traits;
    } else {
        Stats s = ordinaryFromBreed(b, rng);
        if (g == Grade::Ailing) s = ailingFrom(s, rng);
        else d.traits = rollTraits(2, b, t, rng, c.ownBreedTrait, bias, false, {});
        d.stats = s;
    }
    for (const auto& sk : t.skills()) d.skillLevel[sk] = 1;
    return d;
}

DogRecord rollStray(const BreedDef& b, const BreedTable& t, Rng& rng, const RollChances& c, const TraitBias& bias) {
    double r = rng.unit();
    Grade g = r < c.pedigree ? Grade::Pedigree : (r < c.pedigree + c.ailing ? Grade::Ailing : Grade::Ordinary);
    DogRecord d = makeDog(b, t, g, rng, c, bias);
    d.papers = false;  // strays never have papers (D118)
    return d;
}

// ---- grade changes ----

bool gradeDown(DogRecord& d, const BreedTable& t, Rng& rng) {
    if (d.grade == Grade::Ailing) return false;
    if (d.grade == Grade::Pedigree) {
        removePoints(d.stats, 4, 1, rng);
        capStats(d.stats, 4);
        d.fiveStat = -1;
        // lose the signature, keep up to 2 points of the small traits in their order (D109)
        std::vector<std::string> keep;
        int pts = 0;
        for (const auto& id : d.traits) {
            const TraitDef* td = t.trait(id);
            int p = td ? td->price : 1;
            if (p >= 3 || pts + p > 2) continue;
            keep.push_back(id);
            pts += p;
        }
        d.traits = keep;
        d.grade = Grade::Ordinary;
        // a mutt that lost its hardening can earn it again
        d.deeds.clear();
        d.deedSteps.clear();
        d.deedTraitBank.clear();
        d.hardenStats = d.hardenTraits = 0;
        return true;
    }
    d.stats = ailingFrom(d.stats, rng);
    d.fiveStat = -1;
    d.traits.clear();
    d.grade = Grade::Ailing;
    return true;
}

void cure(DogRecord& d, const BreedTable& t, Rng& rng, const RollChances& c) {
    if (d.grade != Grade::Ailing) return;
    const BreedDef* b = t.find(d.breed);
    for (int k = 0; k < 4; ++k) {
        std::vector<double> w;
        for (int i = 0; i < StatCount; ++i) w.push_back(d.stats[i] < 4 ? (b && !b->mutt ? double(b->stats[i]) : 1.0) : 0.0);
        int i = rng.weighted(w);
        if (i < 0) break;
        ++d.stats[i];
    }
    if (b) d.traits = rollTraits(2, *b, t, rng, c.ownBreedTrait, {}, false, {});
    d.grade = Grade::Ordinary;
}

bool makePedigree(DogRecord& d, const BreedTable& t) {
    const BreedDef* b = t.find(d.breed);
    if (!b || b->mutt || d.grade != Grade::Ordinary) return false;
    d.stats = b->stats;
    d.fiveStat = b->fiveStat();
    d.traits = b->traits;
    d.grade = Grade::Pedigree;
    d.papers = true;
    return true;
}

// ---- hardening ----

static void grantFromDeed(DogRecord& d, const BreedTable& t, const DeedDef& deed, int points, Rng& rng,
                          std::vector<std::string>& out) {
    while (points > 0) {
        std::string pick;
        int pickPrice = 0;
        // the deed's own unowned trait with the highest price that fits (3 banked steps -> its signature)
        for (const auto& id : deed.traits) {
            const TraitDef* td = t.trait(id);
            if (!td || d.hasTrait(id) || td->price > points) continue;
            if (td->price > pickPrice) { pick = id; pickPrice = td->price; }
        }
        if (pick.empty()) {  // nothing left in this deed: any rollable trait that fits
            std::vector<std::string> pool;
            for (const auto& td : t.traits())
                if (td.rollable && !d.hasTrait(td.id) && td.price <= points) pool.push_back(td.id);
            if (pool.empty()) return;
            pick = pool[rng.range(0, int(pool.size()) - 1)];
            pickPrice = t.trait(pick)->price;
        }
        d.traits.push_back(pick);
        out.push_back("trait:" + pick);
        points -= pickPrice;
    }
}

std::vector<std::string> addDeed(DogRecord& d, const BreedTable& t, const std::string& deedId, int units, Rng& rng) {
    std::vector<std::string> out;
    const BreedDef* b = t.find(d.breed);
    const DeedDef* deed = t.deed(deedId);
    if (!b || !b->mutt || !deed || d.grade != Grade::Ordinary || d.bondLevel < 2 || units <= 0) return out;
    int& count = d.deeds[deedId];
    count += units;
    int& taken = d.deedSteps[deedId];
    while (taken < count / std::max(1, deed->step) && (d.hardenStats < 4 || d.hardenTraits < 3)) {
        bool wantStat = (taken % 2 == 0 && d.hardenStats < 4) || d.hardenTraits >= 3;
        if (wantStat) {
            int best = -1;
            for (int i : deed->stats) {
                int cap = (d.fiveStat < 0) ? 5 : 4;
                if (d.stats[i] < cap && (best < 0 || d.stats[i] < d.stats[best])) best = i;
            }
            if (best < 0) {  // the deed's stats are full: the lowest other stat that can still grow
                for (int i = 0; i < StatCount; ++i) {
                    int cap = (d.fiveStat < 0) ? 5 : 4;
                    if (d.stats[i] < cap && (best < 0 || d.stats[i] < d.stats[best])) best = i;
                }
            }
            if (best < 0 && d.hardenTraits < 3) {
                wantStat = false;  // nothing can grow: the step goes to a trait instead
            } else {
                if (best >= 0) {
                    if (++d.stats[best] == 5) d.fiveStat = best;
                    out.push_back(std::string("stat:") + kStatNames[best]);
                }
                ++d.hardenStats;
            }
        }
        if (!wantStat) {
            ++d.hardenTraits;
            if (++d.deedTraitBank[deedId] >= 3) {
                grantFromDeed(d, t, *deed, d.deedTraitBank[deedId], rng, out);
                d.deedTraitBank[deedId] = 0;
            } else {
                out.push_back("bank:" + deedId);
            }
        }
        ++taken;
    }
    if (d.hardenStats >= 4 && d.hardenTraits >= 3) {
        for (auto& [id, bank] : d.deedTraitBank) {
            if (bank <= 0) continue;
            if (const DeedDef* x = t.deed(id)) grantFromDeed(d, t, *x, bank, rng, out);
            bank = 0;
        }
        d.grade = Grade::Pedigree;
        out.push_back("hardened");
    }
    return out;
}

bool addSkillUse(DogRecord& d, const std::string& skill, int usesL2, int usesL3) {
    static const double factor[] = {0.6, 0.6, 0.8, 1.0, 1.2, 1.5};  // trainability 1..5 = learning speed (old D30)
    int tr = std::clamp(d.stats[Trainability], 1, 5);
    int& uses = d.skillUses[skill];
    ++uses;
    int lvl = d.skill(skill);
    if (lvl >= 3) return false;
    int need = int((lvl == 1 ? usesL2 : usesL3) / factor[tr] + 0.5);
    if (uses < need) return false;
    d.skillLevel[skill] = lvl + 1;
    uses = 0;
    return true;
}

const char* gradeKey(Grade g, bool mutt) {
    switch (g) {
        case Grade::Pedigree: return mutt ? "grade.hardened" : "grade.pedigree";
        case Grade::Ordinary: return "grade.ordinary";
        default: return "grade.ailing";
    }
}

}  // namespace fd
