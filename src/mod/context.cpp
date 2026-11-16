#include "mod/context.h"

#include <algorithm>

namespace fd {

Context& ctx() {
    static Context c;
    return c;
}

std::vector<std::string> Context::disabledBreeds() const {
    std::vector<std::string> out;
    for (const auto& b : breeds.breeds())
        if (!b.iniSwitch.empty() && !ini.getBool(b.iniSwitch, true)) out.push_back(b.id);
    return out;
}

std::vector<std::string> Context::effectiveTraits(const DogRecord& d) const {
    std::vector<std::string> out = d.traits;
    const BreedDef* b = breeds.find(d.breed);
    if (b && d.grade == Grade::Pedigree && d.bondLevel >= 3)
        for (const auto& id : b->bond3Traits)
            if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    return out;
}

bool Context::hasTrait(const DogRecord& d, const std::string& id) const {
    auto tr = effectiveTraits(d);
    return std::find(tr.begin(), tr.end(), id) != tr.end();
}

std::string Context::gradeText(const DogRecord& d) const {
    const BreedDef* b = breeds.find(d.breed);
    return t(gradeKey(d.grade, b && b->mutt), d.female);
}

std::string Context::breedText(const DogRecord& d) const { return t("breed." + d.breed, d.female); }

}  // namespace fd
