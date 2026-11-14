#pragma once

#include "xdk/hle.h"

// The weapon bar of Future Perfect, for TimeSplitters 2: a pill of the guns the player has, the chosen one lit, shown for
// a moment whenever the gun changes (weapon_bar.cpp)
namespace weapon_bar {
void Draw();  // render thread, each frame inside the ImGui frame
// The weapon slots: slot n (1 up) is the n-th gun in the order the game's own next-weapon goes through them, from the fist.
// RequestSlot asks for it from any thread; ApplyRequest, on the game's thread at the start of the look update, makes it the
// gun in hand.
void RequestSlot(int slot);
void ApplyRequest(X86Context& ctx);
}
