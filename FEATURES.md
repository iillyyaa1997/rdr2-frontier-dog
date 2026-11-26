# Frontier Dog — features

A dog companion for Red Dead Redemption 2 story mode: a dog that lives with the gang, hunts with Arthur, gets hurt, gets
better, and has a character of its own. Every system below can be switched off in `FrontierDog.ini`.

## The dog at your side
- Joins you after the game loads and arrives out of view, like your horse; comes back on its own when left behind, after
  fast travel, trains or cutscenes.
- Keeps up with your horse: sprints after you from 8 m behind, 1.5× faster; comes after you out of view when stuck
  60 m back (0.1.2; gallop test 22.11: mostly 2–15 m behind, up to ~50 m after a snag on a climb).
- Whistle to call it; its hearing range grows with your bond.
- Focus the dog for its prompts: Stay, Follow, Praise, Send home, «Leave it» (stops everything it does on its own) —
  Stay / Follow and Send home from any distance it can hear your whistle.
- Praise is words, from afar and from the saddle (Arthur says it, the dog answers with a happy bark); up close on foot
  the game's own Pet does the patting — never both buttons in one place (0.1.3). A praise within 30 s of a deed (a
  fetch, herbs found, a fight) is worth more. ✅ in game 25.11: Arthur speaks (GREET_DOG)
- Waits outside buildings; on story missions it stays out of the way and hides during cutscenes.
- A paw blip on the radar and the map, and the dog's own core next to Arthur's and the horse's, drawn like them: a worn
  paw that drains from the top as the dog gets hungry (red when starving, blinking red when wounded), its ring the
  dog's health over a grey track that grows with your bond the way Arthur's grows with his level. It shows, hides and
  moves exactly with the game's cores (regular or expanded radar, the HUD reveal, compass or no radar).
- Its name on the focus title, and its own inspect card on Show info: stats (each with a line on what it does), bond,
  traits and skills on two pages (a dog with many traits gets a page 3 for its skills, 0.1.2) —
  also for strays you have not tamed yet, dogs for sale and dogs at home (the game's card would only name the model's
  species).

## Bond, skills, breeds
- Bond levels 1–3, earned by praise, petting, feeding, brushing, treats, fetching and time together.
- The bond grows the dog: +1 to its weakest stat at bond 2 and +1 at bond 3 (a raised pedigree dog: 18 points, a
  standard one 14). Every breed card has one 5 (0.1.3: Australian Stamina, Bluetick Nose, Chesapeake Strength, Labrador
  Trainability).
- Eight skills that grow by doing (levels 1–3): Retrieve, Soft mouth, Trail, Hold, Point, Flush, Guard, Ambush sense — or
  pay a stable hand to train one overnight.
- Twelve breeds: Australian Shepherd, Bloodhound, Bluetick Coonhound, Border Collie, Catahoula, Chesapeake Bay Retriever,
  American Foxhound, Husky, Labrador, Poodle, Mutt and a Shiba Inu (on the husky model, red-and-white, 72 % size).
- Five stats (Strength, Nose, Stamina, Courage, Trainability) and 23 traits (Huntress, Tenacious, Fearless, Grip, Diver,
  Herbalist, Horse herder, Tracker, …).
- Grades: Pedigree (the mutt's is Street-hardened), Standard and Ailing. Every dog's roll is fixed when the mod first
  meets it.

## Life and death
- Health 800 / 960 / 1120 by bond (doubled in 0.1.2: one shot used to put it down), times the dog's own Strength:
  ×0.7 / 0.85 / 1 / 1.15 / 1.3 for Strength 1-5 (a Shiba 816 at bond 2, a Husky 1248); a quarter less when Ailing.
- A badly hurt dog goes down and waits 5, 15 or 30 minutes (by bond) for a Dog Reviver or a Horse Reviver; revived dogs
  limp until treated with horse medicine or a tonic.
- Not saved in time, it is nursed back in camp for a day and returns one grade lower instead of dying; an Ailing dog
  that falls dies for good. Explosions and headshots kill outright, as with horses (optional protection in the ini).
- Whoever was fighting a downed dog turns on Arthur instead of finishing it.
- `Invincible=1` in the ini: nothing hurts the dog, so it never goes down (asked for on Nexus 22.11; bullets checked in
  game). Damage dealt straight from another mod's script is topped up at once, but one such hit bigger than the
  dog's health still kills it.

## Hunting
- Fetches what Arthur shoots: small game in its jaws, medium game dragged, large game hauled backwards on its own
  strength — and loads the carcass onto Arthur's horse when the horse is near.
- «Шкуру на лошадь» / Pelt onto the horse: carrying a pelt, Arthur's own prompt sends the dog to stow it (up to 3);
  hidden within 6 m of the horse and whenever the game shows its own stow, so it never covers it (0.1.3). The pelts
  go into the horse's inventory as well, so the trapper and the butcher buy them. ✅ in game 25.11: a pelt Arthur
  stows himself the game's way is counted once (on the horse 1, in its inventory 1, nothing added twice)
- Shots taken during a carry wait their turn; pelt quality depends on Soft mouth; tired dogs rest.
- Huntress catches three-star game by herself; Point, Flush on command, trail and hold a wounded animal, bring bodies
  and pelts, retrieve from water (Diver from deep water).
- «Find herbs»: leads you to the herbs that really grow nearby (up to 80 m, further with a good nose); when there are
  none it whines and says so. Herbalist shows them within 50 m by itself.

## Guarding
- «Get him!» while aiming at a man or an animal; warns about predators once each; senses ambushes.
- Stands up for you on its own (0.1.2): goes for the nearest man or animal fighting you within 30 m; never lawmen,
  never the gang; a timid dog (Courage 1-2, not Fearless) only barks at them, the Shiba goes in from Courage 2. Not on missions, in camp, when told to
  stay or «Leave it». AutoAttack=0 in the ini turns it off.
- Guards your horse (Guard 1–3), Grip knocks fleeing men down, Fearless stands up to bears, growls at lawmen while
  you are wanted, Horse herder stops a bolting horse.
- «Track» bounty targets sent by Bounties Expansion and Contracts.

## Care
- Hunger and feeding from your satchel (meat, Dog Food, Dog Treat), its own dirt that shows on the coat, brushing,
  resting by the campfire. Brush (focus the dog, on foot, within 3 m) shows once the coat is a little dirty (about 10
  minutes outdoors; a swim washes it); a dirty coat (40) gives the full bond and puts Brush ahead of Praise.
- The Shiba's habits: sulks after a brush, watches you eat, shy of strangers, zoomies, sleeps curled up in camp, quiet
  alerts, wary of water until bond 3, won't carry one-star game.

## Home in camp
- Up to four dogs: one with Arthur, three at home in the gang camp, where you see them lying, sitting and sleeping.
- Take one along for free or let one go for good; at home dogs are fed and rested and their bond grows slowly.

## Collars
- None (0.1.2, his call 24.11: «так себе»). The one collar the game draws as a part of its own (the bloodhound's
  studded leather one) sat too big on some coats; the collar menu, Collars.dat and the Collar= option are gone.

## Stables
- Dogs for sale in the yard of all eight stables, by the door: three of the region's breeds, standard or pedigree,
  re-stocked every three in-game days. They live there: walk about the yard, sit, lie down, come up to Arthur. A dog
  trader stands beside them; walk up to him and his offers show under «Торговец собаками», like the game's shops. The game opens its own stable menu as soon as Arthur steps inside, so the dog
  services are offered out in the yard, next to the dogs for sale. A yard spot inside a building or under a roof
  (Blackwater's, reported on Nexus 22.11) moves out into the open.
- Dog prices: DogPricePercent in the ini (100 = as is) scales the dogs for sale and what the trader pays.
- Talk to the dog trader (0.1.2): your dog's papers on a sheet of the game's map paper (the game's drawing of the
  breed, stats, traits, skills, bond, health, his price and signature, the society's seal when it has papers) and his
  talk on the right — answers with up/down + Enter, the number keys or the pad; each dog for sale shows its own
  papers. TraderTalk=0 in the ini keeps the old prompt list below.
- The dog trader («Другое» leafs through his list, each item fires once per press) sells the dogs you see
  («Купить: Стандартная Дворняга — $15»; the game's own Pet prompt takes the key on a dog for sale, so buying is in this
  list), trains skills, cures an Ailing dog ($40, one day), makes a breed dog Pedigree ($150, seven days), issues
  papers, makes a new name tag (rename), buys your dog (60 % with papers,
  30 % without) and brings a dog from home ($5, 15 seconds).

## Delivery from camp
- Send for a dog at your own campfire in the wild, by telegram through the clerk at any post office, or through a
  passer-by ($2, on foot, only a random «Stranger», never covering the game's own prompts) — each time you pick which
  dog («Another dog»), and the prompt shows the price (0.1.3).
- At your own camp fire the order sits in the game's camp panel next to Sleep, Craft and Leave (a separate prompt took
  the camp panel away, reported on Nexus 22.11; checked in game).
- A courier really rides out of camp: his blip moves along the roads at a horse's pace (0.1.3: it waits for the road
  network to load instead of cutting across country), near you he becomes a rider with your dog running beside his
  horse who keeps to the road until the last 40 m, hands it over and rides off. His marker stays on the map and the
  minimap the whole way, also on the passer-by carrying the message. $5–15 by distance; the trip survives a reload. ✅ in game 25.11: ordered from 856 m, the rider with the dog came within ~2 min and handed it over

## Strays
- Mutts along roads, packs of two or three near towns, never more than three around; always outdoors (never in a
  building, and one that wanders in walks back out). Strays=0 in FrontierDog.ini turns them off. ⏳ not checked in game
- Tame one with food or a pat; a Street-hardened mutt keeps its distance until fed on two or three different days and
  runs off when you fire near it.
- Now and then a lone mutt begs by the door of a butcher or a saloon (all six butchers and nine saloons): when you come
  within 25 m with food it trots up and sits begging.
- A fed stray may tag along for a while, or turn up by the gang camp within a few days; a standard mutt becomes Street-hardened through seven deeds with you — hunting, fights,
  guarding, swimming, the road (every kilometre, and every night out in the wild together) and obeying.

## Items (Lenny's Mod Loader)
- Dog Reviver (stable hand), Dog Food and Dog Treat (butcher) as real items in the satchel.

## Companion mod: Honor Shield
- Killing men or animals that attacked you first costs no honor (a separate .asi, optional); the honor flash for such a
  kill is hidden (experimental).

## Published
- Nexus, mod 11716, 0.1.0 beta (20.11): page with the feature list in English and Russian, 5 screenshots, archive
  FrontierDog.asi + FrontierDog.ini + FrontierDog folder (no LML items, no core icon).

## Also
- English and Russian, picked from the game's language; other languages via text files.
- Script events (`FRONTIER_DOG_*`) for other mods.

## Planned
- Real Shiba coats (red, sesame, black and tan) as own textures — own texture dictionaries work in game now.
- A wet coat after swimming (how the game draws it, to be researched).
- On Nexus with the shop items and the paw core icon: the items need a catalog built on the player's own PC (ours is
  a copy of red_dead_offline's), the icon an LML folder checked in game. README in English and Russian.

## Not possible yet
- Collars and bandanas worth having: the game draws only one collar as a part of its own (the bloodhound's studded
  leather one, dropped 24.11); the foxhound's collar is painted into its coat, and no bandana was found on any of the
  14 dog models (57 coats looked at, 17.11).
- The game's own dirt level reads 0 on dogs, so the mod keeps its own dirt counter (that one works).
