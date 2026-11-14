// The game's button prompts as the buttons of the input in use.
//
// The game draws its prompts as text with a glyph in it: the byte 0xB and a digit 1-8 (the font code at 0x1AC42B), the
// digit picking the picture from a table (0x48D780) of the textures xbstdbutton1-4 and stdbutton5: 1 is the B button, 2 A,
// 3 Y, 4 X, 5 the Back button. The pause screen draws the pad's buttons from one sheet instead, xbbuttons (0xAA360): B, A,
// X and Y in its top row, Start and Back under them. All of them are 32x32 pictures of the Xbox pad's buttons.
//
// The renderer draws a replacement picture in place of a console texture found by its size and the hash of its pixels
// (gpu::ReplaceTexture). The ones for those six textures are set here from data/button_icons.bin (tools/make_button_icons.py)
// and changed whenever what they show changes: the last input used (the keyboard and mouse, or the pad, whose family
// SDL knows: Xbox, PlayStation 4 or 5, Switch), and for the keyboard the keys now bound to those actions, so a key
// rebound in the panel changes its picture at once. The pad's buttons are named by where they are (the pad reports the
// button under the right thumb as A): a Switch pad's "B" is the one at the bottom.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "app/app.h"
#include "common/button_icons.h"
#include "common/controls.h"
#include "common/options.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"

namespace button_icons {
namespace {

constexpr int kCell = 96;  // the pack's pictures are 96x96

struct Pic {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;  // BGRA
    int cell = -1;            // its place in the panel's atlas
};
struct ConsoleTex {
    std::string name;
    uint32_t w = 0, h = 0, n = 0;
    uint64_t hash = 0;
};
struct Set {
    std::vector<std::shared_ptr<gpu::TextureReplacement>> rep;  // for each ConsoleTex; kept for the process's life
};

std::map<std::string, Pic> g_icons;
std::vector<std::string> g_order;
std::vector<ConsoleTex> g_tex;
bool g_tried = false, g_loaded = false;

std::map<std::string, Set> g_sets;
std::set<const gpu::TextureReplacement*> g_mine;
std::map<uint64_t, std::shared_ptr<const gpu::TextureReplacement>> g_saved;  // what was set before, to give back
std::string g_sig;
bool g_active = false, g_sheet = false;
Style g_shown = kXbox;
std::atomic<int> g_last{0};  // the last input used: 1 the pad, 2 the keyboard or mouse

bool g_atlas_sent = false;

uint64_t Key(const ConsoleTex& t) { return (uint64_t(t.w) << 48) ^ (uint64_t(t.h) << 32) ^ t.hash ^ t.n; }

bool Load() {
    if (g_tried) return g_loaded;
    g_tried = true;
    std::ifstream f(app::ExeDir() / "button_icons.bin", std::ios::binary);
    if (!f) f.open(app::ExeDir() / "data" / "button_icons.bin", std::ios::binary);
    char head[12];
    if (!f.read(head, 12) || std::memcmp(head, "BIC1", 4)) {
        LOGW("button_icons: data/button_icons.bin is missing, the buttons stay the game's");
        return false;
    }
    uint32_t icons, textures;
    std::memcpy(&icons, head + 4, 4);
    std::memcpy(&textures, head + 8, 4);
    if (icons > 4096 || textures > 256) return false;
    for (uint32_t i = 0; i < icons; i++) {
        char name[33] = {};
        uint32_t size[2];
        if (!f.read(name, 32) || !f.read(reinterpret_cast<char*>(size), 8) || !size[0] || !size[1] || size[0] > 1024 || size[1] > 1024)
            return false;
        Pic p;
        p.w = size[0];
        p.h = size[1];
        p.px.resize(size_t(p.w) * p.h * 4);
        if (!f.read(reinterpret_cast<char*>(p.px.data()), std::streamsize(p.px.size()))) return false;
        g_order.push_back(name);
        g_icons[name] = std::move(p);
    }
    for (uint32_t i = 0; i < textures; i++) {
        char name[33] = {};
        uint32_t v[3];
        uint64_t hash;
        if (!f.read(name, 32) || !f.read(reinterpret_cast<char*>(v), 12) || !f.read(reinterpret_cast<char*>(&hash), 8)) return false;
        g_tex.push_back({name, v[0], v[1], v[2], hash});
    }
    g_loaded = true;
    LOGR("button_icons: %zu pictures, %zu console textures from button_icons.bin", g_icons.size(), g_tex.size());
    return true;
}

// the pack's name of the picture of a key, mouse button or wheel notch, "" none
std::string KeyName(int vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string("k_") + char(vk);
    if (vk >= '0' && vk <= '9') return std::string("k_") + char(vk);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return std::string("k_") + char('0' + (vk - VK_NUMPAD0));
    if (vk >= VK_F1 && vk <= VK_F12) return "k_F" + std::to_string(vk - VK_F1 + 1);
    switch (vk) {
        case VK_SPACE: return "k_Space";
        case VK_RETURN: return "k_Enter";
        case VK_ESCAPE: return "k_Esc";
        case VK_TAB: return "k_Tab";
        case VK_BACK: return "k_Backspace";
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "k_Shift";
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "k_Ctrl";
        case VK_MENU: case VK_LMENU: case VK_RMENU: return "k_Alt";
        case VK_CAPITAL: return "k_CapsLock";
        case VK_UP: return "k_Up";
        case VK_DOWN: return "k_Down";
        case VK_LEFT: return "k_Left";
        case VK_RIGHT: return "k_Right";
        case VK_HOME: return "k_Home";
        case VK_END: return "k_End";
        case VK_PRIOR: return "k_PageUp";
        case VK_NEXT: return "k_PageDown";
        case VK_INSERT: return "k_Ins";
        case VK_DELETE: return "k_Del";
        case VK_SNAPSHOT: return "k_PrtScrn";
        case VK_NUMLOCK: return "k_NumLock";
        case VK_OEM_MINUS: case VK_SUBTRACT: return "k_Minus";
        case VK_OEM_PLUS: case VK_ADD: return "k_Plus";
        case VK_MULTIPLY: return "k_Asterisk";
        case VK_OEM_2: case VK_DIVIDE: return "k_Slash";
        case VK_OEM_1: return "k_Semicolon";
        case VK_OEM_7: return "k_Quote";
        case VK_OEM_3: return "k_Tilde";
        case VK_OEM_4: return "k_BracketL";
        case VK_OEM_6: return "k_BracketR";
        case VK_OEM_COMMA: return "k_Comma";
        case VK_OEM_PERIOD: case VK_DECIMAL: return "k_Period";
        case VK_LBUTTON: return "k_MouseLeft";
        case VK_RBUTTON: return "k_MouseRight";
        case VK_MBUTTON: return "k_MouseMiddle";
        case VK_XBUTTON1: case VK_XBUTTON2: return "k_MouseOther";
        case controls::kWheelUp: return "k_WheelUp";
        case controls::kWheelDown: return "k_WheelDown";
    }
    return "";
}

// the pack's name of a prompt's button (1 B, 2 A, 3 Y, 4 X, 5 Back, 6 Start) in a pad style
std::string PadName(Style s, int button) {
    static const char* const x[7] = {"", "x_b", "x_a", "x_y", "x_x", "x_back", "x_start"};
    static const char* const p4[7] = {"", "p4_circle", "p4_cross", "p4_triangle", "p4_square", "p4_share", "p4_options"};
    static const char* const p5[7] = {"", "p5_circle", "p5_cross", "p5_triangle", "p5_square", "p5_share", "p5_options"};
    // a Switch pad names the places the other way: its A is on the right, B at the bottom, X on top and Y on the left
    static const char* const sw[7] = {"", "s_a", "s_b", "s_x", "s_y", "s_minus", "s_plus"};
    if (button < 1 || button > 6) return "";
    switch (s) {
        case kPS4: return p4[button];
        case kPS5: return p5[button];
        case kSwitch: return sw[button];
        default: return x[button];
    }
}

// the key bound to an action now: its primary, or its alternate when it has no primary
int BoundKey(const char* id) {
    for (int i = 0; i < controls::kBindingCount; i++)
        if (!std::strcmp(controls::kBindings[i].id, id)) {
            int vk = controls::KeyOf(i);
            return vk ? vk : controls::Key2Of(i);
        }
    return 0;
}

std::string KeyboardName(int button) {
    static const char* const ids[7] = {"", "btn_b", "btn_a", "btn_y", "btn_x", "back", "start"};
    if (button < 1 || button > 6) return "";
    std::string n = KeyName(BoundKey(ids[button]));
    return n.empty() ? "k_Question" : n;
}

std::string NameFor(Style s, int button) { return s == kKeyboard ? KeyboardName(button) : PadName(s, button); }

const Pic* Get(const std::string& name) {
    auto it = g_icons.find(name);
    if (it != g_icons.end()) return &it->second;
    it = g_icons.find("k_Question");
    return it != g_icons.end() ? &it->second : nullptr;
}

Style FamilyStyle() {
    for (int port = 0; port < 4; port++) {
        switch (input::PadFamily(port)) {
            case input::Family::None: continue;
            case input::Family::PlayStation4: return kPS4;
            case input::Family::PlayStation5: return kPS5;
            case input::Family::Switch: return kSwitch;
            default: return kXbox;
        }
    }
    return kKeyboard;  // no pad
}

Style Choose(const game::Options& o) {
    if (o.button_icons_style >= kXbox && o.button_icons_style < kStyleCount) return Style(o.button_icons_style);
    Style family = FamilyStyle();
    if (family == kKeyboard) return kKeyboard;  // no pad connected
    int last = g_last.load();
    return last == 2 ? kKeyboard : family;
}

// a white triangle for the sheet's arrows, from three points of its cell (0-1), edges smoothed
void Triangle(std::vector<uint8_t>& sheet, int sheet_w, int cx, int cy, float ax, float ay, float bx, float by, float px, float py) {
    auto edge = [](float x0, float y0, float x1, float y1, float x, float y) { return (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0); };
    for (int y = 0; y < kCell; y++)
        for (int x = 0; x < kCell; x++) {
            int hit = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float fx = (float(x) + (float(sx) + 0.5f) / 4) / kCell, fy = (float(y) + (float(sy) + 0.5f) / 4) / kCell;
                    float e0 = edge(ax, ay, bx, by, fx, fy), e1 = edge(bx, by, px, py, fx, fy), e2 = edge(px, py, ax, ay, fx, fy);
                    if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0)) hit++;
                }
            if (!hit) continue;
            uint8_t* d = &sheet[(size_t(cy * kCell + y) * size_t(sheet_w) + size_t(cx * kCell + x)) * 4];
            d[0] = d[1] = d[2] = 255;
            d[3] = uint8_t(hit * 255 / 16);
        }
}

void Blit(std::vector<uint8_t>& sheet, int sheet_w, int cx, int cy, const Pic* p) {
    if (!p || p->w != kCell || p->h != kCell) return;
    for (int y = 0; y < kCell; y++)
        std::memcpy(&sheet[(size_t(cy * kCell + y) * size_t(sheet_w) + size_t(cx * kCell)) * 4], &p->px[size_t(y) * kCell * 4], kCell * 4);
}

// the replacements for a set of pictures (names[1..6]), made once
Set& MakeSet(const std::string& sig, const std::string names[7]) {
    auto it = g_sets.find(sig);
    if (it != g_sets.end()) return it->second;
    Set set;
    // the sheet: B A X Y over Start Back and the two arrows the game's menus draw from it
    std::vector<uint8_t> sheet(size_t(kCell) * 4 * kCell * 2 * 4, 0);
    const int sw = kCell * 4;
    Blit(sheet, sw, 0, 0, Get(names[1]));
    Blit(sheet, sw, 1, 0, Get(names[2]));
    Blit(sheet, sw, 2, 0, Get(names[4]));
    Blit(sheet, sw, 3, 0, Get(names[3]));
    Blit(sheet, sw, 0, 1, Get(names[6]));
    Blit(sheet, sw, 1, 1, Get(names[5]));
    Triangle(sheet, sw, 2, 1, 0.20f, 0.10f, 0.20f, 0.90f, 0.72f, 0.50f);  // right
    Triangle(sheet, sw, 3, 1, 0.10f, 0.72f, 0.90f, 0.72f, 0.50f, 0.26f);  // up
    for (const ConsoleTex& t : g_tex) {
        auto r = std::make_shared<gpu::TextureReplacement>();
        r->guest_w = t.w;
        r->guest_h = t.h;
        r->n = t.n;
        r->hash = t.hash;
        r->mips = true;  // drawn small: its smaller levels keep it clean
        int button = 0;
        if (t.name.size() == 12 && t.name.rfind("xbstdbutton", 0) == 0) button = t.name[11] - '0';
        else if (t.name == "stdbutton5") button = 5;
        if (t.name == "xbbuttons") {
            r->w = uint32_t(sw);
            r->h = uint32_t(kCell * 2);
            r->px = sheet;
        } else if (const Pic* p = button >= 1 && button <= 5 ? Get(names[button]) : nullptr) {
            r->w = p->w;
            r->h = p->h;
            r->px = p->px;
        } else {
            r.reset();
        }
        if (r) g_mine.insert(r.get());
        set.rep.push_back(r);
    }
    return g_sets.emplace(sig, std::move(set)).first->second;
}

void Restore() {
    for (const ConsoleTex& t : g_tex) {
        auto s = g_saved.find(Key(t));
        if (s == g_saved.end()) continue;
        if (s->second) gpu::ReplaceTexture(s->second);
        else gpu::EraseReplacement(t.w, t.h, t.n, t.hash);
    }
    g_saved.clear();
    g_active = g_sheet = false;
    g_sig.clear();
    LOGR("button_icons: the game's own buttons again");
}

void Apply(const Set& set) {
    for (size_t i = 0; i < g_tex.size(); i++) {
        const ConsoleTex& t = g_tex[i];
        if (!set.rep[i]) continue;
        auto cur = gpu::FindReplacementEntry(t.w, t.h, t.n, t.hash);
        if (cur && !g_mine.count(cur.get())) g_saved[Key(t)] = cur;  // the pack of Redux's, or whatever else was there
        else if (!g_saved.count(Key(t))) g_saved[Key(t)] = nullptr;
        gpu::ReplaceTexture(set.rep[i]);
        if (t.name == "xbbuttons") g_sheet = true;
    }
    g_active = true;
}

// the panel's atlas: every picture in a grid of 12 across, as RGBA for ImGui
constexpr int kAtlasCols = 12;
void SendAtlas() {
    if (g_atlas_sent || !Load()) return;
    g_atlas_sent = true;
    int rows = (int(g_order.size()) + kAtlasCols - 1) / kAtlasCols;
    uint32_t w = uint32_t(kAtlasCols * kCell), h = uint32_t(rows * kCell);
    std::vector<uint8_t> px(size_t(w) * h * 4, 0);
    int i = 0;
    for (const std::string& name : g_order) {
        Pic& p = g_icons[name];
        p.cell = i;
        int ox = (i % kAtlasCols) * kCell, oy = (i / kAtlasCols) * kCell;
        for (int y = 0; y < kCell && uint32_t(y) < p.h; y++)
            for (int x = 0; x < kCell && uint32_t(x) < p.w; x++) {
                const uint8_t* s = &p.px[(size_t(y) * p.w + size_t(x)) * 4];
                uint8_t* d = &px[(size_t(oy + y) * w + size_t(ox + x)) * 4];
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[3];
            }
        i++;
    }
    {
        std::lock_guard lock(gpu::g_ui_icons_lock);
        gpu::g_ui_icons_px = std::move(px);
        gpu::g_ui_icons_w = w;
        gpu::g_ui_icons_h = h;
    }
    gpu::g_ui_icons_new = true;
}

bool PicUv(const std::string& name, ImTextureID& tex, ImVec2& uv0, ImVec2& uv1) {
    SendAtlas();
    uint64_t id = gpu::g_ui_icons.load();
    const Pic* p = Get(name);
    if (!id || !p || p->cell < 0) return false;
    int rows = (int(g_order.size()) + kAtlasCols - 1) / kAtlasCols;
    int col = p->cell % kAtlasCols, row = p->cell / kAtlasCols;
    tex = ImTextureID(id);
    // half a texel in from the edge so a neighbour's picture does not bleed in
    float e = 0.5f;
    uv0 = {(float(col * kCell) + e) / float(kAtlasCols * kCell), (float(row * kCell) + e) / float(rows * kCell)};
    uv1 = {(float((col + 1) * kCell) - e) / float(kAtlasCols * kCell), (float((row + 1) * kCell) - e) / float(rows * kCell)};
    return true;
}

}  // namespace

void NoteInput(bool pad, bool keyboard) {
    if (keyboard) g_last = 2;
    else if (pad) g_last = 1;
}

void Tick() {
    const game::Options& o = game::Opt();
    if (!o.button_icons) {
        if (g_active) Restore();
        return;
    }
    if (!Load()) return;
    Style st = Choose(o);
    std::string names[7];
    std::string sig = std::to_string(int(st));
    for (int b = 1; b <= 6; b++) {
        names[b] = NameFor(st, b);
        sig += "|" + names[b];
    }
    Set& set = MakeSet(sig, names);
    bool need = !g_active || sig != g_sig;
    // the first pictures of the front end (Redux's, hd_textures.bin) are set when it first draws a header: if one took the
    // place of ours, ours go back
    for (size_t i = 0; i < g_tex.size() && !need; i++)
        if (set.rep[i] && gpu::FindReplacementEntry(g_tex[i].w, g_tex[i].h, g_tex[i].n, g_tex[i].hash) != set.rep[i]) need = true;
    if (!need) return;
    Apply(set);
    if (sig != g_sig) LOGR("button_icons: buttons of set %d (%s %s %s %s)", int(st), names[2].c_str(), names[1].c_str(), names[4].c_str(), names[3].c_str());
    g_sig = sig;
    g_shown = st;
}

bool SheetUpright() { return g_active && g_sheet; }
Style Shown() { return g_shown; }

const char* StyleName(Style s) {
    switch (s) {
        case kXbox: return "Xbox";
        case kPS4: return "PlayStation 4";
        case kPS5: return "PlayStation 5";
        case kSwitch: return "Switch";
        case kKeyboard: return "Keyboard and mouse";
        default: return "Auto";
    }
}

bool KeyIcon(int vk, ImTextureID& tex, ImVec2& uv0, ImVec2& uv1) {
    std::string n = KeyName(vk);
    return !n.empty() && PicUv(n, tex, uv0, uv1);
}

bool PadIcon(Style s, int button, ImTextureID& tex, ImVec2& uv0, ImVec2& uv1) {
    std::string n = PadName(s, button);
    return !n.empty() && PicUv(n, tex, uv0, uv1);
}

bool DrawKey(int vk, float size) {
    ImTextureID tex;
    ImVec2 a, b;
    if (!KeyIcon(vk, tex, a, b)) return false;
    ImGui::Image(tex, ImVec2(size, size), a, b);
    return true;
}

bool DrawPad(Style s, int button, float size) {
    ImTextureID tex;
    ImVec2 a, b;
    if (!PadIcon(s, button, tex, a, b)) return false;
    ImGui::Image(tex, ImVec2(size, size), a, b);
    return true;
}

}  // namespace button_icons
