// Pak replacements: the runtime asks g_replace_file for a host file to open in place of a pak the game opens by its disc path
// (kernel/files.cpp). Here: the Chemical Plant level (hidden_levels.cpp) and the gun paks of the beta_reborn and default_gun
// options (resourcepacks/beta_reborn, resourcepacks/default_gun).
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "app/app.h"
#include "common/options.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"

namespace fs = std::filesystem;

std::string ChemicalPlantPak(const std::string& lower);  // hidden_levels.cpp

namespace {

// the pak of the beta_reborn option: resourcepacks/beta_reborn/<gun.pak | story/l_XX_ST.pak | arcade/l_XX.pak>, the retail pak with
// the Beta Reborn PS2 mod's gun models (tools/mk_beta_reborn.py), when the game opens that path and the pack holds it
std::string BetaReborn(const std::string& lower) {
    std::string s = lower;
    std::replace(s.begin(), s.end(), '\\', '/');
    size_t at = s.rfind("/data/");
    if (at == std::string::npos) return {};
    std::string rel = s.substr(at + 6);
    if (!game::Opt().beta_reborn) {  // the retail gun.pak with the textured Default Gun (tools/mk_eagle.py), when that is on
        if (rel != "gun.pak" || !game::Opt().default_gun) return {};
        fs::path eagle = app::ExeDir() / "resourcepacks" / "default_gun" / "gun.pak";
        std::error_code ec;
        return fs::exists(eagle, ec) ? eagle.string() : std::string();
    }
    if (rel != "gun.pak" && rel.rfind("story/", 0) != 0 && rel.rfind("arcade/", 0) != 0) return {};
    fs::path pak = app::ExeDir() / "resourcepacks" / "beta_reborn" / rel;
    std::error_code ec;
    if (!fs::exists(pak, ec)) return {};
    static int said = 0;
    if (said++ < 80) LOGR("beta_reborn: %s opened from %s", lower.c_str(), pak.string().c_str());
    return pak.string();
}


// resourcepacks/xmas/xmas.bin (tools/xmas/ (xb.py, m1.py, h3.py, c1.py, mk.py in that order): the TimeSplitters 2 Christmas Edition textures, each matched to the Xbox
// texture it replaces by the hash Dolphin names it with): "XMS1", count, then per texture guest width, height, bytes hashed (u32),
// gpu::Hash of those bytes (u64), width, height (u32), the offset of its BGRA pixels in the file (u64); the pixels are read when
// the texture is first drawn. Registered once, when the option is on at the first pak the game opens.
// The Christmas textures on or off, as the option says, at any time: the entries go in when it is turned on and out when it is turned
// off, and the textures already drawn look their picture up again (a change of the replacements)
struct XmasKey {
    uint32_t w, h, n;
    uint64_t hash;
    std::shared_ptr<const gpu::TextureReplacement> prev;  // what the key had before (another pack's picture), put back when it goes
};
std::vector<XmasKey>& XmasRegistered() {
    static std::vector<XmasKey> v;
    return v;
}
// a pack file (XMS1: count, then each: console width, height, bytes hashed, hash, picture width, height, the offset of its BGRA pixels,
// read when the texture is first drawn) put into the renderer; its keys are kept to take them out again
void LoadPack(const fs::path& path, std::vector<XmasKey>& reg, const char* what) {
    std::ifstream f(path, std::ios::binary);
    char head[8];
    if (!f.read(head, 8) || std::memcmp(head, "XMS1", 4)) {
        LOGR("%s: %s not found", what, path.string().c_str());
        return;
    }
    uint32_t count;
    std::memcpy(&count, head + 4, 4);
    auto file = std::make_shared<std::pair<std::mutex, fs::path>>();
    file->second = path;
    uint32_t made = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v[3], size[2];
        uint64_t hash, offset;
        if (!f.read(reinterpret_cast<char*>(v), 12) || !f.read(reinterpret_cast<char*>(&hash), 8) ||
            !f.read(reinterpret_cast<char*>(size), 8) || !f.read(reinterpret_cast<char*>(&offset), 8))
            break;
        if (!size[0] || !size[1] || size[0] > 4096 || size[1] > 4096) continue;
        auto r = std::make_shared<gpu::TextureReplacement>();
        r->guest_w = v[0];
        r->guest_h = v[1];
        r->n = v[2];
        r->hash = hash;
        r->w = size[0];
        r->h = size[1];
        r->mips = true;
        size_t bytes = size_t(size[0]) * size[1] * 4;
        r->load = [file, offset, bytes](std::vector<uint8_t>& px) {
            std::lock_guard lock(file->first);
            std::ifstream in(file->second, std::ios::binary);
            px.resize(bytes);
            in.seekg(std::streamoff(offset));
            return bool(in.read(reinterpret_cast<char*>(px.data()), std::streamsize(bytes)));
        };
        reg.push_back({v[0], v[1], v[2], hash, gpu::FindReplacementEntry(v[0], v[1], v[2], hash)});
        gpu::ReplaceTexture(std::move(r));
        made++;
    }
    LOGR("%s: %u of %u textures registered from %s", what, made, count, path.string().c_str());
}
void SetXmas(bool on) {
    auto& reg = XmasRegistered();
    if (!on) {
        for (const XmasKey& k : reg) {
            gpu::EraseReplacement(k.w, k.h, k.n, k.hash);
            if (k.prev) gpu::ReplaceTexture(k.prev);
        }
        if (!reg.empty()) LOGR("xmas: %zu textures taken out", reg.size());
        reg.clear();
        return;
    }
    if (!reg.empty()) return;
    LoadPack(app::ExeDir() / "resourcepacks" / "xmas" / "xmas.bin", reg, "xmas");
}
// the HD fonts, crosshairs and HUD pieces (hd_hud.bin, tools/xmas/mk_hud.py: the pack from bekoha.github.io/ts2): always
void LoadHudPack() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    static std::vector<XmasKey> reg;
    fs::path p = app::ExeDir() / "hd_hud.bin";
    if (!fs::exists(p)) p = app::ExeDir() / "data" / "hd_hud.bin";
    LoadPack(p, reg, "hd_hud");
}

std::string Replace(const std::string& xbox_path) {
    static bool first = true;
    if (first) {
        first = false;
        LoadHudPack();
        SetXmas(game::Opt().xmas_textures);
    }
    std::string lower = xbox_path;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (std::string chemical = ChemicalPlantPak(lower); !chemical.empty()) return chemical;
    return BetaReborn(lower);
}

}  // namespace


void InitPakReplace() { kernel::g_replace_file = Replace; }
// the panel's option: the Christmas textures in or out at once, no restart
void SetXmasTextures(bool on) { SetXmas(on); }

// The view of TimeSplitters 1's Tommy Gun and Shotgun (beta_reborn: their models replace TS2's, tools/mk_ts1_guns.py). A gun's
// place in front of the eye is its gun record's +0x38 (x, y, z floats; the gun table 0x2C3468, 0x240 a gun). TS1's own table
// (SLUS-200.90, 400 bytes a gun, the same three floats 0x20 bytes after the record's model numbers; its Luger is TS2's to the
// hundredth) holds them for the models gun20 (Tommy Gun) and gun25 (Shotgun): those are put on TS2's guns 23 (Tommy Gun) and
// 25 (Shotgun) while the option is on, TS2's own when it is off, at once.
namespace {
struct View {
    uint32_t gun;
    float ts1[3];
};
constexpr View kViews[] = {
    {23, {0.22f, -0.23f, -0.45f}},  // TS2's 0.17, -0.16, -0.35
    {25, {0.27f, -0.37f, -0.55f}},  // TS2's 0.27, -0.30, -0.55
};
uint32_t g_ts2_view[2][3];  // TS2's own words, read before the first change
int g_view_on = -1;
}  // namespace

void BetaRebornTick() {
    int on = game::Opt().beta_reborn ? 1 : 0;
    if (on == g_view_on) return;
    for (int k = 0; k < 2; k++) {
        uint32_t at = 0x2C3468 + kViews[k].gun * 0x240 + 0x38;
        for (int i = 0; i < 3; i++) {
            if (g_view_on < 0) g_ts2_view[k][i] = kernel::R32(at + 4 * i);
            uint32_t w = g_ts2_view[k][i];
            if (on) std::memcpy(&w, &kViews[k].ts1[i], 4);
            kernel::W32(at + 4 * i, w);
        }
    }
    g_view_on = on;
    LOGR("beta_reborn: Tommy Gun and Shotgun held as in %s", on ? "TimeSplitters 1" : "TimeSplitters 2");
}
