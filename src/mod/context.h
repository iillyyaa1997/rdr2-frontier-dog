// Everything the modules share: settings, texts, data tables, the four dogs.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/kvfile.h"
#include "core/lang.h"
#include "core/rng.h"
#include "dog/store.h"
#include "logic/breeds.h"
#include "logic/economy.h"

namespace fd {

struct Context {
    std::string asiDir, dataDir;
    KvFile ini;        // FrontierDog.ini (player settings)
    KvFile tuning;     // FrontierDog\Companion.dat (timings, ranges, bonding)
    Lang lang;
    BreedTable breeds;
    Economy econ;
    RollChances chances;
    DogStore store;
    Rng rng;

    bool feature(const char* name) const { return ini.getBool(name, true); }
    int tune(const std::string& key, int def) const { return int(tuning.getInt(key, def)); }
    double tunef(const std::string& key, double def) const { return tuning.getFloat(key, def); }
    std::string t(const std::string& key, bool female = false) const { return lang.text(key, female); }
    std::string f(const std::string& key, const std::vector<std::pair<std::string, std::string>>& args,
                  bool female = false) const {
        return lang.format(key, args, female);
    }
    std::vector<std::string> disabledBreeds() const;
    std::vector<std::string> effectiveTraits(const DogRecord& d) const;  // + bond-3 traits of a pedigree
    bool hasTrait(const DogRecord& d, const std::string& id) const;
    std::string gradeText(const DogRecord& d) const;
    // the grade right before the breed name («Стандартная Австралийская овчарка»): it agrees with the breed noun
    // (Lang breed.<id>.noun=f|m), not with the dog's sex; breeds without it (Хаски, Сиба-ину, Бордер-колли) go by sex
    std::string gradeBeforeBreed(const DogRecord& d) const;
    std::string breedText(const DogRecord& d) const;
};

Context& ctx();

}  // namespace fd
