// Every sound in every mode. A level's pak holds the sounds its characters use; Story's paks hold half of each voice
// (voices.cpp makes Story load them all, as the arcade does). A sound the level's pak lacks the game opens loose as
// d:\sfx\<name>.xbs, a path of the developers' PCs that is not on the disc, so it stays silent. sounds.pak holds every
// one of them (the same bytes as the level paks'): a missing d:\sfx file is taken from there, written once beside the
// exe in cache\sfx.
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
#include "core/log.h"
#include "kernel/kernel.h"

namespace fs = std::filesystem;

namespace {

std::mutex g_lock;
bool g_loaded = false;
std::vector<uint8_t> g_pak;
struct Entry {
    uint32_t offset, size;
};
std::map<std::string, Entry> g_entries;  // lower case "male_sg22_die06.xbs"

uint32_t U32(size_t at) {
    uint32_t v = 0;
    if (at + 4 <= g_pak.size()) std::memcpy(&v, g_pak.data() + at, 4);
    return v;
}

// the pak: "P8CK", the directory's offset and count (12 bytes each: name offset, size, data offset), the names' offset
void Load() {
    g_loaded = true;
    if (!kernel::ReadGameFile("d:\\data\\sounds.pak", g_pak) || g_pak.size() < 20 || std::memcmp(g_pak.data(), "P8CK", 4)) {
        LOGW("sounds: no sounds.pak, missing sounds stay silent");
        g_pak.clear();
        return;
    }
    uint32_t dir = U32(4), count = U32(8), names = U32(12);
    for (uint32_t i = 0; i < count; i++) {
        size_t e = size_t(dir) + i * 12;
        uint32_t name = U32(e), size = U32(e + 4), offset = U32(e + 8);
        size_t n = size_t(names) + name;
        if (n >= g_pak.size() || size_t(offset) + size > g_pak.size()) continue;
        std::string s(reinterpret_cast<const char*>(g_pak.data() + n), strnlen(reinterpret_cast<const char*>(g_pak.data() + n), g_pak.size() - n));
        for (char& c : s) c = char(std::tolower(uint8_t(c)));
        if (s.rfind("sfx/", 0) == 0) g_entries[s.substr(4)] = {offset, size};
    }
    LOGR("sounds: %zu sounds in sounds.pak for those a level lacks", g_entries.size());
}

std::string MissingFile(const std::string& xbox_path) {
    std::string p = xbox_path;
    for (char& c : p) c = char(std::tolower(uint8_t(c)));
    std::replace(p.begin(), p.end(), '/', '\\');
    for (const char* prefix : {"\\??\\d:\\sfx\\", "d:\\sfx\\", "\\device\\cdrom0\\sfx\\"}) {
        if (p.rfind(prefix, 0) != 0) continue;
        std::string name = p.substr(std::strlen(prefix));
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_loaded) Load();
        auto it = g_entries.find(name);
        if (it == g_entries.end()) return {};
        fs::path dir = app::ExeDir() / "cache" / "sfx", out = dir / name;
        std::error_code ec;
        if (!fs::exists(out, ec) || fs::file_size(out, ec) != it->second.size) {
            fs::create_directories(dir, ec);
            std::ofstream f(out, std::ios::binary | std::ios::trunc);
            f.write(reinterpret_cast<const char*>(g_pak.data() + it->second.offset), it->second.size);
            if (!f) return {};
        }
        static int logged = 0;
        if (logged++ < 400) LOGR("sounds: %s taken from sounds.pak", name.c_str());
        return out.string();
    }
    return {};
}

}  // namespace

// A sound's bytes from sounds.pak into guest memory: for the game's read of a sound its paks lack (voices.cpp). The name
// as the game gives it ("host0:sfx/male_gn22_08.xbs"), up to max bytes; false when sounds.pak has no such sound.
bool ReadSfx(const std::string& game_name, uint32_t dest, uint32_t max, uint32_t& got) {
    std::string name = game_name;
    for (char& c : name) c = char(std::tolower(uint8_t(c)));
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_loaded) Load();
    auto it = g_entries.find(name);
    if (it == g_entries.end()) return false;
    got = std::min(max, it->second.size);
    std::memcpy(kernel::HostPtr(dest), g_pak.data() + it->second.offset, got);
    return true;
}

void InitSounds() { kernel::g_missing_file = MissingFile; }

// the 32 byte header of an original sound, as sounds.pak holds it (the format, the rate): for the sounds a pack
// replaces, which keep it. The name is "male_gn22_08" in lower case, no folder, no extension.
bool OriginalSfxHeader(const std::string& name, uint8_t header[32], uint32_t* size) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_loaded) Load();
    auto it = g_entries.find(name + ".xbs");
    if (it == g_entries.end() || it->second.size < 32) return false;
    std::memcpy(header, g_pak.data() + it->second.offset, 32);
    if (size) *size = it->second.size;
    return true;
}

// the whole original sound (header and blocks) as sounds.pak holds it, for putting it back over a replacement
bool OriginalSfxBytes(const std::string& name, const uint8_t** data, uint32_t* size) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_loaded) Load();
    auto it = g_entries.find(name + ".xbs");
    if (it == g_entries.end() || it->second.size < 32) return false;
    *data = g_pak.data() + it->second.offset;
    *size = it->second.size;
    return true;
}
