// Care (old D14, phase 4): hunger, the mod's own dirt counter (the game reads 0 for dogs) with an attempt to show it on
// the coat (D134, unverified), brushing, limping after a big hit or a revive and healing it with horse medicine or a
// tonic, resting by Arthur when he sits (the Shiba sleeps curled), the Shiba's sulk after a brush.
#pragma once
namespace fd::care {
void start();
void tick();
bool offersButton1();  // heal takes the praise/feed button
}
