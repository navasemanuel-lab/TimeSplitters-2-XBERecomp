// True 1:1 mouse aim: the mouse turns the view itself, beside the stick's turn.
// The look update (0x148A60) adds the stick to the local player's view angles in degrees, each frame:
//   yaw   += stick.x * [0x497138] * [player+0x8A8] / 60 * [camera+0x324] / 60 * [0x3488A4]   (0x148FE5)
//   pitch += stick.y * [0x49713C] * [player+0x8A8] / 60 * [camera+0x324] / 60 * [0x3488A4]   (0x14907B)
// with the player at [0x497100] and the stick pair at [esi], x right positive and y down positive (the pad's bytes are
// 0x80 plus the stick for X and minus it for Y). The mouse is added to that pair before the multiply, as the stick
// that gives the counts' angle, so the game's own limits and wrapping apply to it unchanged.
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>

#include "app/app.h"
#include "common/controls.h"
#include "common/mouse.h"
#include "common/options.h"
#include "common/weapon_bar.h"
#include "core/log.h"
#include "gpu/shaders.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

float WidenAngle(float deg);  // defined below, outside the anonymous namespace
bool FreeCamOn();             // free_cam.cpp: the free camera takes the mouse

namespace {

constexpr uint32_t kPlayer = 0x497100;       // the player being updated
constexpr uint32_t kCamera = 0x4B2F60;       // its camera
constexpr uint32_t kFrameTime = 0x3488A4;    // the frame's step
constexpr uint32_t kYawSpeed = 0x497138, kPitchSpeed = 0x49713C;
constexpr uint32_t kPitchLimit = 0x497104;  // the most the view looks up or down, degrees
constexpr uint32_t kRamp = 0x8A8;            // the turn's ramp up
constexpr uint32_t kCamScale = 0x324;        // the camera's turn scale
constexpr uint32_t kCamTan = 0x31C;          // tangent of half the view angle
constexpr uint32_t kPadOfPlayer = 0x2CE388;  // the pad each player plays on
constexpr float kDegreesPerCount = 0.10f;    // at sensitivity 100
constexpr float kFactor = 1.0f / 60.0f;

float F(uint32_t va) { return std::bit_cast<float>(kernel::R32(va)); }
void SetF(uint32_t va, float v) { kernel::W32(va, std::bit_cast<uint32_t>(v)); }

// the player the keyboard and mouse play: the one on the first pad
bool LocalPlayer(uint32_t player) {
    if (!player) return false;
    uint32_t id = kernel::R32(player);
    return id < 4 && kernel::R32(kPadOfPlayer + id * 4) == 0;
}

// the view's width against the normal play view's (the game's 60 degrees, widened to the setting): zoomed in, a count
// turns the view less so a movement covers the same part of the screen. The reference is worked out from the setting,
// not the widest view seen on the camera: a wider one seen once (a cutscene's) kept every later frame slower.
float ZoomScale() {
    uint32_t cam = kernel::R32(kCamera);
    if (!cam) return 1.0f;
    float tan = F(cam + kCamTan);
    if (!(tan > 0.01f && tan < 50.0f)) return 1.0f;
    float reference = std::tan(0.5f * WidenAngle(60.0f) * 0.017453293f);
    float scale = std::min(tan / reference, 1.0f);
    mouse::SetZoomed(scale < 0.9f);
    static int logged = 0;
    static float last = 0;
    if (std::abs(scale - last) > 0.05f && logged++ < 40) {
        last = scale;
        LOGR("aim: view tangent %.3f against %.3f normal: counts turn %.2f of the usual (game fov %.1f)", tan, reference,
             scale, F(cam + 0x324));
    }
    return scale;
}

float g_zoom = 1.0f;

// this frame's counts as a stick of the game's turn, or false when there are none or the speed is not known yet
bool Counts(uint32_t speed_va, float& dx, float& dy, float& unit_deg_per_stick, bool pitch) {
    uint32_t player = kernel::R32(kPlayer), cam = kernel::R32(kCamera);
    if (!LocalPlayer(player) || !cam) return false;
    if (FreeCamOn()) return false;  // the free camera turns with the mouse, the body stays
    if (controls::StatusHeld()) {  // the objectives screen: the view stays where it is
        mouse::TakeMotion(dx, dy);
        return false;
    }
    mouse::Aiming();
    g_zoom = ZoomScale();
    float rate = F(speed_va) * F(player + kRamp) * kFactor * F(cam + kCamScale) * kFactor * F(kFrameTime);
    if (!std::isfinite(rate) || std::abs(rate) < 1e-9f) return false;
    unit_deg_per_stick = std::abs(rate);
    (void)pitch;
    return mouse::TakeMotion(dx, dy);
}

float g_pending_y = 0;
float g_mouse_x = 0, g_mouse_y = 0;  // the mouse as the stick that would turn as fast (eased), for the free aim sway

// the pad's stick scaled by its speed setting, and the aiming one when the aim trigger is held
float PadScale(bool vertical) {
    const game::Options& o = game::Opt();
    uint32_t look = vertical ? o.pad_v : o.pad_h, aim = vertical ? o.pad_aim_v : o.pad_aim_h;
    return float(look) / 100.0f * (controls::Aiming() ? float(aim) / 100.0f : 1.0f);
}

}  // namespace

// The game's Auto Lookahead: the look update (0x148A60) levels the view's pitch ([player+0x14C]) back to the horizon
// while the pitch state [player+0x150] is 2 and the game's Auto Lookahead setting ([[player+0x14]+2] bit 8) is set, and sets
// the state to 1 when it is level. Without the option the state is held at 1 before that test, so the view stays where the
// stick or the mouse leaves it. ecx is not used here: eax is the player.
void LookAhead(X86Context& ctx, uint8_t*) {
    if (game::Opt().auto_lookahead) return;
    if (kernel::R32(ctx.eax + 0x150) == 2) kernel::W32(ctx.eax + 0x150, 1);
}

// The crosshair's sway as the view turns (GoldenEye's free aim): 0x144890 sets the target [player+0x88C] (x) and [+0x890] (y)
// to the stick times 0.33 and eases the crosshair's own [+0x894], [+0x898] towards it. At the end of that (0x144A5A) the
// target is scaled by the setting's percent: 0, the crosshair stays centred. The mouse's turn, as the stick that would turn
// as fast, is added to the target first, so the setting gives the mouse the same sway.
void FreeAimSway(X86Context&, uint8_t*) {
    uint32_t player = kernel::R32(kPlayer);
    if (!player) return;
    float k = float(game::Opt().turn_sway) / 100.0f;
    float x = F(player + 0x88C), y = F(player + 0x890);
    if (k > 0 && LocalPlayer(player)) {
        uint32_t flags = kernel::R32(kernel::R32(player + 0x14) + 2);
        x += -g_mouse_x * 0.33f;
        y += g_mouse_y * ((flags & 1) ? 0.33f : -0.33f);
    }
    SetF(player + 0x88C, x * k);
    SetF(player + 0x890, y * k);
}

// before the yaw's multiply: esi is the stick pair
void MouseYaw(X86Context& ctx, uint8_t*) {
    float dx = 0, dy = 0, unit = 0;
    g_pending_y = 0;
    g_mouse_x *= 0.5f;  // eases out without counts
    g_mouse_y *= 0.5f;
    if (LocalPlayer(kernel::R32(kPlayer))) SetF(ctx.esi, F(ctx.esi) * PadScale(false));
    if (!Counts(kYawSpeed, dx, dy, unit, false)) return;
    const game::Options& o = game::Opt();
    const bool aiming = controls::Aiming();
    float step = kDegreesPerCount * float(o.mouse_sensitivity) / 100.0f * g_zoom *
                 (aiming ? float(o.mouse_aim_h) / 100.0f : 1.0f);
    // the pitch is applied at its own point, from the same take
    // mouse up is the stick's pull back (the console's default look is inverted): the invert option gives the stick's
    g_pending_y = dy * float(o.mouse_vertical) / 100.0f * (o.invert_mouse ? 1.0f : -1.0f) * step *
                  (aiming ? float(o.mouse_aim_v) / float(std::max<uint32_t>(o.mouse_aim_h, 1)) : 1.0f);
    float stick = -dx * step / unit;  // the stick's x is right negative here: the mouse right turns right
    g_mouse_x = std::clamp(g_mouse_x + stick * 0.5f, -1.0f, 1.0f);
    SetF(ctx.esi, F(ctx.esi) + stick);
}

// before the pitch's multiply
void MousePitch(X86Context& ctx, uint8_t*) {
    uint32_t player = kernel::R32(kPlayer), cam = kernel::R32(kCamera);
    if (!player || !cam) return;
    if (LocalPlayer(player)) SetF(ctx.esi + 4, F(ctx.esi + 4) * PadScale(true));
    if (g_pending_y == 0) return;
    float rate = F(kPitchSpeed) * F(player + kRamp) * kFactor * F(cam + kCamScale) * kFactor * F(kFrameTime);
    if (!std::isfinite(rate) || std::abs(rate) < 1e-9f) return;
    SetF(ctx.esi + 4, F(ctx.esi + 4) + g_pending_y / std::abs(rate));
    g_mouse_y = std::clamp(g_mouse_y + g_pending_y / std::abs(rate) * 0.5f, -1.0f, 1.0f);
    g_pending_y = 0;
}

// A gun or camera the player controls (control state 8): the look update turns the gun's own angles, not the player's, at
// [gun+0x18] (pitch, minus the stick's y) and [gun+0x20] (yaw, plus the stick's x), the gun's limits (+0x30 to +0x3C)
// holding them. The same stick pair (esi), at 0x148E96 for the pitch and 0x148EC4 for the yaw, with the turn's speed and
// the camera's scale but no ramp of the player's. The mouse is added to that pair as on foot: the pitch comes first.
float g_gun_x = 0;

void MouseGunPitch(X86Context& ctx, uint8_t*) {
    g_gun_x = 0;
    uint32_t player = kernel::R32(kPlayer), cam = kernel::R32(kCamera);
    if (!LocalPlayer(player) || !cam) return;
    SetF(ctx.esi + 4, F(ctx.esi + 4) * PadScale(true));
    float dx = 0, dy = 0;
    if (FreeCamOn()) return;
    if (controls::StatusHeld()) {
        mouse::TakeMotion(dx, dy);
        return;
    }
    mouse::Aiming();
    g_zoom = ZoomScale();
    float unit_y = std::abs(F(kPitchSpeed) * kFactor * F(cam + kCamScale) * kFactor * F(kFrameTime));
    float unit_x = std::abs(F(kYawSpeed) * kFactor * F(cam + kCamScale) * kFactor * F(kFrameTime));
    if (!(unit_y > 1e-9f) || !(unit_x > 1e-9f) || !mouse::TakeMotion(dx, dy)) return;
    const game::Options& o = game::Opt();
    const bool aiming = controls::Aiming();
    float step = kDegreesPerCount * float(o.mouse_sensitivity) / 100.0f * g_zoom *
                 (aiming ? float(o.mouse_aim_h) / 100.0f : 1.0f);
    float pitch = dy * float(o.mouse_vertical) / 100.0f * (o.invert_mouse ? 1.0f : -1.0f) * step *
                  (aiming ? float(o.mouse_aim_v) / float(std::max<uint32_t>(o.mouse_aim_h, 1)) : 1.0f);
    // tested: mouse up must raise the gun, so the stick's y is added as on foot (the fsubr's sign is not the one that shows)
    SetF(ctx.esi + 4, F(ctx.esi + 4) + pitch / unit_y);
    g_gun_x = -dx * step / unit_x;
    static int logged = 0;
    if (logged++ < 3) LOGR("aim: the mouse turns a controlled gun (%.2f, %.2f counts)", dx, dy);
}

void MouseGunYaw(X86Context& ctx, uint8_t*) {
    if (LocalPlayer(kernel::R32(kPlayer))) SetF(ctx.esi, F(ctx.esi) * PadScale(false) + g_gun_x);
    g_gun_x = 0;
}

// the look update's entry: the view is in play, so the cursor is locked in the window and hidden, whatever the branch
// the frame then takes
void MouseInPlay(X86Context& ctx, uint8_t*) {
    weapon_bar::ApplyRequest(ctx);  // a weapon slot key, on the game's thread
    uint32_t player = kernel::R32(kPlayer);
    static int logged = 0;
    bool mine = LocalPlayer(player);
    if (logged < 6) {
        logged++;
        LOGR("aim: look update for player %08X id %u pad %u: %s", player, player ? kernel::R32(player) : 0,
             player && kernel::R32(player) < 4 ? kernel::R32(kPadOfPlayer + kernel::R32(player) * 4) : 99,
             mine ? "the mouse's" : "not the mouse's");
    }
    if (mine) {
        // which control state the player is in, to learn the turrets' and cameras' (0x158: the look update leaves 7, 8 and 9
        // alone); the controlled object's own state is at [[object+0x124]+0xC]
        auto ok = [](uint32_t v) { return v >= 0x10000 && v < 0x8C000000; };
        uint32_t body = kernel::R32(player + 0x1BC), data = ok(body) ? kernel::R32(body + 0x124) : 0;
        uint32_t object = ok(data) ? kernel::R32(data + 0x1444) : 0, odata = ok(object) ? kernel::R32(object + 0x124) : 0;
        uint32_t state = kernel::R32(player + 0x158), seat = kernel::R32(player + 0x998), flags = kernel::R32(player + 0xC);
        uint32_t kind = ok(odata) ? kernel::R32(odata + 0xC) : 0xFFFF;
        static uint32_t last[4] = {0xFFFFFFFF, 0, 0, 0};
        if (state != last[0] || seat != last[1] || kind != last[2] || (flags & 8) != last[3]) {
            last[0] = state, last[1] = seat, last[2] = kind, last[3] = flags & 8;
            LOGR("aim: control state %u, seat %X, object %08X (its state %X), player flags %X", state, seat, object, kind, flags);
        }
        mouse::Aiming();
        // the vertical range: the view is held within plus and minus this many degrees (0x497104, from the game's own
        // setting), raised to nearly straight up and down
        float limit = F(kPitchLimit);
        static bool noted = false;
        if (limit > 1.0f && limit < 89.0f) {
            if (!noted) {
                noted = true;
                LOGR("aim: the game's vertical range of %.1f degrees raised to 89", limit);
            }
            SetF(kPitchLimit, 89.0f);
        }
    }
}

// The projection (0x32D90) makes its aspect, the viewport's width over its height, at 0x32DC8 (on the stack at esp+0xc)
// and builds the matrix (0xE6A00) and the camera's horizontal tangent (+0x320) from it. The game made it for the shape
// it was told, 16:9 or 4:3; the frame shown is stretched to the aspect ratio chosen, so the aspect is made that much
// wider: any ratio, the same vertical angle (Hor+).
// the widening of the game's 60 degrees to the setting (60 to 100)
// (only in play and under the pause screen: the menus' and the cutscenes' cameras keep the game's own angle). The
// level loaded is at 0x4A46EC, the front end's from 100 up: back from a level its station's camera was made while the
// last look update was still recent, a second at the play angle.
float FovFactor() {
    if (kernel::R32(0x4A46EC) >= 100) return 1.0f;
    return mouse::InPlay() || mouse::InPausedWorld() ? float(std::clamp<uint32_t>(game::Opt().fov, 60, 100)) / 60.0f : 1.0f;
}
int logged_fov = 0;

// an angle of the game's camera (degrees) widened to the setting: its 60 degrees to the setting, a scope's zoom (20
// degrees or less) kept as the game makes it, so the scope magnifies as on the console; the angles between, of the zoom
// going in or out, eased from one to the other
float WidenAngle(float deg) {
    float k = FovFactor();
    if (k == 1.0f) return deg;
    float w = std::clamp((deg - 20.0f) / 40.0f, 0.0f, 1.0f);
    return std::min(deg * (1.0f + (k - 1.0f) * w), 150.0f);
}

void ProjectionAspect(X86Context& ctx, uint8_t*) {
    float old = F(ctx.esp + 0xC);
    if (!(old > 0.1f && old < 10.0f)) return;
    // the game is told 4:3 whatever the shown shape (its own wide mode is the dashboard's, not ours)
    const float told = 4.0f / 3.0f;
    static int logged = 0;
    float now = old * app::Aspect() / told;
    if (logged < 4) {
        logged++;
        LOGR("aspect: projection %.3f -> %.3f (shown %.3f, game told %.3f)", old, now, app::Aspect(), told);
    }
    SetF(ctx.esp + 0xC, now);
    // the field of view: the game's 60 degrees, widened to the setting; a scope's zoom kept (WidenAngle)
    float fov = F(ctx.esp + 0x10);
    if (fov > 1.0f && fov < 170.0f) {
        float scaled = WidenAngle(fov);
        static float last = 0;
        if (std::abs(fov - last) > 0.5f && logged_fov++ < 24) {
            last = fov;
            LOGR("fov: projection %.2f -> %.2f degrees", fov, scaled);
        }
        SetF(ctx.esp + 0x10, scaled);
        // the marks laid on surfaces win by a depth offset of the pixel's step, larger by as much as the view widened
        const float rad = 3.14159265f / 360.0f;
        gpu::g_view_widening = std::tan(scaled * rad) / std::tan(fov * rad);
    }
}

// The perspective builder's call (0xE6A00 with the matrix at [esp] and the aspect at [esp+4]) of the passes that take the
// screen's shape, 640 over 480: marks laid on walls and the like. The frame is stretched to the shape chosen, so their
// lens is widened as the camera's, or they float off what they are laid on.
void WidenLens(X86Context& ctx, uint8_t*) {
    float aspect = F(ctx.esp + 4);
    if (!(aspect > 0.1f && aspect < 10.0f)) return;
    SetF(ctx.esp + 4, aspect * app::Aspect() / (4.0f / 3.0f));
    // their angle (radians at esp+8) is widened with the view's, or the marks float off what they are laid on
    float fov = F(ctx.esp + 8);
    const float deg = 180.0f / 3.14159265f;
    if (fov > 0.0f && fov < 3.0f) SetF(ctx.esp + 8, WidenAngle(fov * deg) / deg);
    // their depth is compared with the camera's: the same far plane (free_cam.cpp raises the camera's from 1000 to 20000,
    // and with 1000 here the marks' depth differs from the wall's by more than their offset, so they vanished or blinked)
    float near_plane = F(ctx.esp + 0xC), far_plane = F(ctx.esp + 0x10);
    if (near_plane > 0.0f && near_plane < 10.0f && far_plane > near_plane * 10.0f && far_plane < 20000.0f)
        SetF(ctx.esp + 0x10, 20000.0f);
}
