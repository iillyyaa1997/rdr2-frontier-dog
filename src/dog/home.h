// Home in the gang camp (D98): the dogs at home are seen there while Arthur is in camp (lying, sleeping, sitting near
// one spot); one can be taken along for free, or let go for good. The camp is found by the gang members around Arthur
// (no fixed coordinates — it moves with the story); its last place is kept for deliveries. At home dogs are fed, rest
// and their bonding grows slowly (D139). Hidden while a camp scene plays.
#pragma once
#include "logic/dog.h"
#include "sdk/natives.gen.h"

namespace fd::home {
void start();
void tick();
bool inCamp();
bool campKnown(Vector3& at);
bool homeCard(Ped ped, DogRecord& out);  // a dog at home in camp: its record (the inspect card)
}
