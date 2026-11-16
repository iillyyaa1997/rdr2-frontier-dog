// The dog's core next to Arthur's and the horse's (old D24, D82): shown and hidden only together with the game's cores
// (slot states 39 Arthur, 42 horse), eased like Care Overhaul's adaptive needs cores (show 100 ms, hide after 2 s over 250 ms); it sits on the arc after the game's last core (large radar while the HUD
// is revealed, normal radar otherwise) or in the bottom-left row when there is no radar.
#pragma once
namespace fd::ui {
void coreTick();
}
