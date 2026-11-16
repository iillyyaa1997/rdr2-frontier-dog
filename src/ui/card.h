// The inspect card (old D74–D77): two pages like the horse's Study card, drawn with the game's textures and fonts.
// Opens with the game's Show-info control while the dog is focused; D-pad left/right turns pages; closes when the
// focus is lost or the dog is more than 4 m away. Works for strays too (D102: the roll is visible before taming).
#pragma once
#include "logic/dog.h"
#include "sdk/natives.gen.h"

namespace fd::ui {
void cardTick();
void cardShow(const DogRecord& d, Ped ped);  // test hook / strays
}
