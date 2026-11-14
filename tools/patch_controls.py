import os
os.chdir(r'C:\TS2-XBE\recomp\src\common')

open('controls.h', 'w', encoding='utf-8').write('''#pragma once
#include <string>

#include "core/input.h"

// the keyboard and mouse buttons as the console pad (controls.cpp)
namespace controls {

void Pad(int port, input::Pad& pad);  // GameInfo::pad
bool KeyboardActive();

// each control the keyboard and mouse give: a key or mouse button, rebound in the settings panel
struct Binding {
    const char* id;     // the settings file's key_<id>
    const char* label;  // the panel's
    int analog;         // Pad::analog index, -1 none
    uint16_t bits;      // Pad::buttons
    int stick;          // 1 forward 2 back 3 left 4 right on the left stick, 0 none
    int def, alt1, alt2;  // the default key; keys that always work as well (the menus' Enter and Backspace)
};
extern const Binding kBindings[];
extern const int kBindingCount;
int KeyOf(int i);            // the virtual key now, 0 unbound
void SetKey(int i, int vk);  // 0 unbinds
void ResetKeys();
std::string KeyName(int vk);

}  // namespace controls
''')

s = open('options.h', encoding='utf-8').read()
s = s.replace('''    bool invert_mouse = false;
};''', '''    bool invert_mouse = false;
    // the keys of controls::kBindings: 0xFFFFFFFF the default, 0 none, else a virtual key
    static constexpr int kKeys = 24;
    uint32_t keys[kKeys];
    Options() { std::fill(std::begin(keys), std::end(keys), 0xFFFFFFFFu); }
};''')
s = s.replace('#include <cstdint>', '#include <algorithm>\n#include <cstdint>\n#include <iterator>')
open('options.h', 'w', encoding='utf-8').write(s)

c = open('controls.cpp', encoding='utf-8').read()
a = c.index('struct Key {')
b = c.index('std::atomic<bool> g_keyboard')
c = c[:a] + c[b:]
c = c.replace('#include "common/options.h"\n', '#include "common/options.h"\n#include "common/panel.h"\n', 1)
a = c.index("// the keyboard and mouse join port 1")
b = c.index('// XBE_INPUT_SCRIPT=')
kb = '''// the keyboard and mouse join port 1's pad; true when a key was held
bool Keyboard(input::Pad& p) {
    if (!Foreground() || mouse::TextEntry() || panel::Open()) return false;
    bool any = false;
    int lx = 0, ly = 0;
    for (int i = 0; i < kBindingCount; i++) {
        const Binding& b = kBindings[i];
        bool down = false;
        for (int vk : {KeyOf(i), b.alt1, b.alt2}) {
            if (!vk || !Down(vk)) continue;
            // a click that brought the window back, or the cursor free of the window, is not a button
            if ((vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) && mouse::ButtonsBlocked()) continue;
            down = true;
        }
        if (!down) continue;
        if (b.stick == 1) ly++;
        if (b.stick == 2) ly--;
        if (b.stick == 3) lx--;
        if (b.stick == 4) lx++;
        if (b.analog >= 0) p.analog[b.analog] = kAnalogOn;
        p.buttons |= b.bits;
        any = true;
    }
    if (lx || ly) {
        float len = (lx && ly) ? 0.7071f : 1.0f;
        p.lx = int16_t(lx * len * kStickMax);
        p.ly = int16_t(ly * len * kStickMax);
        any = true;
    }
    // the wheel: zooms a scope in and out (the game's triggers there), otherwise changes weapon (D-pad right and left)
    if (int wheel = mouse::WheelButton()) {
        if (mouse::Zoomed()) {
            p.analog[wheel > 0 ? kRightTrigger : kLeftTrigger] = kAnalogOn;
        } else {
            p.buttons |= wheel > 0 ? kRight : kLeft;
        }
        any = true;
    }
    // the mouse's movement as the right stick, for the screens and vehicles that turn by the stick
    int16_t rx = 0, ry = 0;
    if (mouse::VehicleStick(rx, ry)) {
        p.rx = rx;
        p.ry = ry;
        any = true;
    }
    if (any) p.connected = true;
    return any;
}

'''
c = c[:a] + kb + c[b:]
c = c.replace('''    g_keyboard = Keyboard(p);
    Script(p);''', '''    g_keyboard = Keyboard(p);
    Script(p);
    // Back and Start together open the panel; while it is open the game sees an idle pad
    static bool combo_was = false;
    bool combo = (p.buttons & kBack) && (p.buttons & kStart);
    if (combo && !combo_was) panel::Toggle();
    combo_was = combo;
    if (panel::Holds(port, p)) {
        p.buttons = 0;
        std::fill(std::begin(p.analog), std::end(p.analog), uint8_t(0));
        p.lx = p.ly = p.rx = p.ry = 0;
    }''', 1)
defs = '''
const Binding kBindings[] = {
    {"move_forward", "Avanzar", -1, 0, 1, 'W', 0, 0},
    {"move_back", "Retroceder", -1, 0, 2, 'S', 0, 0},
    {"move_left", "Paso a la izquierda", -1, 0, 3, 'A', 0, 0},
    {"move_right", "Paso a la derecha", -1, 0, 4, 'D', 0, 0},
    {"fire", "Disparar (gatillo derecho)", kRightTrigger, 0, 0, VK_LBUTTON, 0, 0},
    {"aim", "Apuntar / zoom (gatillo izquierdo)", kLeftTrigger, 0, 0, VK_RBUTTON, 0, 0},
    {"btn_a", "Boton A", kA, 0, 0, 'E', VK_RETURN, VK_SPACE},
    {"btn_b", "Boton B", kB, 0, 0, 'F', VK_BACK, VK_XBUTTON1},
    {"btn_x", "Boton X", kX, 0, 0, 'R', 0, 0},
    {"btn_y", "Boton Y", kY, 0, 0, 'Q', 0, 0},
    {"black", "Boton negro", kBlack, 0, 0, 'X', 0, 0},
    {"white", "Boton blanco", kWhite, 0, 0, 'Z', 0, 0},
    {"start", "Start", -1, kStart, 0, VK_ESCAPE, 0, 0},
    {"back", "Back", -1, kBack, 0, VK_TAB, 0, 0},
    {"dpad_up", "Cruceta arriba", -1, kUp, 0, VK_UP, 0, 0},
    {"dpad_down", "Cruceta abajo", -1, kDown, 0, VK_DOWN, 0, 0},
    {"dpad_left", "Cruceta izquierda", -1, kLeft, 0, VK_LEFT, 0, 0},
    {"dpad_right", "Cruceta derecha", -1, kRight, 0, VK_RIGHT, 0, 0},
    {"lthumb", "Pulsar stick izquierdo", -1, kLeftThumb, 0, VK_CONTROL, 0, 0},
    {"rthumb", "Pulsar stick derecho", -1, kRightThumb, 0, VK_MBUTTON, 0, 0},
};
const int kBindingCount = int(std::size(kBindings));

int KeyOf(int i) {
    uint32_t v = game::Opt().keys[i];
    return v == 0xFFFFFFFFu ? kBindings[i].def : int(v);
}
void SetKey(int i, int vk) { game::Opt().keys[i] = uint32_t(vk); }
void ResetKeys() { std::fill(std::begin(game::Opt().keys), std::end(game::Opt().keys), 0xFFFFFFFFu); }

std::string KeyName(int vk) {
    switch (vk) {
        case 0: return "(sin asignar)";
        case VK_LBUTTON: return "Clic izquierdo";
        case VK_RBUTTON: return "Clic derecho";
        case VK_MBUTTON: return "Clic central";
        case VK_XBUTTON1: return "Raton 4";
        case VK_XBUTTON2: return "Raton 5";
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

'''
c = c.replace('void Pad(int port, input::Pad& p) {', defs + 'void Pad(int port, input::Pad& p) {', 1)
c = c.replace('#include <chrono>', '#include <chrono>\n#include <iterator>', 1)
open('controls.cpp', 'w', encoding='utf-8').write(c)

g = open('game_info.cpp', encoding='utf-8').read()
g = g.replace('#include "common/options.h"\n', '#include "common/options.h"\n#include "common/panel.h"\n#include <string>\n#include <vector>\n', 1)
g = g.replace('''bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    return mouse::Message(msg, wp, lp, result);
}''', '''bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    if (panel::Message(msg, wp, lp, result)) return true;
    return mouse::Message(msg, wp, lp, result);
}
void Ui(double fps) { panel::Ui(fps); }
// the settings file's keys: the mouse's, and each control's key_<id>
std::vector<app::Option> MakeOptions() {
    std::vector<app::Option> o{{"invert_mouse", &Opt().invert_mouse, nullptr},
                               {"mouse_sensitivity", nullptr, &Opt().mouse_sensitivity},
                               {"mouse_vertical", nullptr, &Opt().mouse_vertical}};
    static std::string names[Options::kKeys];
    for (int i = 0; i < controls::kBindingCount && i < Options::kKeys; i++) {
        names[i] = std::string("key_") + controls::kBindings[i].id;
        o.push_back({names[i].c_str(), nullptr, &Opt().keys[i]});
    }
    return o;
}''', 1)
a = g.index('    .options = {{"invert_mouse"')
marker = '// game settings kept in the settings file'
b = g.index(marker) + len(marker)
g = g[:a] + '    .options = game::MakeOptions(),       ' + marker + g[b:]
g = g.replace('    .ui = nullptr,                        // ImGui drawing each frame', '    .ui = game::Ui,                       // ImGui drawing each frame', 1)
open('game_info.cpp', 'w', encoding='utf-8').write(g)
print('ok')
