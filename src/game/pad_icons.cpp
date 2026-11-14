// The pad's buttons drawn the right way up in the pause and the game's prompts. Those take them from one texture,
// xbbuttons (its handle at 0x2BF44C), drawn by 0xAA360 (x, y, w, h, button, alpha) with 0x1B75A0 (x, y, w, h, u0, v0,
// u1, v1, colour; the texture coordinates in 16ths of a texel). Its A (button 1, x 32-63) and Y (button 2, x 96-127)
// cells, in the top row (y 0-31), hold their letters upside down, and were drawn so: those two are drawn with v0 and
// v1 swapped. The menus' prompts use the separate xbstdbutton textures, the right way up, and are not touched.
#include <cstdint>

#include "kernel/kernel.h"
#include "xdk/hle.h"

extern bool g_hd_buttons;  // frame_bars.cpp
namespace button_icons { bool SheetUpright(); }  // common/button_icons.cpp

namespace {
constexpr uint32_t kButtonsTexture = 0x2BF44C, kSetTexture = 0x1B7440, kDrawRect = 0x1B75A0;
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000AA360);
X86_FUNC(sub_000AA360) {
    uint32_t button = kernel::R32(ctx.esp + 0x14);
    // Redux's picture of the texture (hd_textures.bin), or the button icons' sheet, has the letters right way up: nothing to turn
    if ((button != 1 && button != 2) || g_hd_buttons || button_icons::SheetUpright()) {
        __imp__sub_000AA360(ctx, base);
        return;
    }
    uint32_t x = kernel::R32(ctx.esp + 4), y = kernel::R32(ctx.esp + 8), w = kernel::R32(ctx.esp + 0xC),
             h = kernel::R32(ctx.esp + 0x10), alpha = kernel::R32(ctx.esp + 0x18);
    uint32_t u0 = button == 1 ? 0x20 : 0x60, u1 = button == 1 ? 0x3F : 0x80;
    X86Context saved = ctx;
    kernel::CallGuest(ctx, kSetTexture, {kernel::R32(kButtonsTexture)});
    kernel::CallGuest(ctx, kDrawRect, {x, y, w, h, u0 << 4, 0x1Fu << 4, u1 << 4, 0u, (alpha & 0xFF) | 0x7F7F7F00u});
    ctx = saved;
    ctx.esp += 4;  // its ret
}
