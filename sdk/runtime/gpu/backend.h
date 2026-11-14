#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "gpu/state.h"

namespace gpu {

// set by a game while its HUD (or any group of draws) must not be drawn: the draws are dropped
inline std::atomic<bool> g_suppress_draws{false};
// diagnostic: log the title's call stack of each kind of draw ("fcdraw:" in the log), set by the free camera
inline std::atomic<bool> g_trace_draws{false};
// the call stack (return addresses) of the last draw of the effect quads' vertex program, for the "heat:" log; the thread
// that made the draw reads it
inline thread_local uint32_t g_draw_stack[8]{};
// set by a game: the black bars baked into movie frames (the overlay) are cropped away on a wide output
inline std::atomic<bool> g_crop_letterbox{false};
inline std::atomic<bool> g_wait_pipelines{false};  // draws wait for their pipelines, none left out meanwhile (the game: the menus, where a missing draw blinks)
// A blurred copy of the picture behind the overlay, for a panel drawn as frosted glass: the overlay sets g_ui_blur_want while it
// wants it; the next frame the picture shown is copied, brought down to an eighth of its size by two box filters, and
// g_ui_blur is its ImGui texture id (0 until then or when the picture is HDR), covering the whole output
inline std::atomic<bool> g_ui_blur_want{false};
inline std::atomic<uint64_t> g_ui_blur{0};
inline std::atomic<bool> g_no_backface_cull{false};  // every triangle drawn from both sides (the game: the whole level mode)
inline std::atomic<bool> g_no_fog{false};  // the scene drawn without its fog (the game: see_all)

// D3DCLEAR flags as the XDK defines them
enum : uint32_t {
    kClearTarget = 0xF0,  // ARGB channels
    kClearZ = 0x01,
    kClearStencil = 0x02,
};

class Backend {
public:
    virtual ~Backend() = default;
    // the title's back buffer size, the output window may differ
    virtual bool Init(uint32_t width, uint32_t height) = 0;
    // rects: count D3DRECTs (4 x int32), null for the whole viewport
    virtual void Clear(const State& s, uint32_t count, const uint8_t* rects, uint32_t flags, uint32_t color, float z,
                       uint32_t stencil) = 0;
    // indices: the title's 16 bit indices, null for non indexed draws, then start is the first vertex
    virtual void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) = 0;
    virtual void Copy(const Surface& src, const Surface& dst, uint32_t count, uint32_t src_rects_va,
                      uint32_t dst_points_va) = 0;
    virtual void Present(const Surface& front) = 0;
    // the CPU is about to touch this resource's memory: write GPU results back first
    virtual void Sync(uint32_t data_va, uint32_t size) = 0;
    // a lock that does not wait for the GPU (NOOVERWRITE): a result a frame or two old is what the console gives too
    virtual void SyncLate(uint32_t data_va) = 0;
    // SyncLate in two for the render thread: DeliverLate (any thread) writes the newest finished copy to guest memory,
    // false when there is none recent; RecordLate (in order with the draws) starts a copy for a later lock
    virtual bool DeliverLate(uint32_t) { return false; }
    virtual void RecordLate(uint32_t) {}
    virtual void Flush() = 0;
    virtual void SetGamma(const uint8_t* ramp /*3 x 256*/) = 0;
    // visibility tests: draws count passing pixels while simple[kMCountEnable / 4] is set, ClearReport zeroes the
    // count, GetReport writes it to a 16 byte report once the GPU is past those draws; without them it stays pending
    virtual void ClearReport() {}
    virtual void GetReport(uint32_t /*report*/) {}

    // the XDK's fences and busy checks against the render thread; a backend that draws at once is never behind
    virtual uint32_t Fence() { return ++fence_; }
    virtual bool FencePending(uint32_t) { return false; }
    virtual void WaitFence(uint32_t) {}
    virtual bool Busy(uint32_t /*resource*/) { return false; }
    virtual void WaitResource(uint32_t /*resource*/) {}
    // until the GPU has drawn present number n, counted from 0; any thread
    virtual void WaitGpuFrame(uint64_t /*n*/) {}

private:
    uint32_t fence_ = 0;
};

std::unique_ptr<Backend> CreateBackend();
// --prepare-shaders: the pipelines of the shipped seeds and earlier runs made into pipelines.bin, no window or game;
// the process's exit code
int PrepareShaders();
// the same backend driven from a thread of its own: calls return once recorded
std::unique_ptr<Backend> CreateThreadedBackend(std::unique_ptr<Backend> inner);

// the HDR10 output as the swap chain was made: 0 off, 1 on, -1 asked for with the display's HDR off in Windows
inline std::atomic<int> g_hdr_output{0};

// A picture atlas for the title's own overlay (RGBA8, one texture): set once from any thread (the pixels, then the new
// flag), made on the render thread in the next UI frame; g_ui_atlas is then its ImGui texture id, 0 until then
inline std::mutex g_ui_atlas_lock;
inline std::vector<uint8_t> g_ui_atlas_px;
inline uint32_t g_ui_atlas_w = 0, g_ui_atlas_h = 0;
inline std::atomic<bool> g_ui_atlas_new{false};
inline std::atomic<uint64_t> g_ui_atlas{0};
// a second one the same way (the title's pictures for the panel: g_ui_icons_px ... g_ui_icons)
inline std::mutex g_ui_icons_lock;
inline std::vector<uint8_t> g_ui_icons_px;
inline uint32_t g_ui_icons_w = 0, g_ui_icons_h = 0;
inline std::atomic<bool> g_ui_icons_new{false};
inline std::atomic<uint64_t> g_ui_icons{0};

// The title's own pictures in place of console textures: a w x h texture whose first n bytes of guest memory hash
// (gpu::Hash) to hash is drawn from px (BGRA8, any size; its coordinates stay the console texture's, normalised by the
// console size). Set from any thread before the texture is first drawn.
struct TextureReplacement {
    uint32_t guest_w = 0, guest_h = 0, n = 0;
    uint64_t hash = 0;
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    // a picture made when the texture is made, not kept (a pack of thousands): fills BGRA8 pixels of w x h
    std::function<bool(std::vector<uint8_t>&)> load;
    mutable bool bad = false;  // its picture could not be made: the console's texture is drawn
    bool mips = false;  // the chain of smaller levels made from it (a picture of the world, not of the screen)
};
inline std::mutex g_replace_lock;
// by console size, then bytes hashed, then hash: a pack of thousands is found without hashing for each
struct ReplacementSize {
    uint32_t n;
    std::unordered_map<uint64_t, std::shared_ptr<const TextureReplacement>> by_hash;
};
inline std::unordered_map<uint64_t, std::vector<ReplacementSize>> g_replacements;
// counts the changes to the replacements made after the first pictures: a texture already drawn is looked up again
inline std::atomic<uint32_t> g_replace_epoch{0};
inline void ReplaceTexture(std::shared_ptr<const TextureReplacement> r) {
    g_replace_epoch.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(g_replace_lock);
    auto& sizes = g_replacements[uint64_t(r->guest_w) << 32 | r->guest_h];
    for (auto& s : sizes)
        if (s.n == r->n) {
            s.by_hash[r->hash] = std::move(r);
            return;
        }
    sizes.push_back({r->n, {}});
    sizes.back().by_hash[r->hash] = std::move(r);
}
inline bool HasReplacementsOfSize(uint32_t guest_w, uint32_t guest_h) {
    std::lock_guard lock(g_replace_lock);
    return g_replacements.count(uint64_t(guest_w) << 32 | guest_h) != 0;
}
// the replacement now set for a console texture (null none), and its removal (the console's texture is drawn again)
inline std::shared_ptr<const TextureReplacement> FindReplacementEntry(uint32_t guest_w, uint32_t guest_h, uint32_t n, uint64_t hash) {
    std::lock_guard lock(g_replace_lock);
    auto it = g_replacements.find(uint64_t(guest_w) << 32 | guest_h);
    if (it == g_replacements.end()) return nullptr;
    for (auto& s : it->second)
        if (s.n == n) {
            auto r = s.by_hash.find(hash);
            return r == s.by_hash.end() ? nullptr : r->second;
        }
    return nullptr;
}
inline void EraseReplacement(uint32_t guest_w, uint32_t guest_h, uint32_t n, uint64_t hash) {
    g_replace_epoch.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard lock(g_replace_lock);
    auto it = g_replacements.find(uint64_t(guest_w) << 32 | guest_h);
    if (it == g_replacements.end()) return;
    for (auto& s : it->second)
        if (s.n == n) {
            // a texture drawn from it keeps a pointer to it: the picture is kept, only its entry goes
            static std::vector<std::shared_ptr<const TextureReplacement>> kept;
            if (auto f = s.by_hash.find(hash); f != s.by_hash.end()) {
                kept.push_back(f->second);
                s.by_hash.erase(f);
            }
        }
}

// perf diagnostic: the title's thread recording for the render thread, and waiting on it
inline std::atomic<uint64_t> g_perf_capture_ns{0}, g_perf_wait_ns{0}, g_perf_waits{0};
inline std::atomic<uint64_t> g_perf_wait_kind_ns[6];  // read-back, resource, fence, flush, copy, next frame
inline std::atomic<uint64_t> g_perf_sync_late{0}, g_perf_sync_block{0};  // read-backs the title waited for, by cause

// The performance overlay's numbers (panel), measured while diag::Perf() and published every half second: per frame
// of the title, in milliseconds unless named otherwise
struct FrameStats {
    bool valid = false;
    double fps = 0, frame = 0, worst = 0;  // the title's frames, the longest of them
    double shown_fps = 0;                  // frames reaching the screen (more with frame interpolation)
    double game = 0;                       // the title's threads busy (their CPU time): the game, recording its draws
    double game_wait = 0;                  // the title's thread waiting on the render thread
    double pace = 0;                       // the title's thread asleep in the frame cap
    double render = 0, draw = 0, compile = 0, copy = 0;  // the render thread's work: in all, draws, shaders, copies
    double present = 0, swap = 0, vsync = 0;             // handing the frame over: in all, the swap, waiting on it
    double gpu = 0;
    double gpu_pass[8] = {};  // scene, full, small, resolve, copy, final, effects, other
    double audio = 0;         // the mixer, its share of one core in percent
    double draws = 0, tris = 0, upload_mb = 0, tex_uploads = 0;
    double shaders = 0, pipelines = 0;  // made this half second
    char threads[160] = {};             // the busiest threads, percent of a core
};
FrameStats Stats();

// visibility test results held at the highest of this many reports in a row: a title testing a sprite against the
// depth it lies on (a sun on its sky) counts pixels that flicker on the host's rasterisation; 1 reports each as is
inline std::atomic<uint32_t> g_report_hold{1};
// F3: save the next frame, every draw and target, under diag::CaptureDir
inline std::atomic<bool> g_capture{false};

}  // namespace gpu
