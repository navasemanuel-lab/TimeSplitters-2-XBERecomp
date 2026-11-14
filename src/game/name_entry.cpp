// The keyboard on the profile name screen (and the other text entries) typed on the PC keyboard. The screen's keyboard is
// a grid the pad moves over (its update is 0x1ACE70 (player), its state the block [0x48D760]: +0x18 the length typed,
// +0x34 the text, +0x38 the most it takes, +0x20 set when it is accepted, +0x24 when it is left). The characters it offers
// are the ones in its four grids from 0x34577C; those are the ones taken here, so every glyph is one the game's font has.
// Backspace deletes, Enter accepts (trailing spaces trimmed as the game's own OK does, an empty name leaves it), Escape
// leaves it. The host's keys are not the pad's meanwhile (mouse::TextEntry).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

#include "app/app.h"
#include "common/mouse.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kBlock = 0x48D760, kGrids = 0x34577C, kGridBytes = 0x90;

bool Allowed(int c) {
    if (c == ' ') return true;
    for (uint32_t i = 0; i < kGridBytes; i++)
        if (kernel::G<uint8_t>(kGrids + i) == c) return true;
    return false;
}
}  // namespace

// at 0x1ACE70, the keyboard's update
void NameEntryTyping(X86Context&, uint8_t*) {
    uint32_t k = kernel::R32(kBlock);
    if (k < 0x10000) return;
    mouse::SetTextEntry(true);
    uint32_t buf = kernel::R32(k + 0x34);
    if (buf < 0x10000) return;
    while (int c = mouse::TakeChar()) {
        uint32_t len = kernel::R32(k + 0x18), most = kernel::R32(k + 0x38);
        if (c == '\b') {
            if (len > 0 && len < 0x100) {
                kernel::G<uint8_t>(buf + len - 1) = 0;
                kernel::W32(k + 0x18, len - 1);
            }
        } else if (c == '\r') {
            while (len > 0 && len < 0x100 && kernel::G<uint8_t>(buf + len - 1) == ' ') kernel::G<uint8_t>(buf + --len) = 0;
            kernel::W32(len ? k + 0x20 : k + 0x24, 1);
        } else if (len < most && most < 0x100 && Allowed(c) && !(c == ' ' && len == 0)) {
            kernel::G<uint8_t>(buf + len) = uint8_t(c);
            kernel::G<uint8_t>(buf + len + 1) = 0;
            kernel::W32(k + 0x18, len + 1);
        }
    }
    static bool esc_was = false;
    HWND hwnd = static_cast<HWND>(app::Window());
    bool esc = hwnd && GetForegroundWindow() == hwnd && (GetAsyncKeyState(VK_ESCAPE) & 0x8000);
    if (esc && !esc_was) kernel::W32(k + 0x24, 1);
    esc_was = esc;
}
