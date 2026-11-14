// The ammunition display at the screen's bottom right (the bullets and their count), drawn larger and lower, as TS2 Redux
// has it. 0xBF0A0 draws it, and every picture of it, the bullets (0x1B75A0) and the letters of the count (by 0x1ACA50, one
// glyph at a time) end in the quad 0x1B5E80 (x0, y0, x1, y1 floats, u0, v0, u1, v1, colour). The display's right edge and
// bottom are the screen's less the margins the callers leave at 0x4A8D08 and 0x4A8D0C (591 and 431 of 639 x 479): the
// quads drawn inside it grow about that corner and go right and down, bullets, letters, outline and shadow alike.
#include <algorithm>
#include <bit>
#include <chrono>
#include <imgui.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>

#include "app/app.h"
#include "common/options.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

bool FreeCamOn();  // free_cam.cpp

namespace {
constexpr uint32_t kScreen = 0x483F28, kRightMargin = 0x4A8D08, kBottomMargin = 0x4A8D0C;
// as Redux has it (measured on its screenshot against the console layout): a fifth larger, 15 to the right, 25 down
constexpr float kBigger = 1.2f, kRight = 15.0f, kDown = 25.0f;
bool g_active = false, g_hud = false, g_left = false, g_in_ring = false;
constexpr uint32_t kLeftMargin = 0x4A8D00;
std::chrono::steady_clock::time_point g_ring_tick;  // the last time the play's HUD was drawn
float g_ax = 0, g_ay = 0;

float F(uint32_t va) { return std::bit_cast<float>(kernel::R32(va)); }
void SetF(uint32_t va, float v) { kernel::W32(va, std::bit_cast<uint32_t>(v)); }

X86_EXTERN_FUNC(__imp__sub_000BF0A0);
X86_FUNC(sub_000BF0A0) {
    if (FreeCamOn()) return kernel::Return(ctx, 0, 0);  // the free camera shows no HUD
    uint32_t screen = kernel::R32(kScreen);
    // the fifth argument is the second hand's: 0 puts the display at the screen's left edge plus the left margin (0x4A8D00) and
    // its pictures go right from there (the x2 guns' left one), else it is at the right edge, going left
    g_left = kernel::R32(ctx.esp + 0x14) == 0;
    g_ax = g_left ? float(int32_t(kernel::R32(screen)) + int32_t(kernel::R32(kLeftMargin)))
                  : float(int32_t(kernel::R32(screen + 4)) - int32_t(kernel::R32(kRightMargin)));
    g_ay = float(int32_t(kernel::R32(screen + 0x18)) - int32_t(kernel::R32(kBottomMargin)));
    static int logged = 0;
    if (logged++ < 6) LOGR("ammo hud: the %s display's corner at %.0f, %.0f", g_left ? "left" : "right", g_ax, g_ay);
    g_active = true;
    __imp__sub_000BF0A0(ctx, base);
    g_active = false;
}

// The corners above the ammunition: inside the player's HUD (0xC92F0) the panel at the top left (time or score, rank),
// as TS2 Redux has it, is bigger and nearer the corner, and the radar at the top right farther out, up and right.
// Positions are in the 640 x 480 screen; k is the scale given to a text's letters.
bool HudCorner(float& x, float& y, float& k) {
    if (!g_hud || g_active) return false;
    if (x < 230.0f && y < 130.0f) {
        k = 1.3f;
        x = 14.0f + (x - 50.0f) * k;
        y = 24.0f + (y - 47.0f) * k;
        return true;
    }
    if (x > 470.0f && y < 150.0f) {
        k = 1.2f;
        x = 630.0f + (x + 47.0f - 630.0f) * k;
        y = 10.0f + (y - 33.0f - 10.0f) * k;
        return true;
    }
    return false;
}

// The HUD's pictures are made for 4:3 and the 640 x 480 frame is shown stretched to a wide output, so they came out too wide.
// Each picture (the quad 0x1B5E80, which every picture, letter and bar ends in) is narrowed by 4:3 over the shape, and kept
// to the edge it belongs to: those at the left stay at the left edge, those at the right at the right edge, the middle ones
// about the middle, as a wide screen has its HUD at the screen's edges. A run of quads that follow each other (the letters
// of a word) take the first one's edge, so words stay whole. Full width pictures (fades, flashes) stay full width.
struct Run {
    float x1 = -1000, y0 = -1000;
    float anchor = 0;
} g_run;
void HudUnstretch(uint32_t a, bool in_ring) {
    if (!game::Opt().hud_unstretch || !g_hud || in_ring) return;
    float k = (4.0f / 3.0f) / app::Aspect();
    if (k > 0.999f || kernel::R32(0x4A46F0) != 1) return;
    float x0 = F(a), x1 = F(a + 8), y0 = F(a + 4);
    if (!(x1 > x0) || x1 - x0 >= 500.0f) return;
    bool follows = std::abs(x0 - g_run.x1) <= 8.0f && std::abs(y0 - g_run.y0) <= 40.0f;
    if (!follows) {
        float cx = (x0 + x1) * 0.5f;
        g_run.anchor = cx < 200.0f ? 0.0f : cx > 440.0f ? 640.0f : 320.0f;
    }
    float a0 = g_run.anchor;
    g_run.x1 = x1;
    g_run.y0 = y0;
    SetF(a, a0 + (x0 - a0) * k);
    SetF(a + 8, a0 + (x1 - a0) * k);
    // the next letter starts where this one ended, in the game's own measure: the run's reference is the original
    static int logged = 0;
    if (logged++ < 3) LOGR("hud: pictures narrowed by %.3f (4:3 over %.3f) at the edges", k, app::Aspect());
}
// The crosshair (and what else the HUD draws around the middle of the screen) is made square for 4:3 and came out too wide on
// a wide output: its pictures are narrowed about the screen's middle by the same ratio, always (not an option), so it is
// square again. The pictures around the middle only: no full width fades, no ring, not the ammunition display.
void CrosshairUnstretch(uint32_t a) {
    if (!g_hud || g_in_ring || g_active || kernel::R32(0x4A46F0) != 1) return;
    float k = (4.0f / 3.0f) / app::Aspect();
    if (k > 0.999f) return;
    float x0 = F(a), y0 = F(a + 4), x1 = F(a + 8), y1 = F(a + 12);
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    if (!(x1 > x0) || x1 - x0 > 220.0f || y1 - y0 > 220.0f || std::abs(cx - 320.0f) > 110.0f || std::abs(cy - 240.0f) > 110.0f) return;
    if (game::Opt().hud_unstretch) return;  // the whole HUD is narrowed then (HudUnstretch)
    SetF(a, 320.0f + (x0 - 320.0f) * k);
    SetF(a + 8, 320.0f + (x1 - 320.0f) * k);
}
}  // namespace

// the player's HUD (0xC92F0, inside a larger recompiled function: marked at its start and at its three exits)
bool FreeCamOn();  // free_cam.cpp
void HudBegin(X86Context&, uint8_t*) {
    g_hud = true;
    g_ring_tick = std::chrono::steady_clock::now();  // the play's HUD is up (the ring is not asked for every frame)
    if (FreeCamOn()) gpu::g_suppress_draws = true;  // the free camera shows no HUD
}
void HudEnd(X86Context&, uint8_t*) {
    g_hud = false;
    gpu::g_suppress_draws = false;
}
// the returns of the small functions the HUD calls on its way (0xC6410, 0xC6430): the HUD goes on after them, so what is
// dropped for the free camera stays dropped
void HudSubEnd(X86Context&, uint8_t*) {
    g_hud = false;
}

// To find where the health and shield arcs, the crosshair and the objectives are drawn: each picture call's caller and
// texture (the last 0x1B7440 set), its arguments as raw words and as floats, once per caller and texture ("hudtrace" in
// the log)
namespace {
uint32_t g_tex = 0;
}
void HudTrace(const char* what, uint32_t ret, uint32_t a, int words) {
    static std::set<uint64_t> seen;
    if (!g_hud) return;  // the player's HUD only: the menus filled the trace before
    if (seen.size() >= 300 || !seen.insert(uint64_t(ret) << 32 | g_tex).second) return;
    char line[400];
    int n = std::snprintf(line, sizeof line, "hudtrace: %s from %08X tex %08X %s:", what, ret, g_tex,
                          g_hud ? "in the HUD" : "elsewhere");
    for (int i = 0; i < words && n < 360; i++) {
        uint32_t w = kernel::R32(a + 4 * i);
        n += std::snprintf(line + n, sizeof line - n, " %X(%g)", w, double(std::bit_cast<float>(w)));
    }
    LOGR("%s", line);
}

// The crosshair (0xC83B0, drawn when the player's option [[0x4B2F60]+0x21C] bit 2 is set): only when the player's flag
// [player+0x884] says so. That flag is set at 0x146A44 to 1 for a state of the player (+0x158) other than 4, 7 and 0xE with
// bit 2 of +0xC set, else 0: the crosshair showed only then. With the reticle_always option the flag is held at 1 for the
// draw (and put back after) in the states where the game does not take the crosshair away (4, 7 and 0xE), as Boston's "Always
// show reticle" and TS2 Redux's option do.
X86_EXTERN_FUNC(__imp__sub_000C83B0);
X86_FUNC(sub_000C83B0) {
    uint32_t player = kernel::R32(0x497100);
    uint32_t old = 0, old_state = 0;
    bool forced = false;
    if (game::Opt().reticle_always && player > 0x10000 && !FreeCamOn()) {
        // the aiming reticle always, with no exception (any weapon, any state, the auto aim's lock or not): the game draws
        // that one when the state is 8 (0xC8AF3) and the flag is set, so the draw sees the player aiming, and puts both back
        old = kernel::R32(player + 0x884);
        old_state = kernel::R32(player + 0x158);
        kernel::W32(player + 0x884, 1);
        kernel::W32(player + 0x158, 8);
        forced = true;
    }
    __imp__sub_000C83B0(ctx, base);
    if (forced) {
        kernel::W32(player + 0x884, old);
        kernel::W32(player + 0x158, old_state);
    }
}

X86_EXTERN_FUNC(__imp__sub_001B7440);
X86_FUNC(sub_001B7440) {
    g_tex = kernel::R32(ctx.esp + 4);
    __imp__sub_001B7440(ctx, base);
}

// The health and shield arcs (the player's status ring, drawn by 0xC4F10: its glow as four quarter pictures by 0x1B75A0
// with x0, y0, x1, y1 in the 640 x 480 screen, its bars as six vertices of x, y, z, colour, u, v written into the push
// buffer [0x47C61C] and ended by the call at 0xC5A4E). The ring is made for 4:3, so on a wide screen its width is stretched
// and it looks squat: its height is raised by the same ratio about the screen's middle, as TS2 Redux has it (the same width
// at the screen's edges, taller). Only with one player: the split screens' rings are inside their own viewports.
namespace {
bool g_menu_ring = false;
float ArcStretch() {
    if (!game::Opt().arcs_tall || kernel::R32(0x4A46F0) != 1) return 1.0f;
    // the pause menu and the screens after a mission draw the same ring (their call is at 0xD1E8A, the play's at 0xC92DA)
    // with their own outline and background, made for the ring as it is: left alone
    if (g_menu_ring) return 1.0f;
    return std::max(1.0f, app::Aspect() / (4.0f / 3.0f));
}
// The ring at the original shape (the arcs_tall option off, as TS2PC has it): the 640 x 480 screen is stretched to the wide
// output, so the ring is squeezed back in x about the screen's middle by the stretch, a 4:3 ring in the middle of the screen
float ArcSquash() {
    if (game::Opt().arcs_tall || kernel::R32(0x4A46F0) != 1 || g_menu_ring) return 1.0f;
    return std::max(1.0f, app::Aspect() / (4.0f / 3.0f));
}
// Each side of the ring, at its 4:3 size, is moved out towards its corner by this much (console pixels) of the room the
// wide screen adds: slightly past where 4:3 puts it, the same dimensions
float ArcSpread(float squash) {
    constexpr float kShare = 0.25f;
    return squash > 1.001f ? (320.0f - 320.0f / squash) * kShare : 0.0f;
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000C4F10);
X86_FUNC(sub_000C4F10) {
    g_menu_ring = kernel::R32(ctx.esp) == 0xD1E8F;
    g_in_ring = !g_menu_ring;
    if (!g_menu_ring) g_ring_tick = std::chrono::steady_clock::now();
    if (!g_menu_ring && game::Opt().ts1_bars && kernel::R32(0x4A46F0) == 1) {  // the bar takes the arcs' place
        g_in_ring = false;
        return kernel::Return(ctx, 0, 0);
    }
    __imp__sub_000C4F10(ctx, base);
    g_menu_ring = false;
    g_in_ring = false;
}

// The TimeSplitters 1 style health bar (the ts1_bars option): drawn over the game at the bottom left, only while the play's
// ring is being asked for (so not in menus, cutscenes or the free camera) and with one player. The player's object keeps its
// health at +0x19C and its most at +0x1A0. Where the shield is kept is not known yet: the words around the health are logged
// as they change, to find it.
void Ts1BarsDraw() {
    if (!game::Opt().ts1_bars || kernel::R32(0x4A46F0) != 1 || FreeCamOn()) return;
    auto since = std::chrono::steady_clock::now() - g_ring_tick;
    if (since > std::chrono::milliseconds(150)) return;
    uint32_t players = kernel::R32(0x497028);
    if (players < 0x10000) return;
    uint32_t obj = kernel::R32(players + 0x1BC);
    if (obj < 0x10000 || (obj & 3)) return;
    float health = F(obj + 0x19C), most = F(obj + 0x1A0);
    if (!(most > 0.0f && most < 10000.0f)) return;
    {
        static uint32_t last[12];
        static int logged = 0;
        for (int i = 0; i < 12 && logged < 60; i++) {
            uint32_t v = kernel::R32(obj + 0x190 + i * 4);
            if (v != last[i]) {
                if (last[i] || logged) LOGR("ts1 bar: object word +%X: %08X -> %08X (%.3f)", 0x190 + i * 4, last[i], v, double(std::bit_cast<float>(v)));
                last[i] = v;
                logged++;
            }
        }
    }
    // the shield: the character's (object +0x124) two floats at +0x10 and +0x14 are what the HUD's flash test (0xC4D60)
    // compares against a share of the other, as it does the health with its most: taken as the shield and its most
    float shield = 0.0f, shield_most = 0.0f;
    uint32_t chr = kernel::R32(obj + 0x124);
    if (chr > 0x10000 && !(chr & 3)) {
        shield = F(chr + 0x10);
        shield_most = F(chr + 0x14);
        static float last[2];
        static int logged = 0;
        if ((shield != last[0] || shield_most != last[1]) && logged++ < 30)
            LOGR("ts1 bar: character %08X +10 %.3f +14 %.3f", chr, double(shield), double(shield_most));
        last[0] = shield;
        last[1] = shield_most;
    }
    float f = std::clamp(health / most, 0.0f, 1.0f);
    float s = shield_most > 0.0f && shield_most < 10000.0f ? std::clamp(shield / shield_most, 0.0f, 1.0f) : 0.0f;
    // as the TS1 HUD: at the top left, the health bar solid green, the shield bar under it in a grey frame, filled blue.
    // What is lost shows as a black piece with a white outline that fades (the bar's ghost holds a moment, then follows)
    ImVec2 size = ImGui::GetIO().DisplaySize;
    float h = size.y * 0.032f, w = size.y * 0.39f, x = size.y * 0.049f, y = size.y * 0.025f, gap = size.y * 0.014f;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    double now = ImGui::GetTime();
    struct Ghost { float shown = 1.0f, last = 1.0f; double lost = -10.0; bool init = false; };
    static Ghost gh[2];
    auto bar = [&](Ghost& g, float v, float by, ImU32 fill, ImU32 frame, float frame_w) {
        if (!g.init) { g.init = true; g.shown = g.last = v; }
        if (v < g.last - 0.002f) {
            if (now - g.lost > 1.0) g.shown = g.last;  // a new loss starts from where the bar was
            g.lost = now;
        }
        g.last = v;
        float age = float(now - g.lost);
        if (age > 0.35f) g.shown = std::max(v, g.shown - float(ImGui::GetIO().DeltaTime) * 0.6f);
        if (v > g.shown || age > 1.2f) g.shown = v;
        float to = x + w * v, ghost_to = x + w * std::max(v, g.shown);
        dl->AddRectFilled({to, by}, {x + w, by + h}, IM_COL32(0, 0, 0, 120));  // the empty part: black, see-through
        dl->AddRectFilled({x, by}, {to, by + h}, fill);
        dl->AddRect({x, by}, {x + w, by + h}, frame, 0.0f, 0, frame_w);
        if (g.shown > v + 0.002f) {
            float a = std::clamp(1.0f - std::max(0.0f, age - 0.45f) / 0.8f, 0.0f, 1.0f);
            dl->AddRectFilled({to, by}, {ghost_to, by + h}, IM_COL32(255, 255, 255, int(235 * a)));
            dl->AddRect({to, by}, {ghost_to, by + h}, IM_COL32(255, 255, 255, int(255 * a)), 0.0f, 0, 2.5f);
        }
    };
    bar(gh[0], f, y, IM_COL32(0, 215, 0, 255), IM_COL32(0, 0, 0, 0), 1.0f);
    bar(gh[1], s, y + h + gap, IM_COL32(40, 70, 255, 215), IM_COL32(150, 150, 150, 230), 2.0f);
}

void ArcBars(X86Context&, uint8_t*) {
    float squash = ArcSquash();
    float k = ArcStretch();
    if (k <= 1.001f && squash <= 1.001f) return;
    uint32_t end = kernel::R32(0x47C61C), start = end - 6 * 24;
    bool ndc = true;
    for (int i = 0; i < 6; i++)
        if (std::abs(F(start + i * 24 + 4)) > 2.0f) ndc = false;
    if (squash > 1.001f) {
        for (int i = 0; i < 6; i++) {
            uint32_t at = start + i * 24;
            float x = F(at);
            float side = ArcSpread(squash) * (x < (ndc ? 0.0f : 320.0f) ? -1.0f : 1.0f);
            SetF(at, ndc ? x / squash + side / 320.0f : 320.0f + (x - 320.0f) / squash + side);
        }
        return;
    }
    static int logged = 0;
    if (logged++ < 4) {
        char line[300];
        int n = std::snprintf(line, sizeof line, "arcs: bar vertices (%s):", ndc ? "unit screen" : "pixels");
        for (int i = 0; i < 6; i++)
            n += std::snprintf(line + n, sizeof line - n, " (%.3f %.3f)", double(F(start + i * 24)),
                               double(F(start + i * 24 + 4)));
        LOGR("%s", line);
    }
    for (int i = 0; i < 6; i++) {
        uint32_t at = start + i * 24 + 4;
        float y = F(at);
        SetF(at, ndc ? y * k : 240.0f + (y - 240.0f) * k);
    }
}

X86_EXTERN_FUNC(__imp__sub_001B75A0);
X86_FUNC(sub_001B75A0) {
    uint32_t ret = kernel::R32(ctx.esp);
    HudTrace("rect", ret, ctx.esp + 4, 9);
    if (ret == 0xC5077 || ret == 0xC50A8 || ret == 0xC50D6 || ret == 0xC50F8) {  // the glow's four quarters
        if (float k = ArcStretch(); k > 1.001f)
            for (uint32_t a : {ctx.esp + 8, ctx.esp + 0xC}) {
                float y = float(int32_t(kernel::R32(a)));
                kernel::W32(a, uint32_t(int32_t(std::lround(240.0f + (y - 240.0f) * k))));
            }
    }
    __imp__sub_001B75A0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_001B5E80);
X86_FUNC(sub_001B5E80) {
    HudTrace("quad", kernel::R32(ctx.esp), ctx.esp + 4, 9);
    {
        uint32_t a = ctx.esp + 4;
        float x0 = F(a), y0 = F(a + 4), x1 = F(a + 8), y1 = F(a + 12), k;
        float nx = x0, ny = y0;
        if (g_in_ring) {
            if (float q = ArcSquash(); q > 1.001f) {
                float cx = (x0 + x1) * 0.5f;
                float side = std::abs(cx - 320.0f) < 20.0f ? 0.0f : ArcSpread(q) * (cx < 320.0f ? -1.0f : 1.0f);
                SetF(a, 320.0f + (x0 - 320.0f) / q + side);
                SetF(a + 8, 320.0f + (x1 - 320.0f) / q + side);
                x0 = F(a);
                x1 = F(a + 8);
                nx = x0;
            }
        } else if (HudCorner(nx, ny, k)) {
            SetF(a, nx);
            SetF(a + 4, ny);
            SetF(a + 8, nx + (x1 - x0) * k);
            SetF(a + 12, ny + (y1 - y0) * k);
        }
    }
    if (g_active) {
        uint32_t a = ctx.esp + 4;
        // the left display grows right from its edge, shifted out by what is left of the margin (never off the screen)
        float shift = g_left ? -std::min(kRight, std::max(0.0f, g_ax - 8.0f)) : kRight;
        SetF(a, g_ax + shift + (F(a) - g_ax) * kBigger);
        SetF(a + 4, g_ay + kDown + (F(a + 4) - g_ay) * kBigger);
        SetF(a + 8, g_ax + shift + (F(a + 8) - g_ax) * kBigger);
        SetF(a + 12, g_ay + kDown + (F(a + 12) - g_ay) * kBigger);
    }
    if (game::Opt().no_letterbox && !g_hud) {
        // (from the menu's View Cutscenes the mode is not Story's: no mode asked)
        // the cutscenes' black bars: a black picture across the whole width, at the top or the bottom, 30 to 110 high
        uint32_t a = ctx.esp + 4;
        float x0 = F(a), y0 = F(a + 4), x1 = F(a + 8), y1 = F(a + 12);
        bool black = (kernel::R32(a + 32) & 0xFFFFFF) == 0;
        bool wide = x0 <= 1.0f && x1 >= 638.0f;
        bool top = y0 <= 1.0f && y1 >= 30.0f && y1 <= 110.0f, bottom = y1 >= 478.0f && y0 >= 370.0f && y0 <= 450.0f;
        if (wide && y1 - y0 >= 20.0f && y1 - y0 <= 160.0f && !(black && (top || bottom))) {
            static std::set<uint64_t> seen;  // the full width pictures that are not taken for bars, once each
            uint64_t key = uint64_t(std::lround(y0)) << 40 ^ uint64_t(std::lround(y1)) << 24 ^ (kernel::R32(a + 32) & 0xFFFFFF);
            if (seen.size() < 12 && seen.insert(key).second)
                LOGR("letterbox: not a bar: y %.0f..%.0f colour %08X from %08X tex %08X", double(y0), double(y1), kernel::R32(a + 32), kernel::R32(ctx.esp), g_tex);
        }
        if (black && wide && (top || bottom)) {
            static int logged = 0;
            if (logged++ < 6) LOGR("letterbox: bar y %.0f..%.0f from %08X tex %08X not drawn", double(y0), double(y1), kernel::R32(ctx.esp), g_tex);
            return kernel::Return(ctx, 0, 0);
        }
    }
    HudUnstretch(ctx.esp + 4, g_in_ring);
    CrosshairUnstretch(ctx.esp + 4);
    __imp__sub_001B5E80(ctx, base);
}
