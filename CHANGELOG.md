# Changelog

## 0.1.2 (22.11.2026)
- Twice the health: 800 / 960 / 1120 by bond level (was 400 / 480 / 560). In play one shot was enough to put the dog
  down wounded (his logs: 3 of 3 times it went down, none killed outright). Headshots and explosions still kill unless
  ProtectFromInstantDeath=1.
- The dog keeps up with your horse better: while you ride and it is behind, it runs 1.3 times faster
  (follow.mounted_rate in Companion.dat).
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
