// A Quit to Desktop line at the end of the main menu (Story, Arcade, Challenge, MapMaker, Options). The menu is built at 0x99CF0:
// each line is an item of the menu library (0xF0EC0 makes it in a menu, +0x20 its text, +0x30 what it does when chosen,
// +0x34 and +0x38 its place). The new line is made after Options', and takes Options' action address (0x99CD0), which
// does nothing but open the Options menu: that entry checks which item chose it, and closes the game for ours.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "menu_text.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kMakeItem = 0xF0EC0, kPlaceItem = 0xF0500, kOptionsAction = 0x99CD0;
uint32_t g_quit_item = 0;
}  // namespace

// before the main menu's size is worked out: esi is the menu
void MainMenuQuit(X86Context& ctx, uint8_t*) {
    static const char* const kText[5] = {"Quit to Desktop", "Quitter vers le bureau", "Zum Desktop beenden", "Esci sul desktop",
                                         "Salir al escritorio"};
    static uint32_t cache[5] = {};
    uint32_t text = menu_text::Address(kText, cache);
    if (!text) return;
    uint32_t item = kernel::CallGuest(ctx, kMakeItem, {ctx.esi, 0x23, 0x100});
    if (!item) return;
    kernel::W32(item + 0x20, text);
    kernel::W32(item + 0x1C, 0x15);
    kernel::W32(item + 0x24, 3);
    kernel::W32(item + 0x34, kernel::CallGuest(ctx, kPlaceItem, {item}));
    kernel::W32(item + 0x38, 5);
    kernel::W32(item + 0x30, kOptionsAction);
    g_quit_item = item;
    static int logged = 0;
    if (logged++ < 2) LOGR("quit: a Quit to Desktop line added to the main menu (item %08X)", item);
}

uint32_t g_seen_item = 0;

// the menu's choose step (0xF1F1D) has the chosen item in eax, before it calls the item's action
void MenuItemChosen(X86Context& ctx, uint8_t*) { g_seen_item = ctx.eax; }

// just before the call of the action (0xF1F46): ours is Options' address, told apart by the item seen above, and the
// game is closed (Options may flash open meanwhile)
void MainMenuAction(X86Context& ctx, uint8_t*) {
    if (!g_quit_item || g_seen_item != g_quit_item || ctx.ecx != kOptionsAction) return;
    LOGR("quit: the game is closed from the main menu");
    app::Save();
    PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0);
}
