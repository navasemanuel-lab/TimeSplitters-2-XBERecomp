// Every gun's model in every level. A Story level's pak holds the models of its own guns only; a gun from elsewhere
// (the Rocket Launcher of story_rockets.cpp) asks for a model the level lacks. The model lookup (0x12C820, by name,
// "ob/guns/rocketlauncher_ph.xbr") tries the loaded paks, then reads the file loose (0x86970 (name, flags, size out):
// its size by 0x863D0, a block of the game's heap by 0xE9D60 (size rounded to 16, flags), the bytes by 0x86450), and
// uses what it got without a check: a model in no loaded pak and not on the disc loose read address 4 and crashed.
// gun.pak holds every gun's model, its missile, its pickups and their textures: what the loose read does not find is
// taken from there, into a block of the game's heap as the loose read makes it.
//
// A model's textures are read by name the same way (0x1B7E60 (id, ...): "host0:textures/%04d.xbt", its size by
// 0x863D0, the bytes by 0x86450 into a block of 0xE9D60): a texture the level's pak lacks had no size, was left out, and
// the model was drawn black. Its size and bytes are taken from gun.pak too (GunPakRead for the read, in voices.cpp's
// hook of 0x86450).
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "app/app.h"
#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kAlloc = 0xE9D60;

std::mutex g_lock;
bool g_loaded = false;
int g_loaded_for = 0;  // the options the pak was read for: the beta_reborn and default_gun settings (the panel changes them)
std::vector<uint8_t> g_pak;
struct Entry {
    uint32_t offset, size;
    const std::vector<uint8_t>* pak;
};
std::vector<uint8_t> g_chrinc;  // chrinc.pak, the menu's character models: John Vattic's (chr104) is in no other pak
std::map<std::string, Entry> g_inc;
std::map<std::string, Entry> g_entries;  // lower case "ob/guns/rocketlauncher_ph.xbr"

uint32_t U32(size_t at) {
    uint32_t v = 0;
    if (at + 4 <= g_pak.size()) std::memcpy(&v, g_pak.data() + at, 4);
    return v;
}

// the name as the pak keeps it: lower case, no "host0:"
std::string Lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    if (s.rfind("host0:", 0) == 0) s = s.substr(6);
    return s;
}

// the pak: "P8CK", the directory's offset and count (12 bytes each: name offset, size, data offset), the names' offset
void Load() {
    g_loaded = true;
    g_loaded_for = int(game::Opt().beta_reborn) * 2 + int(game::Opt().default_gun);
    g_pak.clear();
    g_entries.clear();
    bool beta = false;
    if (game::Opt().beta_reborn) {  // the Beta Reborn gun.pak (pak_replace.cpp) is the game's too
        std::ifstream bf(app::ExeDir() / "resourcepacks" / "beta_reborn" / "gun.pak", std::ios::binary);
        if (bf) { g_pak.assign(std::istreambuf_iterator<char>(bf), {}); beta = true; }
    }
    if (!beta && game::Opt().default_gun) {  // the retail gun.pak with the textured Default Gun
        std::ifstream ef(app::ExeDir() / "resourcepacks" / "default_gun" / "gun.pak", std::ios::binary);
        if (ef) { g_pak.assign(std::istreambuf_iterator<char>(ef), {}); beta = true; }
    }
    if ((!beta && !kernel::ReadGameFile("d:\\data\\gun.pak", g_pak)) || g_pak.size() < 20 || std::memcmp(g_pak.data(), "P8CK", 4)) {
        LOGW("gun.pak: not found, a model a level lacks stays missing");
        g_pak.clear();
        return;
    }
    uint32_t dir = U32(4), count = U32(8), names = U32(12);
    for (uint32_t i = 0; i < count; i++) {
        size_t e = size_t(dir) + i * 12;
        uint32_t name = U32(e), size = U32(e + 4), offset = U32(e + 8);
        size_t n = size_t(names) + name;
        if (n >= g_pak.size() || size_t(offset) + size > g_pak.size()) continue;
        const char* s = reinterpret_cast<const char*>(g_pak.data() + n);
        g_entries[Lower(std::string(s, strnlen(s, g_pak.size() - n)))] = {offset, size, &g_pak};
    }
    LOGR("gun.pak: %zu files for the models a level lacks", g_entries.size());
}

// chrinc.pak ("P4CK": the directory's offset, then 60 bytes an entry: name (48), data offset, size, 0): chr104.xbr, the model
// the character table's row of John Vattic points at, is only here (the game's own chr.pak has 1 to 158 but 18 and 104)
void LoadChrinc() {
    if (!kernel::ReadGameFile("d:\\data\\chrinc.pak", g_chrinc) || g_chrinc.size() < 20 || std::memcmp(g_chrinc.data(), "P4CK", 4)) {
        LOGW("chrinc.pak: not found, John Vattic has no model");
        g_chrinc.clear();
        return;
    }
    uint32_t dir = 0, bytes = 0;
    std::memcpy(&dir, g_chrinc.data() + 4, 4);
    std::memcpy(&bytes, g_chrinc.data() + 8, 4);
    for (size_t e = dir; e + 60 <= size_t(dir) + bytes && e + 60 <= g_chrinc.size(); e += 60) {
        const char* n = reinterpret_cast<const char*>(g_chrinc.data() + e);
        uint32_t offset, size;
        std::memcpy(&offset, g_chrinc.data() + e + 48, 4);
        std::memcpy(&size, g_chrinc.data() + e + 52, 4);
        if (size_t(offset) + size > g_chrinc.size()) continue;
        g_inc[Lower(std::string(n, strnlen(n, 48)))] = {offset, size, &g_chrinc};
    }
    LOGR("chrinc.pak: %zu menu models", g_inc.size());
}

// the file named at guest address name_va in gun.pak
bool Find(uint32_t name_va, Entry& entry, std::string& name) {
    if (name_va < 0x10000) return false;
    name = Lower(kernel::ReadCString(name_va, 128));
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_loaded || g_loaded_for != int(game::Opt().beta_reborn) * 2 + int(game::Opt().default_gun)) Load();
    auto it = g_entries.find(name);
    if (it == g_entries.end()) {
        if (name != "ob/chrs/chr104.xbr") return false;
        if (g_inc.empty() && g_chrinc.empty()) LoadChrinc();
        it = g_inc.find("ob/chrincs/chr104.xbr");
        if (it == g_inc.end()) return false;
    }
    entry = it->second;
    return true;
}

}  // namespace

// A file's bytes from gun.pak into guest memory, for the read of a file the level's paks lack (voices.cpp's hook of
// 0x86450 (name, buffer, size)): false when gun.pak has no such file.
bool GunPakRead(uint32_t name_va, uint32_t dest, uint32_t max, uint32_t& got) {
    Entry entry;
    std::string name;
    if (!dest || !Find(name_va, entry, name)) return false;
    got = std::min(max, entry.size);
    std::memcpy(kernel::HostPtr(dest), entry.pak->data() + entry.offset, got);
    static int logged = 0;
    if (logged++ < 60) LOGR("gun.pak: %s read from gun.pak (%u bytes)", name.c_str(), got);
    return true;
}

// a file's size (name): -1 when no pak holds it and it is not on the disc
X86_EXTERN_FUNC(__imp__sub_000863D0);
X86_FUNC(sub_000863D0) {
    uint32_t name_va = kernel::R32(ctx.esp + 4);
    __imp__sub_000863D0(ctx, base);
    if (int32_t(ctx.eax) > 0) return;
    Entry entry;
    std::string name;
    if (Find(name_va, entry, name)) ctx.eax = entry.size;
}

// the loose read (name, flags, size out): the block, or 0
X86_EXTERN_FUNC(__imp__sub_00086970);
X86_FUNC(sub_00086970) {
    uint32_t name_va = kernel::R32(ctx.esp + 4), flags = kernel::R32(ctx.esp + 8), size_out = kernel::R32(ctx.esp + 12);
    __imp__sub_00086970(ctx, base);
    {
        static int seen = 0;
        bool gun = kernel::ReadCString(name_va, 96).find("ob/guns/") != std::string::npos;
        if (gun || seen++ < 40)
            LOGR("loose read %s flags %u: block %08X (arena low left %d, high left %d)", kernel::ReadCString(name_va, 96).c_str(), flags,
                 ctx.eax, int32_t(kernel::R32(0x3870D4)), int32_t(kernel::R32(0x3870E0)));
    }
    if (ctx.eax) return;
    Entry entry;
    std::string name;
    if (!Find(name_va, entry, name)) return;
    X86Context saved = ctx;
    uint32_t block = kernel::CallGuest(ctx, kAlloc, {(entry.size + 15) & ~15u, flags});
    ctx = saved;
    if (!block) return;
    std::memcpy(kernel::HostPtr(block), entry.pak->data() + entry.offset, entry.size);
    if (size_out) kernel::W32(size_out, entry.size);
    ctx.eax = block;
    static int logged = 0;
    if (logged++ < 40) LOGR("gun.pak: %s taken from gun.pak (%u bytes at %08X)", name.c_str(), entry.size, block);
}

// the model fixup by name (name): logged, to see which model a crash is at
X86_EXTERN_FUNC(__imp__sub_0012C6A0);
X86_FUNC(sub_0012C6A0) {
    uint32_t name_va = kernel::R32(ctx.esp + 4);
    LOGR("model fixup: %s", kernel::ReadCString(name_va, 96).c_str());
    __imp__sub_0012C6A0(ctx, base);
}

// a character made from a spawn record (type, flags, ... see the 0xE0A7C call): logged
X86_EXTERN_FUNC(__imp__sub_001404E0);
X86_FUNC(sub_001404E0) {
    LOGR("spawn: args %08X %08X %08X %08X %08X %08X %08X", kernel::R32(ctx.esp + 4), kernel::R32(ctx.esp + 8), kernel::R32(ctx.esp + 12),
         kernel::R32(ctx.esp + 16), kernel::R32(ctx.esp + 20), kernel::R32(ctx.esp + 24), kernel::R32(ctx.esp + 28));
    __imp__sub_001404E0(ctx, base);
}

// a texture by id (id, slot out, record): the ids the beta_reborn models add (26000 to 32999: the mod's, the Eagle's, TS1's guns) are logged with the slot they got (-1: not made)
X86_EXTERN_FUNC(__imp__sub_001B7E60);
X86_FUNC(sub_001B7E60) {
    uint32_t id = kernel::R32(ctx.esp + 4), out = kernel::R32(ctx.esp + 8);
    __imp__sub_001B7E60(ctx, base);
    static int logged = 0;
    if (id >= 20000 && id < 33000 && logged++ < 400) LOGR("texture %u: slot %d (eax %08X)", id, out ? int32_t(kernel::R32(out)) : -2, ctx.eax);
}

// the model lookup by name (name): logged for the guns, with the model it gives back
X86_EXTERN_FUNC(__imp__sub_0012C820);
X86_FUNC(sub_0012C820) {
    uint32_t name_va = kernel::R32(ctx.esp + 4);
    std::string name = kernel::ReadCString(name_va, 96);
    __imp__sub_0012C820(ctx, base);
    if (name.find("ob/guns/") != std::string::npos) LOGR("gun model lookup %s: model %08X", name.c_str(), ctx.eax);
}

// the pak directory lookup (name): which loaded pak a gun model comes out of
X86_EXTERN_FUNC(__imp__sub_000862A0);
X86_FUNC(sub_000862A0) {
    std::string name = kernel::ReadCString(kernel::R32(ctx.esp + 4), 96);
    __imp__sub_000862A0(ctx, base);
    static int logged = 0;
    if (name.find("ob/guns/") != std::string::npos && logged++ < 120)
        LOGR("pak lookup %s: entry %08X pak %d of %d", name.c_str(), ctx.eax, int32_t(kernel::R32(0x2BA7D8)), int32_t(kernel::R32(0x3585D8)));
}
