// One dog's lasting state (FrontierDog\Dogs\<n>.dat) and the rules that change it: rolls for new dogs, grade down
// instead of death, cure, pedigree at the vet, mutt hardening by deeds (plan D99–D125). Pure logic, unit-tested.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/kvfile.h"
#include "core/rng.h"
#include "logic/breeds.h"

namespace fd {

enum class Grade { Ailing = 0, Ordinary = 1, Pedigree = 2 };  // a mutt's Pedigree = «Закалённая» (street-hardened)
enum class Where { WithArthur = 0, Home = 1, Away = 2 };        // Away: training, at the vet, being nursed in camp

struct DogRecord {
    static constexpr int kSchema = 1;

    std::string uid;        // stable id (creation time + random), never reused
    std::string name;
    std::string breed;
    bool female = false;
    std::string model;      // model name
    int preset = -1;
    std::string collar;     // Collars.dat id, empty: none (17.11)
    int scale = 100;
    Grade grade = Grade::Ordinary;
    bool papers = false;    // bought in a shop or papers bought at a stable (D118)
    Stats stats{};
    int fiveStat = -1;      // the one stat allowed to be 5 (-1 none)
    std::vector<std::string> traits;

    int bondLevel = 1;
    double bondPoints = 0;
    std::map<std::string, int> skillLevel;
    std::map<std::string, int> skillUses;

    double healthFrac = 1.0;  // of max health
    double hunger = 0;        // 0 fed … 100 starving
    double dirt = 0;          // 0 clean … 100 filthy
    bool limp = false;
    long long limpUntil = 0;  // in-game minute

    std::map<std::string, int> deeds;              // hardening counters (mutts)
    std::map<std::string, int> deedSteps;          // hardening steps already taken per deed
    std::map<std::string, int> deedTraitBank;      // trait steps banked per deed, turned into a trait at 3 or at the end
    int hardenStats = 0, hardenTraits = 0;         // steps taken (≤4 / ≤3)

    Where where = Where::Home;
    long long awayUntil = 0;     // in-game minute when an Away dog is back
    std::string awayReason;      // training:<skill> / cure / pedigree / nursed
    std::string awayStable;      // stable id where it can be picked up (empty = comes home)
    long long woundedUntil = 0;  // unix seconds while wounded (0 = not wounded)
    float woundedX = 0, woundedY = 0, woundedZ = 0;

    KvFile extra;  // everything as loaded: keys we do not know survive a save

    int statSum() const;
    int skill(const std::string& id) const;
    bool hasTrait(const std::string& id) const;
    bool isStreetHardened(const BreedTable& t) const;

    void fromKv(const KvFile& kv);
    KvFile toKv() const;
};

struct RollChances {
    double pedigree = 0.10;  // D102
    double ailing = 0.15;    // D119
    double ownBreedTrait = 0.70;  // D101
};

// Trait weights by region (D106 idea 2): trait id -> multiplier on the pool weight.
using TraitBias = std::map<std::string, double>;

// New dogs
DogRecord rollStray(const BreedDef& b, const BreedTable& t, Rng& rng, const RollChances& c, const TraitBias& bias);
DogRecord makeDog(const BreedDef& b, const BreedTable& t, Grade g, Rng& rng, const RollChances& c, const TraitBias& bias);

// Grade changes. Returns false if the dog dies (ailing and not saved).
bool gradeDown(DogRecord& d, const BreedTable& t, Rng& rng);
void cure(DogRecord& d, const BreedTable& t, Rng& rng, const RollChances& c);          // ailing -> ordinary
bool makePedigree(DogRecord& d, const BreedTable& t);                                  // vet, not for mutts

// Hardening (mutts, bonding ≥ 2). Adds deed units; returns texts of steps taken (empty if none), e.g.
// "stat:courage", "trait:grip", "hardened".
std::vector<std::string> addDeed(DogRecord& d, const BreedTable& t, const std::string& deed, int units, Rng& rng);

// Stat helpers
Stats ordinaryFromBreed(const BreedDef& b, Rng& rng);
Stats randomStats(int sum, int lo, int hi, int maxFives, Rng& rng, int* fiveIndex);
std::vector<std::string> rollTraits(int points, const BreedDef& own, const BreedTable& t, Rng& rng, double ownShare,
                                    const TraitBias& bias, bool allowSignatures, const std::vector<std::string>& have);

// A skill used once more; returns true if it reached the next level (uses for L2/L3 × the trainability factor,
// old D18: ~10 and ~25 uses; ailing dogs stop at 3 like every dog, their stat cap is handled elsewhere).
bool addSkillUse(DogRecord& d, const std::string& skill, int usesL2, int usesL3);

const char* gradeKey(Grade g, bool mutt);  // Lang key: grade.pedigree / grade.hardened / grade.ordinary / grade.ailing

}  // namespace fd
