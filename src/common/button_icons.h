#pragma once
#include <cstdint>

#include <imgui.h>

// The buttons the game's prompts show (the menus' "A Select  B Back", the pause screen's) drawn as the buttons of the
// input in use, from data/button_icons.bin (tools/make_button_icons.py): the keys and mouse buttons the player has bound
// to those actions when the keyboard is in use, a PlayStation 4, PlayStation 5 or Switch pad's buttons when that is the pad,
// the Xbox pad's of the pack otherwise. A key rebound in the panel changes its picture at once. (button_icons.cpp)
namespace button_icons {

// the option's values: the buttons follow the last input used, or one set always
enum Style : uint32_t { kAuto = 0, kXbox, kPS4, kPS5, kSwitch, kKeyboard, kStyleCount };

// controls::Pad each poll: whether the pad, and whether the keyboard or mouse, did something
void NoteInput(bool pad, bool keyboard);
// the game's frame (render thread, in the ImGui frame): chooses the set and puts its pictures in place of the console's
void Tick();
// the pad buttons sheet (xbbuttons) is the pack's, its letters the right way up
bool SheetUpright();
// the set the game shows now, for the panel's text
const char* StyleName(Style s);
Style Shown();

// for the panel: the picture of a key or mouse button (a virtual key, controls.h), a pad's button of a style (a logical
// button of the game's prompts, 1 B, 2 A, 3 Y, 4 X, 5 Back, 6 Start); false until the pictures are loaded
bool KeyIcon(int vk, ImTextureID& tex, ImVec2& uv0, ImVec2& uv1);
bool PadIcon(Style s, int button, ImTextureID& tex, ImVec2& uv0, ImVec2& uv1);
// a picture drawn at the cursor as a square of this size (0 not ready)
bool DrawKey(int vk, float size);
bool DrawPad(Style s, int button, float size);

}  // namespace button_icons
