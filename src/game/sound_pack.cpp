// Sound packs: a folder resourcepacks/<pack>/sounds/ beside the exe whose <name>.wav replaces the game's <name>.xbs.
// The Homefront sounds are the first pack (the "Homefront sfx" option). A sound is changed to what the game reads:
//  - the game's own sound files are one 32 byte header (Xbox ADPCM, mono, 22050 Hz mostly) and 36 byte blocks of 64
//    samples; the replacement keeps the original's header, so the rate and the format are as the game expects,
//  - the samples are made mono (the game positions its sounds itself) and resampled to the original's rate with a
//    windowed sinc, so the pitch and the length the game's own settings expect hold,
//  - the Xbox ADPCM is the IMA coder the console's decoder (dsound.cpp DecodeBlock) undoes: a block's first sample
//    is kept exactly in its header, then 64 steps follow.
// The game's own reads are left to it (its sizes, its memory, its tables): a replacement has the very size of the
// original, and is put over what the game read, in the buffer of 0x86450 (the file read, which 0x86970 makes too) or
// over the pak's copy 0x86360 hands out.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

bool OriginalSfxHeader(const std::string& name, uint8_t header[32], uint32_t* size);  // sounds.cpp
bool OriginalSfxBytes(const std::string& name, const uint8_t** data, uint32_t* size);  // sounds.cpp

namespace {

namespace fs = std::filesystem;

std::mutex g_lock;
std::unordered_map<std::string, std::vector<uint8_t>> g_cache;  // by name; empty: not replaced
std::unordered_map<std::string, std::vector<uint8_t>> g_full;   // the same sounds uncut, when longer than the original
std::unordered_map<std::string, uint32_t> g_blocks;             // the guest blocks the uncut ones were put in
constexpr size_t kMostBytes = 1u << 20;                         // the longest a replacement may be
std::atomic<int> g_converted{0};

const int kIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int kStep[89] = {7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
                       31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
                       130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
                       544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
                       2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
                       9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr double kPi = 3.14159265358979323846;

uint32_t U32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
uint16_t U16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

// a WAV file's samples as one channel in -1..1 (the channels averaged); false when it is not one this reads
bool ReadWav(const fs::path& path, std::vector<float>& mono, uint32_t& rate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return false;
    uint16_t tag = 0, channels = 0, bits = 0;
    const uint8_t* data = nullptr;
    size_t data_size = 0;
    rate = 0;
    for (size_t at = 12; at + 8 <= b.size();) {
        uint32_t size = U32(&b[at + 4]);
        const uint8_t* body = &b[at + 8];
        size_t avail = std::min<size_t>(size, b.size() - at - 8);
        if (!std::memcmp(&b[at], "fmt ", 4) && avail >= 16) {
            tag = U16(body);
            channels = U16(body + 2);
            rate = U32(body + 4);
            bits = U16(body + 14);
            if (tag == 0xFFFE && avail >= 26) tag = U16(body + 24);  // extensible: the sub format
        } else if (!std::memcmp(&b[at], "data", 4)) {
            data = body;
            data_size = avail;
            break;
        }
        at += 8 + size + (size & 1);
    }
    if (!data || !channels || !rate || !(tag == 1 || tag == 3)) return false;
    size_t bytes = bits / 8, frames = bytes ? data_size / (bytes * channels) : 0;
    if (!frames || bytes < 1 || bytes > 4 || (tag == 3 && bytes != 4)) return false;
    mono.resize(frames);
    for (size_t i = 0; i < frames; i++) {
        double sum = 0;
        for (uint16_t c = 0; c < channels; c++) {
            const uint8_t* p = data + (i * channels + c) * bytes;
            double v;
            if (tag == 3) {
                float x;
                std::memcpy(&x, p, 4);
                v = x;
            } else if (bytes == 1) v = (int(p[0]) - 128) / 128.0;
            else if (bytes == 2) v = int16_t(U16(p)) / 32768.0;
            else if (bytes == 3) v = (int32_t((uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 24)) >> 8) / 8388608.0;
            else v = int32_t(U32(p)) / 2147483648.0;
            sum += v;
        }
        mono[i] = float(sum / channels);
    }
    return true;
}

// the samples at another rate: a windowed sinc, its cut-off below the lower of the two rates' halves. The kernel is
// tabled at 1/1024 of a sample and read with a linear step: the sine and cosine of each tap made a first load of a
// level's sounds take seconds
std::vector<float> Resample(const std::vector<float>& in, uint32_t from, uint32_t to) {
    if (from == to) return in;
    double ratio = double(from) / to, fc = std::min(1.0, double(to) / from) * 0.96;
    const int zeros = 16;  // zero crossings each side
    long half = long(std::ceil(zeros / fc));
    constexpr int kRes = 1024;
    std::vector<float> table(size_t(half) * kRes + 2, 0.0f);  // the kernel at distance i / kRes
    for (size_t i = 0; i < table.size(); i++) {
        double d = double(i) / kRes, x = d * fc, w = d / double(half);
        double sinc = x < 1e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
        table[i] = d >= double(half) ? 0.0f : float(sinc * (0.5 + 0.5 * std::cos(kPi * w)) * fc);  // Hann
    }
    size_t n = size_t(std::llround(double(in.size()) / ratio));
    std::vector<float> out(n);
    for (size_t i = 0; i < n; i++) {
        double t = double(i) * ratio;
        long centre = long(std::floor(t));
        double sum = 0;
        long lo = std::max<long>(centre - half + 1, 0), hi = std::min<long>(centre + half, long(in.size()) - 1);
        for (long j = lo; j <= hi; j++) {
            double d = std::abs(double(j) - t) * kRes;
            size_t k = size_t(d);
            double f = d - double(k);
            sum += double(in[size_t(j)]) * (double(table[k]) + (double(table[k + 1]) - double(table[k])) * f);
        }
        out[i] = float(sum);
    }
    return out;
}

// one sample as a 4 bit step from the running prediction, as the console's decoder undoes it
int Step(int sample, int& pred, int& idx) {
    int step = kStep[idx], diff = sample - pred, nib = 0, d = step >> 3;
    if (diff < 0) {
        nib = 8;
        diff = -diff;
    }
    if (diff >= step) {
        nib |= 4;
        diff -= step;
        d += step;
    }
    if (diff >= (step >> 1)) {
        nib |= 2;
        diff -= step >> 1;
        d += step >> 1;
    }
    if (diff >= (step >> 2)) {
        nib |= 1;
        d += step >> 2;
    }
    pred = std::clamp(nib & 8 ? pred - d : pred + d, -32768, 32767);
    idx = std::clamp(idx + kIndex[nib], 0, 88);
    return nib;
}

// the 36 byte blocks of 64 samples: the block's first sample exact in its header, then 64 steps (the last is the next
// block's first, which the voice does not play)
void Encode(const std::vector<float>& x, std::vector<uint8_t>& out) {
    size_t blocks = (x.size() + 63) / 64;
    auto sample = [&](size_t i) {
        return i < x.size() ? int(std::clamp<long>(std::lround(double(x[i]) * 32767.0), -32768, 32767)) : 0;
    };
    int idx = 0;
    for (size_t b = 0; b < blocks; b++) {
        int pred = sample(b * 64);
        size_t at = out.size();
        out.resize(at + 36, 0);
        out[at] = uint8_t(pred & 0xFF);
        out[at + 1] = uint8_t((pred >> 8) & 0xFF);
        out[at + 2] = uint8_t(idx);
        for (int k = 0; k < 64; k++) {
            int nib = Step(sample(b * 64 + 1 + size_t(k)), pred, idx);
            out[at + 4 + size_t(k / 2)] |= uint8_t(nib << ((k & 1) * 4));
        }
    }
}

// A converted sound kept in cache/homefront_sfx/<name>.bin: the resampling and the encoding of the first ask took a
// level's load seconds, and were redone at every start. It holds the wav's size and time and the original's size, so a
// changed file or game converts again.
constexpr uint32_t kCacheTag = 0x31584653;  // "SFX1"

fs::path CachePath(const std::string& name) { return app::ExeDir() / "cache" / "homefront_sfx" / (name + ".bin"); }

uint64_t WavStamp(const fs::path& wav, uint64_t* size) {
    std::error_code ec;
    *size = fs::file_size(wav, ec);
    auto t = fs::last_write_time(wav, ec);
    return uint64_t(t.time_since_epoch().count());
}

bool LoadConverted(const std::string& name, const fs::path& wav, uint32_t original_size, std::vector<uint8_t>& bytes,
                   std::vector<uint8_t>& full) {
    std::ifstream in(CachePath(name), std::ios::binary);
    if (!in) return false;
    uint32_t head[3];
    uint64_t stamp[2];
    in.read(reinterpret_cast<char*>(head), sizeof(head));
    in.read(reinterpret_cast<char*>(stamp), sizeof(stamp));
    uint64_t size = 0, time = WavStamp(wav, &size);
    uint32_t sizes[2];
    in.read(reinterpret_cast<char*>(sizes), sizeof(sizes));
    if (!in || head[0] != kCacheTag || head[1] != original_size || stamp[0] != size || stamp[1] != time ||
        sizes[0] != original_size || sizes[1] > kMostBytes)
        return false;
    bytes.resize(sizes[0]);
    full.resize(sizes[1]);
    in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!full.empty()) in.read(reinterpret_cast<char*>(full.data()), std::streamsize(full.size()));
    if (!in) {
        bytes.clear();
        full.clear();
        return false;
    }
    return true;
}

void SaveConverted(const std::string& name, const fs::path& wav, uint32_t original_size, const std::vector<uint8_t>& bytes,
                   const std::vector<uint8_t>& full) {
    std::error_code ec;
    fs::create_directories(CachePath(name).parent_path(), ec);
    std::ofstream out(CachePath(name), std::ios::binary | std::ios::trunc);
    if (!out) return;
    uint32_t head[3] = {kCacheTag, original_size, 0};
    uint64_t size = 0, time = WavStamp(wav, &size), stamp[2] = {size, time};
    uint32_t sizes[2] = {uint32_t(bytes.size()), uint32_t(full.size())};
    out.write(reinterpret_cast<const char*>(head), sizeof(head));
    out.write(reinterpret_cast<const char*>(stamp), sizeof(stamp));
    out.write(reinterpret_cast<const char*>(sizes), sizeof(sizes));
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!full.empty()) out.write(reinterpret_cast<const char*>(full.data()), std::streamsize(full.size()));
}

struct Converted {
    std::vector<uint8_t> bytes, full;  // empty bytes: not replaced
};

// a sound's replacement: from the cache folder, or made from the wav (and kept there). No lock held: it takes a while.
Converted Convert(const std::string& name) {
    Converted c;
    fs::path wav = app::ExeDir() / "resourcepacks" / "homefront" / "sounds" / (name + ".wav");
    uint8_t header[32];
    uint32_t original_size = 0;
    std::error_code ec;
    if (!fs::exists(wav, ec) || !OriginalSfxHeader(name, header, &original_size)) return c;
    // converted before: read back, not resampled and encoded again
    if (LoadConverted(name, wav, original_size, c.bytes, c.full)) return c;
    std::vector<float> mono;
    uint32_t rate = 0;
    if (!ReadWav(wav, mono, rate)) return c;
    uint32_t target = U32(header + 4);
    if (U16(header) == 0x69 && U16(header + 2) == 1 && target >= 4000 && target <= 96000) {
        // exactly the original's size: the game sizes its memory by it, and what is longer would run over the
        // next file. A longer sound is cut with a fade of 2048 samples; a shorter one ends in silence.
        std::vector<float> samples = Resample(mono, rate, target);
        size_t most = (original_size - 32) / 36 * 64;
        if (samples.size() > most) {
            // the uncut one, for the load that can give it room (SoundPackUpgrade)
            std::vector<float> whole(samples.begin(), samples.begin() + std::min(samples.size(), (kMostBytes - 32) / 36 * 64));
            c.full.assign(header, header + 32);
            Encode(whole, c.full);
            samples.resize(most);
            for (size_t i = 0; i < 2048 && i < most; i++) samples[most - 1 - i] *= float(i) / 2048.0f;
            if (g_converted++ < 40) LOGR("soundpack: %s is longer than the original: cut to %zu samples where the game cannot give room", name.c_str(), most);
        }
        c.bytes.assign(header, header + 32);
        Encode(samples, c.bytes);
        c.bytes.resize(original_size, 0);
        SaveConverted(name, wav, original_size, c.bytes, c.full);
        if (g_converted++ < 6)
            LOGR("soundpack: %s from %s (%u Hz to %u Hz, %zu bytes)", name.c_str(), wav.string().c_str(), rate, target, c.bytes.size());
    } else if (g_converted++ < 6) {
        LOGR("soundpack: %s left as it is, its header is not mono Xbox ADPCM", name.c_str());
    }
    return c;
}

// the replacement for a sound, built on first ask; null when no pack has it or the option is off
const std::vector<uint8_t>* Replacement(const std::string& name) {
    if (!game::Opt().homefront_sfx) return nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_cache.find(name);
        if (it != g_cache.end()) return it->second.empty() ? nullptr : &it->second;
    }
    Converted c = Convert(name);
    std::lock_guard<std::mutex> lock(g_lock);
    auto [it, fresh] = g_cache.emplace(name, std::move(c.bytes));
    if (fresh && !c.full.empty()) g_full[name] = std::move(c.full);
    return it->second.empty() ? nullptr : &it->second;
}

// Once, a thread that makes the replacements of every sound in the pack's folder ahead of the game's asks, so a level's
// first load finds them made (and the cache folder full for the next start)
void PrepareAll() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            std::error_code ec;
            std::vector<std::string> names;
            for (auto& e : fs::directory_iterator(app::ExeDir() / "resourcepacks" / "homefront" / "sounds", ec))
                if (e.path().extension() == ".wav") names.push_back(e.path().stem().string());
            auto began = std::chrono::steady_clock::now();
            size_t made = 0;
            for (auto& n : names) {
                if (game::Opt().homefront_sfx) {
                    Replacement(n);
                    made++;
                }
            }
            LOGR("soundpack: %zu sounds made ready ahead in %.1f s", made,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count());
        }).detach();
    });
}

// a sound the game has loaded that a pack has: where the game's record points at it (record), the game's own block
// (orig, of the original's size) and the block of its own the longer replacement was given (big, or 0)
struct Slot {
    std::string name;
    uint32_t record = 0, orig = 0, big = 0;
    bool on = false;  // what its blocks hold now: the pack's sound or the original
};
std::mutex g_slot_lock;
std::vector<Slot> g_slots;
std::unordered_map<std::string, bool> g_has;  // by name: the pack has a wav for it

bool PackHas(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_slot_lock);
    auto it = g_has.find(name);
    if (it == g_has.end()) {
        std::error_code ec;
        it = g_has.emplace(name, fs::exists(app::ExeDir() / "resourcepacks" / "homefront" / "sounds" / (name + ".wav"), ec)).first;
    }
    return it->second;
}

// bytes over a block, its +0x14 (the length the game stored after the read) left as it is
void PutSound(uint32_t block, const uint8_t* data, size_t size) {
    uint32_t keep = kernel::R32(block + 0x14);
    std::memcpy(kernel::HostPtr(block), data, size);
    kernel::W32(block + 0x14, keep);
}

}  // namespace

// Each frame: the option changed since the sounds were loaded, so the loaded ones are swapped between the pack's and the
// original, in the game's memory (the voices read it as they play)
void SoundPackTick() {
    if (game::Opt().homefront_sfx) PrepareAll();  // not before the settings are in
    bool want = game::Opt().homefront_sfx;
    std::lock_guard<std::mutex> lock(g_slot_lock);
    for (size_t i = 0; i < g_slots.size();) {
        Slot& s = g_slots[i];
        if (s.on == want) {
            i++;
            continue;
        }
        uint32_t cur = s.on && s.big ? s.big : s.orig;
        bool alive = kernel::R32(s.record) == cur && kernel::R16(cur) == 0x69;
        const uint8_t* original = nullptr;
        uint32_t original_size = 0;
        const std::vector<uint8_t>* fitted = nullptr;
        if (alive && !OriginalSfxBytes(s.name, &original, &original_size)) alive = false;
        if (alive && want) {
            fitted = Replacement(s.name);
            if (!fitted || fitted->size() != original_size) alive = false;
        }
        if (!alive) {
            g_slots.erase(g_slots.begin() + long(i));
            continue;
        }
        if (want) {
            PutSound(s.orig, fitted->data(), fitted->size());
            uint32_t at = s.orig;
            if (s.big) {
                std::lock_guard<std::mutex> l2(g_lock);
                auto f = g_full.find(s.name);
                if (f != g_full.end()) {
                    PutSound(s.big, f->second.data(), f->second.size());
                    at = s.big;
                }
            }
            kernel::W32(s.record, at);
        } else {
            PutSound(s.orig, original, original_size);
            kernel::W32(s.record, s.orig);
        }
        s.on = want;
        static int logged = 0;
        if (logged++ < 10) LOGR("soundpack: %s switched %s", s.name.c_str(), want ? "to the pack" : "to the original");
        i++;
    }
}

// the sound a guest name stands for ("host0:sfx/male_gn22_08.xbs"): its replacement, or null
const std::vector<uint8_t>* SoundPackFor(uint32_t name_va) {
    if (name_va < 0x10000 || !game::Opt().homefront_sfx) return nullptr;
    std::string n = kernel::ReadCString(name_va, 160);
    if (n.size() < 5) return nullptr;
    for (char& c : n) c = char(std::tolower(uint8_t(c)));
    if (n.compare(n.size() - 4, 4, ".xbs") != 0) return nullptr;
    size_t slash = n.find_last_of("/\\:");
    if (slash != std::string::npos) n = n.substr(slash + 1);
    n.resize(n.size() - 4);
    return Replacement(n);
}

// the pak's own copy in memory of a sound (0x86360 (name)): a replaced one is written over in place, of its own size
X86_EXTERN_FUNC(__imp__sub_00086360);
X86_FUNC(sub_00086360) {
    uint32_t name = kernel::R32(ctx.esp + 4);
    __imp__sub_00086360(ctx, base);
    if (ctx.eax < 0x10000) return;
    if (const auto* sound = SoundPackFor(name)) {
        std::memcpy(kernel::HostPtr(ctx.eax), sound->data(), sound->size());
        static int n = 0;
        if (n++ < 5) LOGR("soundpack: %s put over the pak's copy at %08X (0x86360)", kernel::ReadCString(name, 64).c_str(), ctx.eax);
    }
}

// A level's sound records (0x1C7190 loads each: esi its record, whose first bytes are the file's name, edi the block it
// read, whose +0x14 is the sound's length less the header; the record table's +0x14 of the sound holds the block). Every
// sound a pack has is remembered here for SoundPackTick's live switch. A replacement longer than the original does not
// fit that block: a new one is made of the game's own allocator (it frees it as it does the others) and put in its
// place, its length in its header. Run after the block is stored (0x1C71E0).
void SoundPackUpgrade(X86Context& ctx, uint8_t*) {
    std::string n = kernel::ReadCString(ctx.esi, 160);
    for (char& c : n) c = char(std::tolower(uint8_t(c)));
    size_t slash = n.find_last_of("/\:");
    if (slash != std::string::npos) n = n.substr(slash + 1);
    if (n.size() < 5) return;
    n.resize(n.size() - 4);
    uint32_t records = kernel::R32(0x45188C);
    if (records < 0x10000 || ctx.edi < 0x10000 || !PackHas(n)) return;
    Slot slot{n, records + ctx.ebp + 0x14, ctx.edi, 0, game::Opt().homefront_sfx};
    const std::vector<uint8_t>* fitted = slot.on ? SoundPackFor(ctx.esi) : nullptr;
    const std::vector<uint8_t>* full = nullptr;
    if (fitted) {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_full.find(n);
        if (it != g_full.end() && it->second.size() > fitted->size()) full = &it->second;
    }
    if (full) {
        X86Context saved = ctx;
        uint32_t block = kernel::CallGuest(ctx, 0xE9D60, {(uint32_t(full->size()) + 15) & ~15u, 1});
        ctx = saved;
        if (block) {
            std::memcpy(kernel::HostPtr(block), full->data(), full->size());
            kernel::W32(block + 0x14, uint32_t(full->size()) - 0x20);
            kernel::W32(slot.record, block);
            slot.big = block;
            static int logged = 0;
            if (logged++ < 20) LOGR("soundpack: %s given room: %zu bytes in a block at %08X (the original %zu)", n.c_str(), full->size(), block, fitted->size());
        }
    }
    std::lock_guard<std::mutex> lock(g_slot_lock);
    for (Slot& o : g_slots)
        if (o.record == slot.record) {
            o = slot;
            return;
        }
    g_slots.push_back(slot);
}
