// Collars (17.11): the game has one collar drawn as its own part - the studded leather collar of some bloodhound coats
// (a_c_doghound_01 presets 3, 5, 6, 7: a metaped asset of its own). Put on with _SET_META_PED_TAG it sits on any dog
// model (checked on a husky the same day). Collars.dat lists the collars (asset hashes, a palette and tints may recolour
// one). Every dog of Arthur's wears a collar - its own (kept in its record) or the first in Collars.dat; strays and dogs
// for sale go without until they are his. The stable hand changes it for another one.
#pragma once
#include <string>
#include <vector>
#include "logic/dog.h"
#include "sdk/natives.gen.h"

namespace fd::collars {
std::vector<std::string> ids();          // in Collars.dat order
bool known(const std::string& id);
std::string worn(const DogRecord& d);    // the collar the dog wears: its own or the first one
void dress(Ped ped, const DogRecord& d);  // the coat preset, then the collar if the dog wears one
}
