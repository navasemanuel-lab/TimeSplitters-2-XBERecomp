// The keyboard and mouse as port 1's pad: GameInfo::pad. WASD is the left stick, the arrows the D-pad, the mouse
// buttons the triggers; the mouse's movement turns the view (mouse.cpp, game/aim.cpp).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "app/app.h"
#include "common/button_icons.h"
#include "common/controls.h"
#include "common/debug.h"
#include "common/mouse.h"
#include "common/options.h"
#include "common/panel.h"
#include "common/weapon_bar.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

bool FreeCamPad(input::Pad& pad, bool foreground);  // src/game/free_cam.cpp

namespace controls {

namespace {

constexpr int16_t kStickMax = 32767;
constexpr uint8_t kAnalogOn = 0xFF;
// Pad::analog
enum { kA, kB, kX, kY, kBlack, kWhite, kLeftTrigger, kRightTrigger };
// Pad::buttons
enum : uint16_t {
    kUp = 1 << 0, kDown = 1 << 1, kLeft = 1 << 2, kRight = 1 << 3,
    kStart = 1 << 4, kBack = 1 << 5, kLeftThumb = 1 << 6, kRightThumb = 1 << 7,
};

std::atomic<bool> g_keyboard{false};
std::atomic<bool> g_status{false};
int64_t NowMsSteady() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::atomic<int64_t> g_fire_ms{-100000};  // the last time the fire trigger was held
std::atomic<bool> g_aiming{false};  // the objectives screen is up: Back opens it and closes it, so do B and Start

// a key as the panel binds it: the left and right Ctrl, Shift and Alt as the one key
int Generic(int vk) {
    switch (vk) {
        case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
        case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
        case VK_LMENU: case VK_RMENU: return VK_MENU;
    }
    return vk;
}

// the keys of binding i: its primary and alternate, and the implicit extra, but that one is dropped when another binding
// has it as its own key (Space bound to reload no longer presses A as well)
int KeysOf(int i, int (&out)[3]) {
    int n = 0;
    if (int vk = KeyOf(i)) out[n++] = vk;
    if (int vk = Key2Of(i)) out[n++] = vk;
    if (int alt = kBindings[i].alt2) {
        bool taken = false;
        for (int j = 0; j < kBindingCount && !taken; j++)
            taken = j != i && (Generic(KeyOf(j)) == Generic(alt) || Generic(Key2Of(j)) == Generic(alt));
        if (!taken) out[n++] = alt;
    }
    return n;
}

int Index(const char* id) {
    for (int i = 0; i < kBindingCount; i++)
        if (std::string(kBindings[i].id) == id) return i;
    return -1;
}

// a binding held: its keys, its mouse buttons too when mouse is set
bool BindingDown(const char* id, bool mouse = false) {
    int i = Index(id), keys[3];
    if (i < 0) return false;
    for (int k = 0, n = KeysOf(i, keys); k < n; k++) {
        int vk = keys[k];
        bool button = vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
        if (button && (!mouse || mouse::ButtonsBlocked())) continue;
        if (GetAsyncKeyState(vk) < 0) return true;
    }
    return false;
}

void TrackStatus() {
    static bool back = false, b = false, start = false;
    bool nb = BindingDown("back"), nbb = BindingDown("btn_b"), ns = BindingDown("start");
    if (nb && !back) g_status = !g_status;
    if ((nbb && !b) || (ns && !start)) g_status = false;
    if (!mouse::InPlay()) g_status = false;
    back = nb; b = nbb; start = ns;
}

int g_wheel_now = 0;  // this poll's wheel notch: 1 up, -1 down
bool Down(int vk) {
    if (vk == kWheelUp) return g_wheel_now > 0;
    if (vk == kWheelDown) return g_wheel_now < 0;
    return GetAsyncKeyState(vk) < 0;
}
// a binding of the player's has the wheel: the wheel's own changing of weapon and zooming is left to it
bool WheelClaimed() {
    for (int i = 0; i < kBindingCount; i++)
        for (int vk : {KeyOf(i), Key2Of(i)})
            if (vk == kWheelUp || vk == kWheelDown) return true;
    return false;
}

// a scripted run (XBE_INPUT_SCRIPT) takes no keyboard or mouse: the PC stays usable while it plays
bool Foreground() {
    static const bool scripted = std::getenv("XBE_INPUT_SCRIPT") != nullptr;
    HWND hwnd = static_cast<HWND>(app::Window());
    return !scripted && hwnd && GetForegroundWindow() == hwnd;
}

// The weapon slot keys: a press asks for the n-th gun the player has (weapon_bar.cpp), made the one in hand on the game's thread
void WeaponSlots(bool menu) {
    static bool held[10] = {};
    int first = Index("weapon_1");
    if (first < 0) return;
    for (int n = 0; n < 10; n++) {
        bool down = false;
        int keys[3];
        for (int k = 0, c = KeysOf(first + n, keys); k < c; k++) down = down || Down(keys[k]);
        if (down && !held[n] && !menu) weapon_bar::RequestSlot(n + 1);
        held[n] = down;
    }
}

// The game's own control layout. Each of its layouts (Default, Digital, Classic, Classic B, Radical, Radical B, or the
// player's own) puts the actions on other pad buttons, so a key is not a fixed pad button: it presses the one the layout in
// use has its action on. The first player's controls (the object at [0x497100], a player's 0xD3C bytes) hold the layout
// in use at +0x18, 42 words (the game's 0x147E00 fills them for a layout, 0x140770 reads them):
//   words 0-17, the buttons of the actions below, as the game numbers its buttons (the bits 0x1 left trigger, 0x2 right
//   trigger, 0x4 White, 0x8 Black, 0x10 Y, 0x20 B, 0x40 A, 0x80 X, 0x100 Back, 0x200 left and 0x400 right thumb, 0x800
//   Start, 0x1000 up, 0x2000 right, 0x4000 down, 0x8000 left: the table at 0x348BD0 and 0x348C10 turns the pad's into
//   them), 0 for none
//   words 30-35 the stick axis of Run, Side step, Look, Turn and two more (1 left x, 2 left y, 3 right x, 4 right y; 0 the
//   buttons of the words 6-13 instead), words 36-41 the same while aiming ([+0x884] not 0)
enum Act {
    kFire, kAltFire, kActivate, kReload, kAim, kCrouch, kRunForward, kRunBack, kTurnLeft, kTurnRight, kSideLeft, kSideRight,
    kLookUp, kLookDown, kZoomIn, kZoomOut, kPrevWeapon, kNextWeapon, kActCount
};
constexpr uint32_t kControlState = 0x497100, kConfigAt = 0x18, kConfigWords = 42, kAimingFlag = 0x884;
constexpr uint32_t kAxisWords = 30, kAimAxisWords = 36;

struct Layout {
    uint32_t button[kActCount];
    uint32_t axis[2][2];  // [aiming][run, side step]
    bool aiming;
};

// the game's button bits by Pad::analog index, and by Pad::buttons bit
constexpr uint32_t kAnalogGame[8] = {0x40, 0x20, 0x80, 0x10, 0x08, 0x04, 0x01, 0x02};
struct DigitalGame {
    uint16_t pad;
    uint32_t game;
};
constexpr DigitalGame kDigitalGame[8] = {{kUp, 0x1000},  {kDown, 0x4000},      {kLeft, 0x8000},       {kRight, 0x2000},
                                         {kStart, 0x800}, {kBack, 0x100},      {kLeftThumb, 0x200},   {kRightThumb, 0x400}};

bool KnownGameBit(uint32_t bit) {
    if (!bit) return true;
    for (uint32_t b : kAnalogGame)
        if (b == bit) return true;
    for (const DigitalGame& d : kDigitalGame)
        if (d.game == bit) return true;
    return false;
}

void PressGameBit(input::Pad& p, uint32_t bit) {
    if (!bit) return;
    for (int i = 0; i < 8; i++)
        if (kAnalogGame[i] == bit) p.analog[i] = kAnalogOn;
    for (const DigitalGame& d : kDigitalGame)
        if (d.game == bit) p.buttons |= d.pad;
}

bool GameBitHeld(const input::Pad& p, uint32_t bit) {
    if (!bit) return false;
    for (int i = 0; i < 8; i++)
        if (kAnalogGame[i] == bit && p.analog[i] > 0x40) return true;
    for (const DigitalGame& d : kDigitalGame)
        if (d.game == bit && (p.buttons & d.pad)) return true;
    return false;
}

bool Readable(uint32_t va, uint32_t length) {
    if (va < 0x10000 || va + length < va || va + length > 0x04000000) return false;
    for (uint32_t pg = va & ~(mem::kPage - 1); pg <= va + length - 1; pg += mem::kPage)
        if (!mem::IsCommitted(pg)) return false;
    return true;
}

// the layout in use now; false when the game has no player's controls yet or what is there is not a layout (the keys then
// keep the pad buttons of their defaults)
bool ReadLayout(Layout& layout) {
    uint32_t state = kernel::R32(kControlState);
    if (!Readable(state + kConfigAt, kConfigWords * 4) || !Readable(state + kAimingFlag, 4)) return false;
    uint32_t word[kConfigWords];
    for (uint32_t i = 0; i < kConfigWords; i++) word[i] = kernel::R32(state + kConfigAt + i * 4);
    bool any = false;
    for (int a = 0; a < kActCount; a++) {
        if (!KnownGameBit(word[a])) return false;
        layout.button[a] = word[a];
        any = any || (a <= kCrouch && word[a]);
    }
    for (int g = 0; g < 2; g++)
        for (int n = 0; n < 2; n++) {
            uint32_t axis = word[(g ? kAimAxisWords : kAxisWords) + n];
            if (axis > 4) return false;
            layout.axis[g][n] = axis;
        }
    layout.aiming = kernel::R32(state + kAimingFlag) != 0;
    if (!any) return false;
    static uint32_t last[kActCount + 4] = {};
    uint32_t now[kActCount + 4];
    for (int a = 0; a < kActCount; a++) now[a] = layout.button[a];
    now[kActCount] = layout.axis[0][0];
    now[kActCount + 1] = layout.axis[0][1];
    now[kActCount + 2] = layout.axis[1][0];
    now[kActCount + 3] = layout.axis[1][1];
    if (std::memcmp(last, now, sizeof(now))) {
        std::memcpy(last, now, sizeof(now));
        LOGR("controls: the game's layout: fire %X alt fire %X activate %X reload %X aim %X crouch %X, zoom %X/%X weapon %X/%X, "
             "run axis %u side step axis %u (aiming %u/%u)",
             now[kFire], now[kAltFire], now[kActivate], now[kReload], now[kAim], now[kCrouch], now[kZoomIn], now[kZoomOut],
             now[kPrevWeapon], now[kNextWeapon], now[kActCount], now[kActCount + 1], now[kActCount + 2], now[kActCount + 3]);
    }
    return true;
}

// a key of the action b in play: the pad button the layout has the action on, or the stick axis it has movement on
// (ax: how far each stick axis is pushed, 1 left x, 2 left y, 3 right x, 4 right y); false when b is not an action of the layout
bool PressAction(input::Pad& p, const Layout& layout, const Binding& b, int (&ax)[5]) {
    int act = b.act;
    if (b.stick) {
        static const int kMove[5] = {-1, kRunForward, kRunBack, kSideLeft, kSideRight};
        act = kMove[b.stick];
    }
    if (act < 0 || act >= kActCount) return false;
    if (act == kRunForward || act == kRunBack || act == kSideLeft || act == kSideRight) {
        const bool run = act == kRunForward || act == kRunBack;
        int axis = int(layout.axis[layout.aiming ? 1 : 0][run ? 0 : 1]);
        if (axis >= 1 && axis <= 4) {
            ax[axis] += (act == kRunForward || act == kSideRight) ? 1 : -1;
            return true;
        }
    }
    PressGameBit(p, layout.button[act]);
    return true;
}

// the keyboard and mouse join port 1's pad; true when a key was held. layout: the game's control layout in play, null in the menus
bool Keyboard(input::Pad& p, const Layout* layout) {
    if (!Foreground() || mouse::TextEntry() || panel::Open()) return false;
    bool any = false;
    int ax[5] = {};  // how far each stick axis is pushed: 1 left x, 2 left y, 3 right x, 4 right y
    TrackStatus();
    // outside the play the mouse is the menus': left click A, right click B, the wheel the D-pad up and down
    // (the look update stops for a moment now and then in play, a death or a cutscene: the menu's reading waits a bit)
    static uint64_t last_play = 0;
    uint64_t now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count());
    if (mouse::InPlay()) last_play = now;
    const bool menu = now - last_play > 700;
    if (menu) layout = nullptr;
    g_wheel_now = mouse::WheelButton();
    for (int i = 0; i < kBindingCount; i++) {
        const Binding& b = kBindings[i];
        bool down = false;
        int keys[3];
        for (int k = 0, n = KeysOf(i, keys); k < n; k++) {
            int vk = keys[k];
            if (!Down(vk)) continue;
            // in the menus the clicks are the pointer's (A and B below), not the buttons they are bound to in play
            if (menu && (vk == VK_LBUTTON || vk == VK_RBUTTON)) continue;
            // a click that brought the window back, or the cursor free of the window, is not a button
            if ((vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) && mouse::ButtonsBlocked()) continue;
            down = true;
        }
        if (!down) continue;
        if (layout && PressAction(p, *layout, b, ax)) {
            any = true;
            continue;
        }
        if (b.stick == 1) ax[2]++;
        if (b.stick == 2) ax[2]--;
        if (b.stick == 3) ax[1]--;
        if (b.stick == 4) ax[1]++;
        if (b.analog >= 0) p.analog[b.analog] = kAnalogOn;
        p.buttons |= b.bits;
        any = true;
    }
    WeaponSlots(menu);
    if (menu && !mouse::ButtonsBlocked()) {
        if (Down(VK_LBUTTON)) { p.analog[kA] = kAnalogOn; any = true; }
        if (Down(VK_RBUTTON)) { p.analog[kB] = kAnalogOn; any = true; }
    }
    // each stick on its own: a diagonal is slowed on one stick, not across two
    for (int stick = 0; stick < 2; stick++) {
        int x = ax[1 + 2 * stick], y = ax[2 + 2 * stick];
        if (!x && !y) continue;
        float len = (x && y) ? 0.7071f : 1.0f;
        (stick ? p.rx : p.lx) = int16_t(x * len * kStickMax);
        (stick ? p.ry : p.ly) = int16_t(y * len * kStickMax);
        any = true;
    }
    // the wheel: zooms a scope in and out (the game's triggers there), otherwise changes weapon (D-pad right and left)
    if (int wheel = g_wheel_now; wheel && !WheelClaimed()) {
        const bool aim_key = BindingDown("aim", true);  // aiming (whatever it is bound to): the wheel zooms the scope
        if (layout) {  // the layout's own buttons for zooming and for changing weapon
            PressGameBit(p, layout->button[aim_key ? (wheel > 0 ? kZoomIn : kZoomOut) : (wheel > 0 ? kNextWeapon : kPrevWeapon)]);
        } else if (aim_key) {
            p.buttons |= wheel > 0 ? kUp : kDown;  // the D-pad up and down zoom in and out while aiming
        } else {
            p.buttons |= wheel > 0 ? kRight : kLeft;
        }
        any = true;
    }
    if (g_wheel_now && WheelClaimed()) any = true;
    if (menu) {
        if (int notches = mouse::MenuPointer(true).wheel) {
            p.buttons |= notches > 0 ? kUp : kDown;
            any = true;
        }
    }
    // the mouse's movement as the right stick, for the screens and vehicles that turn by the stick
    int16_t rx = 0, ry = 0;
    if (mouse::VehicleStick(rx, ry)) {
        p.rx = rx;
        p.ry = ry;
        any = true;
    }
    // the MapMaker editor's cursor follows the mouse (the left stick it is moved by)
    if (int16_t ex = 0, ey = 0; mouse::EditorStick(ex, ey)) {
        p.lx = int16_t(std::clamp(int(p.lx) + ex, -int(kStickMax), int(kStickMax)));
        p.ly = int16_t(std::clamp(int(p.ly) + ey, -int(kStickMax), int(kStickMax)));
        any = true;
    }
    if (any) p.connected = true;
    return any;
}

// XBE_INPUT_SCRIPT="<seconds>:<button>[*<hold seconds>],...": presses for testing without hands on the game, held
// 150 ms unless given; buttons A B X Y BLACK WHITE START BACK UP DOWN LEFT RIGHT LT RT, sticks at full tilt LXP LXN LYP
// LYN RXP RXN RYP RYN (P positive: right or up), the mouse moving MXP MXN MYP MYN, the menu pointer at
// P<x>/<y> of the frame (Q<x>/<y> clicking there), seconds from the first poll
void Script(input::Pad& p) {
    struct Press {
        double at;
        std::string button;
        double hold;
    };
    static const std::vector<Press> presses = [] {
        std::vector<Press> out;
        const char* s = std::getenv("XBE_INPUT_SCRIPT");
        for (std::string rest = s ? s : ""; !rest.empty();) {
            size_t comma = rest.find(',');
            std::string item = rest.substr(0, comma);
            rest = comma == std::string::npos ? "" : rest.substr(comma + 1);
            size_t colon = item.find(':');
            if (colon == std::string::npos) continue;
            std::string button = item.substr(colon + 1);
            size_t star = button.find('*');
            double hold = star == std::string::npos ? 0.15 : std::atof(button.c_str() + star + 1);
            out.push_back({std::atof(item.c_str()), button.substr(0, star), hold});
        }
        return out;
    }();
    if (presses.empty()) return;
    static const auto start = std::chrono::steady_clock::now();
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    for (const Press& pr : presses) {
        if (t < pr.at || t > pr.at + pr.hold) continue;
        const std::string& b = pr.button;
        static const std::pair<const char*, int> kAnalogNames[] = {{"A", kA},         {"B", kB},         {"X", kX},
                                                                   {"Y", kY},         {"BLACK", kBlack}, {"WHITE", kWhite},
                                                                   {"LT", kLeftTrigger}, {"RT", kRightTrigger}};
        static const std::pair<const char*, uint16_t> kBitNames[] = {{"UP", kUp},       {"DOWN", kDown}, {"LEFT", kLeft},
                                                                     {"RIGHT", kRight}, {"START", kStart}, {"BACK", kBack}};
        for (auto& [n, i] : kAnalogNames)
            if (b == n) p.analog[i] = kAnalogOn;
        for (auto& [n, bit] : kBitNames)
            if (b == n) p.buttons |= bit;
        static const std::pair<const char*, int16_t input::Pad::*> kStickNames[] = {
            {"LX", &input::Pad::lx}, {"LY", &input::Pad::ly}, {"RX", &input::Pad::rx}, {"RY", &input::Pad::ry}};
        for (auto& [n, axis] : kStickNames)
            if (b.size() == 3 && b.compare(0, 2, n) == 0) p.*axis = b[2] == 'P' ? kStickMax : int16_t(-kStickMax);
        // the menu pointer at x/y of the frame (0-1): P moves it, Q clicks there
        if ((b[0] == 'P' || b[0] == 'Q') && b.find('/') != std::string::npos) {
            float x = float(std::atof(b.c_str() + 1)), y = float(std::atof(b.c_str() + b.find('/') + 1));
            mouse::TestPointer(x, y, b[0] == 'Q', false);
            if (b[0] == 'Q') p.analog[kA] = kAnalogOn;
        }
        constexpr int kCounts = 10;  // a poll
        if (b == "MXP" || b == "MXN") mouse::Inject(b[2] == 'P' ? kCounts : -kCounts, 0);
        if (b == "MYP" || b == "MYN") mouse::Inject(0, b[2] == 'P' ? -kCounts : kCounts);
        p.connected = true;
    }
}

}  // namespace


const Binding kBindings[] = {
    {"move_forward", "Avanzar", -1, 0, 1, 'W', 0, 0},
    {"move_back", "Retroceder", -1, 0, 2, 'S', 0, 0},
    {"move_left", "Paso a la izquierda", -1, 0, 3, 'A', 0, 0},
    {"move_right", "Paso a la derecha", -1, 0, 4, 'D', 0, 0},
    {"fire", "Disparar (gatillo derecho)", kRightTrigger, 0, 0, VK_LBUTTON, 0, 0, 0},
    {"aim", "Apuntar (gatillo izquierdo)", kLeftTrigger, 0, 0, VK_RBUTTON, 0, 0, 4},
    {"btn_a", "Activar (boton A)", kA, 0, 0, 'E', VK_RETURN, VK_SPACE, 2},
    {"btn_b", "Boton B (atras en menus; sin uso en juego)", kB, 0, 0, 'F', VK_BACK, VK_XBUTTON1},
    {"btn_x", "Agacharse (boton X)", kX, 0, 0, VK_CONTROL, 'C', VK_LCONTROL, 5},
    {"btn_y", "Recargar (boton Y)", kY, 0, 0, 'R', 0, 0, 3},
    {"black", "Boton negro (sin uso por defecto)", kBlack, 0, 0, 0, 0, 0},
    {"white", "Boton blanco (sin uso por defecto)", kWhite, 0, 0, 0, 0, 0},
    {"start", "Start", -1, kStart, 0, VK_ESCAPE, 0, 0},
    {"back", "Back", -1, kBack, 0, VK_TAB, 0, 0},
    {"dpad_up", "Zoom + con visor (cruceta arriba)", -1, kUp, 0, VK_UP, 0, 0, 14},
    {"dpad_down", "Zoom - con visor (cruceta abajo)", -1, kDown, 0, VK_DOWN, 0, 0, 15},
    {"dpad_left", "Arma anterior (cruceta izquierda)", -1, kLeft, 0, VK_LEFT, 0, 0, 16},
    {"dpad_right", "Arma siguiente (cruceta derecha)", -1, kRight, 0, VK_RIGHT, 0, 0, 17},
    {"lthumb", "Pulsar stick izquierdo (sin uso por defecto)", -1, kLeftThumb, 0, 0, 0, 0},
    {"rthumb", "Disparo alternativo (stick derecho pulsado)", -1, kRightThumb, 0, VK_MBUTTON, 0, 0, 1},
    {"weapon_1", "Arma 1 (de las que llevas, en orden)", -1, 0, 0, '1', 0, 0},
    {"weapon_2", "Arma 2 (de las que llevas, en orden)", -1, 0, 0, '2', 0, 0},
    {"weapon_3", "Arma 3 (de las que llevas, en orden)", -1, 0, 0, '3', 0, 0},
    {"weapon_4", "Arma 4 (de las que llevas, en orden)", -1, 0, 0, '4', 0, 0},
    {"weapon_5", "Arma 5 (de las que llevas, en orden)", -1, 0, 0, '5', 0, 0},
    {"weapon_6", "Arma 6 (de las que llevas, en orden)", -1, 0, 0, '6', 0, 0},
    {"weapon_7", "Arma 7 (de las que llevas, en orden)", -1, 0, 0, '7', 0, 0},
    {"weapon_8", "Arma 8 (de las que llevas, en orden)", -1, 0, 0, '8', 0, 0},
    {"weapon_9", "Arma 9 (de las que llevas, en orden)", -1, 0, 0, '9', 0, 0},
    {"weapon_10", "Arma 10 (de las que llevas, en orden)", -1, 0, 0, '0', 0, 0},
};
const int kBindingCount = int(std::size(kBindings));

int KeyOf(int i) {
    uint32_t v = game::Opt().keys[i];
    return v == 0xFFFFFFFFu ? kBindings[i].def : int(v);
}
int Key2Of(int i) {
    uint32_t v = game::Opt().keys2[i];
    return v == 0xFFFFFFFFu ? kBindings[i].alt1 : int(v);
}
int SetKey(int i, int slot, int vk) {
    int lost = -1;
    if (vk) {
        for (int j = 0; j < kBindingCount; j++) {
            for (int s = 0; s < 2; s++) {
                if (j == i && s == slot) continue;
                int cur = s == 0 ? KeyOf(j) : Key2Of(j);
                if (cur && Generic(cur) == Generic(vk)) {
                    (s == 0 ? game::Opt().keys[j] : game::Opt().keys2[j]) = 0;
                    if (j != i) lost = j;
                }
            }
        }
    }
    (slot == 0 ? game::Opt().keys[i] : game::Opt().keys2[i]) = uint32_t(vk);
    return lost;
}
void ResetKeys() {
    std::fill(std::begin(game::Opt().keys), std::end(game::Opt().keys), 0xFFFFFFFFu);
    std::fill(std::begin(game::Opt().keys2), std::end(game::Opt().keys2), 0xFFFFFFFFu);
}

std::string KeyName(int vk) {
    switch (vk) {
        case 0: return "(sin asignar)";
        case VK_LBUTTON: return "Clic izquierdo";
        case VK_RBUTTON: return "Clic derecho";
        case VK_MBUTTON: return "Clic central";
        case VK_XBUTTON1: return "Raton 4";
        case VK_XBUTTON2: return "Raton 5";
        case kWheelUp: return "Rueda arriba";
        case kWheelDown: return "Rueda abajo";
    }
    bool extended = vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_INSERT ||
                    vk == VK_DELETE || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT;
    LONG sc = LONG(MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC)) << 16 | (extended ? 1 << 24 : 0);
    wchar_t w[64] = {};
    if (!GetKeyNameTextW(sc, w, 64) || !w[0]) return "Tecla " + std::to_string(vk);
    char u[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof(u), nullptr, nullptr);
    return u;
}

// Gyro aiming: a controller's turn as the mouse's counts, so it turns the view wherever the mouse does. Only in play:
// in a menu the counts would move its pointer.
void GyroAim() {
    const game::Options& o = game::Opt();
    float yaw = 0, pitch = 0;
    bool have = false;
    for (int port = 0; port < 4 && !have; port++) have = input::TakeGyro(port, yaw, pitch);
    static float rest_x = 0, rest_y = 0;
    if (!have) return;
    if (!o.gyro_aim || !mouse::InPlay() || mouse::MenuMode() || mouse::TextEntry() || panel::Open()) {
        rest_x = rest_y = 0;
        return;
    }
    // degrees of the view as counts: the mouse's step (0.1 degrees at sensitivity 100), up and down share undone
    float speed = float(std::clamp<uint32_t>(o.gyro_speed, 10, 1000)) / 100.0f;
    float count = 0.1f * float(std::max<uint32_t>(o.mouse_sensitivity, 1)) / 100.0f;
    float vertical = float(std::max<uint32_t>(o.mouse_vertical, 1)) / 100.0f;
    rest_x += -yaw * speed / count;
    rest_y += -pitch * speed / count / vertical;
    int dx = int(rest_x), dy = int(rest_y);
    rest_x -= float(dx);
    rest_y -= float(dy);
    if (dx || dy) mouse::Inject(dx, dy);
}

void Pad(int port, input::Pad& p) {
    if (port != 0) return;
    debug::Tick();
    GyroAim();
    if (panel::Open()) g_status = false;  // the objectives view is not left on under the panel
    // the pad's own use (its buttons, triggers and sticks) and the keyboard's tell the prompts which buttons to show
    bool pad_used = p.connected && (p.buttons || std::any_of(std::begin(p.analog), std::end(p.analog), [](uint8_t v) { return v > 0x60; }) ||
                                    std::abs(int(p.lx)) > 16000 || std::abs(int(p.ly)) > 16000 || std::abs(int(p.rx)) > 16000 ||
                                    std::abs(int(p.ry)) > 16000);
    // the game's control layout, while a level is played: what the keys press, and which button is aim and which is fire
    Layout layout = {};
    const bool in_play = mouse::InPlay() && ReadLayout(layout);
    g_keyboard = Keyboard(p, in_play ? &layout : nullptr);
    button_icons::NoteInput(pad_used && !g_keyboard, g_keyboard);
    Script(p);
    p.connected = true;  // the keyboard and mouse are always a pad: no reconnect prompt, no clash with a real one
    if (port == 0) g_aiming = in_play ? GameBitHeld(p, layout.button[kAim]) : p.analog[kLeftTrigger] > 0x40;
    if (port == 0 && (in_play ? GameBitHeld(p, layout.button[kFire]) : p.analog[kRightTrigger] > 0x40)) g_fire_ms = NowMsSteady();
    // Back and Start together open the panel; while it is open the game sees an idle pad
    static bool combo_was = false;
    bool combo = (p.buttons & kBack) && (p.buttons & kStart);
    if (combo && !combo_was) panel::Toggle();
    combo_was = combo;
    FreeCamPad(p, Foreground() && !panel::Open() && !mouse::TextEntry());  // F6: the free camera (src/game/free_cam.cpp)
    if (panel::Holds(port, p)) {
        p.buttons = 0;
        std::fill(std::begin(p.analog), std::end(p.analog), uint8_t(0));
        p.lx = p.ly = p.rx = p.ry = 0;
    }
    mouse::KeepLocked();
}

bool StatusHeld() { return g_status; }
bool Aiming() { return g_aiming; }
bool FiredWithin(int ms) { return NowMsSteady() - g_fire_ms.load() < ms; }

bool KeyboardActive() { return g_keyboard; }

}  // namespace controls
