# Changelog

## 0.1.2 (24.11.2026)
- Talk to the dog trader: walk up to him and press «Talk». Your dog's papers lie on the left — a sheet of the
  game's own map paper with its breed drawn from the game's satchel, its stats, traits, skills, bond and health, the
  trader's price, his signature and the society's seal when the dog has papers — and the trader talks on the right:
  train, show the dogs for sale (each with its own papers), papers, the vet, a name tag, his price for your dog,
  bring a dog from home. Up/down and Enter or the number keys, Backspace/«So long» to leave; on a pad the game's
  Select/Back. `TraderTalk=0` in the ini brings back the old prompt list.
- The Blackwater dog trader stood inside the stable, where walking up to him started the game's own stable menu
  (reported on Nexus): a yard spot under a roof now moves out into the open, as one inside a building already did.
- The inspect card no longer runs off its frame when a dog has many traits (reported on Nexus): page 2 measures
  itself, long descriptions wrap, and when traits and skills do not fit the skills go to a page 3.
- Collars are gone: the only collar the game draws sat too big on some coats (asked about on Nexus).
- Brush shows much sooner: once the coat is a little dirty (about 10 minutes outdoors) instead of after 40 minutes,
  and a dirty dog's Brush is no longer pushed out of a full prompt list. A quick brush gives a little bond, a dirty
  coat the full 5.
- `DogPricePercent=100` in FrontierDog.ini: the price of the dogs for sale in percent, for economy mods (asked for on
  Nexus); what the trader pays for your dog follows it.
- Twice the health: 800 / 960 / 1120 by bond level (was 400 / 480 / 560). In play one shot was enough to put the dog
  down wounded (his logs: 3 of 3 times it went down, none killed outright). Headshots and explosions still kill unless
  ProtectFromInstantDeath=1.
- The dog keeps up with your horse: while you ride and it is more than 8 m behind, it sprints after you at once (it
  used to trot and run only from 25 m) and 1.5 times faster; stuck or left more than 60 m behind, it comes after you
  out of view (follow.* in Companion.dat). Test rides 22.11 at the gallop: 2-15 m behind most of the way, up to ~50 m
  after a snag on a climb (was 80-120 m).
- Big pelts no longer fall off the horse: an old fix-up took every pelt that was not in the pelt stack off the
  horse, and the game always lays big pelts that way.
- "Send word to camp" is offered to passers-by outdoors only: at a bartender it took a place in his menu (and seemed
  to hide the canteen refill of RDR2 Care Overhaul).

## 0.1.1 (22.11.2026)
- At a camp Arthur set up, "Send for a dog" now sits in the game's camp panel next to Sleep, Craft and Leave. Before,
  it showed as a separate prompt that took the whole camp panel away, so you could not sleep or fast travel there
  (reported on Nexus). It shows only at your own camp fire now, not at every seat.
- `Invincible=1` in FrontierDog.ini: nothing hurts the dog, so it never goes down wounded (asked for on Nexus).
  Checked in game: bullets do no damage. Damage another mod deals straight from a script gets through the game's
  protection; the mod tops the health up at once, but one such hit bigger than the dog's health still kills it.

## 0.1.0 (in development)
- Foundation: natives generator, invoker, build with a generated ScriptHookRDR2 import library.
