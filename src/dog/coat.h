// A dog's coat: the model's outfit preset kept in its record. (Collars were dropped on 24.11, his call: «ошейники в
// принципе можно убрать полностью, так как они так себе» — the one collar the game draws, the bloodhound's studded
// leather one, sat too big on some coats; Nexus 22.11 asked for a way to hide it.)
#pragma once
#include "logic/dog.h"
#include "sdk/natives.gen.h"

namespace fd::coat {
void dress(Ped ped, const DogRecord& d);  // the coat preset the dog was rolled with
}
