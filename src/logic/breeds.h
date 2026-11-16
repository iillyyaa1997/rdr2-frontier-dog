// Breed and trait tables from FrontierDog\Breeds.dat (pure data, no game calls).
#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>

#include "core/kvfile.h"

namespace fd {

enum Stat { Strength = 0, Nose, Stamina, Courage, Trainability, StatCount };
using Stats = std::array<int, StatCount>;

struct TraitDef {
    std::string id;
    int price = 1;        // 1–3 points; 3 = signature
    bool rollable = true; // may appear on ordinary / street-hardened dogs
};

// A deed a mutt hardens through (D125): which stats and traits its steps grow.
struct DeedDef {
    std::string id;                  // hunt, fight, road, water, guard, command
    std::vector<int> stats;          // stat indices, first = main
    std::vector<std::string> traits; // trait ids this deed can give
    int step = 10;                   // counter units per hardening step
};

struct BreedDef {
    std::string id;
    std::vector<std::string> models;   // first = main look; the rest are alternatives (mutts)
    int preset = -1;                   // outfit preset, -1 = random
    int scale = 100;                   // percent
    std::vector<unsigned> animalTypes; // the game's animal type hashes, to recognise strays
    Stats stats{};                     // pedigree card, sums to 16
    std::vector<std::string> traits;   // pedigree trait mix (5 points)
    std::vector<std::string> bond3Traits;
    int price = 0;                     // base dollars (shop prices are derived, D117)
    bool mutt = false;                 // mutt rules: random stats, street-hardened instead of pedigree
    std::string iniSwitch;             // e.g. EnableShiba
    bool strayOnly = false;            // never in a shop when true
    KvFile habits;                     // generic habit switches (obey_delay_ms, water_shy_bond, ...)
    int fiveStat() const;              // index of the natural 5 or -1
};

class BreedTable {
public:
    bool load(const KvFile& data);
    const BreedDef* find(const std::string& id) const;
    const BreedDef* byAnimalType(unsigned hash) const;
    const BreedDef* byModel(unsigned modelHash) const;   // joaat of a model name
    const TraitDef* trait(const std::string& id) const;
    const std::vector<BreedDef>& breeds() const { return breeds_; }
    const std::vector<TraitDef>& traits() const { return traits_; }
    const std::vector<DeedDef>& deeds() const { return deeds_; }
    const DeedDef* deed(const std::string& id) const;
    const std::vector<std::string>& skills() const { return skills_; }
    const KvFile& raw() const { return raw_; }
    int traitPoints(const std::vector<std::string>& ids) const;

private:
    std::vector<BreedDef> breeds_;
    std::vector<TraitDef> traits_;
    std::vector<DeedDef> deeds_;
    std::vector<std::string> skills_;
    KvFile raw_;
};

}  // namespace fd
