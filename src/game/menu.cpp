// The mouse in the menus of the front end's menu library (the boxes of Select Profile, the main menu, Options...):
// the line under the pointer is chosen, a click takes it (the left button is A in the menus, controls.cpp), and a
// line of choices (a slider, a setting's values) follows the pointer while the button is held on it.
//
// A menu's update (0xF1900, (screen, menu)) places it at screen+0x1C + menu+0x18, screen+0x20 + menu+0x1C (pixels of
// the 640x480 screen, ints and floats). Its items are a grid: the first
// at +0x38C, the next row by item+8, the next column by item+0xC; the chosen one is row +0x398, column +0x39C, and
// 0xF02C0 (menu, row) chooses a row as the pad does (a row flagged 8 is a label). An item lies at +0x64, +0x68 in its
// menu, +0x6C wide and +0x70 high, +0x28 its flags; one flagged 0x100 is in the menu's list, which is shown from +0x24
// to +0x28 and scrolled by the float +0x54. An item of choices has their count at +0x40, the one shown at +0x44, the
// choices at +0x48 (0x1C each, flag 1 at +8: skipped) and at +0x4C what it calls on a change (screen, menu, item, old).
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "common/mouse.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "menu_text.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kChooseRow = 0xF02C0;
constexpr uint32_t kListed = 0x100, kLabel = 0x8;

int32_t I(uint32_t va) { return int32_t(kernel::R32(va)); }
float F(uint32_t va) { return std::bit_cast<float>(kernel::R32(va)); }

struct Hit {
    uint32_t item = 0;
    int row = -1, col = -1;
    float x = 0, w = 0;  // the item's left and width, screen pixels
};

// the pointer in the 640x480 screen: the frame shown is the menus' stretched to its shape, or 4:3 in its middle
bool ScreenPointer(const mouse::Pointer& p, float& x, float& y) {
    if (!p.valid) return false;
    float u = p.x;
    if (app::g_pillarbox_menus) {
        float aspect = app::Aspect(), part = (4.0f / 3.0f) / aspect;
        if (part < 1.0f) u = (u - (1.0f - part) / 2) / part;
    }
    x = u * 640.0f;
    y = p.y * 480.0f;
    return u >= 0 && u < 1;
}

Hit Under(uint32_t screen, uint32_t menu, float px, float py) {
    float mx = float(I(screen + 0x1C)) + F(menu + 0x18), my = float(I(screen + 0x20)) + F(menu + 0x1C);
    int32_t list_top = I(menu + 0x24), list_bottom = I(menu + 0x28);
    float scroll = F(menu + 0x54);
    int r = 0;
    for (uint32_t row = kernel::R32(menu + 0x38C); row && r < 64; row = kernel::R32(row + 8), r++) {
        int c = 0;
        for (uint32_t it = row; it && c < 16; it = kernel::R32(it + 0xC), c++) {
            uint32_t flags = kernel::R32(it + 0x28);
            if (flags & kLabel) continue;
            float x = mx + float(I(it + 0x64)), y = float(I(it + 0x68)), w = float(I(it + 0x6C)), h = float(I(it + 0x70));
            if (flags & kListed) {
                y += float(list_top) - scroll;
                if (y < float(list_top) - 0.5f || y + h > float(list_bottom) + 0.5f) continue;  // scrolled out
            }
            y += my;
            if (w > 0 && h > 0 && px >= x && px < x + w && py >= y && py < y + h) return {it, r, c, x, w};
        }
    }
    return {};
}

// A volume bar (item +0x50 is its draw 0x8EE20, +0x18 1 the music's, 3 the sound's; the level 0..16 in 0x2BB864 and
// 0x2BB868) is changed by the pad's left and right (bits 0x10 and 0x20 of the pad words 0xEFBB0 gathers, read at 0x8F05C
// and 0x8F0CF): the pointer holds the bar's draw rectangle (recorded as it is drawn) and pushes those bits, a step a
// frame, until the level is under the pointer.
constexpr uint32_t kBarDraw = 0x8EE20;
std::unordered_map<uint32_t, std::array<float, 3>> g_bars;  // item -> the rectangle's left, top, right as drawn

void Slide(X86Context& ctx, uint32_t item, float px) {
    auto it = g_bars.find(item);
    if (it == g_bars.end()) return;
    float left = it->second[0] + 30.0f, right = it->second[2] - 10.0f;  // where the bar is drawn (0x8EE20)
    if (right <= left) return;
    int32_t want = std::clamp(int32_t(std::lround((px - left) / (right - left) * 16.0f)), 0, 16);
    uint32_t level = I(item + 0x18) == 1 ? 0x2BB864 : 0x2BB868;
    int32_t have = I(level);
    if (want == have) return;
    // the level is set as the pad's step does (0x8F087..0x8F09B): stored, the dirty flag, the volumes applied
    kernel::W32(level, uint32_t(want));
    kernel::W32(0x35BE30, 1);
    kernel::CallGuest(ctx, 0x1BA400, {});
    static int logged = 0;
    if (logged++ < 6) LOGR("menu: volume bar %08X from %.0f to %.0f, pointer %.0f, level %d -> %d", item, left, right, px, have, want);
}
uint32_t g_dragging_bar = 0;  // the bar the held click began on: it follows the pointer outside its rectangle too

// a held click on an item of choices: the one under the pointer across the item's right part, where they are shown
void Drag(X86Context& ctx, uint32_t screen, uint32_t menu, const Hit& h, float px) {
    int32_t count = I(h.item + 0x40);
    if (count < 2 || count > 64) return;
    float left = h.x + h.w * 0.5f, width = h.w * 0.5f;  // the label on the left half, the choices on the right
    if (px < left) return;
    int32_t want = std::clamp(int32_t((px - left) / width * float(count)), 0, count - 1);
    uint32_t choices = kernel::R32(h.item + 0x48);
    if (choices && (kernel::R32(choices + uint32_t(want) * 0x1C + 8) & 1)) return;  // a choice the pad skips too
    int32_t old = I(h.item + 0x44);
    if (want == old) return;
    kernel::W32(h.item + 0x44, uint32_t(want));
    if (uint32_t changed = kernel::R32(h.item + 0x4C)) kernel::CallGuest(ctx, changed, {screen, menu, h.item, uint32_t(old)});
}

}  // namespace

// the menu update's entry: [esp+4] the screen, [esp+8] the menu
// Diagnostic (XBE_DUMP_PROFILES=1): the 16 profiles (0x20D8 bytes each from [0x493144]) written to profiles\slot_N.bin
// beside the exe when they change, to see which bytes a profile's progress is.
static void DumpProfiles() {
    static const bool on = std::getenv("XBE_DUMP_PROFILES") != nullptr;
    static uint32_t tick = 0;
    static std::vector<uint8_t> last[16];
    if (!on || (tick++ % 120) != 0) return;
    uint32_t base = kernel::R32(0x493144);
    if (!base) return;
    for (uint32_t slot = 0; slot < 16; slot++) {
        const uint8_t* at = static_cast<const uint8_t*>(kernel::HostPtr(base + slot * 0x20D8));
        if (!at[0x20]) continue;  // no name
        std::vector<uint8_t> now(at, at + 0x20D8);
        if (now == last[slot]) continue;
        last[slot] = now;
        std::filesystem::path dir = app::ExeDir() / "profiles";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream f(dir / ("slot_" + std::to_string(slot) + ".bin"), std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(now.data()), std::streamsize(now.size()));
        LOGR("profiles: slot %u written (%s)", slot, reinterpret_cast<const char*>(at + 0x20));
    }
}

void MenuTick(X86Context& ctx, uint8_t*) {
    DumpProfiles();
    uint32_t screen = kernel::R32(ctx.esp + 4), menu = kernel::R32(ctx.esp + 8);
    if (!screen || !menu) return;
    // only the menu the pads work, as the update's own input step (0xF1B11): shown (4 in +0xC, set once its opening,
    // the float +0x3AC counting down to 0, is under 1), not closing (0x20), on a screen that is not closing (2 in
    // screen+8), takes moves (2), and the front end's input not held ([[0x4AE668]+0x1C] negative)
    uint32_t flags = kernel::R32(menu + 0xC);
    static uint32_t gate_logged = 0, gate_tick = 0;
    // the in-game pause menu stays at 0x26 on a screen of 0x6F (bit 0x40) while it is open and takes the pad
    bool pause = (kernel::R32(screen + 8) & 0x40) != 0;
    bool closed = pause ? (flags & 6) != 6 : ((flags & 0x26) != 6 || (kernel::R32(screen + 8) & 2));
    uint32_t fe = kernel::R32(0x4AE668);
    bool held = fe && int8_t(kernel::R32(fe + 0x1C) & 0xFF) < 0;
    if ((closed || held) && gate_tick++ % 60 == 0 && gate_logged++ < 40)
        LOGR("menu: pointer off (menu %08X flags %X, screen flags %X, input held %d)", menu, flags, kernel::R32(screen + 8), held ? 1 : 0);
    if (closed || held) return;
    mouse::InMenu();
    if (pause) {
        mouse::InPause();
        static uint32_t pause_ticks = 0;
        if (pause_ticks++ % 300 == 0 && pause_ticks < 1500) LOGR("menu: pause menu %08X takes the pointer (flags %X, screen flags %X)", menu, flags, kernel::R32(screen + 8));
    }
    mouse::Pointer p = mouse::MenuPointer(false);
    float px, py;
    if (!p.left) g_dragging_bar = 0;
    if (!ScreenPointer(p, px, py)) return;
    if (g_dragging_bar && p.left) {
        Slide(ctx, g_dragging_bar, px);
        return;
    }
    // the pointer chooses when it moves, clicks, or comes over another line (a menu opened under it): the keys and
    // the pad keep their own choice while it rests on the same one
    static float last_x = -1, last_y = -1;
    static uint32_t last_item = 0;
    bool moved = std::abs(px - last_x) > 0.5f || std::abs(py - last_y) > 0.5f;
    last_x = px;
    last_y = py;
    Hit h = Under(screen, menu, px, py);
    // the pause menu's lines keep no place or height (they are placed as drawn): its five lines, under the title, are
    // spaced evenly down the 480 screen (centres measured from the frame)
    if (pause && !h.item) {
        int r = 0, selectable = 0;
        uint32_t first = 0;
        for (uint32_t row = kernel::R32(menu + 0x38C); row && r < 64; row = kernel::R32(row + 8), r++)
            if (!(kernel::R32(row + 0x28) & kLabel)) {
                if (!selectable++) first = row;
            }
        if (selectable == 5 && first && I(first + 0x70) == 0) {
            int labels = r - selectable;
            int line = int(std::floor((py - 128.7f) / 68.5f + 0.5f));
            float centre = 128.7f + 68.5f * float(line);
            if (line >= 0 && line < 5 && std::abs(py - centre) < 32.0f && px > 120.0f && px < 520.0f) {
                uint32_t row = kernel::R32(menu + 0x38C);
                for (int k = 0; k < labels + line && row; k++) row = kernel::R32(row + 8);
                if (row) h = {row, labels + line, 0, 0, 640};
            }
        }
    }
    if (!h.item || (!moved && !p.left && h.item == last_item)) return;
    last_item = h.item;
    if (h.row != I(menu + 0x398) || h.col != I(menu + 0x39C)) {
        kernel::W32(menu + 0x39C, uint32_t(h.col));
        kernel::CallGuest(ctx, kChooseRow, {menu, uint32_t(h.row)});
    }
    if (p.left) {
        if (kernel::R32(h.item + 0x50) == kBarDraw) {
            g_dragging_bar = h.item;
            Slide(ctx, h.item, px);
        }
        else Drag(ctx, screen, menu, h, px);
    }
}

// the volume bar's draw (screen, item, rectangle): the rectangle kept for the pointer
X86_EXTERN_FUNC(__imp__sub_0008EE20);
X86_FUNC(sub_0008EE20) {
    uint32_t item = kernel::R32(ctx.esp + 8), rect = kernel::R32(ctx.esp + 12);
    if (item && rect) g_bars[item] = {F(rect), F(rect + 4), F(rect + 8)};
    __imp__sub_0008EE20(ctx, base);
}

// Diagnostic: each row a menu gets (the layout call 0xF0500, the row on the stack, its text at +0x20) with the builder that
// made it, to find the menus' builders (the Player Options one) from the log.
X86_EXTERN_FUNC(__imp__sub_000F0500);
X86_FUNC(sub_000F0500) {
    uint32_t row = kernel::R32(ctx.esp + 4), from = kernel::R32(ctx.esp);
    static int logged = 0;
    uint32_t text = row >= 0x10000 ? kernel::R32(row + 0x20) : 0;
    if (logged++ < 400 && text >= 0x10000) LOGR("menu: row \"%s\" from %08X", kernel::ReadCString(text, 48).c_str(), from);
    if (text >= 0x10000) {  // the controller options' rows: to find where the Auto Aim setting lives
        std::string t = kernel::ReadCString(text, 48);
        static int dumped = 0;
        if ((t.find("Aim") != std::string::npos || t.find("Vibration") != std::string::npos) && dumped++ < 8) {
            LOGR("menu: option row \"%s\" from %08X, row words:", t.c_str(), from);
            for (uint32_t i = 0; i < 0x30; i += 16)
                LOGR("menu:   +%02X %08X %08X %08X %08X", i, kernel::R32(row + i), kernel::R32(row + i + 4), kernel::R32(row + i + 8), kernel::R32(row + i + 12));
        }
    }
    __imp__sub_000F0500(ctx, base);
}

// Delete Profile on the profile screen. The game's Select Profile screen (state machine 0xA2FF0) deletes the chosen profile
// with the pad's fourth button (the "4 Delete Profile" prompt, the Y button; the confirmation "Are you sure you want to
// delete the ... Profile from memory?" follows): its update asks 0x87AA0 for the pressed buttons at 0xA320B and tests
// bit 7 of the answer. The Delete key presses that bit there, once, and only on that screen.
static std::atomic<int64_t> g_delete_at{0};  // steady-clock ms of the key's press, 0 none
static int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void RequestDeleteProfile() { g_delete_at.store(NowMs(), std::memory_order_relaxed); }

X86_EXTERN_FUNC(__imp__sub_00087AA0);
X86_FUNC(sub_00087AA0) {
    uint32_t from = kernel::R32(ctx.esp);
    __imp__sub_00087AA0(ctx, base);
    if (from == 0xA3210) {  // the profile screen's Y test
        int64_t at = g_delete_at.exchange(0, std::memory_order_relaxed);
        if (at && NowMs() - at < 400) {
            ctx.eax |= 0x80;
            LOGR("menu: Delete key pressed Delete Profile on the profile screen");
        }
    }
}

// The profile list's footer shows only "Select  Back": the game builds a "4 Delete Profile" row for it (-5) that is never
// drawn there. After the screen's builder (0xA0F50 (menu)), on the list's state (2), the footer row (-4) says Delete too, so
// the player sees the key that deletes (Y on the pad, R or Delete on the keyboard).
X86_EXTERN_FUNC(__imp__sub_000A0F50);
X86_FUNC(sub_000A0F50) {
    uint32_t menu = kernel::R32(ctx.esp + 4);
    __imp__sub_000A0F50(ctx, base);
    uint32_t fe = kernel::R32(0x4AE668);
    if (!menu || !fe) return;
    uint32_t screen = kernel::R32(menu + 4);
    uint32_t state = kernel::R32(0x35B9C0 + 4 * kernel::R32(fe));
    std::string title = screen ? kernel::ReadCString(kernel::R32(screen + 0x630), 32) : "";
    uint32_t row = kernel::CallGuest(ctx, 0xED670, {menu, 0xFFFFFFFCu});  // -4
    std::string now = row ? kernel::ReadCString(kernel::R32(row + 0x20), 48) : "";
    uint32_t del = kernel::CallGuest(ctx, 0xED670, {menu, 0xFFFFFFFBu});  // -5: the game's own "Delete Profile" row
    std::string delt = del ? kernel::ReadCString(kernel::R32(del + 0x20), 48) : "";
    static std::string last_diag;
    std::string diag = title + "|" + std::to_string(state) + "|" + now + "|" + delt;
    static int logged = 0;
    if (diag != last_diag && logged < 40) {
        last_diag = diag;
        logged++;
        LOGR("menu: profile builder: menu %08X title \"%s\" state %u footer row \"%s\" row -5 \"%s\"", menu, title.c_str(), state,
             now.c_str(), delt.c_str());
    }
    // the profile list is the builder call that has the game's own Delete row (-5): no title needed
    if (!row || !del || delt.find('\x0b') == std::string::npos || now.rfind("\x0b" "2 ", 0) != 0) return;
    static uint32_t text = 0;
    static std::string made;
    if (now.find("\x0b" "4") == std::string::npos) {
        if (made.empty() || made.find(now) != 0) {
            made = now + "  " + menu_text::GameText(1747);
            text = mem::AllocSystem(uint32_t((made.size() + 4) & ~3u));
            if (!text) return;
            std::memcpy(kernel::HostPtr(text), made.c_str(), made.size() + 1);
        }
        kernel::W32(row + 0x20, text);
        kernel::W32(row + 0x38, kernel::CallGuest(ctx, 0xF0550, {row}));
        kernel::W32(row + 0x34, kernel::CallGuest(ctx, 0xF0500, {row}));
    }
}

// "Select Controller" skipped. The screen (built by 0x1C3610, kept by 0x1C3BA0) resets the player-to-controller mapping to
// port 1 to 4 (0xDA6C0 (0, 1, 2, 3), the first thing the builder does) and waits until each player has chosen a controller:
// its update leaves for the main menu as soon as the byte of every player at 0x451448 is set (the choosing sets it,
// 0x1C3B95). The builder marks them all chosen, so the screen closes at its first update and the players keep the ports
// in order, as when the pads are plugged in.
X86_EXTERN_FUNC(__imp__sub_001C3610);
uint32_t g_select_controller_screen = 0;  // the screen being skipped (frame_bars.cpp does not draw it)
X86_FUNC(sub_001C3610) {
    g_select_controller_screen = kernel::R32(ctx.esp + 4);
    __imp__sub_001C3610(ctx, base);
    uint32_t players = kernel::R32(0x4ADD68);
    if (players > 4) players = 4;
    (void)players;  // off: Select Controller is shown as the game makes it
    static int logged = 0;
    if (logged++ < 5) LOGR("menu: Select Controller skipped for %u players", players);
}

// the skipped screen's update (it draws its items): nothing it draws reaches the screen
X86_EXTERN_FUNC(__imp__sub_001C3BA0);
X86_FUNC(sub_001C3BA0) {
    // the first updates after the builder do nothing: its leaving (0x9EA80 reads each player's controller key, +0xD0) ran in
    // the same frame as the builder and crashed (log 16:42, 0x1AEA10 from 0x9EAE3), the choosing took the game whole seconds
    static uint32_t screen = 0;
    static int waited = 0;
    uint32_t s = kernel::R32(ctx.esp + 4);
    if (s != screen) { screen = s; waited = 0; }
    bool was = gpu::g_suppress_draws.load(std::memory_order_relaxed);
    if (false && waited < 30) {
        waited++;
        ctx.esp += 4;  // its ret
    } else {
        __imp__sub_001C3BA0(ctx, base);
    }
    gpu::g_suppress_draws.store(was, std::memory_order_relaxed);
}

// a key (player +0xD0) the controller table (0x2BA840) does not have: the game takes it as no controller (0x1AEA10 returns 0 for
// a record that is empty, 0x9EA80 then gives the player a colour) instead of reading the table's end (0x0C)
X86_EXTERN_FUNC(__imp__sub_001AEA10);
X86_FUNC(sub_001AEA10) {
    uint32_t key = kernel::R32(ctx.esp + 4);
    if (kernel::CallGuest(ctx, 0x87A30, {key}) == 0x0C) {
        static int logged = 0;
        if (logged++ < 5) LOGR("menu: controller key %08X is not in the table, taken as none", key);
        ctx.eax = 0;
        ctx.esp += 4;  // its ret
        return;
    }
    __imp__sub_001AEA10(ctx, base);
}

// The map's logic (0x174780, the id in eax): finds the entry of the table 0x3F1D98 (0x28 bytes each, [0x3EFE94] of them) with that id
// and reads its type at +4. An id that no entry has gave null+4 (a crash: El INFIERNO and Castillo 2019_2, converted GameCube maps).
// Here the game takes it as a condition that is not met (eax 0) and the ids the table has are logged.
X86_EXTERN_FUNC(__imp__sub_00174780);
X86_FUNC(sub_00174780) {
    uint32_t id = ctx.eax;
    int32_t count = int32_t(kernel::R32(0x3EFE94));
    bool found = false;
    for (int32_t i = 0; i < count && i < 0x100; i++)
        if (kernel::R32(0x3F1D98 + 0x28 * i) == id) {
            found = true;
            break;
        }
    if (!found) {
        static int logged = 0;
        if (logged++ < 8) {
            std::string ids;
            for (int32_t i = 0; i < count && i < 0x100; i++) ids += " " + std::to_string(kernel::R32(0x3F1D98 + 0x28 * i));
            LOGR("logic: id %u (called from %08X) is not in the table of %d:%s; taken as not met", id, kernel::R32(ctx.esp), count,
                 ids.c_str());
        }
        ctx.eax = 0;
        ctx.esp += 4;  // its ret
        return;
    }
    __imp__sub_00174780(ctx, base);
}

// Select Controller is made (the Players menu asks for it) and closes on its first update, showing nothing (builder 0x1C3610 and
// update 0x1C3BA0 above, its frame hidden by frame_bars.cpp): the screen after it is the one the game's own flow asks for, the
// main menu with its Select Storage Location and Select Profile screens. Only logged here.
X86_EXTERN_FUNC(__imp__sub_000879F0);
X86_FUNC(sub_000879F0) {
    uint32_t desc = kernel::R32(ctx.esp + 4);
    if (desc == 0x348CF4) {
        static int logged = 0;
        if (logged++ < 6) LOGR("menu: Select Controller screen asked for (context %u)", kernel::R32(kernel::R32(0x4AE668)));
    }
    __imp__sub_000879F0(ctx, base);
}

// Select Controller is never made. The Players menu pushes it with 0x88010 (0x9AC6F) and then sets the profile flags
// (0x35F078.., 0x35B9C0..); what the screen does when every player has a controller is done here two frames later, from the
// frame loop (SelectControllerTick): the port to player mapping (0xDA6C0, the builder's first act), the main menu (0x87EF0),
// the storage and profile choice (0x9EA80), the other contexts' screens (0x1C2C40).
static int g_sc_pending = 0;
X86_EXTERN_FUNC(__imp__sub_00088010);
X86_FUNC(sub_00088010) {
    if (false && kernel::R32(ctx.esp + 4) == 0x348CF4) {  // off: it skipped the storage and profile screens
        g_sc_pending = 1;
        static int logged = 0;
        if (logged++ < 6) LOGR("menu: Select Controller not made (context %u)", kernel::R32(kernel::R32(0x4AE668)));
        ctx.esp += 4;  // its ret
        return;
    }
    __imp__sub_00088010(ctx, base);
}

static void SelectControllerTick(X86Context& ctx) {
    if (!g_sc_pending) return;
    if (++g_sc_pending < 4) return;
    g_sc_pending = 0;
    kernel::CallGuest(ctx, 0xDA6C0, {0, 1, 2, 3});
    kernel::CallGuest(ctx, 0x87EF0, {0x2BCA2C});
    kernel::CallGuest(ctx, 0x9EA80, {});
    kernel::CallGuest(ctx, 0x1C2C40, {});
    LOGR("menu: Select Controller left, the screens after it started");
}

// Diagnostic: the MapMaker's and the profiles' save reader (0x11F6B0 (entry, buffer), 0 read, -3 signature, -4 the file:
// not opened, over 0x1FE0 bytes or no signature) and what it was given, for a map that "Unable to load Map" is shown for.
X86_EXTERN_FUNC(__imp__sub_0011F6B0);
X86_FUNC(sub_0011F6B0) {
    uint32_t entry = kernel::R32(ctx.esp + 4), buffer = kernel::R32(ctx.esp + 8);
    // the entry's path field (+0x20, 16 bytes) holds "u:\\" and the 12 characters of the folder and "\\" with no end: the
    // bytes after it (+0x30, the time) make the path longer and the file is not found: the end is made for the read
    uint8_t keep = entry ? kernel::G<uint8_t>(entry + 0x30) : 0;
    if (entry) kernel::G<uint8_t>(entry + 0x30) = 0;
    std::string name = kernel::ReadCString(entry, 32), path = kernel::ReadCString(entry + 0x20, 32);
    __imp__sub_0011F6B0(ctx, base);
    if (entry) kernel::G<uint8_t>(entry + 0x30) = keep;
    static int logged = 0;
    if (logged++ < 60)
        LOGR("saves: read \"%s\" from %s: result %d, %u data bytes, starts %08X", name.c_str(), path.c_str(), int32_t(ctx.eax),
             uint32_t(kernel::R32(entry + 0x3A) & 0xFFFF), buffer ? kernel::R32(buffer) : 0);
}

// Delete Profile on the list of saved profiles (the screen 0x348C90 made by 0x1C4C10, updated by 0x1C3070: the rows "Manu",
// "Captain Ash"... that Check for Saved Profiles shows; its update only knows A and B). X on the pad (bit 0x80 of the buttons,
// as the game's own Delete test) or the Delete key asks to delete the chosen profile: the screen is held and a question
// is drawn over it (game_info.cpp, DeleteProfilePrompt), A or Enter confirms, B or Esc cancels. Confirmed, the profile's
// folder in UDATA (the one whose SaveMeta.xbx names it) is deleted and the row leaves the list.
namespace {
std::mutex g_del_mutex;
std::string g_del_name;                // the question's profile, empty when none
std::string g_del_text;                // the question as the box shows it
std::filesystem::path g_del_folder;    // its folder
uint32_t g_del_row = 0;                // its row in the list
int g_del_wait = 0;                    // frames the screen's update stays held after the question closed

// "Name=Profile Manu" of a save folder's SaveMeta.xbx (UTF-16), the text after "Name="
std::string SaveMetaName(const std::filesystem::path& dir) {
    std::ifstream f(dir / "SaveMeta.xbx", std::ios::binary);
    if (!f) return {};
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string text;
    for (size_t i = 0; i + 1 < raw.size(); i += 2) {
        unsigned char lo = uint8_t(raw[i]), hi = uint8_t(raw[i + 1]);
        if (i == 0 && lo == 0xFF && hi == 0xFE) continue;
        text += hi ? '?' : char(lo);
    }
    size_t at = text.find("Name=");
    if (at == std::string::npos) return {};
    size_t end = text.find_first_of("\r\n", at);
    return text.substr(at + 5, end == std::string::npos ? std::string::npos : end - at - 5);
}

bool SameText(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower(uint8_t(a[i])) != std::tolower(uint8_t(b[i]))) return false;
    return true;
}

// the folder of the profile the list shows under this name
std::filesystem::path ProfileFolder(const std::string& name) {
    std::string root = kernel::ResolvePath("\\Device\\Harddisk0\\Partition1\\UDATA\\4553000A", 0);
    std::error_code ec;
    if (root.empty() || !std::filesystem::is_directory(root, ec)) return {};
    for (auto& e : std::filesystem::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        std::string meta = SaveMetaName(e.path());
        if (meta.empty()) continue;
        if (SameText(meta, name) || SameText(meta, "Profile " + name)) return e.path();
    }
    return {};
}
}  // namespace

std::string DeleteProfilePrompt() {
    std::lock_guard<std::mutex> lock(g_del_mutex);
    return g_del_name.empty() ? std::string() : g_del_text;
}

extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int);

// Called each frame by the profile list's footer patch (the screen's own update is not what the list runs): X on the pad
// (bit 0x80 of the buttons, as the game's own Delete test) or the Delete key asks to delete the chosen profile, the
// question shows and the same key again, within 6 seconds with the same profile chosen, deletes it.
static std::atomic<bool> g_del_open{false};  // the question is up: the list gets no buttons
static bool g_del_reading = false;           // this file reads the buttons (the list's own read answers none)
static int g_del_hold = 0;                   // frames the list still gets none after the question closed

// the question is the game's own wording (its text table: 0x70C "Are you sure you want to delete the", 0x710 "Profile from
// memory?", 0x714 the Yes / No prompt), as TSPC asks it
static std::string DeleteQuestion(const std::string& name) {
    uint32_t tbl = kernel::R32(0x335EA8 + kernel::R32(0x3864FC) * 4);
    auto s = [&](uint32_t off) { return kernel::ReadCString(kernel::R32(tbl + off), 96); };
    return s(0x70C) + "\n" + name + " " + s(0x710) + "\n\n" + s(0x714);
}

X86_EXTERN_FUNC(__imp__sub_001C24D0);
X86_FUNC(sub_001C24D0) {
    if ((g_del_open.load() || g_del_hold > 0) && !g_del_reading) {
        ctx.eax = 0;
        ctx.esp += 4;  // its ret
        return;
    }
    __imp__sub_001C24D0(ctx, base);
}

static uint32_t DelButtons(X86Context& ctx, uint32_t menu) {
    g_del_reading = true;
    uint32_t m = kernel::CallGuest(ctx, 0x1C24D0, {kernel::R32(menu + 8)}) | kernel::CallGuest(ctx, 0x87AA0, {0});
    g_del_reading = false;
    return m;
}

// Called each frame by the profile list's footer patch (the screen's own update is not what the list runs): X on the pad
// (bit 0x80 of the buttons, as the game's own Delete test) or the Delete key asks to delete the chosen profile with a
// question in a box over the list; A answers Yes (the profile goes), B No. The list gets no buttons meanwhile.
static void ProfileDeleteTick(X86Context& ctx, uint32_t menu) {
    static uint32_t prev = 0;
    static bool del_down = false;
    if (g_del_hold > 0) g_del_hold--;
    uint32_t mask = DelButtons(ctx, menu);
    uint32_t rise = mask & ~prev;
    prev = mask;
    bool down = (GetAsyncKeyState(0x2E) & 0x8000) != 0;  // the Delete key
    bool ask = (rise & 0x80) || (down && !del_down);
    del_down = down;
    uint32_t sel = kernel::CallGuest(ctx, 0xED770, {menu});
    bool good = sel >= 0x10000 && kernel::R32(sel + 0x18) == 1001 && !(kernel::R32(sel + 0x28) & 1);
    std::string pending;
    {
        std::lock_guard<std::mutex> lock(g_del_mutex);
        pending = g_del_name;
    }
    if (pending.empty()) {
        g_del_open = false;
        if (!ask || !good) return;
        std::string name = kernel::ReadCString(kernel::R32(sel + 0x20), 48);
        std::filesystem::path folder = ProfileFolder(name);
        if (folder.empty()) {
            LOGR("profiles: no folder holds the profile \"%s\"", name.c_str());
            return;
        }
        std::lock_guard<std::mutex> lock(g_del_mutex);
        g_del_name = name;
        g_del_text = DeleteQuestion(name);
        g_del_folder = folder;
        g_del_row = sel;
        g_del_open = true;
        LOGR("profiles: delete \"%s\" asked (%s)", name.c_str(), folder.string().c_str());
        return;
    }
    bool yes = (rise & 0x840) != 0, no = (rise & 0x120) != 0;
    if (!good || sel != g_del_row) no = true, yes = false;
    if (!yes && !no) return;
    g_del_open = false;
    g_del_hold = 12;
    if (no) {
        std::lock_guard<std::mutex> lock(g_del_mutex);
        g_del_name.clear();
        g_del_text.clear();
        LOGR("profiles: delete of \"%s\" refused", pending.c_str());
        return;
    }
    std::filesystem::path folder;
    {
        std::lock_guard<std::mutex> lock(g_del_mutex);
        folder = g_del_folder;
        g_del_name.clear();
        g_del_text.clear();
    }
    std::error_code ec;
    auto n = std::filesystem::remove_all(folder, ec);
    LOGR("profiles: deleted \"%s\" (%s): %d files, %s", pending.c_str(), folder.string().c_str(), int(n),
         ec ? ec.message().c_str() : "ok");
    if (ec) return;
    // the row leaves the list: by its place in the menu's rows (+0x38C, each next at +4)
    uint32_t pos = 0, r = kernel::R32(menu + 0x38C);
    while (r >= 0x10000 && r != sel && pos < 64) r = kernel::R32(r + 4), pos++;
    uint32_t old_row = kernel::R32(menu + 0x398);
    if (r == sel) {
        kernel::CallGuest(ctx, 0xED530, {menu, pos});
        uint32_t rows = kernel::R32(menu + 0x394);
        kernel::CallGuest(ctx, kChooseRow, {menu, rows ? std::min(old_row, rows - 1) : 0});
        LOGR("profiles: row %u left the list (now %u rows)", pos, rows);
    }
}

// The profile list's footer: its prompt row ("(A) Select (B) Back") gets "(X) Delete" after it, so the key that deletes (X on
// the pad, Delete on the keyboard) is shown. Called each frame by the frame draw (frame_bars.cpp): the screen shown is
// the "Select Profile" one when its title says so; its rows are the menu's list ([screen + 0x68] the menu, its first row at
// +0x38C, each row's next at +4, text at +0x20, size at +0x34 and +0x38 from 0xF0500 and 0xF0550).
void ProfileFooterPatch(X86Context& ctx) {
    SelectControllerTick(ctx);
    uint32_t fe = kernel::R32(0x4AE668);
    if (!fe) return;
    uint32_t screen = kernel::R32(fe + 0x2F0);
    if (screen < 0x10000 || !menu_text::IsProfileTitle(kernel::ReadCString(kernel::R32(screen + 0x630), 40))) return;
    uint32_t menu = kernel::R32(screen + 0x68);
    if (menu < 0x10000) return;
    static uint32_t last_menu = 0;
    bool dump = menu != last_menu;
    last_menu = menu;
    static uint32_t text = 0;
    static std::string made;
    bool list = false;
    for (uint32_t r = kernel::R32(menu + 0x38C), k = 0; r >= 0x10000 && k < 40; r = kernel::R32(r + 4), k++)
        if (menu_text::IsProfileTitle(kernel::ReadCString(kernel::R32(r + 0x20), 40))) list = true;
    if (dump) LOGR("profiles: screen %08X bars %d and %d, profile list %d", screen, int32_t(kernel::R32(screen + 0x634)),
                   int32_t(kernel::R32(screen + 0x638)), int(list));
    if (!list) return;  // the same screen also asks for the storage, and the first profile
    ProfileDeleteTick(ctx, menu);
    int n = 0;
    for (uint32_t r = kernel::R32(menu + 0x38C); r >= 0x10000 && n < 40; r = kernel::R32(r + 4), n++) {
        std::string t = kernel::ReadCString(kernel::R32(r + 0x20), 48);
        if (dump)
            LOGR("profiles: row id %d text \"%s\" flags %X at %d,%d", int32_t(kernel::R32(r + 0x18)), t.c_str(),
                 kernel::R32(r + 0x28), int32_t(kernel::R32(r + 0x64)), int32_t(kernel::R32(r + 0x68)));
        if (t.rfind("\x0b" "2 ", 0) != 0 || t.find("\x0b" "4") != std::string::npos) continue;
        if (made.empty() || made.rfind(t, 0) != 0) {
            made = t + "\n" + menu_text::GameText(441);  // a second line, as TSPC shows it (the text draw breaks lines at 10)
            text = mem::AllocSystem(uint32_t((made.size() + 4) & ~3u));
            if (!text) return;
            std::memcpy(kernel::HostPtr(text), made.c_str(), made.size() + 1);
        }
        kernel::W32(r + 0x20, text);
        // two lines are centred on the row's place: lowered half a line so the first stays where it was
        kernel::W32(r + 0x68, uint32_t(int32_t(kernel::R32(r + 0x68)) + 12));
        kernel::W32(r + 0x38, kernel::CallGuest(ctx, 0xF0550, {r}));
        kernel::W32(r + 0x34, kernel::CallGuest(ctx, 0xF0500, {r}));
        LOGR("profiles: footer row \"%s\" now says Delete", t.c_str());
    }
}


// The MapMaker editor (the code from 0x108000 to 0x130000: its views, cursor and tools) reads what each pad button is
// bound to through 0xDA2D0 every frame. A read from there is the editor being up: the mouse then moves its cursor (as the
// left stick it is moved by) and its clicks are A and B (controls.cpp, mouse.cpp). The menus the editor opens (Editor
// Settings, saving, loading) are the game's own with the pointer.
X86_EXTERN_FUNC(__imp__sub_000DA2D0);
X86_FUNC(sub_000DA2D0) {
    uint32_t from = kernel::R32(ctx.esp);
    if (from >= 0x108000 && from < 0x130000) {
        static int logged = 0;
        if (logged++ == 0) LOGR("mapmaker: the editor's pad reads seen (from %08X), the mouse moves its cursor", from);
        mouse::InGameCursor();
    }
    __imp__sub_000DA2D0(ctx, base);
}
