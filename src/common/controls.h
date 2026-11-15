#pragma once
#include <string>

#include "core/input.h"

// the keyboard and mouse buttons as the console pad (controls.cpp)
namespace controls {

void Pad(int port, input::Pad& pad);  // GameInfo::pad
bool KeyboardActive();
bool Aiming();  // port 1's left trigger (aim) is held, from the keyboard or a pad
bool FiredWithin(int ms);  // port 1's right trigger (fire) was held in the last ms milliseconds
bool StatusHeld();  // the Back key is down: the status screen, where the mouse does not turn the view

// each control the keyboard and mouse give: a key or mouse button, rebound in the settings panel
struct Binding {
    const char* id;     // the settings file's key_<id>
    const char* label;  // the panel's
    int analog;         // Pad::analog index, -1 none
    uint16_t bits;      // Pad::buttons
    int stick;          // 1 forward 2 back 3 left 4 right on the left stick, 0 none
    int def, alt1, alt2;  // the default key; its default alternate; a key that works as well unless another action has it
};
constexpr int kWheelUp = 0x100, kWheelDown = 0x101;  // the mouse wheel's notches, bindable as keys
extern const Binding kBindings[];
extern const int kBindingCount;
int KeyOf(int i);            // the primary virtual key now, 0 unbound
int Key2Of(int i);           // the alternate one
// binds a key to slot (0 primary, 1 alternate) of binding i, 0 unbinds; a key does one thing, so the action that had it
// loses it: its index, or -1
int SetKey(int i, int slot, int vk);
void ResetKeys();
std::string KeyName(int vk);

}  // namespace controls
