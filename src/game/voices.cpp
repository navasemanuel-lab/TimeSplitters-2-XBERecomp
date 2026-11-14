// Every character's whole voice in Story. The loader of a character type's sounds (0x42860, and the one at 0x53880 that
// counts the same lists) reads up to 18 pain sounds (+0) and 6 death sounds (+0x24) from the type's voice. When the game
// mode ([0x4AD80C]) is 4, Story, it loads half of each for everyone but the players' characters (and one death sound for
// those), to keep the console's memory, so a level's soldiers die with the same one or two cries. The other modes load
// them all: here Story takes that way.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

bool ReadSfx(const std::string& game_name, uint32_t dest, uint32_t max, uint32_t& got);  // sounds.cpp
const std::vector<uint8_t>* SoundPackFor(uint32_t name_va);  // sound_pack.cpp
bool GunPakRead(uint32_t name_va, uint32_t dest, uint32_t max, uint32_t& got);         // gun_pak.cpp

// before the jump that takes Story's halving: do not take it
bool StoryVoicesAll(X86Context&, uint8_t*) {
    static int logged = 0;
    if (logged++ < 2) LOGR("voices: Story loads every pain and death sound, as the other modes do");
    return true;
}

// The loader's whole-list path (the other modes') goes on to the type's last sound (+0x34) but not to the one before it
// (+0x32), which the halved path of Story loads: with every list taken in Story that sound was left out. Loaded here,
// before the +0x34 one, with ebx the type's voice.
void StoryVoiceExtra(X86Context& ctx, uint8_t*) {
    int16_t id = int16_t(kernel::R32(ctx.ebx + 0x32) & 0xFFFF);
    if (!id) return;
    static int logged = 0;
    if (logged++ < 2) LOGR("voices: the extra sound %d of a type loaded as well", id);
    kernel::CallGuest(ctx, 0x1C6BC0, {uint32_t(int32_t(id))});
}

// The voice pool's pause (0x1C6C30, the mask of the voices to pause on the stack, 0x200 the game's): a voice playing is
// paused, but one not started yet (state 0 waiting or 1 about to play, at +0x10 of its 0x2C bytes from 0x451F94) is held
// (+0x13 set, so the voice's step at 0x1C67EA does not start it) until the game resumes. The pause's own sound is asked
// for as the game pauses, so it was held and played only when the game resumed, twice with the resume's. Voices not
// started are let start.
X86_EXTERN_FUNC(__imp__sub_001C6C30);
X86_FUNC(sub_001C6C30) {
    uint32_t mask = kernel::R32(ctx.esp + 4);
    __imp__sub_001C6C30(ctx, base);
    static int logged = 0;
    for (uint32_t voice = 0x451F94; voice + 0x10 <= 0x452A78; voice += 0x2C) {
        uint32_t state = kernel::R16(voice + 0x10);
        if (!kernel::R32(voice) || state > 1 || !(kernel::R32(voice + 0x14) & mask) || !X86_MEM8(voice + 0x13)) continue;
        X86_MEM8(voice + 0x13) = 0;
        if (logged++ < 8) LOGR("sounds: a voice not started (state %u) is not held by the pause", state);
    }
}

// The game's own pause (0x1C6CB0) pauses every voice but the menu's (flag 0x200 at +0x14) the same way: one not started is
// held at +0x13. The pause sound is asked for as the game pauses, so it too waited for the resume.
X86_EXTERN_FUNC(__imp__sub_001C6CB0);
X86_FUNC(sub_001C6CB0) {
    __imp__sub_001C6CB0(ctx, base);
    static int logged = 0;
    for (uint32_t voice = 0x451F94; voice <= 0x452A68; voice += 0x2C) {
        uint32_t state = kernel::R16(voice + 0x10);
        if (!kernel::R32(voice) || state > 1 || (kernel::R32(voice + 0x14) & 0x200) || !X86_MEM8(voice + 0x13)) continue;
        X86_MEM8(voice + 0x13) = 0;
        if (logged++ < 8) LOGR("sounds: a voice not started (state %u, flags %X) is not held by the game's pause", state, kernel::R32(voice + 0x14));
    }
}

// A loaded sound's data begins with its 0x20-byte header (the tag 0x69 of Xbox ADPCM first, the rate at +4).
static bool SoundDataOk(uint32_t data) {
    return data && (kernel::R16(data) == 0x69 || (data >= 0x20 && kernel::R16(data - 0x20) == 0x69));
}

// Diagnostic: every sound the game asks to play (0x1c6460, the sound's number on the stack). A sound whose record
// ([0x45188c] + 24 per number, the data at +0x14) has no data was never loaded: the game starts nothing, so it is silent.
// The log names each one by its SFX_ name (0x452d18, 0x30 per number) and says why it was not played.
X86_EXTERN_FUNC(__imp__sub_001C6460);
X86_FUNC(sub_001C6460) {
    int32_t id = int32_t(kernel::R32(ctx.esp + 4));
    uint32_t record = kernel::R32(0x45188C) + uint32_t(id) * 24;
    bool known = id >= 0 && id < 2000 && kernel::R32(0x45188C);
    uint32_t data = known ? kernel::R32(record + 0x14) : 0, flags = known ? kernel::R16(record + 0xE) : 0;
    uint32_t frame = kernel::R32(0x40BC54), count = kernel::R32(0x451BE4), pool = kernel::R32(0x451BE0);
    bool repeated = false;
    if (frame == kernel::R32(0x349E64))
        for (uint32_t i = 0; i < count && i < 30; i++) repeated |= int32_t(kernel::R32(0x451BE8 + i * 4)) == id;
    __imp__sub_001C6460(ctx, base);
    static int logged = 0, unloaded = 0;
    if (!known) return;
    static int bad = 0;
    if (data && !SoundDataOk(data) && bad++ < 30)
        LOGR("sounds: %d %s plays from memory that is not a sound (data %08X: %08X %08X %08X %08X)", id,
             kernel::ReadCString(0x452D18 + uint32_t(id) * 0x30, 48).c_str(), data, kernel::R32(data), kernel::R32(data + 4),
             kernel::R32(data + 8), kernel::R32(data + 12));
    const char* why = !data ? "NOT LOADED" : ctx.eax == 0xFFFFFFFF ? "not started" : nullptr;
    if (!why) {
        if (logged++ < 80) LOGR("sounds: %d %s plays (flags %X)", id, kernel::ReadCString(0x452D18 + uint32_t(id) * 0x30, 48).c_str(), flags);
        return;
    }
    if (!(flags & 4) && unloaded++ < 600)
        LOGR("sounds: %d %s %s (flags %X, mode %u, %s, %u free voices, sound %u)", id, kernel::ReadCString(0x452D18 + uint32_t(id) * 0x30, 48).c_str(), why,
             flags, kernel::R32(0x4AD80C), repeated ? "already asked this frame" : "first this frame", pool, kernel::R32(0x2BB860));
}

// The PC build loads every sound of sounddata whatever the mode (pc_sound.c's soundPreload reads them all from sfx/). The
// console marks some sounds arcade-only (flag 0x40 in the record's word at +0xE) and Story (mode 4, [0x4AD80C]) does not
// ask for them: among them are the characters' extra pain (hit) and death sounds, which is why Story's soldiers cry out
// with so few. The two tests of that, 0x1AA2D0 (1: take it, as the arcade does) and 0x1AA310 (0: leave it), let Story take
// those, by the sound's name (0x452D18, 0x30 a sound): hit, pain, flinch and death sounds, not the arcade's announcer, its
// countdown, time up, shield and points sounds.
bool StoryTakesSound(int32_t id) {
    if (id < 0 || id >= 2000 || !kernel::R32(0x45188C) || kernel::R32(0x4AD80C) != 4) return false;
    if (!(kernel::R16(kernel::R32(0x45188C) + uint32_t(id) * 24 + 0xE) & 0x40)) return false;
    std::string name = kernel::ReadCString(0x452D18 + uint32_t(id) * 0x30, 48);
    for (char& c : name) c = char(std::toupper(uint8_t(c)));
    for (const char* no : {"GOTTA", "TIME", "COUNT", "RESPAWN", "SHIELD", "PERFECT", "SNARE", "POINT", "SWEEP", "BAG", "ANNOUNC", "HURT"})
        if (name.find(no) != std::string::npos) return false;
    bool taken = false;
    for (const char* yes : {"DIE", "DEATH", "HIT", "PAIN", "FLINCH", "GRUNT", "SCREAM", "GROAN"})
        if (name.find(yes) != std::string::npos) taken = true;
    static int logged = 0, left = 0;
    if (taken ? logged++ < 60 : left++ < 60)
        LOGR("sounds: arcade-only %s (%d) %s in Story", name.c_str(), id, taken ? "taken" : "left out");
    return taken;
}

X86_EXTERN_FUNC(__imp__sub_001AA2D0);
X86_FUNC(sub_001AA2D0) {
    __imp__sub_001AA2D0(ctx, base);
    if (!ctx.eax && StoryTakesSound(int32_t(kernel::R32(ctx.esp + 4)))) ctx.eax = 1;
}

X86_EXTERN_FUNC(__imp__sub_001AA310);
X86_FUNC(sub_001AA310) {
    __imp__sub_001AA310(ctx, base);
    if (!ctx.eax && StoryTakesSound(int32_t(kernel::R32(ctx.esp + 4)))) ctx.eax = 1;
}

// Diagnostic: the voice step (0x1C67C0, eax the voice, 0x2C bytes from 0x451F94: +0 the DirectSound buffer, +0x10 the
// state, +0x12 a delay, +0x13 held, +0x14 flags). A voice that starts goes from state 1 to 3 (state 0 counts a delay down); one that stays in 1 for
// 30 steps is waiting for something (held, or the buffer not ready), and is named in the log with its bytes.
X86_EXTERN_FUNC(__imp__sub_001C67C0);
X86_FUNC(sub_001C67C0) {
    uint32_t voice = ctx.eax;
    uint32_t before = kernel::R16(voice + 0x10);
    __imp__sub_001C67C0(ctx, base);
    static std::unordered_map<uint32_t, int> waiting;
    static int started = 0, stuck = 0;
    uint32_t after = kernel::R16(voice + 0x10);
    if (before == 1 && after == 1) {
        if (++waiting[voice] == 30 && stuck++ < 20)
            LOGR("sounds: voice %08X waits in state 1 (held %u, delay %u, flags %X, buffer %08X)", voice, X86_MEM8(voice + 0x13),
                 X86_MEM8(voice + 0x12), kernel::R32(voice + 0x14), kernel::R32(voice));
    } else {
        waiting.erase(voice);
        if (before == 1 && after == 3 && started++ < 40)
            LOGR("sounds: voice %08X started (flags %X, buffer %08X)", voice, kernel::R32(voice + 0x14), kernel::R32(voice));
    }
}

// Diagnostic: after a level's sounds are loaded (0x1C6F50), each character type that has any of its pain or death
// sounds loaded, with how many of its lists are: the voice of type t is the table 0x2A9C44 (0x44 a type, its voice at
// +0), the pain sounds 18 words from +0, the death sounds 6 from +0x24 (a 0 ends a list), a sound's data at +0x14 of its
// 24-byte record from [0x45188C]. The same lines in Story and in an arcade level show what Story lacks.
X86_EXTERN_FUNC(__imp__sub_001C6F50);
X86_FUNC(sub_001C6F50) {
    __imp__sub_001C6F50(ctx, base);
    uint32_t records = kernel::R32(0x45188C);
    if (!records) return;
    int bad_ids = 0;
    auto loaded = [&](uint32_t list, int max, int& have) {
        int total = 0;
        have = 0;
        for (int i = 0; i < max; i++) {
            int32_t id = int16_t(kernel::R16(list + uint32_t(i) * 2));
            if (!id) break;
            total++;
            if (id > 0 && id < 2000 && kernel::R32(records + uint32_t(id) * 24 + 0x14)) {
                have++;
                if (!SoundDataOk(kernel::R32(records + uint32_t(id) * 24 + 0x14))) bad_ids++;
            }
        }
        return total;
    };
    LOGR("voices: sounds loaded for level %u, mode %u", kernel::R32(0x4A46EC), kernel::R32(0x4AD80C));
    for (uint32_t type = 0; type < 80; type++) {
        uint32_t voice = kernel::R32(0x2A9C44 + type * 0x44);
        if (voice < 0x10000) continue;
        int pain_have, death_have;
        int pain = loaded(voice, 18, pain_have), death = loaded(voice + 0x24, 6, death_have);
        if (!pain_have && !death_have) continue;
        LOGR("voices: type %u pain %d of %d, death %d of %d", type, pain_have, pain, death_have, death);
    }
    LOGR("voices: %d loaded sounds right after the load are not a sound", bad_ids);
}

// Diagnostic: the game's read of a named file from its paks (0x86450 (name, buffer, size)): a sound (.xbs) read that leaves
// no sound header (the tag 0x69) in the buffer is named in the log with what the read returned.
X86_EXTERN_FUNC(__imp__sub_00086450);
static void ReadNamed(X86Context& ctx, uint8_t* base) {
    uint32_t name = kernel::R32(ctx.esp + 4), buffer = kernel::R32(ctx.esp + 8), size = kernel::R32(ctx.esp + 12);
    __imp__sub_00086450(ctx, base);
    // a gun's model or texture the level's paks lack (gun_pak.cpp)
    uint32_t from_gun_pak = 0;
    if (!ctx.eax && GunPakRead(name, buffer, size, from_gun_pak)) {
        ctx.eax = from_gun_pak;
        return;
    }
    static int bad = 0, good = 0;
    std::string n = name >= 0x10000 ? kernel::ReadCString(name, 96) : std::string();
    if (n.size() < 4 || n.compare(n.size() - 4, 4, ".xbs") != 0 || buffer < 0x10000) return;
    if (kernel::R16(buffer) == 0x69) {
        good++;
        return;
    }
    // the game's paks do not hold this sound (a Story level's pak has half of each voice): it is read from sounds.pak,
    // which has every one, as the PC build reads them all from sfx/
    uint32_t got = 0;
    if (!ctx.eax && ReadSfx(n, buffer, size, got)) {
        ctx.eax = got;
        static int taken = 0;
        if (taken++ < 5) LOGR("sounds: %s read from sounds.pak (%u bytes), the game's paks lack it", n.c_str(), got);
        return;
    }
    if (bad++ < 40)
        LOGR("sounds: read of %s into %08X (%u bytes) returned %u and left %08X %08X (%d reads of sounds were fine so far)", n.c_str(), buffer,
             size, ctx.eax, kernel::R32(buffer), kernel::R32(buffer + 4), good);
}

// the read, then a sound a pack replaces over what was read (sound_pack.cpp): of the very size of the original, so the
// game's own sizes and memory are as they were
X86_FUNC(sub_00086450) {
    uint32_t name = kernel::R32(ctx.esp + 4), buffer = kernel::R32(ctx.esp + 8), size = kernel::R32(ctx.esp + 12);
    ReadNamed(ctx, base);
    if (int32_t(ctx.eax) <= 0 || buffer < 0x10000) return;
    if (const auto* sound = SoundPackFor(name)) {
        uint32_t got = std::min<uint32_t>(std::min<uint32_t>(size, ctx.eax), uint32_t(sound->size()));
        std::memcpy(kernel::HostPtr(buffer), sound->data(), got);
        static int n = 0;
        if (n++ < 5) LOGR("soundpack: %u bytes of %s put over the read into %08X (0x86450)", got, kernel::ReadCString(name, 64).c_str(), buffer);
    }
}
