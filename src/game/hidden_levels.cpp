// The hidden arcade maps: Dam (level 35) and Wild West (level 32) are in the Arcade level list but never unlocked: their
// lock descriptor (row +0x1C of the list at 0x2BD588, 0x30 bytes a map) is kind 0, which 0x1AE6D0 answers with "locked"
// whatever the profile holds. 0x1AEA70 (index in the list) says whether the map's entry is open; the two are open.
// Their arcade paks (arcade/l_35_AR.pak, arcade/l_32_AR.pak) are on the disc, and their setups take the modes of mask 4
// (the deathmatch family, Vampire and Gladiator among them).
#include <filesystem>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// The Arcade list is read from a copy with one spare entry after the 20 maps (the [[imm]] entries of config/game.toml).
namespace fs = std::filesystem;

namespace {
constexpr uint32_t kOld = 0x2BD588, kEntry = 0x30, kCount = 20, kRows = 21, kSpare = 53, kSpareText = 0x91C;
uint32_t g_list = 0;

uint32_t List() {
    if (g_list) return g_list;
    g_list = mem::AllocSystem(kRows * kEntry);
    for (uint32_t w = 0; w < kCount * kEntry / 4; w++) kernel::W32(g_list + w * 4, kernel::R32(kOld + w * 4));
    for (uint32_t w = 0; w < kEntry / 4; w++) kernel::W32(g_list + kCount * kEntry + w * 4, kernel::R32(g_list + 19 * kEntry + w * 4));
    uint32_t row = g_list + kCount * kEntry;
    kernel::W32(row, kSpareText);
    kernel::W32(row + 4, kSpare);
    kernel::W32(row + 8, 0xFFFFFFFF);
    kernel::W32(row + 0x1C, 1);
    return g_list;
}
}  // namespace

extern "C" uint32_t GameArcadeList() { return List(); }

X86_EXTERN_FUNC(__imp__sub_001AEA70);
X86_FUNC(sub_001AEA70) {
    uint32_t index = kernel::R32(ctx.esp + 4);
    if (index < kRows) {
        uint32_t number = kernel::R32(List() + index * kEntry + 4);
        if (number == 32 || number == 34 || number == 35) {
            static int logged = 0;
            if (logged++ < 2) LOGR("levels: hidden map %u opened in the arcade list", number);
            kernel::Return(ctx, 1, 0);
            return;
        }
    }
    __imp__sub_001AEA70(ctx, base);
}

// The list builder (0x943E8, one pass a map) skips a map whose lock descriptor (entry +0x1C) is 0: Dam and Wild West are
// not locked but left out of the menu. Before each pass (ebx is the entry's level number, entry +4) the descriptor of
// the two is made a profile one, which the hook above then answers as open.
void HiddenMapEntry(X86Context& ctx, uint8_t*) {
    uint32_t number = kernel::R32(ctx.ebx);
    if ((number == 32 || number == 35) && !kernel::R32(ctx.ebx + 0x18)) {
        kernel::W32(ctx.ebx + 0x18, 1);
        static int logged = 0;
        if (logged++ < 2) LOGR("levels: hidden map %u added to the arcade list", number);
    }
}

// Notre Dame (level 34, arcade pak l_34_AR, deathmatch setup) has no entry in the list. The builder stops one entry short
// of the list's end (the tileset's, level 100, which no mode shows), so that entry becomes Notre Dame's, and its line is
// built here, after the loop (0x944C2, edi the menu), as the loop builds one: the item's index is the entry's.
// One of those lines, built as the loop builds one: map number, its index in the list, its text's id
void AddMapItem(X86Context& ctx, uint32_t menu, uint32_t number, uint32_t index, uint32_t text_id) {
    constexpr uint32_t kMakeItem = 0xF0EC0, kSetup = 0xDEA80, kModeMasks = 0x2BD1F4;
    uint32_t setup = kernel::CallGuest(ctx, kSetup, {number});
    uint32_t mask = kernel::R32(kModeMasks + kernel::R32(0x35C42C) * 0x1C);
    if (!setup || !(kernel::R32(setup) & mask)) return;
    uint32_t item = kernel::CallGuest(ctx, kMakeItem, {menu, 0x5F, 0x100});
    if (!item) return;
    uint32_t text = kernel::R32(kernel::R32(0x335EA8 + kernel::R32(0x3864FC) * 4) + text_id * 4);
    kernel::W32(item + 0x20, text);
    kernel::W32(item + 0x1C, 0x15);
    kernel::W32(item + 0x24, 3);
    kernel::W32(item + 0x34, 0x14);
    kernel::W32(item + 0x38, 0x21);
    kernel::W32(item + 0x30, 0x941F0);
    kernel::W32(item + 0x50, 0x94230);
    kernel::W32(item + 0x18, index);
    static int logged = 0;
    if (logged++ < 4) LOGR("levels: map %u added to the arcade list (item %08X)", number, item);
}

void HiddenNotreDame(X86Context& ctx, uint8_t*) {
    uint32_t row = List() + 19 * kEntry;
    if (kernel::R32(row + 4) != 34) {
        kernel::W32(row, 0x697);
        kernel::W32(row + 4, 34);
        kernel::W32(row + 8, 0xFFFFFFFF);
        kernel::W32(row + 0x1C, 1);
    }
    X86Context saved = ctx;
    uint32_t menu = ctx.edi;
    AddMapItem(ctx, menu, 34, 19, 0x697);
    ctx = saved;
}

// The texture maker (0x1B7A50) turns the descriptor's kind (+0x14: 0 to 3) into a texture format held in [esp+0x10], and leaves
// it unset for any other kind, so the format is whatever was on the stack and the texture create reads outside its table.
// Notre Dame's level has such a kind; it takes the first one's format. At 0x1B7AC6, ebx the kind, edi the descriptor.
void TextureKindOther(X86Context& ctx, uint8_t*) {
    if (ctx.ebx < 4) return;
    static int logged = 0;
    if (logged++ < 4)
        LOGR("levels: texture kind %u has no format (from %08X), the first kind's taken", ctx.ebx, kernel::R32(ctx.esp + 0x2C));
    if (logged < 5)
        LOGR("levels: descriptor %08X: %08X %08X %08X %08X %08X %08X %08X %08X", ctx.edi, kernel::R32(ctx.edi), kernel::R32(ctx.edi + 4),
             kernel::R32(ctx.edi + 8), kernel::R32(ctx.edi + 12), kernel::R32(ctx.edi + 16), kernel::R32(ctx.edi + 20),
             kernel::R32(ctx.edi + 24), kernel::R32(ctx.edi + 28));
    kernel::W32(ctx.esp + 0x10, 0x0C);
}
