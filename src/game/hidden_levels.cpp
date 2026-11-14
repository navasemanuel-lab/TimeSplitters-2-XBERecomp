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

#include "game/chemical_setup.inc"

// Chemical Plant: the real TimeSplitters level (level 5 of TimeSplitters 2 PC's xbob.pak) under level number 53 (arcade/l_53_AR.pak,
// made by tools/mk_chemical_plant.py from the retail l_35_AR.pak, in resourcepacks/chemical_plant). Its level row is one of the
// zeroed rows of the level table (0x2D0F48, 0x20 bytes: text, number, tune, sky, story setup, arcade setup) given Dam's
// words, with the sky and the arcade setup the PC build has for the map (tools/mk_chem_setup.py), and it is the list's 21st map: the list (0x2BD588, 0x30 bytes a map, 20 maps) has no room after it, so the code
// reads a copy (the [[imm]] entries of config/game.toml).
namespace fs = std::filesystem;

namespace {
constexpr uint32_t kOld = 0x2BD588, kEntry = 0x30, kCount = 20, kRows = 21, kChemical = 53, kChemicalText = 0x91C;
constexpr uint32_t kLevels = 0x2D0F48, kLevelRow = 0x20, kDamRow = 10, kFreeRow = 23;
uint32_t g_list = 0;

fs::path ChemicalPak() { return app::ExeDir() / "resourcepacks" / "chemical_plant" / "arcade" / "l_53_AR.pak"; }
bool ChemicalAvailable() {
    static int have = -1;
    if (have < 0) {
        std::error_code ec;
        have = fs::exists(ChemicalPak(), ec) ? 1 : 0;
        if (!have) LOGW("levels: %s not found, Chemical Plant is left out of the arcade list", ChemicalPak().string().c_str());
    }
    return have == 1;
}

uint32_t List() {
    if (g_list) return g_list;
    g_list = mem::AllocSystem(kRows * kEntry);
    for (uint32_t w = 0; w < kCount * kEntry / 4; w++) kernel::W32(g_list + w * 4, kernel::R32(kOld + w * 4));
    for (uint32_t w = 0; w < kEntry / 4; w++) kernel::W32(g_list + kCount * kEntry + w * 4, kernel::R32(g_list + 19 * kEntry + w * 4));
    uint32_t row = g_list + kCount * kEntry;
    kernel::W32(row, kChemicalText);
    kernel::W32(row + 4, kChemical);
    kernel::W32(row + 8, 0xFFFFFFFF);
    kernel::W32(row + 0x1C, 1);
    if (kernel::R32(kLevels + kFreeRow * kLevelRow + 4) == 0) {  // a removed level's row
        for (uint32_t w = 0; w < kLevelRow / 4; w++)
            kernel::W32(kLevels + kFreeRow * kLevelRow + w * 4, kernel::R32(kLevels + kDamRow * kLevelRow + w * 4));
        kernel::W32(kLevels + kFreeRow * kLevelRow, kChemicalText);
        kernel::W32(kLevels + kFreeRow * kLevelRow + 4, kChemical);
        // the level is the real Chemical Plant (TimeSplitters 2 PC's level 5): its own sky (index 5, as that build's row has it)
        // and its own arcade setup (the PC executable's), written to memory here: header, pickup table, weapon lists
        constexpr uint32_t kHeaderWords = sizeof(kChemHeader) / 4, kPickWords = sizeof(kChemPickups) / 4, kListWords = sizeof(kChemLists) / 4;
        uint32_t setup = mem::AllocSystem((kHeaderWords + kPickWords + kListWords) * 4);
        uint32_t pick = setup + kHeaderWords * 4, lists = pick + kPickWords * 4;
        for (uint32_t w = 0; w < kHeaderWords; w++) kernel::W32(setup + w * 4, kChemHeader[w]);
        for (uint32_t w = 0; w < kPickWords; w++) kernel::W32(pick + w * 4, kChemPickups[w]);
        for (uint32_t w = 0; w < kListWords; w++) kernel::W32(lists + w * 4, kChemLists[w]);
        kernel::W32(setup + 5 * 4, pick);
        kernel::W32(setup + 9 * 4, lists);
        kernel::W32(kLevels + kFreeRow * kLevelRow + 8, 6);  // the tune: music/ts1_chemical_plant44fx3_7july.wav (1-based index 6 of the music table at 0x34372C, the PC build's too)
        kernel::W32(kLevels + kFreeRow * kLevelRow + 0xC, 5);
        kernel::W32(kLevels + kFreeRow * kLevelRow + 0x14, setup);
        // the music: the game plays the level's group of 13 tracks (table 0x3441C0, 13 words a group, the group the level's row index in
        // the level table; 1-based indexes into the music table at 0x34372C). The removed level's group is empty; the PC build's for
        // the map has track 6, music/ts1_chemical_plant44fx3_7july.wav, as the main one and its variants
        constexpr uint32_t kMusicGroups = 0x3441C0, kGroup[13] = {0, 6, 0, 0, 6, 6, 6, 48, 62, 65, 0, 0, 0};
        for (uint32_t k = 0; k < 13; k++) kernel::W32(kMusicGroups + (kFreeRow * 13 + k) * 4, kGroup[k]);
    }
    return g_list;
}
}  // namespace

extern "C" uint32_t GameArcadeList() { return List(); }

// the pak of Chemical Plant (the file's path as the game opens it, lower case): its own
std::string ChemicalPlantPak(const std::string& lower) {
    std::string s = lower;
    for (char& c : s) if (c == '\\') c = '/';
    if (s.size() < 18 || s.compare(s.size() - 18, 18, "arcade/l_53_ar.pak") != 0 || !ChemicalAvailable()) return {};
    return ChemicalPak().string();
}

X86_EXTERN_FUNC(__imp__sub_001AEA70);
X86_FUNC(sub_001AEA70) {
    uint32_t index = kernel::R32(ctx.esp + 4);
    if (index < kRows) {
        uint32_t number = kernel::R32(List() + index * kEntry + 4);
        if (number == 32 || number == 34 || number == 35 || number == kChemical) {
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
    if (ChemicalAvailable()) AddMapItem(ctx, menu, kChemical, 20, kChemicalText);
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
