# Frontier Dog — features

A dog companion for Red Dead Redemption 2 story mode: a dog that lives with the gang, hunts with Arthur, gets hurt, gets
better, and has a character of its own. Every system below can be switched off in `FrontierDog.ini`.

## The dog at your side
- Joins you after the game loads and arrives out of view, like your horse; comes back on its own when left behind, after
  fast travel, trains or cutscenes.
- Whistle to call it; its hearing range grows with your bond.
- Focus the dog for its prompts: Stay, Follow, Praise, Send home, «Leave it» (stops everything it does on its own) —
  Stay / Follow and Send home from any distance it can hear your whistle.
- Waits outside buildings; on story missions it stays out of the way and hides during cutscenes.
- A paw blip on the radar and the map, and the dog's own core next to Arthur's and the horse's, drawn like them: a worn
  paw that drains from the top as the dog gets hungry (red when starving, blinking red when wounded), its ring the
  dog's health over a grey track that grows with your bond the way Arthur's grows with his level. It shows, hides and
  moves exactly with the game's cores (regular or expanded radar, the HUD reveal, compass or no radar).
- Its name on the focus title, and its own inspect card on Show info: stats, bond, traits and skills on two pages —
  also for strays you have not tamed yet, dogs for sale and dogs at home (the game's card would only name the model's
  species).

## Bond, skills, breeds
- Bond levels 1–3, earned by praise, petting, feeding, brushing, treats, fetching and time together.
- Eight skills that grow by doing (levels 1–3): Retrieve, Soft mouth, Trail, Hold, Point, Flush, Guard, Ambush sense — or
  pay a stable hand to train one overnight.
- Twelve breeds: Australian Shepherd, Bloodhound, Bluetick Coonhound, Border Collie, Catahoula, Chesapeake Bay Retriever,
  American Foxhound, Husky, Labrador, Poodle, Mutt and a Shiba Inu (on the husky model, red-and-white, 72 % size).
- Five stats (Strength, Nose, Stamina, Courage, Trainability) and 23 traits (Huntress, Tenacious, Fearless, Grip, Diver,
  Herbalist, Horse herder, Tracker, …).
- Grades: Pedigree (the mutt's is Street-hardened), Standard and Ailing. Every dog's roll is fixed when the mod first
  meets it.

## Life and death
- A badly hurt dog goes down and waits 5, 15 or 30 minutes (by bond) for a Dog Reviver or a Horse Reviver; revived dogs
  limp until treated with horse medicine or a tonic.
- Not saved in time, it is nursed back in camp for a day and returns one grade lower instead of dying; an Ailing dog
  that falls dies for good. Explosions and headshots kill outright, as with horses (optional protection in the ini).
- Whoever was fighting a downed dog turns on Arthur instead of finishing it.

## Hunting
- Fetches what Arthur shoots: small game in its jaws, medium game dragged, large game hauled backwards on its own
  strength — and loads the carcass onto Arthur's horse when the horse is near.
- Shots taken during a carry wait their turn; pelt quality depends on Soft mouth; tired dogs rest.
- Huntress catches three-star game by herself; Point, Flush on command, trail and hold a wounded animal, bring bodies
  and pelts, retrieve from water (Diver from deep water).
- «Find herbs»: leads you to nearby herbs; Herbalist shows them within 50 m by itself.

## Guarding
- «Get him!» while aiming at a man or an animal; warns about predators once each; senses ambushes.
- Guards your horse (Guard 1–3), Grip knocks fleeing men down, Fearless stands up to bears, growls at lawmen while
  you are wanted, Horse herder stops a bolting horse.
- «Track» bounty targets sent by Bounties Expansion and Contracts.

## Care
- Hunger and feeding from your satchel (meat, Dog Food, Dog Treat), its own dirt that shows on the coat, brushing,
  resting by the campfire.
- The Shiba's habits: sulks after a brush, watches you eat, shy of strangers, zoomies, sleeps curled up in camp, quiet
  alerts, wary of water until bond 3, won't carry one-star game.

## Home in camp
- Up to four dogs: one with Arthur, three at home in the gang camp, where you see them lying, sitting and sleeping.
- Take one along for free or let one go for good; at home dogs are fed and rested and their bond grows slowly.

## Stables
- Dogs for sale outside all eight stables: three of the region's breeds, standard or pedigree, re-stocked every three
  in-game days.
- The stable hand trains skills, cures an Ailing dog ($40, one day), makes a breed dog Pedigree ($150, seven days), issues
  papers, makes a new name collar (rename), buys your dog (60 % with papers, 30 % without) and brings a dog from home
  ($5, 15 seconds).

## Delivery from camp
- Send for a dog at your own campfire in the wild, by telegram at any post office, or through a passer-by ($2) — each
  time you pick which dog.
- A courier really rides out of camp: his blip moves along the roads at a horse's pace, near you he becomes a rider with
  your dog running beside his horse, hands it over and rides off. $5–15 by distance; the trip survives a reload.

## Strays
- Mutts along roads, packs of two or three near towns, never more than three around.
- Tame one with food or a pat; a Street-hardened mutt keeps its distance until fed on two or three different days and
  runs off when you fire near it.
- Now and then a lone mutt begs by the door of a butcher or a saloon (all six butchers and nine saloons): when you come
  by with food it trots up and sits begging.
- A fed stray may tag along for a while, or turn up by the gang camp within a few days; a standard mutt becomes Street-hardened through seven deeds with you — hunting, fights,
  guarding, swimming, the road (every kilometre, and every night out in the wild together) and obeying.

## Items (Lenny's Mod Loader)
- Dog Reviver (stable hand), Dog Food and Dog Treat (butcher) as real items in the satchel.

## Companion mod: Honor Shield
- Killing men or animals that attacked you first costs no honor (a separate .asi, optional); the honor flash for such a
  kill is hidden (experimental).

## Also
- English and Russian, picked from the game's language; other languages via text files.
- Script events (`FRONTIER_DOG_*`) for other mods.

## Planned
- Real Shiba coats (red, sesame, black and tan) as own textures — own texture dictionaries work in game now.
- A wet coat after swimming (how the game draws it, to be researched).
- Publishing on Nexus: README in English and Russian, the page text, a Vortex-ready archive.

## Not possible yet
- Collars and bandanas: the game has no collar model; coat variants with a collar drawn on are still to be looked for.
- The game's own dirt level reads 0 on dogs, so the mod keeps its own dirt counter (that one works).
