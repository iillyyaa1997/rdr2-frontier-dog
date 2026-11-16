// Strays (D102, D105, D106, D119): every dog the mod meets gets its card rolled once (pedigree / ordinary / ailing,
// trait roll biased by the district); mutts the mod adds itself — loners by roads, packs of 2–3 near towns, at most 3
// around, removed when Arthur leaves; a street-hardened mutt is wary: it keeps its distance until fed on 2–3
// different days, then it lets itself be patted; a fed stray sometimes follows Arthur for a while.
#pragma once
#include "logic/dog.h"
#include "sdk/natives.gen.h"

namespace fd::strays {
void tick();
DogRecord cardFor(Ped dog);       // rolled once per ped
bool wary(Ped dog);               // street-hardened and not trusting Arthur yet
void forget(Ped dog);             // tamed or gone
void spawnNow();                  // test hook: a stray encounter now (ignores the timer, keeps the cap)
}
