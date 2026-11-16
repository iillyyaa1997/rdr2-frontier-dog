// Hunting help (old D12, D13, D84): what Arthur shot is fetched by size, skill and strength; too big yet = the dog
// runs to it and barks (marks it). Shots during a carry are queued. Deliveries grow skills, bonding and mutt hardening.
#pragma once
#include "sdk/natives.gen.h"

namespace fd::hunting {
void tick();
void onEvent(Hash type, int index);
bool busy();
void giveUp();  // the dog lets go (whistle by an Obedient dog, being sent home)
}
