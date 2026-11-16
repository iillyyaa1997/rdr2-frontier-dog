// Delivery (D114–D116): a dog from home is brought to Arthur by a rider — the dog runs beside his horse, they appear
// out of view and come along the roads all the way, he hands the dog over and rides off. Ordered at Arthur's own
// campfire in the wild, through a passer-by (who rides/walks off with the message), at a stable ("Привести", stables
// module), or by a long whistle hold (off by default). Telegraph offices: not yet (coordinates).
#pragma once
namespace fd::delivery {
void start();
void tick();
bool active();
bool order(int slot, const char* how);  // true if a rider is on the way
}
