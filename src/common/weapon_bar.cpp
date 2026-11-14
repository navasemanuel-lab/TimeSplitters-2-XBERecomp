// The weapon bar of Future Perfect, for TimeSplitters 2, drawn over the view as the settings panel is: a dark band
// with the fist and each gun the player has, the one in hand larger and lit, shown for a
// moment whenever the gun in hand changes and fading out. TimeSplitters 2 has no such bar.
//
// What it reads: the character (the first local player's: [[0x497100] + 0x1BC] + 0x124) holds the gun in hand as an
// entry of the game's weapon table (+0xC8; the table is 0x2C9BF0, 0x24 bytes an entry: the guns of the two hands, 41
// entries, 0 the fist, a gun and then its pair "(x2)") and the entries it has as 41 words from +0x984. The entries
// are the game's gun names in order (the list at 0x337784), minus two the game never gives (13 and 14).
//
// The pictures: weapon_icons.bin beside the exe (tools/make_weapon_icons.py, from the guns' promo models), an atlas of 256x128 cells, the icons as
// the bar shows them in rows 0-4 and as the chosen one in rows 5-9, handed to the render thread as one texture.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "common/mouse.h"
#include "common/weapon_bar.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace weapon_bar {
namespace {

constexpr uint32_t kGameState = 0x497100, kEntries = 41, kOwned = 0x984, kEntry = 0xC8;
constexpr int kCols = 8, kCellW = 256, kCellH = 128;
constexpr uint32_t kAtlasW = 8 * kCellW, kAtlasH = 10 * kCellH;

// weapon table entry -> the atlas cell of its icon (-1 none)
constexpr int8_t kIcon[kEntries] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                                    21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, -1, 35, 36, -1, -1, -1};

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool LoadAtlas() {
    static int state = 0;  // 0 not tried, 1 handed over, -1 failed
    if (state) return state > 0;
    state = -1;
    std::ifstream f(app::ExeDir() / "weapon_icons.bin", std::ios::binary);
    if (!f) f.open(app::ExeDir() / "data" / "weapon_icons.bin", std::ios::binary);
    char head[12];
    if (!f.read(head, 12) || std::string(head, 4) != "WIC1") return false;
    uint32_t w, h;
    std::memcpy(&w, head + 4, 4);
    std::memcpy(&h, head + 8, 4);
    if (w != kAtlasW || h != kAtlasH) return false;
    std::vector<uint8_t> px(size_t(w) * h * 4);
    if (!f.read(reinterpret_cast<char*>(px.data()), std::streamsize(px.size()))) return false;
    // The guns made alike in size: each icon (and its lit twin five rows down, the same fit for both) is cut to what it
    // draws and fitted, aspect kept, into the same box of its cell, so a pistol and a rocket launcher stand at a similar
    // size, all horizontal and centred, none larger than the others.
    {
        std::vector<uint8_t> fit(px.size(), 0);
        auto at = [&](std::vector<uint8_t>& v, int x, int y) { return &v[(size_t(y) * w + size_t(x)) * 4]; };
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < kCols; col++) {
                int ox = col * kCellW, oy = row * kCellH;
                int minx = kCellW, miny = kCellH, maxx = -1, maxy = -1;
                for (int pair = 0; pair < 2; pair++)
                    for (int y = 0; y < kCellH; y++)
                        for (int x = 0; x < kCellW; x++)
                            if (at(px, ox + x, oy + pair * 5 * kCellH + y)[3] > 24) {
                                minx = std::min(minx, x), maxx = std::max(maxx, x);
                                miny = std::min(miny, y), maxy = std::max(maxy, y);
                            }
                if (maxx < minx) continue;
                float bw = float(maxx - minx + 1), bh = float(maxy - miny + 1);
                const float tw = kCellW * 0.86f, th = kCellH * 0.72f;
                float s = std::min({tw / bw, th / bh, 2.0f});
                float dw = bw * s, dh = bh * s;
                float dx0 = (kCellW - dw) / 2, dy0 = (kCellH - dh) / 2;
                for (int pair = 0; pair < 2; pair++)
                    for (int y = 0; y < kCellH; y++)
                        for (int x = 0; x < kCellW; x++) {
                            float sx = (float(x) + 0.5f - dx0) / s + float(minx) - 0.5f;
                            float sy = (float(y) + 0.5f - dy0) / s + float(miny) - 0.5f;
                            if (sx < float(minx) - 0.5f || sx > float(maxx) + 0.5f || sy < float(miny) - 0.5f || sy > float(maxy) + 0.5f) continue;
                            int x0 = std::clamp(int(std::floor(sx)), minx, maxx), y0 = std::clamp(int(std::floor(sy)), miny, maxy);
                            int x1 = std::min(x0 + 1, maxx), y1 = std::min(y0 + 1, maxy);
                            float fx = std::clamp(sx - float(x0), 0.0f, 1.0f), fy = std::clamp(sy - float(y0), 0.0f, 1.0f);
                            const uint8_t* p00 = at(px, ox + x0, oy + pair * 5 * kCellH + y0);
                            const uint8_t* p10 = at(px, ox + x1, oy + pair * 5 * kCellH + y0);
                            const uint8_t* p01 = at(px, ox + x0, oy + pair * 5 * kCellH + y1);
                            const uint8_t* p11 = at(px, ox + x1, oy + pair * 5 * kCellH + y1);
                            uint8_t* d = at(fit, ox + x, oy + pair * 5 * kCellH + y);
                            for (int c = 0; c < 4; c++) {
                                float v = (p00[c] * (1 - fx) + p10[c] * fx) * (1 - fy) + (p01[c] * (1 - fx) + p11[c] * fx) * fy;
                                d[c] = uint8_t(std::clamp(v, 0.0f, 255.0f));
                            }
                        }
            }
        px = std::move(fit);
    }
    {
        std::lock_guard lock(gpu::g_ui_atlas_lock);
        gpu::g_ui_atlas_px = std::move(px);
        gpu::g_ui_atlas_w = w;
        gpu::g_ui_atlas_h = h;
    }
    gpu::g_ui_atlas_new = true;
    state = 1;
    return true;
}

// an address of the guest's memory: the 64 MB of main memory, or the same mapped at 0x80000000 (a pointer read from a
// structure not made yet can be anything)
bool Mapped(uint32_t va, uint32_t length) {
    if (va < 0x10000 || va + length < va) return false;
    if (!(va + length <= 0x04000000 || (va >= 0x80000000 && va + length <= 0x88000000))) return false;
    // a pointer into the gap between the image and the heap, or a freed block, is in range but not committed
    uint32_t lo = va, hi = va + (length ? length - 1 : 0);
    for (uint32_t pg = lo & ~(mem::kPage - 1); pg <= hi; pg += mem::kPage)
        if (!mem::IsCommitted(pg)) return false;
    return true;
}

uint32_t Character() {
    if (!Mapped(kGameState, 4)) return 0;
    uint32_t state = kernel::R32(kGameState);
    if (!Mapped(state + 0x1BC, 4)) return 0;
    uint32_t player = kernel::R32(state + 0x1BC);
    if (!Mapped(player + 0x124, 4)) return 0;
    uint32_t chr = kernel::R32(player + 0x124);
    if (chr & 3) return 0;
    return Mapped(chr, kOwned + kEntries * 4 + 8) ? chr : 0;
}

}  // namespace

namespace {
std::atomic<int> g_request{0};
}
void RequestSlot(int slot) { g_request = slot; }

// The game's own next-weapon (0xB3250, as 0xB7486 calls it) gives the gun after the one in hand: walked from the fist it
// lists the guns in the order the pad cycles them. The slot's gun is written as the one in hand, with the change's sound
// (0xB73D7: sound 0x3E for the player).
void ApplyRequest(X86Context& ctx) {
    int slot = g_request.exchange(0);
    if (slot < 1) return;
    uint32_t chr = Character();
    if (!chr) return;
    uint32_t state = kernel::R32(kernel::R32(kGameState) + 0x158);
    if (state == 4 || state == 6 || state == 7 || state == 8 || state == 0xE) return;  // a vehicle, a turret, a camera
    uint32_t held = kernel::R32(chr + kEntry);
    if (held >= kEntries) return;
    X86Context saved = ctx;
    // the slots are the bar's own list, in the table's order (the fist, then each gun held that has a picture), so the
    // number pressed is the place the bar shows, none left out
    uint32_t order[kEntries];
    int n = 0;
    for (uint32_t e = 0; e < kEntries; e++) {
        if (kIcon[e] < 0) continue;
        if (e && !kernel::R32(chr + kOwned + e * 4)) continue;
        order[n++] = e;
    }
    static int logged = 0;
    if (logged++ < 6) LOGR("weapon slot %d of %d guns (in hand: %u)", slot, n, held);
    if (slot <= n && order[slot - 1] != held) {
        kernel::CallGuest(ctx, 0x1A5EA0, {0x3E, kernel::R32(chr), 0x3F800000u});
        kernel::W32(chr + kEntry, order[slot - 1]);
    }
    ctx = saved;
}

void Draw() {
    if (!LoadAtlas() || !gpu::g_ui_atlas.load()) return;
    static uint32_t last_chr = 0, last_entry = 0xFFFFFFFF;
    static double until = 0;
    uint32_t chr = Character();
    if (!chr) {
        last_chr = 0;
        return;
    }
    uint32_t entry = kernel::R32(chr + kEntry);
    if (entry >= kEntries) return;  // not a character in play
    double now = NowMs();
    if (chr != last_chr || entry != last_entry) {
        if (last_chr) until = now + 2600;  // a change: shown; the first sight of a character is not
        if (!last_chr) until = now + 2600;
        last_chr = chr;
        last_entry = entry;
    }
    if (now >= until || mouse::MenuMode()) return;
    float alpha = float(std::clamp((until - now) / 450.0, 0.0, 1.0));

    int slots[kEntries], count = 0, chosen = -1;
    for (uint32_t e = 0; e < kEntries; e++) {
        if (kIcon[e] < 0) continue;
        if (e && !kernel::R32(chr + kOwned + e * 4)) continue;
        if (e == entry) chosen = count;
        slots[count++] = int(e);
    }
    if (count == 0) return;

    ImGuiIO& io = ImGui::GetIO();
    float sc = io.DisplaySize.y / 1080.0f;
    float cw = 96 * sc, ch = 48 * sc, pitch = 122 * sc;
    float pill_h = 96 * sc, pad = 90 * sc;
    float width = pitch * float(count - 1) + cw + pad * 2;
    float cx = io.DisplaySize.x / 2, cy = io.DisplaySize.y * 0.16f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    auto a = [&](int v) { return int(float(v) * alpha); };
    float x0 = cx - width / 2, y0 = cy - pill_h / 2;
    // as Future Perfect's: frosted glass, the picture behind it blurred (gpu::g_ui_blur, a copy of the frame an eighth of its
    // size, asked for while the bar is shown) under a translucent dark tint; no outline, no marker above it
    gpu::g_ui_blur_want = true;
    // a grid of quads whose vertex alpha fades to nothing at every edge: no rectangle shows, the glass just thins out
    auto fade = [](float t, float zone) {  // t 0..1 across, zone the share faded at each end
        float d = std::min(t, 1.0f - t) / zone;
        d = std::clamp(d, 0.0f, 1.0f);
        return d * d * (3.0f - 2.0f * d);
    };
    auto glass = [&](ImTextureID id, bool textured, ImVec2 white_uv, int r, int g, int b, int amax) {
        const int nx = 32, ny = 8;
        if (textured) dl->PushTextureID(id);
        dl->PrimReserve(nx * ny * 6, (nx + 1) * (ny + 1));
        ImDrawIdx base = ImDrawIdx(dl->_VtxCurrentIdx);
        for (int j = 0; j <= ny; j++)
            for (int i = 0; i <= nx; i++) {
                float tx = float(i) / nx, ty = float(j) / ny;
                float al = fade(tx, 0.28f) * fade(ty, 0.42f);
                ImVec2 pos{x0 + width * tx, y0 + pill_h * ty};
                ImVec2 uv = textured ? ImVec2{pos.x / io.DisplaySize.x, pos.y / io.DisplaySize.y} : white_uv;
                dl->PrimWriteVtx(pos, uv, IM_COL32(r, g, b, int(float(amax) * al * alpha)));
            }
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                ImDrawIdx v0 = ImDrawIdx(base + j * (nx + 1) + i), v1 = ImDrawIdx(v0 + 1), v2 = ImDrawIdx(v0 + (nx + 1)), v3 = ImDrawIdx(v2 + 1);
                dl->PrimWriteIdx(v0); dl->PrimWriteIdx(v1); dl->PrimWriteIdx(v3);
                dl->PrimWriteIdx(v0); dl->PrimWriteIdx(v3); dl->PrimWriteIdx(v2);
            }
        if (textured) dl->PopTextureID();
    };
    if (uint64_t blur = gpu::g_ui_blur.load()) glass(ImTextureID(blur), true, {}, 255, 255, 255, 255);
    glass(0, false, io.Fonts->TexUvWhitePixel, 10, 12, 16, 175);
    ImTextureID tex = ImTextureID(gpu::g_ui_atlas.load());
    float first = cx - pitch * float(count - 1) / 2;
    for (int i = 0; i < count; i++) {
        int cell = kIcon[slots[i]];
        bool on = i == chosen;
        int col = cell % kCols, row = cell / kCols + (on ? 5 : 0);
        ImVec2 uv0{float(col * kCellW) / kAtlasW, float(row * kCellH) / kAtlasH};
        ImVec2 uv1{float((col + 1) * kCellW) / kAtlasW, float((row + 1) * kCellH) / kAtlasH};
        float s = on ? 1.12f : 1.0f;
        float x = first + pitch * float(i), w = cw * s, h = ch * s;
        dl->AddImage(tex, {x - w / 2, cy - h / 2}, {x + w / 2, cy + h / 2}, uv0, uv1,
                     IM_COL32(255, 255, 255, a(255)));
    }
}

}  // namespace weapon_bar
