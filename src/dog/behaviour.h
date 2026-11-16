// What the companion does besides following: attack on Arthur's aim, eating from the satchel, tracking a target
// (BountiesExpansion/Contracts send DOG_COMPANION_SET_TRACKABLE_TARGET; other mods can use our FRONTIER_DOG_* events),
// warning about predators (once per predator, no loop), waiting outside interiors, «Не мешай», story-mission rules.
#pragma once
#include "sdk/natives.gen.h"

namespace fd::behaviour {
void start();
void tick();
void onScriptEvent(int index);   // event group 1
bool autonomyAllowed();          // false on missions, in cutscenes, with «Не мешай»
bool busy();                     // a task of its own is running (attack, track, warning)
bool feedOffered();              // the feed prompt takes the praise button this frame
bool trackOffered();             // the track prompt takes the send-home button this frame
}
