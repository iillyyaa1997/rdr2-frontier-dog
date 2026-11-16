// The inspect card (old D74–D77): two pages like the horse's Study card, drawn with the game's textures and fonts.
// Replaces the game's own Show info for our dogs (companion, strays - D102, dogs for sale, dogs at home): the game's
// card names the model's species ("Siberian Husky" on the Shiba). D-pad left/right turns pages; the same key, losing
// the focus or more than 6 m close it.
#pragma once
#include <string>

#include "logic/dog.h"
#include "sdk/natives.gen.h"

namespace fd::ui {
void cardStart();  // the "Show info" prompt (again after a save load: the script restarts)
void cardTick();
void cardShow(const DogRecord& d, Ped ped);  // test hook / strays
void cardHide();
void cardPage(int page);  // test hook
#ifdef FD_TEST
void cardInfoControl(const std::string& control);  // test hook: the Show-info prompt on another control
#endif
}
