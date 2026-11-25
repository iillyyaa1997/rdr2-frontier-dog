#include "test.h"
#include "core/kvfile.h"
#include "logic/breeds.h"
#include "logic/dog.h"
#include "logic/economy.h"

static const fd::BreedTable& table() {
    static fd::BreedTable t;
    static bool loaded = false;
    if (!loaded) { fd::KvFile k; k.load("data/FrontierDog/Breeds.dat"); loaded = t.load(k); }
    return t;
}

TEST(breed_cards_sum_16_and_trait_budget_5) {
    const auto& t = table();
    CHECK_EQ(t.breeds().size(), size_t(12));
    for (const auto& b : t.breeds()) {
        int s = 0; for (int v : b.stats) s += v;
        if (s != 16) std::printf("  breed %s sums %d\n", b.id.c_str(), s);
        CHECK_EQ(s, 16);
        int p = t.traitPoints(b.traits);
        if (p != 5) std::printf("  breed %s traits %d\n", b.id.c_str(), p);
        CHECK_EQ(p, 5);
        for (auto& id : b.traits) CHECK(t.trait(id) != nullptr);
    }
    CHECK(t.byAnimalType(0x59F5325D) == t.find("mutt"));
    CHECK_EQ(t.find("husky")->fiveStat(), int(fd::Strength));
}

TEST(ordinary_and_ailing_rolls) {
    const auto& t = table();
    fd::Rng rng(42);
    fd::RollChances c;
    for (const auto& b : t.breeds()) {
        for (int k = 0; k < 200; ++k) {
            auto o = fd::makeDog(b, t, fd::Grade::Ordinary, rng, c, {});
            CHECK_EQ(o.statSum(), 12);
            for (int v : o.stats) CHECK(v >= 1 && v <= 4);
            CHECK_EQ(t.traitPoints(o.traits), 2);
            for (auto& id : o.traits) CHECK(t.trait(id)->price <= 2);
            auto a = fd::makeDog(b, t, fd::Grade::Ailing, rng, c, {});
            CHECK_EQ(a.statSum(), 8);
            for (int v : a.stats) CHECK(v >= 1 && v <= 3);
            CHECK(a.traits.empty());
        }
    }
}

TEST(pedigree_and_street_hardened) {
    const auto& t = table();
    fd::Rng rng(7);
    fd::RollChances c;
    auto p = fd::makeDog(*t.find("shiba"), t, fd::Grade::Pedigree, rng, c, {});
    CHECK_EQ(p.statSum(), 16);
    CHECK_EQ(p.fiveStat, int(fd::Courage));
    CHECK(p.hasTrait("huntress"));
    int fivesTotal = 0, signatures = 0;
    for (int k = 0; k < 300; ++k) {
        auto m = fd::makeDog(*t.find("mutt"), t, fd::Grade::Pedigree, rng, c, {});
        CHECK_EQ(m.statSum(), 16);
        int fives = 0; for (int v : m.stats) { CHECK(v >= 1 && v <= 5); if (v == 5) ++fives; }
        CHECK(fives <= 1);
        fivesTotal += fives;
        CHECK_EQ(t.traitPoints(m.traits), 5);
        for (auto& id : m.traits) if (t.trait(id)->price == 3) ++signatures;
        CHECK(m.isStreetHardened(t));
        CHECK(m.scale >= 85 && m.scale <= 110);
    }
    CHECK(fivesTotal > 0);
    CHECK(signatures > 0);
}

TEST(stray_mix_roughly_matches_chances) {
    const auto& t = table();
    fd::Rng rng(3);
    fd::RollChances c;
    int ped = 0, ail = 0, n = 5000;
    for (int k = 0; k < n; ++k) {
        auto d = fd::rollStray(*t.find("collie"), t, rng, c, {});
        CHECK(!d.papers);
        if (d.grade == fd::Grade::Pedigree) ++ped;
        if (d.grade == fd::Grade::Ailing) ++ail;
    }
    CHECK(ped > n * 0.08 && ped < n * 0.12);
    CHECK(ail > n * 0.13 && ail < n * 0.17);
}

TEST(grade_down_chain_and_death) {
    const auto& t = table();
    fd::Rng rng(11);
    fd::RollChances c;
    auto d = fd::makeDog(*t.find("husky"), t, fd::Grade::Pedigree, rng, c, {});
    d.bondLevel = 3; d.skillLevel["fetch"] = 3;
    CHECK(fd::gradeDown(d, t, rng));
    CHECK(d.grade == fd::Grade::Ordinary);
    CHECK_EQ(d.statSum(), 12);
    for (int v : d.stats) CHECK(v <= 4);
    CHECK(t.traitPoints(d.traits) <= 2);
    CHECK_EQ(d.bondLevel, 3);          // bonding and skills stay (D109)
    CHECK_EQ(d.skill("fetch"), 3);
    CHECK(fd::gradeDown(d, t, rng));
    CHECK(d.grade == fd::Grade::Ailing);
    CHECK_EQ(d.statSum(), 8);
    CHECK(d.traits.empty());
    CHECK(!fd::gradeDown(d, t, rng));  // ailing and not saved: dies for good (D108)
}

TEST(cure_and_vet_pedigree) {
    const auto& t = table();
    fd::Rng rng(5);
    fd::RollChances c;
    auto d = fd::makeDog(*t.find("labrador"), t, fd::Grade::Ailing, rng, c, {});
    fd::cure(d, t, rng, c);
    CHECK(d.grade == fd::Grade::Ordinary);
    CHECK_EQ(d.statSum(), 12);
    CHECK_EQ(t.traitPoints(d.traits), 2);
    CHECK(fd::makePedigree(d, t));
    CHECK(d.grade == fd::Grade::Pedigree);
    CHECK(d.papers);
    CHECK(d.hasTrait("soft_mouth"));
    auto m = fd::makeDog(*t.find("mutt"), t, fd::Grade::Ordinary, rng, c, {});
    CHECK(!fd::makePedigree(m, t));    // a mutt is hardened only by deeds (D122)
}

TEST(mutt_hardening_by_deeds) {
    const auto& t = table();
    fd::Rng rng(9);
    fd::RollChances c;
    auto m = fd::makeDog(*t.find("mutt"), t, fd::Grade::Ordinary, rng, c, {});
    CHECK(fd::addDeed(m, t, "fight", 100, rng).empty());  // bonding 1: nothing yet
    m.bondLevel = 2;
    int traitPts0 = t.traitPoints(m.traits);
    std::vector<std::string> all;
    for (int k = 0; k < 40 && m.grade == fd::Grade::Ordinary; ++k) {
        auto ev = fd::addDeed(m, t, "fight", 4, rng);   // one step per call
        all.insert(all.end(), ev.begin(), ev.end());
    }
    CHECK(m.grade == fd::Grade::Pedigree);
    CHECK(m.isStreetHardened(t));
    CHECK_EQ(m.statSum(), 16);
    CHECK_EQ(t.traitPoints(m.traits), traitPts0 + 3);
    CHECK(m.hasTrait("fearless") || m.hasTrait("grip") || m.hasTrait("tough"));
    CHECK(!all.empty() && all.back() == "hardened");
    int fives = 0; for (int v : m.stats) if (v == 5) ++fives;
    CHECK(fives <= 1);
}

TEST(record_roundtrip_keeps_unknown_keys) {
    const auto& t = table();
    fd::Rng rng(1);
    fd::RollChances c;
    auto d = fd::makeDog(*t.find("shiba"), t, fd::Grade::Pedigree, rng, c, {});
    d.name = "Мики"; d.female = true; d.bondPoints = 12.5; d.deeds["hunt"] = 3; d.woundedX = 1.5f;
    fd::KvFile kv = d.toKv();
    kv.set("from_a_newer_version", "x");
    fd::DogRecord r; r.fromKv(kv);
    CHECK_EQ(r.name, std::string("Мики"));
    CHECK(r.female);
    CHECK_EQ(r.stats[fd::Courage], 5);
    CHECK_EQ(r.traits.size(), d.traits.size());
    CHECK_EQ(r.deeds["hunt"], 3);
    CHECK_EQ(r.toKv().get("from_a_newer_version"), std::string("x"));
    CHECK_EQ(r.toKv().getInt("schema"), 1LL);
}

TEST(prices_and_stock) {
    const auto& t = table();
    fd::Economy e;
    CHECK_EQ(fd::shopPrice(*t.find("bloodhound"), fd::Grade::Pedigree, e), 250);
    CHECK_EQ(fd::shopPrice(*t.find("bloodhound"), fd::Grade::Ordinary, e), 80);
    CHECK_EQ(fd::shopPrice(*t.find("shiba"), fd::Grade::Pedigree, e), 300);
    CHECK_EQ(fd::shopPrice(*t.find("mutt"), fd::Grade::Pedigree, e), 15);
    fd::Rng rng(2);
    fd::RollChances c;
    auto d = fd::makeDog(*t.find("collie"), t, fd::Grade::Ordinary, rng, c, {});
    d.papers = false;
    CHECK_EQ(fd::sellPrice(d, t, e), 21);   // 70 × 30 %
    d.papers = true;
    CHECK_EQ(fd::sellPrice(d, t, e), 42);   // 70 × 60 %
    d.stats[fd::Trainability] = 5; d.skillLevel["fetch"] = 1;
    CHECK_EQ(fd::trainingPrice(d, "fetch", e), 28);
    d.skillLevel["fetch"] = 3;
    CHECK_EQ(fd::trainingPrice(d, "fetch", e), -1);
    CHECK_EQ(fd::deliveryFee(500, e), 6);
    CHECK_EQ(fd::deliveryFee(9000, e), 15);
    std::vector<std::string> br = {"labrador", "chesapeake", "bluetick", "mutt"};
    auto a = fd::stableStock("st_denis", br, 10, t, e, c, {});
    auto b = fd::stableStock("st_denis", br, 11, t, e, c, {});   // same 3-day period
    CHECK_EQ(a.size(), size_t(3));
    CHECK_EQ(a[0].dog.breed, b[0].dog.breed);
    CHECK_EQ(a[0].dog.uid, b[0].dog.uid);
    for (auto& s : a) { CHECK(s.dog.papers); CHECK(s.dog.grade != fd::Grade::Ailing); }
}

TEST(max_health_by_bond_strength_and_grade) {
    const auto& t = table();
    fd::Rng rng(5);
    fd::RollChances c;
    std::vector<double> bond = {800, 960, 1120}, str = {0.7, 0.85, 1, 1.15, 1.3};
    auto shiba = fd::makeDog(*t.find("shiba"), t, fd::Grade::Pedigree, rng, c, {});
    auto husky = fd::makeDog(*t.find("husky"), t, fd::Grade::Pedigree, rng, c, {});
    CHECK_EQ(shiba.stats[fd::Strength], 2);
    CHECK_EQ(husky.stats[fd::Strength], 5);
    shiba.bondLevel = husky.bondLevel = 2;
    CHECK_EQ(fd::maxHealth(shiba, bond, str, 0.25), 816);
    CHECK_EQ(fd::maxHealth(husky, bond, str, 0.25), 1248);
    husky.bondLevel = 3;
    CHECK_EQ(fd::maxHealth(husky, bond, str, 0.25), 1456);
    shiba.grade = fd::Grade::Ailing;
    CHECK_EQ(fd::maxHealth(shiba, bond, str, 0.25), 612);
    CHECK_EQ(fd::maxHealth(husky, bond, {}, 0.25), 1120);  // no strength row: by bond only, as before
}

TEST(bond_growth_and_pedigree_cards) {
    const auto& t = table();
    fd::Rng rng(7);
    fd::RollChances c;
    for (const char* b : {"australian", "bluetick", "chesapeake", "labrador"}) {
        auto d = fd::makeDog(*t.find(b), t, fd::Grade::Pedigree, rng, c, {});
        int fives = 0;
        for (int v : d.stats) fives += v == 5;
        CHECK_EQ(fives, 1);  // every breed has its 5 (25.11)
        CHECK_EQ(d.statSum(), 16);
    }
    auto d = fd::makeDog(*t.find("shiba"), t, fd::Grade::Pedigree, rng, c, {});
    CHECK(fd::applyBondGrowth(d).empty());  // bond 1: nothing
    d.bondLevel = 3;
    auto g = fd::applyBondGrowth(d);
    CHECK_EQ(int(g.size()), 2);
    CHECK_EQ(d.statSum(), 18);
    CHECK_EQ(d.bondStats, 2);
    CHECK(fd::applyBondGrowth(d).empty());  // given once only
    CHECK(!fd::refreshPedigreeCard(d, t));  // card + bond points: already right
    d.stats[0] = 1;
    CHECK(fd::refreshPedigreeCard(d, t));
    CHECK_EQ(d.statSum(), 18);
    CHECK(fd::gradeDown(d, t, rng));  // pedigree -> ordinary keeps the bond's points: 18 - 4
    CHECK_EQ(d.statSum(), 14);
    auto a = fd::makeDog(*t.find("mutt"), t, fd::Grade::Ailing, rng, c, {});
    a.bondLevel = 2;
    fd::applyBondGrowth(a);
    for (int v : a.stats) CHECK(v <= 3);  // ailing: below 3 only
}
