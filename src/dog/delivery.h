// Delivery (D114-D116, D144): a dog from home is brought to Arthur by a rider from the camp. While far away the courier
// is a horse blip on the map that goes along the road nodes at a horse's pace (the game does not move peds that far);
// within live range he becomes the real rider with the dog running beside his horse, rides up, hands the dog over and
// rides off. The journey survives a reload (Dogs\courier.dat). Ordered at Arthur's own campfire in the wild, by
// telegram at a post office, through a passer-by (who first rides to camp with the message), or by a long whistle hold
// (off by default).
#pragma once
namespace fd::delivery {
void start();
void tick();
bool active();
bool order(int slot, const char* how);  // true if a rider is on the way
#ifdef FD_TEST
void testCampGroup(int mode, int tab);
void testScenario();
#endif
}
