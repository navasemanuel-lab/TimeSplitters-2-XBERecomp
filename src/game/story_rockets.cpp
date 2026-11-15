// Rocket Launcher in every Story level (the story_rockets option): the first player has the Rocket Launcher, its
// ammunition full, from the start of each Story level and after each restart, beside the level's own guns.
//
// A character's guns are reset at 0x42AA0 (character): the fist at +0x984, the 41 entries after it cleared, the 28
// ammunition counts at +0xA2C cleared. The Rocket Launcher is entry 19 of the weapon table (0x2C9BF0, 0x24 bytes an
// entry, +4 its gun); a gun's ammunition type is at +0x10 of the gun table (0x2C3468, 0x240 bytes a gun) and a type's
// most at +0 of 0x2C1678 (0xD0 bytes a type), as 0xAFF50 fills a gun's ammunition. The mode is at 0x4AD80C, 4 in
// Story (0xDFC10 takes the Story level's own gun list there).
//
// The gun is given on the frame after the reset, once the character is the first player's ([[0x497100]+0x1BC]+0x124),
// by writing what a pickup (0xB5E90, 0xB04D0) writes, without its message, sound or change of the gun in hand.
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kMode = 0x4AD80C, kStory = 4;
// The game's mode word is 4 in Challenge's Infiltration levels too, which load as Story levels; the menu's choice
// ([0x35C40C]) tells them apart: 4 for Story, 7 for Challenge (the level load's log has both). The guns are for Story only.
constexpr uint32_t kMenuMode = 0x35C40C, kMenuStory = 4;
bool StoryProper() { return kernel::R32(kMode) == kStory && kernel::R32(kMenuMode) == kMenuStory; }
constexpr uint32_t kGameState = 0x497100;
constexpr uint32_t kWeapons = 0x2C9BF0, kWeaponSize = 0x24;
constexpr uint32_t kGuns = 0x2C3468, kGunSize = 0x240, kGunAmmo = 0x10;
constexpr uint32_t kAmmoMost = 0x2C1678, kAmmoSize = 0xD0, kAmmoTypes = 0x1C;
constexpr uint32_t kOwned = 0x984, kAmmo = 0xA2C;

// the guns given in Story: the weapon table's entry, the models' names and the sounds' names the level must load for it
struct Given {
    uint32_t weapon;
    bool (*enabled)();
    std::vector<const char*> models, sounds;
    const char* name;
};
const Given kGiven[] = {
    {19, [] { return game::Opt().story_rockets; }, {"ob/guns/rocketlauncher", "ob/guns/missile", "ob/ammo/ammorocket"}, {"ROCKET", "MISSILE"}, "rockets"},
};

// the guns whose assets (models, textures, HUD pictures, sounds) the current level asked for when it loaded: a gun turned on
// afterwards (the option changed in the panel, then a restart, which does not load the level again) has none of them, and
// given anyway it was drawn black with the screen black. It is given from the next load on.
bool g_asked[sizeof(kGiven) / sizeof(kGiven[0])] = {};

std::unordered_set<uint32_t> g_reset;  // characters whose guns were reset since the last frame looked

// an address of the guest's main memory, or the same mapped at 0x80000000
bool Mapped(uint32_t va, uint32_t length) {
    if (va < 0x10000 || va + length < va) return false;
    if (!(va + length <= 0x04000000 || (va >= 0x80000000 && va + length <= 0x88000000))) return false;
    // a pointer into the gap between the image and the heap, or a freed block, is in range but not committed
    uint32_t lo = va, hi = va + (length ? length - 1 : 0);
    for (uint32_t pg = lo & ~(mem::kPage - 1); pg <= hi; pg += mem::kPage)
        if (!mem::IsCommitted(pg)) return false;
    return true;
}

uint32_t FirstPlayer() {
    uint32_t state = kernel::R32(kGameState);
    if (!Mapped(state + 0x1BC, 4)) return 0;
    uint32_t player = kernel::R32(state + 0x1BC);
    if (!Mapped(player + 0x124, 4)) return 0;
    uint32_t chr = kernel::R32(player + 0x124);
    return Mapped(chr, kAmmo + kAmmoTypes * 4) ? chr : 0;
}

// A level loads its models, sounds and textures at its start (0x168E60): what its setup asked for, the models by
// 0x149810, the sounds by 0x1C6F50 (those whose record, [0x45188C] + 24 a number, has -1 at +0x10, as 0x1C6BC0 asks),
// the models' textures by 0x149D20. A gun the level has no use for is in none of them: its model was read later from
// gun.pak (gun_pak.cpp) without its textures, black, and its sounds were never loaded. The launcher's are asked for
// before 0x149810 as the level's own are: each object (0x2D89C0, 0x24 bytes an object, its file's name at +0xC)
// named rocketlauncher, missile or ammorocket by 0x14A590 (object), each sound named ...ROCKET... or ...MISSILE... (the
// names at 0x452D18, 0x30 a sound) by -1 in its record. The files come from gun.pak and sounds.pak when the level's paks
// lack them.
constexpr uint32_t kObjects = 0x2D89C0, kObjectSize = 0x24, kObjectCount = 0x5F4, kRequestObject = 0x14A590;
constexpr uint32_t kSoundRecords = 0x45188C, kSoundNames = 0x452D18;

// A gun's own assets are asked for by 0xB0010 (gun) as the level's setup asks for each of its guns': its models, its
// sounds, its missile (rocket1, missile) and its ammunition's HUD textures (the misc table at 0x2BF390, 16 bytes an
// entry, asked for by 0xA9BA0 (entry): ammo_rocket and hudrockets for the launcher). Without the HUD's, the counter's
// rockets and a part of the gun's own display were drawn without a texture, black (the box over the screen).
constexpr uint32_t kAskGun = 0xB0010;

void AskGun(X86Context& ctx, const Given& g) {
    int asked = 0;
    uint32_t gun = kernel::R32(kWeapons + g.weapon * kWeaponSize + 4);
    {
        X86Context saved = ctx;
        kernel::CallGuest(ctx, kAskGun, {gun});
        ctx = saved;
        LOGR("%s: gun %u's assets asked for", g.name, gun);
    }
    for (uint32_t obj = 0; obj < kObjectCount; obj++) {
        uint32_t file = kernel::R32(kObjects + obj * kObjectSize + 0xC);
        if (!Mapped(file, 4)) continue;
        std::string name = kernel::ReadCString(file, 64);
        bool wanted = false;
        for (const char* m : g.models) wanted |= name.find(m) != std::string::npos;
        if (!wanted) continue;
        X86Context saved = ctx;
        kernel::CallGuest(ctx, kRequestObject, {obj});
        ctx = saved;
        if (asked++ < 8) LOGR("%s: object %u %s asked for", g.name, obj, name.c_str());
    }
    uint32_t records = kernel::R32(kSoundRecords);
    if (!Mapped(records, 2000 * 24)) return;
    for (uint32_t id = 1; id < 2000; id++) {
        std::string name = kernel::ReadCString(kSoundNames + id * 0x30, 48);
        if (name.rfind("SFX_", 0) != 0) continue;
        bool wanted = false;
        for (const char* m : g.sounds) wanted |= name.find(m) != std::string::npos;
        if (!wanted) continue;
        kernel::W32(records + id * 24 + 0x10, 0xFFFFFFFF);
        if (asked++ < 16) LOGR("%s: sound %u %s asked for", g.name, id, name.c_str());
    }
}

void Give(uint32_t chr, const Given& g) {
    uint32_t gun = kernel::R32(kWeapons + g.weapon * kWeaponSize + 4);
    uint32_t type = kernel::R32(kGuns + gun * kGunSize + kGunAmmo);
    kernel::W32(chr + kOwned + g.weapon * 4, 1);
    if (type < kAmmoTypes) kernel::W32(chr + kAmmo + type * 4, kernel::R32(kAmmoMost + type * kAmmoSize));
    static int logged = 0;
    if (logged++ < 8) LOGR("%s: weapon %u given to %08X (gun %u, ammunition type %u)", g.name, g.weapon, chr, gun, type);
}

}  // namespace

void InfiniteHealthFrame();  // infinite_health.cpp
void UnusedContentFrame();   // unused_content.cpp
void UnlockFrame(X86Context& ctx);  // unlock_menu.cpp

// the guns' reset (character)
X86_EXTERN_FUNC(__imp__sub_00042AA0);
X86_FUNC(sub_00042AA0) {
    uint32_t chr = kernel::R32(ctx.esp + 4);
    __imp__sub_00042AA0(ctx, base);
    if (game::Opt().story_rockets && chr) g_reset.insert(chr);
}

// the level's models loaded, at its start, before its sounds and textures
X86_EXTERN_FUNC(__imp__sub_00149810);
X86_FUNC(sub_00149810) {
    for (bool& a : g_asked) a = false;
    {  // which kinds of level load here (Story, Arcade, Challenge's Infiltration): the game's mode words and the menu's choice
        static int logged = 0;
        if (logged++ < 40)
            LOGR("level load: mode [4AD80C] %u [4AD810] %u, menu mode [35C40C] %u index [35C418] %u number [35C41C] %u [35C42C] %u, level [4A46EC] %u",
                 kernel::R32(kMode), kernel::R32(0x4AD810), kernel::R32(0x35C40C), kernel::R32(0x35C418), kernel::R32(0x35C41C),
                 kernel::R32(0x35C42C), kernel::R32(0x4A46EC));
    }
    if (StoryProper())
        for (size_t i = 0; i < sizeof(kGiven) / sizeof(kGiven[0]); i++)
            {  // asked for whether the option is on or not: switched on in the panel and then a restart (which does not load
               // the level again), the gun's assets are already there. Nothing is given while the option is off.
                AskGun(ctx, kGiven[i]);
                g_asked[i] = true;
            }
    __imp__sub_00149810(ctx, base);
}

// once a frame, before the frame's clock is read (pace.cpp hooks inside it)
X86_EXTERN_FUNC(__imp__sub_001ADF80);
X86_FUNC(sub_001ADF80) {
    InfiniteHealthFrame();
    UnusedContentFrame();
    UnlockFrame(ctx);
    if (!g_reset.empty()) {
        uint32_t chr = FirstPlayer();
        if (chr && g_reset.count(chr)) {
            if (StoryProper())
                for (size_t i = 0; i < sizeof(kGiven) / sizeof(kGiven[0]); i++)
                    if (kGiven[i].enabled() && g_asked[i]) Give(chr, kGiven[i]);
            g_reset.clear();
        }
        else if (g_reset.size() > 256) g_reset.clear();  // characters that never became the player's
    }
    __imp__sub_001ADF80(ctx, base);
}
