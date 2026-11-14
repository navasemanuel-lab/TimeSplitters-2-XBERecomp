// Infinite health (the infinite_health option): the players take no damage at all.
//
// Every hit goes through 0x540C0 (message): the message's target object at +0, its damage at +0x1C; a character's
// (type 8 at +0xC of its object) is applied by 0x53E60, another object's by its own handler (+0x16C) or straight off its
// health. A character's object keeps its health at +0x19C and its most at +0x1A0. With the option on a message to a
// player's object is dropped: no health lost, no flinch. The players' states are at [0x497028], 0xD3C bytes each, as
// many as [0x4A46F0], their objects at +0x1BC. What lowers health elsewhere is put back to the most each frame.
#include <cstdint>
#include <cstring>

#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

bool FreeCamOn();  // free_cam.cpp: the body takes no damage while the camera flies

namespace {
constexpr uint32_t kPlayers = 0x497028, kPlayerSize = 0xD3C, kPlayerCount = 0x4A46F0, kObject = 0x1BC;
constexpr uint32_t kHealth = 0x19C, kMost = 0x1A0;

// the title's memory, its system pool and its threads' stacks (a hit's message is on the sender's stack, 0xD0000000 up)
// (a pointer into the gap between the image and the heap, or a freed block, is in range but not committed: read while a
// level loads, the players' table holds such)
bool Mapped(uint32_t va) {
    if (va < 0x10000) return false;
    if (va < 0x04000000 || (va >= 0x80000000 && va < 0x88000000)) return mem::IsCommitted(va);
    return va >= 0xB0000000 && va < 0xE0000000;
}

// the players' states and objects live in the title's memory, never on a stack: an address out in the threads' range is
// a stale pointer there (a freed object is filled with 0xD7, 0xD7D7D994 killed Wild West's load)
bool InHeap(uint32_t va) {
    return va >= 0x10000 && (va < 0x04000000 || (va >= 0x80000000 && va < 0x88000000)) && mem::IsCommitted(va);
}

float F32(uint32_t va) {
    uint32_t v = kernel::R32(va);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// the object of each player, through f
template <class F> void EachPlayer(F f) {
    uint32_t players = kernel::R32(kPlayers);
    uint32_t count = kernel::R32(kPlayerCount);
    if (!InHeap(players) || count > 4) return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t obj = kernel::R32(players + i * kPlayerSize + kObject);
        if (InHeap(obj) && InHeap(obj + kMost + 4) && !(obj & 3)) f(obj);
    }
}

// a message's target is a player's when it is the player's object, or any object that shares its character (+0x124)
bool IsPlayer(uint32_t obj) {
    if (!Mapped(obj)) return false;
    uint32_t chr = Mapped(obj + 0x128) ? kernel::R32(obj + 0x124) : 0;
    bool is = false;
    EachPlayer([&](uint32_t p) { is |= p == obj || (chr && kernel::R32(p + 0x124) == chr); });
    return is;
}

float AsFloat(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// every player's health now, to put back what a hit's handler took
struct Saved { uint32_t obj, health; };
int SaveHealth(Saved* out) {
    int n = 0;
    EachPlayer([&](uint32_t obj) { if (n < 4) out[n++] = {obj, kernel::R32(obj + kHealth)}; });
    return n;
}
}  // namespace

void InfiniteHealthFrame() {
    if (!game::Opt().infinite_health && !FreeCamOn()) return;
    EachPlayer([](uint32_t obj) {
        float health = F32(obj + kHealth), most = F32(obj + kMost);
        if (health > 0.0f && most > 0.0f && most < 10000.0f && health < most) kernel::W32(obj + kHealth, kernel::R32(obj + kMost));
    });
}

// a hit (message): dropped for a player; and what the hit's handler took from a player in some other way is put back
X86_EXTERN_FUNC(__imp__sub_000540C0);
X86_FUNC(sub_000540C0) {
    {   // the hits a player's shots make (the attacker at +4 is the player's object), to see what a gun's shot carries
        static int seen = 0;
        uint32_t m = kernel::R32(ctx.esp + 4);
        uint32_t t = Mapped(m) ? kernel::R32(m) : 0, from = Mapped(m) ? kernel::R32(m + 4) : 0;
        if (seen < 80 && Mapped(m + 0x28) && Mapped(t + 0x10) && Mapped(from + 0x10) && IsPlayer(from)) {
            seen++;
            uint32_t o = kernel::R32(m + 0x10);
            if (Mapped(o + 0x20)) LOGR("hit: object %08X: %08X %08X %08X %08X %08X %08X %08X %08X", o, kernel::R32(o), kernel::R32(o + 4), kernel::R32(o + 8), kernel::R32(o + 0xC), kernel::R32(o + 0x10), kernel::R32(o + 0x14), kernel::R32(o + 0x18), kernel::R32(o + 0x1C));
            LOGR("hit: from %08X ret %08X target %08X type %u health %.1f damage %.2f words %08X %08X %08X %08X %08X %08X %08X %08X %08X", from, kernel::R32(ctx.esp), t, kernel::R32(t + 0xC),
                 F32(t + kHealth), F32(m + 0x1C), kernel::R32(m + 8), kernel::R32(m + 0xC), kernel::R32(m + 0x10), kernel::R32(m + 0x14),
                 kernel::R32(m + 0x18), kernel::R32(m + 0x20), kernel::R32(m + 0x24), kernel::R32(m + 0x28), kernel::R32(m + 0x2C));
        }
    }
    if (game::Opt().one_hit && kernel::R32(0x4AD80C) == 4) {
        // Story: any damage to a character is enough to kill it
        uint32_t m = kernel::R32(ctx.esp + 4);
        uint32_t t = Mapped(m) ? kernel::R32(m) : 0;
        if (Mapped(t) && Mapped(t + kMost + 4) && kernel::R32(t + 0xC) == 8 && F32(m + 0x1C) > 0.0f) {
            static int logged = 0;
            if (logged++ < 4) LOGR("health: one hit, damage %.1f on %08X raised to kill", F32(m + 0x1C), t);
            kernel::W32(m + 0x1C, 0x49742400);  // 1000000.0f
        }
    }
    if (!game::Opt().infinite_health && !FreeCamOn()) return __imp__sub_000540C0(ctx, base);
    uint32_t msg = kernel::R32(ctx.esp + 4);
    uint32_t target = Mapped(msg) ? kernel::R32(msg) : 0;
    bool mine = IsPlayer(target);
    static int logged = 0;
    if (mine && logged++ < 6) LOGR("health: a hit on the player's %08X dropped (damage %.1f)", target, F32(msg + 0x1C));
    if (mine) {
        ctx.esp += 4;  // its ret
        return;
    }
    Saved saved[4];
    int n = SaveHealth(saved);
    __imp__sub_000540C0(ctx, base);
    for (int i = 0; i < n; i++)
        if (F32(saved[i].obj + kHealth) < AsFloat(saved[i].health)) kernel::W32(saved[i].obj + kHealth, saved[i].health);
}

