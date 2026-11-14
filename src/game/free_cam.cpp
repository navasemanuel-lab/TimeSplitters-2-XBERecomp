// The free camera: F6 (or Back with the right stick clicked) leaves the body where it stands and flies the view through
// the level, the player unhurt while it is out. Single player in a level only.
//
// The view: the player's camera ([0x4B2F60]) is updated by 0x35930, which places it (+0x27C), works out the direction it
// looks along and the vector the view's roll is made from (+0x304) and ends with the view matrix, LookAt(matrix,
// place, direction, up) (0xE67F0, called at 0x36D47 with the matrix at [esp] and nine floats after it: the place, the
// direction, the up). The matrices the frame is drawn with (view times projection, the frustum) are made from that
// matrix right after, so replacing the call's place, direction and up moves the whole view. The hook runs at the call.
//
// The controls (port 1's, kept from the game while the camera is free): the left stick or WASD moves along the view,
// the right stick, the arrows or the mouse turn it; A (Space) or Space rises, the left stick's click, Ctrl or C sinks; the
// right trigger or Shift fly faster, the left trigger slower.
//
// Neither the HUD (its draws are dropped between its start and its ends, ammo_hud.cpp) nor the damage the body takes
// (infinite_health.cpp asks FreeCamOn) are there while the camera is free.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <set>
#include <vector>

#include "common/controls.h"
#include "common/mouse.h"
#include "common/options.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kCamera = 0x4B2F60;
constexpr uint32_t kLevel = 0x4A46EC;    // the level loaded, the front end's from 100 up
constexpr uint32_t kPlayerCount = 0x4A46F0;
constexpr uint32_t kPlace = 0x27C;  // the camera's place
constexpr uint16_t kLeftThumb = 1 << 6, kRightThumb = 1 << 7, kBackBit = 1 << 5;
constexpr int kA = 0, kLeftTrigger = 6, kRightTrigger = 7;

std::atomic<bool> g_wanted{false};  // asked for by the key
std::atomic<bool> g_on{false};      // flying
std::mutex g_pad_lock;
input::Pad g_pad;  // port 1's controls while flying

struct Fly {
    uint32_t camera = 0;
    float p[3] = {}, yaw = 0, pitch = 0, up = 1.0f;
    std::chrono::steady_clock::time_point at;
} g_fly;

float& F(uint32_t va) { return kernel::G<float>(va); }

bool Valid(uint32_t va, uint32_t size) {
    return va >= 0x10000 && mem::IsCommitted(va) && mem::IsCommitted(va + size - 1);
}

// in a level on its own: not the front end's cameras, not a split screen
bool Allowed() {
    return kernel::R32(kLevel) < 100 && kernel::R32(kPlayerCount) == 1 && mouse::InPlay();
}

// the whole level drawn: the free camera, or the render_all debug option in a level (F1, applied at once)
bool WholeLevel() {
    return g_on || (game::Opt().render_all && kernel::R32(kLevel) < 100);
}

float Axis(int16_t v) {
    float x = float(v) / 32767.0f;
    return std::abs(x) < 0.2f ? 0.0f : x;  // past stick drift
}

bool Key(int vk) { return GetAsyncKeyState(vk) < 0; }

// a frame of flight: the place and angles moved by the controls since the last
void Steer(float dt) {
    input::Pad pad;
    {
        std::lock_guard lock(g_pad_lock);
        pad = g_pad;
    }
    float mx = 0, my = 0;
    mouse::Aiming();
    mouse::TakeMotion(mx, my);
    const game::Options& o = game::Opt();
    float turn = 0.0025f * float(o.mouse_sensitivity) / 100.0f;
    float keys_turn = ((Key(VK_RIGHT) ? 1.0f : 0.0f) - (Key(VK_LEFT) ? 1.0f : 0.0f)) * 1.8f * dt;
    float keys_pitch = ((Key(VK_UP) ? 1.0f : 0.0f) - (Key(VK_DOWN) ? 1.0f : 0.0f)) * 1.5f * dt;
    // the screen's right is direction x up: turning right lowers the yaw
    g_fly.yaw -= mx * turn + Axis(pad.rx) * 2.5f * dt + keys_turn;
    float up_down = my * turn * float(o.mouse_vertical) / 100.0f * (o.invert_mouse ? -1.0f : 1.0f);
    g_fly.pitch = std::clamp(g_fly.pitch - up_down - Axis(pad.ry) * 2.0f * dt + keys_pitch, -1.55f, 1.55f);

    float cy = std::cos(g_fly.yaw), sy = std::sin(g_fly.yaw), cp = std::cos(g_fly.pitch), sp = std::sin(g_fly.pitch);
    float d[3] = {sy * cp, sp, cy * cp}, right[3] = {-cy, 0, sy};
    float speed = 8.0f;
    if (pad.analog[kRightTrigger] > 0x40 || Key(VK_SHIFT)) speed *= 4;
    if (pad.analog[kLeftTrigger] > 0x40) speed *= 0.25f;
    float forward = Axis(pad.ly), side = Axis(pad.lx);
    float rise = (pad.analog[kA] > 0x40 || Key(VK_SPACE) ? 1.0f : 0.0f) -
                 ((pad.buttons & kLeftThumb) || Key(VK_CONTROL) || Key('C') ? 1.0f : 0.0f);
    for (int i = 0; i < 3; i++) g_fly.p[i] += (d[i] * forward + right[i] * side) * speed * dt;
    g_fly.p[1] += rise * speed * dt;
}

}  // namespace

// the camera is free (infinite_health.cpp, ammo_hud.cpp, aim.cpp)
bool FreeCamOn() { return g_on; }

// pad poll, port 1: F6 or Back with the right stick clicked toggles; while flying the controls are the camera's, true
// and the game sees an idle pad (Start stays, to pause)
bool FreeCamPad(input::Pad& pad, bool foreground) {
    static bool key_was = false, combo_was = false;
    bool key = foreground && Key(VK_F6);
    bool combo = (pad.buttons & kBackBit) && (pad.buttons & kRightThumb);
    if ((key && !key_was) || (combo && !combo_was)) g_wanted = !g_wanted;
    key_was = key;
    combo_was = combo;
    if (!g_on) {
        gpu::g_suppress_draws = false;  // a HUD start without its end must not leave the screen empty
        return false;
    }
    {
        std::lock_guard lock(g_pad_lock);
        g_pad = pad;
    }
    uint16_t start = pad.buttons & (1 << 4);
    pad.buttons = start;
    std::fill(std::begin(pad.analog), std::end(pad.analog), uint8_t(0));
    pad.lx = pad.ly = pad.rx = pad.ry = 0;
    return true;
}

// The whole level drawn: the scene's draw (0x1DA30) takes the rooms to draw from the camera's table: each room's entry
// (camera + 0x358 + room * 12: the depth it was reached at through the portals, negative when it was not, then the
// rectangle of the view it shows through, int16 min x, min y, max x, max y) for the rooms 1 to [0x4B33DC], and draws
// those reached whose rectangle is in the viewport ([0x483F28]: left, right at +4, top at +0x14, bottom at +0x18).
// While the camera is free every room is made reached and shown through the whole viewport, so the level is all there
// from anywhere, outside it as well (FP's free camera does the same with its own table).
namespace {
constexpr uint32_t kRoomCount = 0x4B33DC, kViewport = 0x483F28;
constexpr uint32_t kRoomSeen = 0x358, kRoomSeenSize = 12, kDeepest = 0xCC4;

void ShowAllRooms() {
    uint32_t camera = kernel::R32(kCamera), view = kernel::R32(kViewport);
    // the free camera, or the render_all option in a level: every room reached and shown through the whole viewport
    if (!WholeLevel() || !Valid(camera, kDeepest + 4) || !Valid(view, 0x1C))
        return;
    int32_t left = int32_t(kernel::R32(view)), right = int32_t(kernel::R32(view + 4));
    int32_t top = int32_t(kernel::R32(view + 0x14)), bottom = int32_t(kernel::R32(view + 0x18));
    // the entries end where the deepest is kept
    uint32_t rooms = std::min(kernel::R32(kRoomCount), (kDeepest - kRoomSeen) / kRoomSeenSize - 1);
    // The draw (0x1DA30, like 0x1F450 before it) sorts the rooms by their depth into 80 lists (0x34E140 the counts, bytes;
    // 0x34F328 + depth * 200 the rooms, a byte each) and draws them nearest first, then back to front for the see-through
    // pass. The rooms the portals did not reach are put after the deepest reached, 100 to a depth so no list overflows.
    int32_t deepest = int32_t(kernel::R32(camera + kDeepest));
    if (deepest < 0) deepest = 0;
    if (deepest > 0x4E) deepest = 0x4E;
    int32_t last = deepest;
    uint32_t added = 0;
    for (uint32_t room = 1; room <= rooms; room++) {
        uint32_t e = camera + kRoomSeen + room * kRoomSeenSize;
        if (int32_t(kernel::R32(e)) < 0) {
            int32_t d = std::min<int32_t>(deepest + 1 + int32_t(added++ / 100), 0x4F);
            kernel::W32(e, uint32_t(d));
            last = std::max(last, d);
        }
        kernel::G<int16_t>(e + 4) = int16_t(left);
        kernel::G<int16_t>(e + 6) = int16_t(top);
        kernel::G<int16_t>(e + 8) = int16_t(right);
        kernel::G<int16_t>(e + 10) = int16_t(bottom);
    }
    kernel::W32(camera + kDeepest, uint32_t(last));
}

// What the rooms' screen rectangles are also used for: 0x1BF10, each frame, marks for each player the rooms whose rectangle
// is on the screen (a byte at [0x4B2F6C] + player * 0xF84 + 0xCC8 + room) and for the level the rooms any player sees
// (a byte at [0x4B33E4] + room). The effects (particles, glares, decals: 0x1A4C20 sets the players' bits of their visible
// mask from those bytes), the rooms' objects' own room (0x14CCA0) and what else reads them leave out whatever is in a room
// that is not marked, however the room's rectangle was made. While the whole level is drawn every room is marked.
constexpr uint32_t kPlayers = 0x4B2F68, kPlayerBase = 0x4B2F6C, kPlayerSize = 0xF84, kRoomSeenByte = 0xCC8, kLevelSeen = 0x4B33E4;

void MarkAllRoomsSeen() {
    if (!WholeLevel()) return;
    uint32_t rooms = std::min<uint32_t>(kernel::R32(kRoomCount), 0xDD);
    uint32_t players = std::min<uint32_t>(kernel::R32(kPlayers), 4);
    uint32_t base = kernel::R32(kPlayerBase), level = kernel::R32(kLevelSeen);
    if (!rooms) return;
    if (Valid(base, players * kPlayerSize))
        for (uint32_t p = 0; p < players; p++)
            for (uint32_t room = 1; room <= rooms; room++) kernel::G<uint8_t>(base + p * kPlayerSize + kRoomSeenByte + room) = 1;
    if (Valid(level, rooms + 1))
        for (uint32_t room = 1; room <= rooms; room++) kernel::G<uint8_t>(level + room) = 1;
}
// Objects that are in no room: the game lists an object by its room (+0x14, 0x14E060 for the draw lists) and an object
// that overlaps none of the rooms' boxes (the helicopter in the sky, whatever stands outside the level) has room 0, which is
// never drawn, so it is gone however the rooms are made to show. While the whole level is drawn those objects are put in
// the player's room for the draw (room, its list of one, +0x14, +0xCC, +0xF4) and given back as they were when the frame's
// draw ends, so the game's own logic never sees the change.
struct Roomless {
    uint32_t obj, room, list, count;
};
std::vector<Roomless> g_roomless;

constexpr uint32_t kObjects = 0x3EC504, kObjectCount = 0x494C0C, kObjectSize = 0x288;

void RestoreRoomless() {
    for (const Roomless& r : g_roomless) {
        kernel::W32(r.obj + 0x14, r.room);
        kernel::W32(r.obj + 0xCC, r.list);
        kernel::W32(r.obj + 0xF4, r.count);
    }
    g_roomless.clear();
}

// The vehicles (props of type 0x100: the helicopter in the sky) are drawn from the player's room in play as well, whatever room
// the game thinks they are in: the rooms reached through the portals are the ones it draws, and a helicopter high above the
// level or in a room the portals do not reach is never in them.
constexpr uint32_t kVehicleType = 0x100;

void PlaceRoomless() {
    bool whole = WholeLevel();
    if (!g_roomless.empty() || (!whole && kernel::R32(kLevel) >= 100)) return;
    uint32_t camera = kernel::R32(kCamera), objects = kernel::R32(kObjects), n = kernel::R32(kObjectCount);
    if (!Valid(camera, 0x298) || n > 4096 || !Valid(objects, n * kObjectSize)) return;
    uint32_t rooms = kernel::R32(kRoomCount), room = kernel::R32(camera + 0x294);
    if (room < 1 || room > rooms) room = 1;
    if (!rooms) return;
    bool type2 = kernel::R32(0x2B7764) != 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o = objects + i * kObjectSize, flags = kernel::R32(o + 0x1C);
        uint32_t at = kernel::R32(o + 0x14);
        bool in_room = kernel::R32(o + 0xF4) != 0 && ((at >= 1 && at <= rooms) || at == 0xDE);
        bool vehicle = kernel::R32(o + 0xC) == kVehicleType;
        if (!whole && !vehicle) continue;
        if ((flags & 0xC0000001) != 1 || at == 0xDE) continue;  // not active, a child, or in the global list
        if (in_room && !vehicle) continue;
        if (!type2 && kernel::R32(o + 0xC) == 2) continue;
        g_roomless.push_back({o, at, kernel::R32(o + 0xCC), kernel::R32(o + 0xF4)});
        kernel::W32(o + 0x14, room);
        kernel::W32(o + 0xCC, room);
        kernel::W32(o + 0xF4, 1);
    }
    static uint32_t last = 0xFFFFFFFF;
    if (g_roomless.size() != last && last != 0xFFFFFFFE) {
        LOGR("whole level: %zu objects in no room drawn from room %u", g_roomless.size(), room);
        last = uint32_t(g_roomless.size());
    }
}
}  // namespace

// The objects drawn from a room (0x14E0F0(room) is called for each, before 0x14E6F0 draws them): it works each object's
// box through the camera and sets +0x1C's bit 0x800000 (culled) on the ones the view's rectangle and frustum leave out, and
// 0x14E6F0 draws none that has it. The corners of a box are taken through |w|, so an object that is far away or at an angle
// the test mistakes (the helicopter in the sky shows from one direction and not from the next) is culled whatever the
// rooms were made to show. While the whole level is drawn the flag is taken off after the test: the graphics card clips
// what is outside the view anyway.
X86_EXTERN_FUNC(__imp__sub_0014E0F0);
X86_FUNC(sub_0014E0F0) {
    uint32_t room = kernel::R32(ctx.esp + 4);
    bool whole = WholeLevel();
    bool all = room <= 0xDE && (whole || kernel::R32(kLevel) < 100);
    uint32_t count = all ? kernel::R32(0x496920 + room * 4) : 0, list = all ? kernel::R32(0x496CA0 + room * 4) : 0;
    if (count > 1024 || !Valid(list, count * 4)) count = 0;
    // the object's own mask of the players it is visible to (+0x30, a bit a player): 0x12A370 draws nothing for a player
    // whose bit is not in it, so it is set for the player drawing (0x14DEA0 asks the same before it makes the transforms)
    uint32_t camera = kernel::R32(kCamera), bit = Valid(camera, 0x218) ? 1u << (kernel::R32(camera + 0x214) & 31) : 0;
    for (uint32_t i = 0; i < count && bit; i++) {
        uint32_t o = kernel::R32(list + i * 4);
        if (Valid(o, 0x270) && (whole || kernel::R32(o + 0xC) == kVehicleType)) kernel::W32(o + 0x30, kernel::R32(o + 0x30) | bit);
    }
    __imp__sub_0014E0F0(ctx, base);
    if (!count) return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t o = kernel::R32(list + i * 4);
        if (!Valid(o, 0x270) || (!whole && kernel::R32(o + 0xC) != kVehicleType)) continue;
        uint32_t fl = kernel::R32(o + 0x1C);
        // 0x14E6F0 draws an object inside its own screen rectangle (+0x100 left, +0x104 top, +0x108 right, +0x10C bottom, set as
        // the scissor): the rectangle the test above worked out for a box it mistook is empty or off the screen, so the
        // object was drawn and then clipped away. It is the whole view here, as the game itself leaves an object it does not
        // test (0x14E74D: the view's rectangle and 0x400000)
        uint32_t vp = kernel::R32(0x483F28);
        if (Valid(vp, 0x1C)) {
            auto put = [&](uint32_t at, int32_t v) { F(o + at) = float(v); };
            put(0x100, int32_t(kernel::R32(vp)));
            put(0x104, int32_t(kernel::R32(vp + 0x14)));
            put(0x108, int32_t(kernel::R32(vp + 4)));
            put(0x10C, int32_t(kernel::R32(vp + 0x18)));
            fl |= 0x400000;
        }
        kernel::W32(o + 0x1C, fl & ~0x800000u);
    }
}

X86_EXTERN_FUNC(__imp__sub_0014E060);
X86_FUNC(sub_0014E060) {
    PlaceRoomless();
    __imp__sub_0014E060(ctx, base);
}

// To find what keeps an object from being drawn: the objects 0x12A370 is asked to draw in a frame, and, every two seconds
// of the free camera, the active ones that were not, with what decides it
std::set<uint32_t> g_drawn;
X86_EXTERN_FUNC(__imp__sub_0012A370);
X86_FUNC(sub_0012A370) {
    if (WholeLevel()) {
        uint32_t node = kernel::R32(ctx.esp + 4);
        if (Valid(node, 0x13C)) g_drawn.insert(kernel::R32(node + 0x138));
    }
    __imp__sub_0012A370(ctx, base);
}

std::set<uint32_t> g_meshed;
X86_EXTERN_FUNC(__imp__sub_001BBE90);
X86_FUNC(sub_001BBE90) {
    if (WholeLevel()) {
        uint32_t node = kernel::R32(ctx.esp + 4);
        if (Valid(node, 0x13C)) g_meshed.insert(kernel::R32(node + 0x138));
    }
    __imp__sub_001BBE90(ctx, base);
}

// The vehicles: a level's setup lists them (header + 0x4C, + 0x50 the count, 0x18 each: create flags, object, mode, flags,
// paths, path count; Compound's arcade setup 0x2F0D70 has one, the helicopter, object 345 ob/copter/copter.xbr, flying ten
// sine paths from 0x2F0510) and 0xE17A8 makes each with 0x377C0 -> 0x152D40 when its create flags fit the mode (0xDF210).
// They are props of type 0x100 (+0xC). While the whole level is drawn, every two seconds each one is in the log with what
// decides whether it is drawn: active (+0x1C bit 1) and not a child (bit 0x80000000), culled (0x800000), its room and rooms,
// the players it is visible to (+0x30) and whether the draw (0x12A370) and the meshes (0x1BBE90) took it this frame.
void LogVehicles() {
    uint32_t objects = kernel::R32(kObjects), n = kernel::R32(kObjectCount);
    if (n > 4096 || !Valid(objects, n * kObjectSize)) return;
    static uint32_t lines = 0;
    uint32_t found = 0;
    for (uint32_t i = 0; i < n && lines < 2000; i++) {
        uint32_t o = objects + i * kObjectSize;
        if (kernel::R32(o + 0xC) != 0x100 && kernel::R32(o + 8) != 345) continue;
        found++;
        lines++;
        LOGR("vehicle: #%u type %X object %u room %u rooms %u (%u %u %u) flags %08X mask %08X at %.1f %.1f %.1f drawn %d meshed %d",
             i, kernel::R32(o + 0xC), kernel::R32(o + 8), kernel::R32(o + 0x14), kernel::R32(o + 0xF4), kernel::R32(o + 0xCC),
             kernel::R32(o + 0xD0), kernel::R32(o + 0xD4), kernel::R32(o + 0x1C), kernel::R32(o + 0x30), double(F(o + 0x58)),
             double(F(o + 0x5C)), double(F(o + 0x60)), int(g_drawn.count(o)), int(g_meshed.count(o)));
    }
    static uint32_t last_found = 0xFFFFFFFF;
    if (found != last_found && lines < 2000) {
        lines++;
        LOGR("vehicle: %u in the level's %u props (level %u)", found, n, kernel::R32(kLevel));
    }
    last_found = found;
}

// The rooms' visibility, worked out for the camera each frame after it moved (0xDDE31, 0xDE089, 0xDE0E1): 0x1E8B0 marks every
// room unreached (depth -1), 0x1E9B0 and 0x1EAB0 walk the portals from the camera's room (+0x294) through the ones whose plane
// faces the camera, 0x1EBE0 and 0x1EF00 cut each room's rectangle down to its portals on the screen. With the whole level
// drawn every room is reached and shown through the whole viewport right after, before anything reads the table this frame.
X86_EXTERN_FUNC(__imp__sub_0001F450);
X86_FUNC(sub_0001F450) {
    __imp__sub_0001F450(ctx, base);
    ShowAllRooms();
}

X86_EXTERN_FUNC(__imp__sub_000DE610);
X86_FUNC(sub_000DE610) {
    // the faces the game turns away from the camera are drawn too: a wall seen from outside the level is a one sided face
    gpu::g_no_backface_cull.store(WholeLevel(), std::memory_order_relaxed);
    g_drawn.clear();
    g_meshed.clear();
    __imp__sub_000DE610(ctx, base);
    RestoreRoomless();
    static uint32_t frame = 0, vehicle_frame = 0;
    if (WholeLevel() && (++vehicle_frame % 120) == 0) LogVehicles();
    if (!g_on || (++frame % 120) != 0) return;
    uint32_t objects = kernel::R32(kObjects), n = kernel::R32(kObjectCount), camera = kernel::R32(kCamera);
    if (n > 4096 || !Valid(objects, n * kObjectSize) || !Valid(camera, 0x300)) return;
    LOGR("undrawn: camera at %.0f %.0f %.0f, room %u, player bit %u, %zu drawn", double(F(camera + kPlace)), double(F(camera + kPlace + 4)),
         double(F(camera + kPlace + 8)), kernel::R32(camera + 0x294), kernel::R32(camera + 0x214), g_drawn.size());
    uint32_t shown = 0;
    for (uint32_t o : g_drawn) {
        bool in_table = o >= objects && o < objects + n * kObjectSize;
        if (g_meshed.count(o) && in_table) continue;
        if (!Valid(o, 0x270)) continue;
        LOGR("entered-not-meshed: obj %08X in_table %d meshed %d type %X kind %u room %u flags %08X mask %08X f38 %08X at %.0f %.0f %.0f", o,
             in_table, int(g_meshed.count(o)), kernel::R32(o + 0xC), kernel::R32(o + 8), kernel::R32(o + 0x14), kernel::R32(o + 0x1C),
             kernel::R32(o + 0x30), kernel::R32(o + 0x38), double(F(o + 0x58)), double(F(o + 0x5C)), double(F(o + 0x60)));
    }
    LOGR("meshed: %zu of %zu entered", g_meshed.size(), g_drawn.size());
    for (uint32_t i = 0; i < n && shown < 0; i++) {
        uint32_t o = objects + i * kObjectSize, fl = kernel::R32(o + 0x1C);
        if (!(fl & 1) || g_drawn.count(o)) continue;
        shown++;
        LOGR("undrawn: #%u type %X kind %u room %u rooms %u (%u %u) flags %08X mask %08X f38 %08X f264 %08X c4 %u c8 %u at %.0f %.0f %.0f",
             i, kernel::R32(o + 0xC), kernel::R32(o + 8), kernel::R32(o + 0x14), kernel::R32(o + 0xF4), kernel::R32(o + 0xCC),
             kernel::R32(o + 0xD0), fl, kernel::R32(o + 0x30), kernel::R32(o + 0x38), kernel::R32(o + 0x264), kernel::R32(o + 0xC4),
             kernel::R32(o + 0xC8), double(F(o + 0x58)), double(F(o + 0x5C)), double(F(o + 0x60)));
    }
}

X86_EXTERN_FUNC(__imp__sub_000DDAA0);
X86_FUNC(sub_000DDAA0) {
    __imp__sub_000DDAA0(ctx, base);
    RestoreRoomless();
}

X86_EXTERN_FUNC(__imp__sub_0001BF10);
X86_FUNC(sub_0001BF10) {
    __imp__sub_0001BF10(ctx, base);
    MarkAllRoomsSeen();
}

// The held weapon and its hands: drawn at the end of the scene (0x1DF61) by 0x58510(player), which builds a projection of
// its own (0.15 to 1000, the camera's weapon angle at +0x324) and draws the player's first person objects with it. Not
// drawn while the camera is free.
X86_EXTERN_FUNC(__imp__sub_00058510);
X86_FUNC(sub_00058510) {
    if (g_on) return kernel::Return(ctx, 0, 0);
    __imp__sub_00058510(ctx, base);
}

// The same family's second pass, after the scene's glares: 0x59380 draws the weapon-space list (the tracers and trails)
X86_EXTERN_FUNC(__imp__sub_00059380);
X86_FUNC(sub_00059380) {
    if (g_on) return kernel::Return(ctx, 0, 0);
    __imp__sub_00059380(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_0001DA30);
X86_FUNC(sub_0001DA30) {
    ShowAllRooms();
    MarkAllRoomsSeen();
    __imp__sub_0001DA30(ctx, base);
}

// LookAt(matrix, place, direction, up) of the player's camera, before its call: the free camera's place, direction and up
void FreeCamLookAt(X86Context& ctx, uint8_t*) {
    uint32_t camera = kernel::R32(kCamera);
    if (!Valid(camera, 0x330)) return;
    // To find out whether the levels keep the camera's opening shot Siberia has (the view travelling to the player at the
    // start): each change of the camera's mode ([0x4B2F70], set by 0x31AB0(mode) and the scripts), with the level and
    // where the view is, in the log
    {
        static uint32_t last_mode = 0xFFFFFFFF, last_level = 0xFFFFFFFF, logged = 0;
        uint32_t mode = kernel::R32(0x4B2F70), level = kernel::R32(kLevel);
        if ((mode != last_mode || level != last_level) && level < 100 && logged++ < 400) {
            LOGR("cammode: level %u mode %u (was %u) var %u, view at %.1f %.1f %.1f, target %08X", level, mode,
                 level == last_level ? last_mode : 0xFFFFFFFFu, kernel::R32(0x4B2F68), double(F(ctx.esp + 4)),
                 double(F(ctx.esp + 8)), double(F(ctx.esp + 12)), kernel::R32(camera + 0x2A0));
        }
        last_mode = mode;
        last_level = level;
    }
    bool allowed = Allowed();
    bool want = g_wanted && allowed;
    if (g_wanted && !allowed && kernel::R32(kLevel) >= 100) g_wanted = false;
    uint32_t args = ctx.esp + 4;  // the place, then the direction and the up
    auto now = std::chrono::steady_clock::now();
    if (want && (!g_on || g_fly.camera != camera)) {
        // taken from where the game's view is
        for (int i = 0; i < 3; i++) g_fly.p[i] = F(args + i * 4);
        float d[3] = {F(args + 12), F(args + 16), F(args + 20)};
        float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 1e-4f) {
            g_fly.yaw = std::atan2(d[0], d[2]);
            g_fly.pitch = std::asin(std::clamp(d[1] / len, -1.0f, 1.0f));
        }
        g_fly.up = F(args + 28) < 0.0f ? -1.0f : 1.0f;
        g_fly.camera = camera;
        g_fly.at = now;
        {
            std::lock_guard lock(g_pad_lock);
            g_pad = {};
        }
        {  // what the objects far from the view are, to find what keeps them from being drawn (one list a flight)
            uint32_t objects = kernel::R32(kObjects), n = kernel::R32(kObjectCount), shown = 0, active = 0;
            if (n <= 4096 && Valid(objects, n * kObjectSize)) {
                for (uint32_t i = 0; i < n; i++) {
                    uint32_t o = objects + i * kObjectSize, fl = kernel::R32(o + 0x1C);
                    if (fl & 1) active++;
                }
                for (uint32_t i = 0; i < n && shown < 120; i++) {
                    uint32_t o = objects + i * kObjectSize, fl = kernel::R32(o + 0x1C);
                    float dx = F(o + 0x58) - g_fly.p[0], dy = F(o + 0x5C) - g_fly.p[1], dz = F(o + 0x60) - g_fly.p[2];
                    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (!(dist > 150.0f && dist < 1e7f)) continue;
                    shown++;
                    LOGR("objfar: #%u type %X kind %u room %u rooms %u flags %08X mask %08X f38 %08X f264 %08X at %.0f %.0f %.0f dist %.0f",
                         i, kernel::R32(o + 0xC), kernel::R32(o + 8), kernel::R32(o + 0x14), kernel::R32(o + 0xF4), fl,
                         kernel::R32(o + 0x30), kernel::R32(o + 0x38), kernel::R32(o + 0x264), double(F(o + 0x58)),
                         double(F(o + 0x5C)), double(F(o + 0x60)), double(dist));
                }
                LOGR("objfar: %u objects, %u active, %u far shown", n, active, shown);
            }
        }
        LOGR("freecam: on at %.2f %.2f %.2f, direction %.2f %.2f %.2f, up %.2f %.2f %.2f", g_fly.p[0], g_fly.p[1],
             g_fly.p[2], d[0], d[1], d[2], F(args + 24), F(args + 28), F(args + 32));
    } else if (!want && g_on) {
        LOGR("freecam: off");
    }
    g_on = want;
    gpu::g_trace_draws = want;  // the log gets the title's call stack of each kind of draw ("fcdraw:") while it flies
    if (!g_on) return;
    float dt = std::clamp(std::chrono::duration<float>(now - g_fly.at).count(), 0.0f, 0.1f);
    g_fly.at = now;
    Steer(dt);
    float cy = std::cos(g_fly.yaw), sy = std::sin(g_fly.yaw), cp = std::cos(g_fly.pitch), sp = std::sin(g_fly.pitch);
    float d[3] = {sy * cp, sp, cy * cp};
    // straight up, without the sway the game's camera breathes with
    float u[3] = {0.0f, g_fly.up, 0.0f};
    for (int i = 0; i < 3; i++) {
        F(args + i * 4) = g_fly.p[i];
        F(args + 12 + i * 4) = d[i];
        F(args + 24 + i * 4) = u[i];
        F(camera + kPlace + i * 4) = g_fly.p[i];
    }
}

// The player camera's projection, 0x32D90(near, far, aspect scale, fov): the game gives it 0.15 to 1000 (0x34827 and 0x35A11),
// so what is further than 1000 is cut away, in play and from the free camera alike. With render_all the far plane is pushed out
// (with the depth's range the near plane sets, a far plane that much further costs next to no precision) and the fog is off
// (gpu::g_no_fog), so the whole level shows from anywhere.
X86_EXTERN_FUNC(__imp__sub_00032D90);
X86_FUNC(sub_00032D90) {
    // the far plane is out past the sky in play (a helicopter high above the level was cut at 1000), all of it with render_all
    float& far_plane = F(ctx.esp + 8);
    far_plane = game::Opt().render_all ? 10000000.0f : std::max(far_plane, 20000.0f);
    __imp__sub_00032D90(ctx, base);
}
