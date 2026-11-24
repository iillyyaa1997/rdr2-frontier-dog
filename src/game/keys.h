// Number keys for the trader's talk (24.11): the game has no control for the digits, so the keyboard is read
// directly - only while the game's window is in front.
#pragma once

namespace fd::game {
int digitJustPressed();  // 1..9 pressed this frame (top row or numpad), 0 none
}
