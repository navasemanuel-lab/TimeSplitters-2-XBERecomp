// The front end's frame on a wide screen as TS2 Redux has it: the black bar at the bottom nearer the edge on every
// screen, Story's Select Level and Select Difficulty with the top one higher too, the main menu with no header and the
// logo big in its place, and the header's picture and logo at their own shape, not stretched.
//
// A screen of the menu library keeps where its bars end in the 640x480 screen: the top one's lower edge at +0x634, the
// bottom one's upper edge at +0x638 (0: none, as the profile screens), the title string at +0x630. Its builder sets
// them, builds its items (the first by 0xED390 (screen, size)) and lays the button prompts out from them; the frame
// draw (0x89130) draws the bars, their lines and the title from the current screen's and the one going (the frame
// manager [0x4AE668] +0x2F0 and +0x2F4) and draws no header while the top edge is 0. With the menus wide the edges are
// moved before the first item is built, so the bars, the title and the prompts move together. The main menu is built
// by 0x99CF0 (screen), Story's Select Level by 0x94B70 and Select Difficulty by 0xA09C0.
//
// The header is drawn by 0x1B7430 (x0, y0, x1, y1, z floats, u0, v0, u1, v1 in 16ths of a texel, four colours) with
// textures by the index 0xA9AA0 takes: [0x35C4B8] + 0x69 its picture across the width, 0x71 the logo at 48..380. The
// 640x480 screen is stretched to the output's shape, so on a wider one those are drawn narrower by (4:3) / its shape:
// the picture against the right edge, its first column of texels carried on to the left one, and the logo a fifth
// bigger at the left.
//
// With data/hd_textures.bin (tools/make_hd_textures.py: TS2 Redux's pictures) the renderer draws the headers and the
// logo from those instead: a header has the logo drawn in, so it is drawn whole across the width as Redux has it and the
// logo over it is left out, and the main menu's logo is Redux's with a glow.
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <string>

#include "app/app.h"
#include "common/options.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "menu_text.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// the pack replaced the pad buttons' texture (xbbuttons) with Redux's, its letters right way up (pad_icons.cpp)
bool g_hd_buttons = false;

namespace {
constexpr uint32_t kTopEdge = 0x634, kBottomEdge = 0x638;
constexpr uint32_t kTop = 40, kBottom = 452;  // for 0x50 and 410; Redux: about 41 and 455
constexpr uint32_t kFrames = 0x4AE668, kCurrent = 0x2F0, kGoing = 0x2F4, kProgress = 0x2F8, kScreenSet = 0x35C4B8;
constexpr uint32_t kTextureIndex = 0xA9AA0, kSetTexture = 0x1B7440, kBlend = 0x1B4F30, kQuad = 0x1B7430;
constexpr uint32_t kLogo = 0x71, kPicture = 0x69;
constexpr float kMainMenuDown = 60.0f;  // the main menu's box under its big logo

enum class Building { None, Other, Story, Main };
Building g_building = Building::None;
uint32_t g_main_menu = 0;  // the main menu's screen
bool g_building_main = false;
uint32_t g_main_box = 0;   // its menu, made by its first item's call
bool g_in_frame = false;
uint32_t g_last_index = 0;  // the texture index last asked for in the frame draw

bool g_hd_header = false;  // the headers drawn from Redux's pictures, the logo in them
bool g_hd_logo = false;    // the logo drawn from its picture

float F(uint32_t v) { return std::bit_cast<float>(v); }
uint32_t U(float f) { return std::bit_cast<uint32_t>(f); }

// how much narrower a 4:3 picture is drawn in the 640 wide screen: 1 without the menus stretched
float Narrow() {
    if (!game::Opt().menus_wide) return 1.0f;
    return std::min((4.0f / 3.0f) / app::Aspect(), 1.0f);
}

void Build(Building what) { g_building = game::Opt().menus_wide ? what : Building::None; }

// hd_textures.bin: "HDT1", count, then each: name (32), console width, height, bytes hashed, hash (u64), width, height,
// its BGRA pixels; handed to the renderer once, before the front end draws its first header
void LoadHd() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    std::ifstream f(app::ExeDir() / "hd_textures.bin", std::ios::binary);
    if (!f) f.open(app::ExeDir() / "data" / "hd_textures.bin", std::ios::binary);
    char head[8];
    if (!f.read(head, 8) || std::memcmp(head, "HDT1", 4)) return;
    uint32_t count;
    std::memcpy(&count, head + 4, 4);
    for (uint32_t i = 0; i < count; i++) {
        char name[33] = {};
        uint32_t v[3], size[2];
        uint64_t hash;
        if (!f.read(name, 32) || !f.read(reinterpret_cast<char*>(v), 12) || !f.read(reinterpret_cast<char*>(&hash), 8) ||
            !f.read(reinterpret_cast<char*>(size), 8) || !size[0] || !size[1] || size[0] > 4096 || size[1] > 4096)
            return;
        auto r = std::make_shared<gpu::TextureReplacement>();
        r->guest_w = v[0];
        r->guest_h = v[1];
        r->n = v[2];
        r->hash = hash;
        r->w = size[0];
        r->h = size[1];
        r->px.resize(size_t(r->w) * r->h * 4);
        if (!f.read(reinterpret_cast<char*>(r->px.data()), std::streamsize(r->px.size()))) return;
        std::string n = name;
        if (n.rfind("ts2banner", 0) == 0) g_hd_header = true;
        if (n == "ts2newlogo") g_hd_logo = true;
        if (n == "xbbuttons") g_hd_buttons = true;  // Redux's, right way up: pad_icons.cpp must not turn A and Y again
        gpu::ReplaceTexture(std::move(r));
    }
    LOGR("frame: %u pictures from hd_textures.bin", count);
}
}  // namespace

// Story's Select Level builder (screen)
X86_EXTERN_FUNC(__imp__sub_00094B70);
X86_FUNC(sub_00094B70) {
    Build(Building::Story);
    __imp__sub_00094B70(ctx, base);
    g_building = Building::None;
}

// Story's Select Difficulty builder (screen)
X86_EXTERN_FUNC(__imp__sub_000A09C0);
X86_FUNC(sub_000A09C0) {
    Build(Building::Story);
    __imp__sub_000A09C0(ctx, base);
    g_building = Building::None;
}

// the main menu's builder (screen)
X86_EXTERN_FUNC(__imp__sub_00099CF0);
X86_FUNC(sub_00099CF0) {
    g_main_menu = kernel::R32(ctx.esp + 4);
    Build(Building::Main);
    g_building_main = true;
    __imp__sub_00099CF0(ctx, base);
    g_building_main = false;
    g_building = Building::None;
    // its menu is laid out in the middle between the bars (the menu's top in the screen a float at +0x1C): down from
    // the logo
    uint32_t m = g_main_box;
    g_main_box = 0;
    if (m && kernel::R32(g_main_menu + kTopEdge) == 0) {
        float y = F(kernel::R32(m + 0x1C));
        kernel::W32(m + 0x1C, U(y + kMainMenuDown));
        static int logged = 0;
        if (logged++ < 2) LOGR("frame: main menu %08X its box %08X from %.0f to %.0f", g_main_menu, m, y, y + kMainMenuDown);
    }
}

// an item of a screen made (screen, size): the screen's edges moved before its first
X86_EXTERN_FUNC(__imp__sub_000ED390);
X86_FUNC(sub_000ED390) {
    uint32_t s = kernel::R32(ctx.esp + 4);
    if (s == g_main_menu && !g_building_main) g_main_menu = 0;  // its screen built again as another
    Building what = g_building;
    g_building = Building::None;
    if (what == Building::None && game::Opt().menus_wide) what = Building::Other;
    uint32_t top = kernel::R32(s + kTopEdge), bottom = kernel::R32(s + kBottomEdge);
    // only edges as the builders set them: a screen's later items, and the ones built again, keep what was moved
    if (what != Building::None && bottom >= 380 && bottom <= 410) {
        uint32_t new_top = top, new_bottom = kBottom - (410 - bottom);
        if (what == Building::Story && top == 0x50) new_top = kTop;
        if (what == Building::Main && s == g_main_menu) new_top = 0;
        kernel::W32(s + kTopEdge, new_top);
        kernel::W32(s + kBottomEdge, new_bottom);
        static int logged = 0;
        if (logged++ < 8) LOGR("frame: screen %08X bars at %u and %u (were %u and %u)", s, new_top, new_bottom, top, bottom);
    }
    __imp__sub_000ED390(ctx, base);
    if (what == Building::Main && s == g_main_menu) g_main_box = ctx.eax;
}

// the frame draw: the main menu's logo after it, under the screens' menus
void ProfileFooterPatch(X86Context& ctx);  // game/menu.cpp
extern uint32_t g_select_controller_screen;  // game/menu.cpp
X86_EXTERN_FUNC(__imp__sub_00089130);
X86_FUNC(sub_00089130) {
    LoadHd();
    ProfileFooterPatch(ctx);
    // Select Controller is left at once. The screen is made as always (its builder, menu.cpp, marks every player as having
    // a controller) and the first frame that has it as the screen shown does what its update does when every player has
    // one (0x1C3BCF): it is closed (0x87D70), the main menu is gone to (0x87EF0), the profile and storage choice starts
    // (0x9EA80) and the other contexts' screens close (0x1C2C40). It is never drawn.
    if (uint32_t fm = kernel::R32(kFrames); fm && kernel::R32(fm) == 0) {
        uint32_t sc = 0;
        for (uint32_t at : {fm + kCurrent, fm + kGoing}) {
            uint32_t scr = kernel::R32(at);
            if (!sc && scr >= 0x10000 && kernel::R32(scr + 4) == 0x348CF4 && !(kernel::R32(scr + 8) & 2)) sc = scr;
        }
        if (false && sc) {  // the update (menu.cpp) closes it after a short wait
            kernel::CallGuest(ctx, 0x87D70, {sc});
            kernel::CallGuest(ctx, 0x87EF0, {0x2BCA2C});
            kernel::CallGuest(ctx, 0x9EA80, {});
            kernel::CallGuest(ctx, 0x1C2C40, {});
            kernel::CallGuest(ctx, 0x89D70, {sc});
            static int logged = 0;
            if (logged++ < 5) LOGR("menu: Select Controller %08X left in the frame draw", sc);
        }
    }
    // the frame of that screen, going or current, is not drawn either
    uint32_t hidden_at[2] = {0, 0}, hidden[2] = {0, 0};
    if (uint32_t fm = kernel::R32(kFrames)) {
        for (int i = 0; i < 2; i++) {
            uint32_t at = fm + (i ? kGoing : kCurrent), scr = kernel::R32(at);
            if (false && scr >= 0x10000 && (kernel::R32(scr + 4) == 0x348CF4 || kernel::R32(scr + 0x630) == 0x220065)) {
                hidden_at[i] = at;
                hidden[i] = scr;
                kernel::W32(at, 0);
            }
        }
    }
    g_in_frame = true;
    // while Select Controller is current or going nothing of the frame is drawn
    bool was = false;
    if (hidden_at[0] || hidden_at[1]) was = gpu::g_suppress_draws.exchange(true, std::memory_order_relaxed);
    __imp__sub_00089130(ctx, base);
    if (hidden_at[0] || hidden_at[1]) gpu::g_suppress_draws.store(was, std::memory_order_relaxed);
    g_in_frame = false;
    for (int i = 0; i < 2; i++)
        if (hidden_at[i]) kernel::W32(hidden_at[i], hidden[i]);
    // shown as the main menu's part of the change between screens (+0x2F8 its progress 0..1, +0x2F4 the one going),
    // squared so it comes in as the header goes out
    uint32_t frames = kernel::R32(kFrames);
    if (!game::Opt().menus_wide || !g_main_menu || !frames || kernel::R32(g_main_menu + kTopEdge) != 0) return;
    float p = std::clamp(F(kernel::R32(frames + kProgress)), 0.0f, 1.0f);
    float shown = kernel::R32(frames + kCurrent) == g_main_menu ? p * p
                  : kernel::R32(frames + kGoing) == g_main_menu ? (1 - p) * (1 - p)
                                                                 : 0.0f;
    if (shown <= 0) return;
    // the logo's texels as the header draws them (512 by 196 from row 40), 332 by 60 there at 4:3: here 120 high; or
    // Redux's picture laid out as they are, at their own shape and bigger
    float h = 120.0f, w = 332.0f / 60.0f * h * Narrow(), y0 = 22.0f;
    uint32_t v0 = 0x280, v1 = 0xEC0;
    if (g_hd_logo) h = 184.0f, w = 512.0f / 196.0f * h * Narrow(), y0 = 16.0f;
    float x0 = 320.0f - w / 2;
    uint32_t colour = uint32_t(0x7F * shown) << 24 | 0x7F7F7F;
    X86Context saved = ctx;
    uint32_t index = kernel::CallGuest(ctx, kTextureIndex, {kLogo});
    kernel::CallGuest(ctx, kSetTexture, {index});
    kernel::CallGuest(ctx, kBlend, {2});
    kernel::CallGuest(ctx, kQuad, {U(x0), U(y0), U(x0 + w), U(y0 + h), U(1.0f), 0, v0, 0x2000, v1, colour, colour,
                                   colour, colour});
    ctx = saved;
}

// the texture index for a draw: recorded in the frame draw, to know the header's quads
X86_EXTERN_FUNC(__imp__sub_000A9AA0);
X86_FUNC(sub_000A9AA0) {
    if (g_in_frame) g_last_index = kernel::R32(ctx.esp + 4);
    __imp__sub_000A9AA0(ctx, base);
}

// a quad drawn (x0, y0, x1, y1, z, u0, v0, u1, v1, colours): the header's picture and logo at their shape
X86_EXTERN_FUNC(__imp__sub_001B7430);
void HudTrace(const char* what, uint32_t ret, uint32_t a, int words);  // ammo_hud.cpp
X86_FUNC(sub_001B7430) {
    HudTrace("header", kernel::R32(ctx.esp), ctx.esp + 4, 9);
    if (g_in_frame && g_hd_header) {
        uint32_t a = ctx.esp + 4;
        float x0 = F(kernel::R32(a)), x1 = F(kernel::R32(a + 8));
        // the header's picture whole across the width, the logo in it
        if (g_last_index == kernel::R32(kScreenSet) + kPicture && x0 == 0.0f && x1 == 640.0f) {
            kernel::W32(a + 0x18, 0);
            kernel::W32(a + 0x20, 0x1000);
            __imp__sub_001B7430(ctx, base);
            return;
        }
        // and no logo over it
        if (g_last_index == kLogo && x0 == 48.0f && x1 == 380.0f) {
            ctx.esp += 4;  // its ret
            return;
        }
    }
    float k = g_in_frame ? Narrow() : 1.0f;
    if (k < 1.0f) {
        uint32_t a = ctx.esp + 4;
        float x0 = F(kernel::R32(a)), x1 = F(kernel::R32(a + 8));
        if (g_last_index == kernel::R32(kScreenSet) + kPicture && x0 == 0.0f && x1 == 640.0f) {
            float left = 640.0f * (1.0f - k);
            kernel::W32(a, U(left));
            // the gap on the left: the picture's first column of texels across it
            uint32_t args[13];
            for (uint32_t i = 0; i < 13; i++) args[i] = kernel::R32(a + i * 4);
            args[0] = U(0.0f);
            args[2] = U(left + 1.0f);
            args[7] = args[5] + 0x10;
            X86Context saved = ctx;
            kernel::CallGuest(ctx, kQuad, args, 13);
            ctx = saved;
        } else if (g_last_index == kLogo && x0 == 48.0f && x1 == 380.0f) {
            float y0 = F(kernel::R32(a + 4)), y1 = F(kernel::R32(a + 0xC));
            constexpr float kBigger = 1.2f, kLeft = 30.0f;
            kernel::W32(a, U(kLeft));
            kernel::W32(a + 8, U(kLeft + (x1 - x0) * k * kBigger));
            kernel::W32(a + 4, U(y1 - (y1 - y0) * kBigger));
        }
    }
    __imp__sub_001B7430(ctx, base);
}


// The profile list's footer, "(A) Select (B) Back", has no Delete Profile on it (the game builds the prompt and never draws
// it). While the "Select Profile" title is on the screen (its draw, seen in the text draw below), the footer's draw gets a
// second line, "(X) Delete Profile", drawn by the same function a line lower. The footer may come through any of the text
// draws, so each logs its texts (escaped) once and tries it.
static int64_t g_profile_title_ms = 0;
static bool g_footer_extra = false;
static int64_t NowMsFB() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
static std::string EscapedText(uint32_t p) {
    std::string out;
    if (p < 0x10000 || p >= 0xE0000000) return out;
    for (int i = 0; i < 48; i++) {
        uint32_t c = kernel::R32(p + i) & 0xFF;
        if (c == 0) break;
        if (c < 32 || c > 126) {
            char b[8];
            snprintf(b, sizeof b, "\\x%02X", c);
            out += b;
        } else out += char(c);
    }
    return out;
}
// by_ptr: x and y are pointers to ints (0x1ACD10), else the floats themselves (0x1AC990)
static void FooterLine(X86Context& ctx, uint32_t fn, bool by_ptr) {
    uint32_t ret = kernel::R32(ctx.esp);
    uint32_t sp = kernel::R32(ctx.esp + 0xC);
    std::string t = EscapedText(sp);
    static std::set<uint32_t> seen;
    if (seen.size() < 80 && t.find("\\x0B") != std::string::npos) {
        uint32_t key = ret ^ fn;
        for (char c : t) key = key * 31 + uint8_t(c);
        if (seen.insert(key).second) LOGR("texttrace2: %06X from %08X text '%s'", fn, ret, t.c_str());
    }
    if (g_footer_extra || !g_profile_title_ms || NowMsFB() - g_profile_title_ms > 250 || t.rfind("\\x0B" "2 ", 0) != 0) return;
    static uint32_t line = 0, holder = 0;
    if (!line) {
        const std::string localized = menu_text::GameText(441);
        const char* s = localized.c_str();
        line = mem::AllocSystem(32);
        holder = mem::AllocSystem(16);
        if (!line || !holder) return;
        std::memcpy(kernel::HostPtr(line), s, strlen(s) + 1);
    }
    uint32_t font = kernel::R32(0x40BB28);
    float adv = font >= 0x10000 ? F(kernel::R32(font + 0x10)) : 0.0f;
    if (!(adv > 4.0f && adv < 60.0f)) adv = 24.0f;
    uint32_t y2;
    if (by_ptr) {
        kernel::W32(holder, uint32_t(int32_t(kernel::R32(kernel::R32(ctx.esp + 8))) + int32_t(adv)));
        y2 = holder;
    } else {
        y2 = U(F(kernel::R32(ctx.esp + 8)) + adv);
    }
    uint32_t args[4] = {kernel::R32(ctx.esp + 4), y2, line, kernel::R32(ctx.esp + 0x10)};
    X86Context saved = ctx;
    g_footer_extra = true;
    kernel::CallGuest(ctx, fn, args, 4);
    g_footer_extra = false;
    ctx = saved;
    static int logged = 0;
    if (logged++ < 3) LOGR("profiles: Delete Profile line drawn under the footer by %06X, text '%s'", fn, t.c_str());
}
X86_EXTERN_FUNC(__imp__sub_001ACD10);
X86_FUNC(sub_001ACD10) {
    FooterLine(ctx, 0x1ACD10, true);
    __imp__sub_001ACD10(ctx, base);
}
X86_EXTERN_FUNC(__imp__sub_001AC990);
X86_FUNC(sub_001AC990) {
    FooterLine(ctx, 0x1AC990, false);
    __imp__sub_001AC990(ctx, base);
}

// Story's level name with its year (the year, a line break and the name) is drawn by 0x89A18 through 0x1ACA50 (x, y,
// string, ...), called from 0x89CF0, in the font style 0x16 (its letters' scale at +8 and +0xC of the style [0x40BB2C],
// its font's line advance at +0x10 of [0x40BB28], a capital about 0.7 of it high), its top at y. As TS2 Redux has it:
// drawn bigger, its last line resting just over the bottom bar (kBottom).
X86_EXTERN_FUNC(__imp__sub_001ACA50);
X86_FUNC(sub_001ACA50) {
    if (kernel::R32(ctx.esp) != 0x89CF5) {
        // a trace to find the pickup messages' caller: each caller's first call
        static std::set<uint32_t> seen;
        uint32_t ret = kernel::R32(ctx.esp);
        char text[64] = "";
        uint32_t p = kernel::R32(ctx.esp + 0xC);
        if (p >= 0x10000 && p < 0xE0000000) {
            // the control bytes shown as \xNN (the button glyphs: \x0B then the button), so a footer is not an empty string
            int n = 0;
            for (int i = 0; i < 40 && n < 58; i++) {
                uint32_t c = kernel::R32(p + i) & 0xFF;
                if (c == 0) break;
                if (c < 32 || c > 126) n += snprintf(text + n, sizeof text - n, "\\x%02X", c);
                else text[n++] = char(c);
            }
            text[n] = 0;
        }
        // The profile list's footer, "(A) Select (B) Back": the game builds a Delete Profile prompt for it but never draws
        // it. A second line is drawn under it, with the same call, while the "Select Profile" title is on the screen.
        {
            int64_t& title_at = g_profile_title_ms;
            bool& extra = g_footer_extra;
            int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (ret == 0x89965 && menu_text::IsProfileTitle(text)) title_at = now;
            if (!extra && title_at && now - title_at < 250 && !strncmp(text, "\\x0B" "2 ", 6)) {
                static uint32_t line = 0;
                if (!line) {
                    const std::string localized = menu_text::GameText(441);
                    const char* t = localized.c_str();
                    line = mem::AllocSystem(32);
                    if (line) std::memcpy(kernel::HostPtr(line), t, strlen(t) + 1);
                }
                if (line) {
                    uint32_t font = kernel::R32(0x40BB28);
                    float adv = font >= 0x10000 ? F(kernel::R32(font + 0x10)) : 0.0f;
                    if (!(adv > 4.0f && adv < 60.0f)) adv = 24.0f;
                    float y = F(kernel::R32(ctx.esp + 8)) + adv;
                    uint32_t args[4] = {kernel::R32(ctx.esp + 4), U(y), line, kernel::R32(ctx.esp + 0x10)};
                    X86Context saved = ctx;
                    extra = true;
                    kernel::CallGuest(ctx, 0x1ACA50, args, 4);
                    extra = false;
                    ctx = saved;
                    static int logged = 0;
                    if (logged++ < 3) LOGR("profiles: Delete Profile line drawn under the footer at y %.1f", double(y));
                }
            }
        }
        float tx = F(kernel::R32(ctx.esp + 4)), ty = F(kernel::R32(ctx.esp + 8));
        // once per caller and text (the objectives' right side is one call with NEW, COMPLETE...)
        if (seen.size() < 400) {
            uint32_t key = ret;
            for (const char* c = text; *c; c++) key = key * 31 + uint8_t(*c);
            if (seen.insert(key).second) LOGR("texttrace: from %08X x %.1f y %.1f '%s'", ret, tx, ty, text);
        }
        // the pickup messages ("Got the ..."), 0xBBE31's call: lower, as Redux has them, over the ammunition's height
        if (ret == 0xBBE36) kernel::W32(ctx.esp + 8, U(ty + 43.0f));
        // the objectives (the list at the bottom of the screen, called from the HUD at 0xCDDDE: the line on the left;
        // 0xCDF11 and 0xCE093: NEW, OBJECTIVE and COMPLETE on the right): the same size, farther apart as TS2 Redux has
        // them, the left line nearer the left edge (x 85 to 21) and the words on the right nearer the right edge (COMPLETE
        // ends at about 620 of 640)
        if (ret >= 0xCD000 && ret < 0xCF000 && ty > 295.0f && ty < 345.0f && (tx < 300.0f || tx > 430.0f))
            kernel::W32(ctx.esp + 4, U(tx < 300.0f ? tx - 64.0f : tx + 64.0f));
        return __imp__sub_001ACA50(ctx, base);
    }
    constexpr float kBigger = 1.0f, kOverBar = 36.0f, kLeft = 16.0f;
    uint32_t style = kernel::R32(0x40BB2C), font = kernel::R32(0x40BB28);
    uint32_t sx = kernel::R32(style + 8), sy = kernel::R32(style + 0xC);
    int lines = 1;
    const char* text = static_cast<const char*>(kernel::HostPtr(kernel::R32(ctx.esp + 0xC)));
    for (int i = 0; i < 64 && text[i]; i++) lines += text[i] == 10;
    // from the first line's top to the last one's foot
    float letters = (float(lines - 1) + 0.7f) * float(kernel::R32(font + 0x10)) * F(sy) * kBigger;
    kernel::W32(ctx.esp + 4, U(kLeft));
    kernel::W32(ctx.esp + 8, U(float(kBottom) - kOverBar - letters));
    kernel::W32(style + 8, U(F(sx) * kBigger));
    kernel::W32(style + 0xC, U(F(sy) * kBigger));
    __imp__sub_001ACA50(ctx, base);
    kernel::W32(style + 8, sx);
    kernel::W32(style + 0xC, sy);
}
