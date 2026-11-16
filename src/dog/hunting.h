// Hunting help (old D12, D13, D84): what Arthur shot is fetched by size, skill and strength; too big yet = the dog
// runs to it and barks (marks it). Shots during a carry are queued. Deliveries grow skills, bonding and mutt hardening.
#pragma once
#include "sdk/natives.gen.h"

namespace fd::hunting {
void tick();
void onEvent(Hash type, int index);
bool busy();
void giveUp();  // the dog lets go (whistle by an Obedient dog, being sent home)
// bring a given carcass/body/pelt (Huntress catch, «Притащить тело», «Отнести к лошади»); false if it cannot
bool bring(Ped carcass, bool keepQuality, bool body, bool pelt, bool toHorse);
// the game's own stow of a carcass onto a horse (test hook 'stow'); the 3 s check is logged by the carry task
bool stowOnHorse(Ped carcass, Ped horse, int slot, int flags, bool deadConfig);
int freeHorseSlot(Ped horse);
}
