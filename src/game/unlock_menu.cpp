// Unlock Everything: a line under View Gallery in Options > Player Options. Choosing it does what the game's own
// unlock-everything routine does (0x1AE4F0, which only runs for a profile named DUFFINS; the PC build's debug menu calls
// the same code unconditionally) for the profile the menu shows: every cutscene, cheat, arcade league and challenge bit
// set, the 66 challenge grades gold, the feature flags set. The profile is in memory; the game saves it as it saves
// any change (leaving the menu, finishing a level).
//
// The Player Options menu is built at 0x97B10: each line is an item of the menu library (0xF0EC0 makes it in a menu,
// +0x20 its text, +0x30 what it does when chosen, +0x34 and +0x38 its place), made as the others are, before the menu's
// size is worked out.
#include <atomic>
#include <cstring>

#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "menu_text.h"
#include "xdk/hle.h"

namespace dispatch {
uint32_t HostThunk(X86Func* fn);  // cpu/dispatch.cpp: a guest address that calls host code
}

namespace {
constexpr uint32_t kMakeItem = 0xF0EC0, kPlaceItem = 0xF0550;
constexpr uint32_t kProfileStats = 0x195A40;  // (slot): the progress inside the slot's profile, or a scratch one
constexpr uint32_t kScratchStats = 0x3F3B50, kProfiles = 0x493144, kProfileSize = 0x20D8, kStatsOffset = 0xD24;
constexpr uint32_t kShownSlot = 0x35C440;  // the player the menu shows

uint32_t g_item = 0, g_text = 0, g_done_text = 0, g_chosen = 0;

uint32_t Text(const char* s, size_t n) {
    uint32_t va = mem::AllocSystem(uint32_t((n + 4) & ~3u));
    if (va) std::memcpy(kernel::HostPtr(va), s, n);
    return va;
}

// what the game's routine writes (0x1AE531...), for the shown profile's progress
bool Unlock(X86Context& ctx) {
    uint32_t slot = kernel::R32(kShownSlot);
    uint32_t stats = kernel::CallGuest(ctx, kProfileStats, {slot});
    uint32_t first = kernel::R32(kProfiles);
    if (stats == kScratchStats || !first || stats < first + kStatsOffset ||
        stats >= first + 16 * kProfileSize) {
        LOGR("unlock: no profile for slot %u (stats %08X), nothing unlocked", slot, stats);
        return false;
    }
    for (uint32_t off = 0xBC8; off < 0xC20; off += 4) kernel::W32(stats + off, 0xFFFFFFFF);  // lists and arcade leagues
    for (uint32_t off = 0x4A8; off < 0x4A8 + 0x42 * 0x14; off += 0x14) kernel::W32(stats + off, 4);  // challenges
    for (uint32_t off = 0x1328; off < 0x133C; off += 4) kernel::W32(stats + off, 1);                 // feature flags
    kernel::W32(stats + 0xBC4, 0xFFFFFFFF);                                                          // cutscenes
    LOGR("unlock: profile of slot %u (stats %08X), everything unlocked", slot, stats);
    return true;
}

std::atomic<int> g_request{0};  // the panel asked (F1): done on the game's thread, at a frame
std::atomic<int> g_result{0};   // 0 nothing yet, 1 unlocked, 2 no profile to unlock

// (menu): the line's action
void UnlockChosen(X86Context& ctx, uint8_t*) {
    Unlock(ctx);
    if (g_item && g_done_text) kernel::W32(g_item + 0x20, g_done_text);
    kernel::Return(ctx, 0, 0);
}
}  // namespace

// before the Player Options menu's size is worked out: esi is the menu
void PlayerOptionsUnlock(X86Context& ctx, uint8_t*) {
    static const char* const kText[5] = {"Unlock Everything", "Tout déverrouiller", "Alles freischalten", "Sblocca tutto",
                                         "Desbloquear todo"};
    static const char* const kDone[5] = {"Everything Unlocked", "Tout est déverrouillé", "Alles freigeschaltet", "Tutto sbloccato",
                                         "Todo desbloqueado"};
    static uint32_t cache_text[5] = {}, cache_done[5] = {};
    if (!g_chosen) g_chosen = dispatch::HostThunk(UnlockChosen);
    g_text = menu_text::Address(kText, cache_text);
    g_done_text = menu_text::Address(kDone, cache_done);
    X86Context saved = ctx;
    uint32_t item = kernel::CallGuest(ctx, kMakeItem, {saved.esi, 0x1E, 0x100});
    if (item) {
        kernel::W32(item + 0x20, g_text);
        kernel::W32(item + 0x1C, 0x15);
        kernel::W32(item + 0x24, 3);
        kernel::W32(item + 0x34, 0x14);
        kernel::W32(item + 0x38, kernel::CallGuest(ctx, kPlaceItem, {item}));
        kernel::W32(item + 0x30, g_chosen);
        g_item = item;
        static int logged = 0;
        if (logged++ < 2) LOGR("unlock: an Unlock Everything line added to Player Options (item %08X)", item);
    }
    ctx = saved;
}

// The settings panel's Unlock Everything (F1 > Trucos): the same unlock, for the profile the game last showed, done at the
// next frame on the game's thread (it calls the game's code). 0 nothing asked yet, 1 unlocked, 2 no profile to unlock.
void UnlockAllRequest() {
    g_result = 0;
    g_request = 1;
}
int UnlockAllResult() { return g_result; }
void UnlockFrame(X86Context& ctx) {
    if (!g_request.exchange(0)) return;
    g_result = Unlock(ctx) ? 1 : 2;
}
