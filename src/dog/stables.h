// Stables (old D17, D19, D32; new D112, D117, D118, D120, D124): dogs for sale stand outside each of the 8 stables
// (3 of its 4 regional breeds, re-picked every 3 in-game days, ordinary or pedigree), and the stable hand's menu
// serves the dog with Arthur: training, the vet (cure / pedigree), papers, a new name collar, selling, and bringing a
// dog from home for $5.
#pragma once
#include <string>

#include "logic/dog.h"
#include "sdk/natives.gen.h"

namespace fd::stables {
void start();
void tick();
bool saleCard(Ped ped, DogRecord& out);  // a dog for sale: its record (the inspect card)
bool isTrader(Ped p);                    // the dog trader in a stable yard (never a target for «Взять!»)
Ped trader();                            // the dog trader of the stable Arthur is at (0 = none)
bool talking();                          // the talk with the trader is open (24.11)
#ifdef FD_TEST
void testTalk(const std::string& what);  // open / up / down / accept / back / <digit> / close
#endif
}
