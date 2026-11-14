// Content that is in the game's code and paks but cut from its menus:
//
// Gothic tileset (gothic_tileset): the MapMaker's Tile Set line is a gamevar (0x2D31D8: value, min, max, flags, display,
// ...) whose max is 3 and whose display list (8 bytes an entry: value, text id) holds four tilesets; the fifth,
// Gothic (text 2011, value 4), is left out. The code that uses it is complete: the tileset switches at 0xF95D0 and
// 0xF99A0 have five cases, and the tile definitions are five sets of 30 (0x48B1E0 + (tileset * 30 + tile) * 64). The
// text ids are slots of the language's table, [[0x335EA8 + 4 * language]] + 4 * id.
//
// Default Gun (default_gun): gun 45 of the gun table (0x2C3468, 0x240 bytes a gun; models ob/guns/defaultgun_*, a Desert
// Eagle) is in the weapon table (0x2C9BF0, 0x24 bytes) as weapons 40 (one) and 41 (two), and in the list of the Arcade's
// custom weapon sets (0x2CADA4, 41 weapon numbers; the menu is built by 0x8C860, a line for each weapon 0x1AEA50 says is
// available, its name text id the first word of 0x2CA1D0 + 72 * weapon). The weapon's descriptor is "never available" and its
// two fire modes are empty (no damage); its name is the text 2234, "Unarmed". It is made available, given the damage of
// the Garrett Revolver (mode 1) and the Luger (mode 2), and its two lines named Desert Eagle (the text tables are full:
// the line's text is changed after the builder has set it).
#include <cstdint>
#include <cstring>

#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kTilesetVar = 0x2D31D8;   // gamevar of the MapMaker's tileset
constexpr uint32_t kGothicText = 0x7DB;      // "Gothic": the id after Virtual, Industrial, Victorian, Alien (0x7D7..0x7DA)
constexpr uint32_t kGothicValue = 4;

void GothicTileset() {
    uint32_t disp = kernel::R32(kTilesetVar + 0x10);
    if (kernel::R32(kTilesetVar + 8) != 3 || !disp) {   // another build, or already done
        LOGW("unused: tileset gamevar is not as expected (max %u, display %08X), left alone", kernel::R32(kTilesetVar + 8), disp);
        return;
    }
    // the four entries, the fifth and the end (0, 0), in memory of our own: the old list has no room after it
    uint32_t list = mem::AllocSystem(6 * 8);
    if (!list) return;
    for (uint32_t i = 0; i < 4; i++) {
        kernel::W32(list + i * 8, kernel::R32(disp + i * 8));
        kernel::W32(list + i * 8 + 4, kernel::R32(disp + i * 8 + 4));
    }
    kernel::W32(list + 32, kGothicValue);
    kernel::W32(list + 36, kGothicText);
    kernel::W32(list + 40, 0);
    kernel::W32(list + 44, 0);
    kernel::W32(kTilesetVar + 0x10, list);
    kernel::W32(kTilesetVar + 8, kGothicValue);
    LOGR("unused: Gothic tileset added to the MapMaker (list %08X)", list);
}

constexpr uint32_t kGuns = 0x2C3468, kGunSize = 0x240, kEagleGun = 45, kGarrett = 3, kLuger = 2;
constexpr uint32_t kNames = 0x2CA1D0, kNameSize = 72, kUnarmedText = 2234;
constexpr uint32_t kLanguage = 0x3864FC, kLanguages = 0x335EA8;
constexpr uint32_t kEagleWeapon = 40, kEagleWeapon2 = 41;

// the guns' two fire modes: blocks of 0xB0 bytes at +0x64 and +0x114 (the same layout in every gun: +0x34 and +0x38 the
// shot's sounds, +0x40 and +0x44 the second pair, +0x4C, +0x50 and +0x64 the indexes of the model's own parts). The
// Default Gun's are empty; the Garrett Revolver's (mode 1) and the Luger's (mode 2) are copied but for those fields
void EagleStats() {
    uint32_t eagle = kGuns + kEagleGun * kGunSize;
    if (kernel::R32(eagle) != 978) {  // another build
        LOGW("unused: gun %u is not the Default Gun, left alone", kEagleGun);
        return;
    }
    auto copy = [&](uint32_t from, uint32_t block) {
        for (uint32_t rel = 0; rel < 0xB0; rel += 4) {
            if (rel == 0x34 || rel == 0x38 || rel == 0x40 || rel == 0x44 || rel == 0x4C || rel == 0x50 || rel == 0x64) continue;
            kernel::W32(eagle + block + rel, kernel::R32(kGuns + from * kGunSize + block + rel));
        }
    };
    copy(kGarrett, 0x64);
    copy(kLuger, 0x114);
    // the gun's own header differs from the Luger's (the one it shares its model's layout and ammo type 1 with) in +0xC, +0x14,
    // +0x20 and +0x28: the fire code reads +0x10..+0x1C (the ammo types, and what asks for ammo) and compares +0x28 with the
    // ammo (0xB3405), so the Default Gun's zeros and 0x28 are the Luger's
    for (uint32_t rel : {0xCu, 0x14u, 0x20u, 0x28u, 0x50u}) kernel::W32(eagle + rel, kernel::R32(kGuns + kLuger * kGunSize + rel));
    // each fire mode's +0x64 is the row of the damage table (0x2B7068, 0x38 bytes a row: the Luger's is 0, the Garrett's 2) the
    // shot's hit message takes its weapon from; -1 is the Default Gun's own, and a hit with -1 was given no damage at all (0x55270)
    for (uint32_t block : {0x64u, 0x114u}) kernel::W32(eagle + block + 0x64, kernel::R32(kGuns + kGarrett * kGunSize + 0x64 + 0x64));
    LOGR("unused: Default Gun given the fire modes of the Garrett Revolver and the Luger");
}

bool g_in_menu = false;   // the custom weapon set menu is being built
int g_eagle_lines = 0;
uint32_t g_name1 = 0, g_name2 = 0;

uint32_t Text(const char* s) {
    size_t n = std::strlen(s) + 1;
    uint32_t va = mem::AllocSystem(uint32_t((n + 3) & ~3u));
    if (va) std::memcpy(kernel::HostPtr(va), s, n);
    return va;
}

void DefaultGun() {
    g_name1 = Text("Desert Eagle");
    g_name2 = Text("Desert Eagle (x2)");
    EagleStats();
}

}  // namespace

// weapon number: the progress check of the weapon's lock descriptor. The Default Gun's is "never".
X86_EXTERN_FUNC(__imp__sub_001AEA50);
X86_FUNC(sub_001AEA50) {
    uint32_t weapon = kernel::R32(ctx.esp + 4);
    if (game::Opt().default_gun && (weapon == kEagleWeapon || weapon == kEagleWeapon2)) {
        kernel::Return(ctx, 1, 0);
        return;
    }
    __imp__sub_001AEA50(ctx, base);
}

// the custom weapon set menu's builder
X86_EXTERN_FUNC(__imp__sub_0008C860);
X86_FUNC(sub_0008C860) {
    g_in_menu = game::Opt().default_gun && g_name1;
    g_eagle_lines = 0;
    __imp__sub_0008C860(ctx, base);
    g_in_menu = false;
}

// (item): the width of an item's text, worked out in the builder after the line's text was set: the two lines that read
// "Unarmed" are weapons 40 and 41 (the list's order), named here
X86_EXTERN_FUNC(__imp__sub_000F0550);
X86_FUNC(sub_000F0550) {
    if (g_in_menu) {
        uint32_t item = kernel::R32(ctx.esp + 4);
        uint32_t table = kernel::R32(kLanguages + 4 * kernel::R32(kLanguage));
        uint32_t unarmed = kernel::R32(table + 4 * kUnarmedText);
        if (kernel::R32(item + 0x20) == unarmed && g_eagle_lines < 2)
            kernel::W32(item + 0x20, g_eagle_lines++ ? g_name2 : g_name1);
    }
    __imp__sub_000F0550(ctx, base);
}

// The name of a weapon is read from the names table (0x2CA1D0) in each menu that lists weapons, as the text id then the
// language's line; weapons 40 and 41 hold the id of "Unarmed". The menus that draw the name from the table, not from the item's
// text the builder set above (the Arcade custom weapon set's draw at 0x8C71B, and the reads at 0xB5F2D and 0x10B6A2), get ours:
// the hook gives the line in ecx for the weapon in `weapon` and skips the instruction that read it (a three byte `mov ecx,
// [eax+ecx*4]`).
namespace {
bool EagleName(X86Context& ctx, uint32_t weapon) {
    if (!game::Opt().default_gun || !g_name1 || (weapon != kEagleWeapon && weapon != kEagleWeapon2)) return false;
    ctx.ecx = weapon == kEagleWeapon ? g_name1 : g_name2;
    return true;
}
}  // namespace

bool EagleNameEbx(X86Context& ctx, uint8_t*) { return EagleName(ctx, ctx.ebx); }
bool EagleNameEdi(X86Context& ctx, uint8_t*) { return EagleName(ctx, ctx.edi); }

// MapMaker Story with more than one player (mapmaker_coop). The MapMaker's Play action (0x11B4E0 region) builds the game
// settings on its stack (0x90 bytes: +0x48 the map's kind, 0x64, +0x4C the mode, 4 for Story, +0x58 the flags, 0x4000000 for a
// MapMaker Story) and hands them to 0xA6FD0, which copies them to 0x4AD720; the level's start, 0xA7040, copies them to
// 0x4AD7C0 and sets the players' count, [0x4A46F0], from their first word. Nothing in the Play action asks for more than one
// player, so a Story map gets the count the settings hold. After 0xA7040 the count is raised to the controllers connected
// ([0x2CE3A8], at most four) before the level creates its players (0xA73C0 loops up to [0x4A46F0]).
namespace {
constexpr uint32_t kSettings = 0x4AD7C0, kKind = 0x4AD808, kMode = 0x4AD80C, kFlags = 0x4AD818, kPlayers = 0x4A46F0, kPads = 0x2CE3A8;
}

X86_EXTERN_FUNC(__imp__sub_000A7040);
X86_FUNC(sub_000A7040) {
    __imp__sub_000A7040(ctx, base);
    uint32_t kind = kernel::R32(kKind), mode = kernel::R32(kMode);
    if (kind != 0x64) return;
    uint32_t pads = kernel::R32(kPads), players = kernel::R32(kPlayers);
    LOGR("mapmaker: game started, mode %u flags %08X players %u pads %u settings %u %u %u %u", mode, kernel::R32(kFlags), players,
         pads, kernel::R32(kSettings), kernel::R32(kSettings + 4), kernel::R32(kSettings + 8), kernel::R32(kSettings + 12));
    if (!game::Opt().mapmaker_coop || mode != 4 || pads < 2 || pads > 4 || players >= pads) return;
    kernel::W32(kSettings, pads);
    kernel::W32(kPlayers, pads);
    LOGR("mapmaker: Story played by %u players", pads);
}

// MapMaker memory (mapmaker_memory). The map's budget (0x114FB0) adds up the cost of every object and logic node (the table at
// 0x347660, 0x98 bytes a type) and sets the "over budget" flag 0x400000 of [0x49D500] when a sum passes its cap (200, 2400, 4400
// and 3500; the usage, [0x4A27E8], is the last sum). The same 3500 is asked by 0x115410 (room for one more trigger) and by
// 0x117319 in the play check ("No more free memory"). With the option the flag is cleared after the budget, a trigger always
// has room, and the play check sees the usage under the cap.
X86_EXTERN_FUNC(__imp__sub_00114FB0);
X86_FUNC(sub_00114FB0) {
    __imp__sub_00114FB0(ctx, base);
    if (game::Opt().mapmaker_memory) kernel::W32(0x49D500, kernel::R32(0x49D500) & ~0x400000u);
}

X86_EXTERN_FUNC(__imp__sub_00115410);
X86_FUNC(sub_00115410) {
    if (game::Opt().mapmaker_memory) { kernel::Return(ctx, 1, 0); return; }
    __imp__sub_00115410(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_001171C0);
X86_FUNC(sub_001171C0) {
    uint32_t usage = kernel::R32(0x4A27E8);
    bool clamp = game::Opt().mapmaker_memory && usage > 0xDAC;
    if (clamp) kernel::W32(0x4A27E8, 0xDAC);
    __imp__sub_001171C0(ctx, base);
    if (clamp) kernel::W32(0x4A27E8, usage);
}

// once a frame (story_rockets.cpp's hook of 0x1ADF80): the guest's own memory exists by then
void UnusedContentFrame() {
    static bool done = false;
    if (done) return;
    done = true;
    if (game::Opt().gothic_tileset) GothicTileset();
    if (game::Opt().default_gun) DefaultGun();
}
