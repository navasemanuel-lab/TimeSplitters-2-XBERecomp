#pragma once
#include <cstdint>

namespace input {
struct Pad;
}

// the settings panel over the game (panel.cpp): F1, or Back and Start together on a pad
namespace panel {

void Ui(double fps);  // render thread, each frame inside the ImGui frame
// window thread: F1 opens and closes it; true returns result instead of the host's handling
bool Message(unsigned msg, uintptr_t wp, intptr_t lp, intptr_t* result);
void Toggle();  // any thread
// opens the panel on a tab (0 graphics, 1 audio, 2 controls, 3 gameplay), any thread
void OpenTab(int tab);
bool Open();
// a pad poll, after the keyboard's: the game sees the pad idle, while the panel is open and after it until what
// closed it is let go
bool Holds(int port, const input::Pad& pad);

}  // namespace panel
