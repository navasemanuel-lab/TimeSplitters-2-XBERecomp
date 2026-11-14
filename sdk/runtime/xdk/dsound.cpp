// DirectSound (XDK 5849) on a host software mixer. Buffers and streams are read from guest memory as they play.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <vector>

#include "app/app.h"
#include "audio/audio.h"
#include "xbe/xbe.h"
#include "xdk/hle.h"
#include "xdk/layout.h"

using namespace kernel;

namespace kernel {
extern const xbe::Image* g_image;
}
namespace dispatch {
X86Func* Find(uint32_t guest);
}

// configs written before the SDK knew the name
#ifndef SYM_IDirectSound_SynchPlayback
#define SYM_IDirectSound_SynchPlayback FFFFFF00
#define HAS_IDirectSound_SynchPlayback 0
#endif
#ifndef SYM_IDirectSound_DownloadEffectsImage
#define SYM_IDirectSound_DownloadEffectsImage FFFFFF01
#define HAS_IDirectSound_DownloadEffectsImage 0
#endif
#ifndef SYM_IDirectSound_GetEffectData
#define SYM_IDirectSound_GetEffectData FFFFFF02
#define HAS_IDirectSound_GetEffectData 0
#endif
#ifndef SYM_IDirectSound_CommitEffectData
#define SYM_IDirectSound_CommitEffectData FFFFFF03
#define HAS_IDirectSound_CommitEffectData 0
#endif
#ifndef SYM_IDirectSound_GetOutputLevels
#define SYM_IDirectSound_GetOutputLevels FFFFFF04
#define HAS_IDirectSound_GetOutputLevels 0
#endif
#ifndef SYM_IDirectSoundBuffer_SetPlayRegion
#define SYM_IDirectSoundBuffer_SetPlayRegion FFFFFF05
#define HAS_IDirectSoundBuffer_SetPlayRegion 0
#endif
#ifndef SYM_IDirectSoundBuffer_SetAllParameters
#define SYM_IDirectSoundBuffer_SetAllParameters FFFFFF06
#define HAS_IDirectSoundBuffer_SetAllParameters 0
#endif
#ifndef SYM_IDirectSoundBuffer_SetFormat
#define SYM_IDirectSoundBuffer_SetFormat FFFFFF07
#define HAS_IDirectSoundBuffer_SetFormat 0
#endif
#ifndef SYM_IDirectSoundBuffer_SetOutputBuffer
#define SYM_IDirectSoundBuffer_SetOutputBuffer FFFFFF08
#define HAS_IDirectSoundBuffer_SetOutputBuffer 0
#endif
#ifndef SYM_IDirectSoundBuffer_SetRolloffCurve
#define SYM_IDirectSoundBuffer_SetRolloffCurve FFFFFF09
#define HAS_IDirectSoundBuffer_SetRolloffCurve 0
#endif
#ifndef SYM_IDirectSoundBuffer_StopEx
#define SYM_IDirectSoundBuffer_StopEx FFFFFF0A
#define HAS_IDirectSoundBuffer_StopEx 0
#endif
#ifndef SYM_DirectSoundUseLightHRTF
#define SYM_DirectSoundUseLightHRTF FFFFFF0B
#define HAS_DirectSoundUseLightHRTF 0
#endif
#ifndef SYM_IDirectSoundStream_SetEG
#define SYM_IDirectSoundStream_SetEG FFFFFF0C
#define HAS_IDirectSoundStream_SetEG 0
#endif
#ifndef SYM_IDirectSoundBuffer_Pause
#define SYM_IDirectSoundBuffer_Pause FFFFFF0D
#define HAS_IDirectSoundBuffer_Pause 0
#endif
#ifndef SYM_CDirectSoundStream_SetRolloffCurve
#define SYM_CDirectSoundStream_SetRolloffCurve FFFFFF0E
#define HAS_CDirectSoundStream_SetRolloffCurve 0
#endif

namespace {

constexpr uint32_t kStreamVtable = SYM(DSound_StreamVtable);  // CDirectSoundStream XMediaObject vtable
constexpr uint32_t kStreamVtable2 = SYM(DSound_StreamVtable2);
constexpr uint32_t kPacketPending = 0x8000000A, kFlushed = 0x80004004;  // E_PENDING, E_ABORT

struct Packet {
    uint32_t data, size, completed_ptr, status_ptr, context;
    uint32_t used = 0;
};

struct Voice {
    uint32_t obj = 0;
    bool stream = false;
    int refs = 1;
    // format
    uint16_t tag = 1, channels = 1, bits = 16, align = 2;
    uint32_t rate = 48000;
    // buffer
    // play region in bytes from data, loop region in bytes from the play region
    uint32_t data = 0, bytes = 0, loop_start = 0, loop_len = 0, play_start = 0, play_len = 0;
    uint32_t owned = 0;  // memory the library made for the buffer
    bool playing = false, looping = false, paused = false;
    bool sync = false;  // held until IDirectSound_SynchPlayback
    bool active = false, ending = false;  // stream: took a packet since it went quiet, quiet again once they run out
    double pos = 0;  // frames
    uint32_t freq = 0;
    // stream
    uint32_t max_packets = 1, callback = 0, context = 0;
    std::deque<Packet> packets;
    // levels
    float volume = 1, headroom = 1;
    uint8_t nbins = 0, bin[8] = {};
    int32_t bin_mb[32] = {};  // by mix bin
    std::vector<float> curve;  // rolloff
    bool is3d = false;
    uint32_t mode = 0;  // DS3DMODE: 1 head relative, 2 disabled
    float p[3] = {}, min_dist = 1, max_dist = 1e9f;
    bool placed = false;  // the title gave it a position
    bool has_room = false;  // SetI3DL2Source was called: its room level is what the voice sends to the reverb
    int32_t room_mb = 0;
    // the levels the last mix ended on: the next one glides from them, so a sound that moves or turns does not click
    float last[2][6] = {};  // by voice channel and speaker (audio::g_channels of them)
    bool mixed = false;
    float peak = 0;  // the loudest sample the buffer's play has mixed (XBE_TRACE_AUDIO)
    // ADPCM block cache
    int64_t block = -1;
    int16_t decoded[65 * 8];
};

struct Done {
    uint32_t voice, callback, context, packet_context, status;
};

std::recursive_mutex g_lock;
std::map<uint32_t, Voice> g_voices;
float g_listener[3] = {}, g_front[3] = {0, 0, 1}, g_top[3] = {0, 1, 0};
float g_rolloff = 1.0f;  // IDirectSound_SetRolloffFactor: 0 none, 1 the real world
// Completed packets' callbacks, run as the XDK runs them: in DirectSoundDoWork on the title's thread, so they never
// overlap the title's own stream code. A stream flushed or released runs its own at once, before it is gone.
std::deque<Done> g_done;
std::condition_variable_any g_done_cv;
thread_local std::vector<Done> t_flushed;  // run by the STREAM call that flushed them, once g_lock is let go
std::atomic<int64_t> g_pumped{0};          // the last DirectSoundDoWork, steady ms
bool g_started = false;
uint32_t g_ds = 0;

float Db(int32_t hundredths) { return hundredths <= -10000 ? 0.0f : std::pow(10.0f, hundredths / 2000.0f); }

uint32_t FramesOf(const Voice& v, uint32_t bytes) {
    if (v.tag == 0x69) return bytes / (36u * v.channels) * 64;  // WAVE_FORMAT_XBOX_ADPCM
    return bytes / std::max<uint32_t>(v.align, 1);
}
uint32_t BytesOf(const Voice& v, uint32_t frames) {
    if (v.tag == 0x69) return frames / 64 * 36 * v.channels;
    return frames * v.align;
}

const int kIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int kStep[89] = {7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
                       31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
                       130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
                       544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
                       2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
                       9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

// one 36 byte per channel IMA block, the voice plays 64 of its 65 frames
void DecodeBlock(const uint8_t* b, int ch, int16_t* out) {
    for (int c = 0; c < ch; c++) {
        int pred = int16_t(b[c * 4] | b[c * 4 + 1] << 8), idx = std::min<int>(b[c * 4 + 2], 88);
        out[c] = int16_t(pred);
        const uint8_t* d = b + ch * 4;
        for (int k = 0; k < 8; k++)
            for (int j = 0; j < 8; j++) {
                int n = (d[(k * ch + c) * 4 + j / 2] >> ((j & 1) * 4)) & 15;
                int step = kStep[idx], diff = step >> 3;
                if (n & 1) diff += step >> 2;
                if (n & 2) diff += step >> 1;
                if (n & 4) diff += step;
                pred = std::clamp(n & 8 ? pred - diff : pred + diff, -32768, 32767);
                idx = std::clamp(idx + kIndex[n], 0, 88);
                out[(1 + k * 8 + j) * ch + c] = int16_t(pred);
            }
    }
}

// sample of channel c at frame f of memory starting at data
float Sample(Voice& v, uint32_t data, uint32_t f, int c) {
    c = std::min<int>(c, v.channels - 1);
    const uint8_t* p = static_cast<const uint8_t*>(HostPtr(data));
    if (v.tag == 0x69) {
        int64_t at = int64_t(data) + int64_t(f / 64) * 36 * v.channels;
        if (at != v.block) {
            DecodeBlock(static_cast<const uint8_t*>(HostPtr(uint32_t(at))), std::min<int>(v.channels, 8), v.decoded);
            v.block = at;
        }
        return v.decoded[(f % 64) * v.channels + c] / 32768.0f;
    }
    if (v.bits == 8) return (p[f * v.align + c] - 128) / 128.0f;
    int16_t s;
    std::memcpy(&s, p + f * v.align + c * 2, 2);
    return s / 32768.0f;
}

void Complete(Voice& v, Packet& pk, uint32_t status) {
    if (pk.completed_ptr) W32(pk.completed_ptr, status == 0 ? pk.size : pk.used);
    if (pk.status_ptr) W32(pk.status_ptr, status);
    if (v.callback) {
        g_done.push_back({v.obj, v.callback, v.context, pk.context, status});
        g_done_cv.notify_one();
    } else if (pk.context) {
        if (ObjectRef e = FromHandle(pk.context)) SetEvent(*e);
    }
}

// the console's stereo downmix: centre, back pair and back crosstalk at -3 dB, the sub dropped
float StereoFold(uint32_t bin) {
    bool folded = bin == 2 || bin == 4 || bin == 5 || bin == 8 || bin == 9;
    return folded ? 0.707f : 1.0f;
}

// XBE_TRACE_AUDIO set: each voice's levels to the log every 3 seconds while it plays
bool TraceLevels(uint32_t obj) {
    static const bool on = std::getenv("XBE_TRACE_AUDIO") != nullptr;
    if (!on) return false;
    static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> logged;
    auto now = std::chrono::steady_clock::now();
    auto it = logged.find(obj);
    if (it != logged.end() && now - it->second < std::chrono::seconds(3)) return false;
    logged[obj] = now;
    return true;
}

// gain of voice channel c on each host speaker: left and right, or with 5.1 out also the centre, the sub and the back
// pair (the console's first six bins, in the same order). A hardware voice holds two channels and owns a run of
// slots, which alternate left and right.
void Levels(Voice& v, float g[2][6]) {
    for (int c = 0; c < 2; c++)
        for (int e = 0; e < 6; e++) g[c][e] = 0;
    const bool surround = audio::g_channels == 6;
    static const uint8_t kFront[2] = {0, 1};
    const uint8_t* bin = v.nbins ? v.bin : kFront;
    uint32_t n = v.nbins ? v.nbins : 2, voices = (std::max<uint32_t>(v.channels, 1) - 1) / 2 + 1;
    uint32_t per = std::max<uint32_t>(n / voices, 2), step = v.channels == 1 ? 1 : 2;
    bool speakers = false;
    for (uint32_t s = 0; s < n; s++) speakers |= bin[s] < 10 && bin[s] != 3;
    for (uint32_t c = 0; c < 2; c++) {
        for (uint32_t s = c; c < v.channels && s < per && s < n; s += step) {
            uint32_t b = bin[s];
            if (speakers && b > 9) continue;  // effect sends
            if (speakers && surround) {
                // the console's own speakers; its back crosstalk (6 to 9) to the back pair at -3 dB
                float x = Db(v.bin_mb[b]) * v.volume * v.headroom;
                if (b < 6) g[c][b] += x;
                else g[c][4 + (b & 1)] += x * 0.707f;
                continue;
            }
            if (speakers && b == 3) continue;  // the sub, on stereo
            float x = Db(v.bin_mb[b]) * v.volume * v.headroom * StereoFold(b);
            // even bins are left, odd right, 2 is the centre
            bool left = !speakers || b == 2 || !(b & 1);
            bool right = !speakers || b == 2 || (b & 1);
            // slots that reach one speaker add, as on the DSP; effect sends alone play the loudest
            if (speakers) {
                if (left) g[c][0] += x;
                if (right) g[c][1] += x;
            } else {
                g[c][0] = std::max(g[c][0], x);
                g[c][1] = std::max(g[c][1], x);
            }
        }
    }
    // a 3D voice the title never placed is mixed by its own bin volumes (TimeSplitters FP's dialogue streams): no
    // listener distance or pan of a position it does not have
    if (TraceLevels(v.obj ^ 1)) {
        {

            LOGR("audio: voice %08X %s %s%s vol %.3f head %.3f bins %u [%u:%d %u:%d %u:%d %u:%d] gain %.3f %.3f",
                 v.obj, v.stream ? "stream" : "buffer", v.is3d ? "3d" : "2d", v.placed ? " placed" : "", v.volume,
                 v.headroom, v.nbins, v.bin[0], v.bin_mb[v.bin[0]], v.bin[1], v.bin_mb[v.bin[1]], v.bin[2],
                 v.bin_mb[v.bin[2]], v.bin[3], v.bin_mb[v.bin[3]], g[0][0], g[0][1]);
        }
    }
    if (!v.is3d || v.mode == 2 || !v.placed) return;
    bool rel = v.mode == 1;  // the position is already in listener space
    float d[3] = {v.p[0], v.p[1], v.p[2]};
    for (int i = 0; i < 3 && !rel; i++) d[i] -= g_listener[i];
    // the XDK's distance (DSOUND 0x409D06) takes the position's x and z only: the height between the listener and the
    // sound does not count
    float dist = std::sqrt(d[0] * d[0] + d[2] * d[2]);
    float att = 1;
    if (dist > v.min_dist && v.curve.empty()) {
        // DirectSound's inverse distance law, scaled by the listener's rolloff factor
        att = v.min_dist / (v.min_dist + g_rolloff * (std::min(dist, v.max_dist) - v.min_dist));
    } else if (dist > v.min_dist && v.max_dist > v.min_dist) {
        // points spread evenly from min to max distance, 1.0 at min
        float at = (std::min(dist, v.max_dist) - v.min_dist) / (v.max_dist - v.min_dist) * v.curve.size();
        size_t i = std::min(size_t(at), v.curve.size() - 1);
        float from = i ? v.curve[i - 1] : 1.0f;
        att = from + (v.curve[i] - from) * (at - i);
    }
    float right[3] = {g_top[1] * g_front[2] - g_top[2] * g_front[1], g_top[2] * g_front[0] - g_top[0] * g_front[2],
                      g_top[0] * g_front[1] - g_top[1] * g_front[0]};
    float side = rel ? d[0] : d[0] * right[0] + d[1] * right[1] + d[2] * right[2];
    // a voice turned down by its distance, to the log without XBE_TRACE_AUDIO: at most every 3 seconds each, 150 in all
    if (att < 0.6f) {
        static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> told;
        static int lines = 0;
        auto now = std::chrono::steady_clock::now(), &last = told[v.obj];
        if (lines < 150 && now - last > std::chrono::seconds(3)) {
            last = now;
            lines++;
            LOGR("audio: voice %08X %s quieter by distance: dist %.2f (%.2f %.2f %.2f, listener %.2f %.2f %.2f) min %.2f max "
                 "%.2f mode %u curve %zu att %.3f vol %.3f",
                 v.obj, v.stream ? "stream" : "buffer", dist, v.p[0], v.p[1], v.p[2], g_listener[0], g_listener[1], g_listener[2], v.min_dist,
                 v.max_dist, v.mode, v.curve.size(), att, v.volume);
        }
    }
    if (TraceLevels(v.obj ^ 2)) {
        {
            LOGR("audio: voice %08X %s dist %.2f min %.2f max %.2f curve %zu att %.3f vol %.3f head %.3f bins %u",
                 v.obj, v.stream ? "stream" : "buffer", dist, v.min_dist, v.max_dist, v.curve.size(), att, v.volume,
                 v.headroom, v.nbins);
        }
    }
    // a stream (the characters' voices) as TimeSplitters FP PC (TheDoffman/timesplitters-future-perfect-pc) hears it:
    // the loudest of its bins plus the distance attenuation, alike on both sides, so a voice across the level is not
    // lost in one ear as well. Effects lean to their side as the title's light HRTF (DirectSoundUseLightHRTF, 0x279D5)
    // has them: heard by both ears at any distance, the far one at 3/4 at most to one side. Within a unit (the
    // player's own guns, at 0.1) they are centred, from one to two the lean grows in
    constexpr float kFarEar = 0.75f;
    float width = std::clamp(dist - 1.0f, 0.0f, 1.0f);
    float pan = dist > 1e-3f && !v.stream ? std::clamp(side / dist, -1.0f, 1.0f) * width * (1.0f - kFarEar) : 0.0f;
    for (int c = 0; c < 2; c++)
        for (int e = 0; e < 6; e++) {
            bool left = e == 0 || e == 4, right = e == 1 || e == 5;
            g[c][e] *= att * (left ? std::min(1.0f, 1.0f - pan) : right ? std::min(1.0f, 1.0f + pan) : 1.0f);
        }
    // 5.1: an effect behind the listener moves from the front pair to the back one, the power kept, by how far
    // behind it is; as the lean, from one to two units away (the player's own sounds stay in front)
    if (!surround || v.stream || dist <= 1e-3f) return;
    float ahead = rel ? d[2] : d[0] * g_front[0] + d[1] * g_front[1] + d[2] * g_front[2];
    float back = std::clamp(-ahead / dist, 0.0f, 1.0f) * width;
    float keep = std::sqrt(1.0f - back), move = std::sqrt(back);
    for (int c = 0; c < 2; c++)
        for (int e = 0; e < 2; e++) {
            g[c][4 + e] += g[c][e] * move;
            g[c][e] *= keep;
        }
}

// a stream goes quiet: the synch hold ends, a started one loses its pause as well
void Deactivate(Voice& v) {
    v.sync = false;
    if (v.active) v.active = v.paused = v.ending = false;
}

bool TraceAudio();

// The console's I3DL2 reverb, which its DSP ran from the listener the title sets room by room (a cave, a corridor, the
// open air each hand it other values): an early reflection and a late tail of combs and all passes, the tail's length
// the listener's decay time, its level the room level plus the reverb's. Each voice sends its room level of
// SetI3DL2Source (or an effect bin's) into it; a listener with a short decay or a low level (open ground) is nearly
// silent, a cave rings.
struct Reverb {
    static constexpr int kCombs = 6, kAll = 3;
    static constexpr int kCombLen[kCombs] = {1215, 1293, 1389, 1476, 1548, 1623};
    static constexpr int kAllLen[kAll] = {605, 480, 371};
    // the target the listener asks for, and what the mix has reached on the way
    float t_late = 0, t_early = 0, t_damp = 0.2f, t_fb[kCombs] = {}, t_delay = 0, t_pre = 0;
    float late = 0, early = 0, damp = 0.2f, fb[kCombs] = {};
    std::vector<float> comb[2][kCombs], all[2][kAll], line, pre;
    int ci[2][kCombs] = {}, ai[2][kAll] = {}, li = 0, pi = 0;
    float lp[2][kCombs] = {};
    bool ready = false;

    void Init() {
        for (int s = 0; s < 2; s++) {
            for (int i = 0; i < kCombs; i++) comb[s][i].assign(size_t(kCombLen[i] + s * 23), 0.0f);
            for (int i = 0; i < kAll; i++) all[s][i].assign(size_t(kAllLen[i] + s * 11), 0.0f);
        }
        line.assign(24000, 0.0f);
        pre.assign(24000, 0.0f);
        ready = true;
    }
    // DSI3DL2LISTENER: room, room HF, rolloff, decay time, decay HF ratio, reflections, their delay, reverb, its delay
    void Set(int32_t room, float decay, float hf_ratio, int32_t reflections, float refl_delay, int32_t reverb, float rev_delay) {
        decay = std::clamp(decay, 0.1f, 20.0f);
        t_late = Db(room + reverb) * 0.8f;
        t_early = Db(room + reflections) * 0.6f;
        t_damp = std::clamp(1.0f - std::min(hf_ratio, 1.0f), 0.0f, 0.9f) * 0.6f;
        t_delay = std::clamp(refl_delay, 0.0f, 0.4f);
        t_pre = std::clamp(rev_delay, 0.0f, 0.4f);
        for (int i = 0; i < kCombs; i++) t_fb[i] = std::pow(10.0f, -3.0f * kCombLen[i] / (audio::kRate * decay));
    }
    bool Active() const { return false; }  // the reverb is switched off (dropped for now)
    // send: the mono bus of the block; the wet sound is added to the speakers of out
    void Process(const float* send, float* out, uint32_t frames, uint32_t n) {
        if (!ready) Init();
        // the room changes glide, over about half a second, instead of jumping
        constexpr float kGlide = 0.04f;
        late += (t_late - late) * kGlide;
        early += (t_early - early) * kGlide;
        damp += (t_damp - damp) * kGlide;
        for (int i = 0; i < kCombs; i++) fb[i] += (t_fb[i] - fb[i]) * kGlide;
        const int lsz = int(line.size()), psz = int(pre.size());
        const int dl = std::clamp(int(t_delay * audio::kRate), 1, lsz - 1), dr = std::min(dl + 37, lsz - 1);
        const int dp = std::clamp(int(t_pre * audio::kRate), 1, psz - 1);
        for (uint32_t f = 0; f < frames; f++) {
            float x = send[f];
            line[size_t(li)] = x;
            pre[size_t(pi)] = x;
            float er[2] = {line[size_t((li - dl + lsz) % lsz)], line[size_t((li - dr + lsz) % lsz)]};
            float in = pre[size_t((pi - dp + psz) % psz)] * 0.2f;
            li = (li + 1) % lsz;
            pi = (pi + 1) % psz;
            float wet[2];
            for (int s = 0; s < 2; s++) {
                float sum = 0;
                for (int i = 0; i < kCombs; i++) {
                    std::vector<float>& c = comb[s][i];
                    float y = c[size_t(ci[s][i])];
                    lp[s][i] = y * (1.0f - damp) + lp[s][i] * damp;
                    c[size_t(ci[s][i])] = in + lp[s][i] * fb[i];
                    if (++ci[s][i] >= int(c.size())) ci[s][i] = 0;
                    sum += y;
                }
                for (int i = 0; i < kAll; i++) {
                    std::vector<float>& a = all[s][i];
                    float b = a[size_t(ai[s][i])];
                    float y = b - sum;
                    a[size_t(ai[s][i])] = sum + b * 0.5f;
                    if (++ai[s][i] >= int(a.size())) ai[s][i] = 0;
                    sum = y;
                }
                wet[s] = sum * late + er[s] * early;
            }
            if (n >= 2) {
                out[f * n] += wet[0];
                out[f * n + 1] += wet[1];
            }
            if (n >= 6) {
                out[f * n + 4] += wet[0] * 0.6f;
                out[f * n + 5] += wet[1] * 0.6f;
            }
        }
    }
};
Reverb g_reverb;
bool g_reverb_locked = false;  // the host sets the room's reverb (DsoundRoomReverb): the title's own listener calls are left out
std::vector<float> g_send;

void Mix(float* out, uint32_t frames) {
    struct Timed {
        std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
        ~Timed() {
            audio::g_mix_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now() - t)
                                            .count());
        }
    } timed;
    const uint32_t n = audio::g_channels;
    std::memset(out, 0, frames * n * sizeof(float));
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    const bool wet = g_reverb.Active();
    g_send.assign(frames, 0.0f);
    for (auto& [obj, v] : g_voices) {
        if (v.paused || v.sync || (!v.stream && !v.playing)) {
            v.mixed = false;
            continue;
        }
        float g[2][6], from[2][6], dg[2][6];
        Levels(v, g);
        float send = 0;
        if (wet) {
            if (v.has_room) send = Db(v.room_mb);
            for (uint32_t s = 0; s < v.nbins; s++)
                if (v.bin[s] >= 10) send = std::max(send, Db(v.bin_mb[v.bin[s]]));
            send *= v.volume * v.headroom * 0.5f;
        }
        // from the last mix's levels to these across the block; a voice just started takes them at once
        for (int c = 0; c < 2; c++)
            for (uint32_t e = 0; e < n; e++) {
                from[c][e] = v.mixed ? v.last[c][e] : g[c][e];
                dg[c][e] = (g[c][e] - from[c][e]) / float(std::max<uint32_t>(frames, 1));
                v.last[c][e] = g[c][e];
            }
        v.mixed = true;
        uint32_t base = v.data + v.play_start;
        double step = double(v.freq ? v.freq : v.rate) / audio::kRate;
        for (uint32_t i = 0; i < frames; i++) {
            uint32_t f = uint32_t(v.pos);
            float a, b, a1, b1;  // this frame and the next, interpolated between
            if (v.stream) {
                while (!v.packets.empty() && f >= FramesOf(v, v.packets.front().size)) {
                    Packet pk = v.packets.front();
                    v.packets.pop_front();
                    pk.used = pk.size;
                    Complete(v, pk, 0);
                    v.pos -= FramesOf(v, pk.size);
                    f = uint32_t(v.pos);
                }
                if (v.packets.empty()) {
                    v.pos = 0;
                    if (v.ending) Deactivate(v);
                    break;
                }
                Packet& pk = v.packets.front();
                a = Sample(v, pk.data, f, 0);
                b = Sample(v, pk.data, f, 1);
                if (f + 1 < FramesOf(v, pk.size)) {
                    a1 = Sample(v, pk.data, f + 1, 0);
                    b1 = Sample(v, pk.data, f + 1, 1);
                } else if (v.packets.size() > 1) {
                    a1 = Sample(v, v.packets[1].data, 0, 0);
                    b1 = Sample(v, v.packets[1].data, 0, 1);
                } else {
                    a1 = a;
                    b1 = b;
                }
            } else {
                uint32_t start = v.looping ? FramesOf(v, v.loop_start) : 0;
                uint32_t end = FramesOf(v, v.looping ? v.loop_start + v.loop_len : v.play_len);
                if (f >= end) {
                    if (!v.looping || end <= start) {
                        if (TraceAudio()) {
                            static int told = 0;
                            if (told++ < 200) LOGR("audio: buffer %08X finished at frame %.0f of %u, loudest sample %.3f, frequency %u (rate %u), from byte %u of %u", v.obj, v.pos, end, v.peak, v.freq, v.rate, v.play_start, v.bytes);
                        }
                        v.playing = v.paused = false;
                        v.pos = 0;
                        v.peak = 0;
                        break;
                    }
                    v.pos = start + std::fmod(v.pos - start, double(end - start));
                    f = uint32_t(v.pos);
                }
                if (!v.data) break;
                a = Sample(v, base, f, 0);
                b = Sample(v, base, f, 1);
                uint32_t next = f + 1 < end ? f + 1 : v.looping ? start : f;
                a1 = Sample(v, base, next, 0);
                b1 = Sample(v, base, next, 1);
                v.peak = std::max(v.peak, std::max(std::abs(a), std::abs(b)));
            }
            float t = float(v.pos - f);
            a += (a1 - a) * t;
            b += (b1 - b) * t;
            float k = float(i + 1);
            if (send > 0) g_send[i] += (a + b) * send;
            for (uint32_t e = 0; e < n; e++)
                out[i * n + e] += a * (from[0][e] + dg[0][e] * k) + b * (from[1][e] + dg[1][e] * k);
            v.pos += step;
        }
    }
    if (wet || g_reverb.Active()) g_reverb.Process(g_send.data(), out, frames, n);
    // the mix a little under full scale, its peaks bent round instead of cut off: many sounds at once (a fight, every
    // cry of every character) no longer clip into a harsh buzz
    constexpr float kKnee = 0.6f;
    const float kMaster = 0.7f * std::pow(10.0f, float(app::Get().audio_boost) / 20.0f);
    for (uint32_t i = 0; i < frames * n; i++) {
        float x = out[i] * kMaster, a = std::abs(x);
        if (a > kKnee) x = std::copysign(kKnee + (1 - kKnee) * std::tanh((a - kKnee) / (1 - kKnee)), x);
        out[i] = x;
    }
    // mono: both speakers play the two sides together
    if (audio::g_mono && n == 2)
        for (uint32_t i = 0; i < frames; i++) out[i * 2] = out[i * 2 + 1] = 0.5f * (out[i * 2] + out[i * 2 + 1]);
}

void Render(float* out, uint32_t frames) {
    const uint32_t ch = audio::g_channels;
    // the game paused in the background (app::Pace): silence, the voices keep their place
    if (app::g_paused) return std::fill(out, out + frames * ch, 0.0f);
    Mix(out, frames);
}

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void Run(X86Context& ctx, const std::vector<Done>& done) {
    for (const Done& d : done) CallGuest(ctx, d.callback, {d.context, d.packet_context, d.status});
}

void Start() {
    if (g_started) return;
    g_started = true;
    audio::StartOutput(Render);
    // for a title that never calls DirectSoundDoWork: its callbacks come from here instead
    StartHostThread("dsound", [](X86Context& ctx) {
        constexpr int64_t kPumpMs = 250;
        for (;;) {
            std::vector<Done> done;
            {
                std::unique_lock<std::recursive_mutex> lock(g_lock);
                g_done_cv.wait_for(lock, std::chrono::milliseconds(50));
                if (NowMs() - g_pumped < kPumpMs) continue;
                done.assign(g_done.begin(), g_done.end());
                g_done.clear();
            }
            Run(ctx, done);
        }
    });
}

// WAVEFORMATEX
void ReadFormat(Voice& v, uint32_t wfx) {
    if (!wfx) return;
    v.tag = R16(wfx);
    v.channels = std::max<uint16_t>(R16(wfx + 2), 1);
    v.rate = R32(wfx + 4);
    v.align = std::max<uint16_t>(R16(wfx + 12), 1);
    v.bits = R16(wfx + 14);
    if (v.tag != 1 && v.tag != 0x69) LOG_FIRST(8, "dsound: format tag %X not supported", v.tag);
}

// DSMIXBINS: the voice's bins and their volumes
void SetBins(Voice& v, uint32_t mixbins) {
    v.nbins = 0;  // none listed: front left and right
    if (!mixbins) return;
    uint32_t n = std::min<uint32_t>(R32(mixbins), 8), pairs = R32(mixbins + 4);
    for (uint32_t i = 0; i < n; i++) {
        v.bin[i] = R32(pairs + i * 8) & 31;
        v.bin_mb[v.bin[i]] = int32_t(R32(pairs + i * 8 + 4));
    }
    v.nbins = uint8_t(n);
}
// volumes only, the bins stay
void SetBinVolumes(Voice& v, uint32_t mixbins) {
    if (!mixbins) return;
    uint32_t n = std::min<uint32_t>(R32(mixbins), 32), pairs = R32(mixbins + 4);
    for (uint32_t i = 0; i < n; i++) v.bin_mb[R32(pairs + i * 8) & 31] = int32_t(R32(pairs + i * 8 + 4));
}
// 600 mB unless 3D or a mix in or effect in buffer
// XBE_TRACE_AUDIO set: voices made and fed go to the log, for debugging
bool TraceAudio() {
    static const bool on = std::getenv("XBE_TRACE_AUDIO") != nullptr;
    return on;
}
float DefaultHeadroom(uint32_t flags) { return flags & 0x382010 ? 1.0f : Db(-600); }

Voice* Find(uint32_t obj) {
    auto it = g_voices.find(obj);
    return it == g_voices.end() ? nullptr : &it->second;
}

uint32_t NewBuffer(uint32_t desc, uint32_t out) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Start();
    uint32_t obj = mem::AllocSystem(0x40);
    std::memset(HostPtr(obj), 0, 0x40);
    Voice& v = g_voices[obj];
    v.obj = obj;
    uint32_t flags = R32(desc + 4), bytes = R32(desc + 8);  // DSBUFFERDESC: size, flags, bytes, format, mix bins
    ReadFormat(v, R32(desc + 12));
    SetBins(v, R32(desc + 16));
    v.is3d = flags & 0x10;  // DSBCAPS_CTRL3D
    v.headroom = DefaultHeadroom(flags);
    if (TraceAudio()) LOGR("audio: buffer %08X flags %X bytes %u tag %X ch %u rate %u", obj, flags, bytes, v.tag, v.channels, v.rate);
    if (bytes) {
        v.data = v.owned = mem::AllocContiguous(bytes, 0, 0xFFFFFFFF, 4);
        v.bytes = v.play_len = v.loop_len = bytes;
    }
    W32(out, obj);
    return 0;
}

float F32(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

}  // namespace

// The reverb of the room the player is in, from the host (src/game/reverb_rooms.cpp); until it is switched off the title's
// own listener calls do not change it. Rooms with no preset: DsoundRoomReverbOff, the title's calls rule again.
void DsoundRoomReverb(int room, float decay, float hf_ratio, int reflections, float refl_delay, int reverb, float rev_delay) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_reverb.Set(room, decay, hf_ratio, reflections, refl_delay, reverb, rev_delay);
    g_reverb_locked = true;
}
void DsoundRoomReverbOff() {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    g_reverb_locked = false;
}

// device

// DSOUND objects are the host's; .XTLID names linked exports that would still run console code on them
static void AuditExports() {
    static const uint32_t handled[] = {
        SYM(DirectSoundCreate), SYM(IDirectSound_Release), SYM(DirectSoundUseFullHRTF), SYM(DirectSoundUseLightHRTF),
        SYM(IDirectSound_GetSpeakerConfig),
        SYM(IDirectSound_SetEffectData), SYM(IDirectSound_EnableHeadphones), SYM(IDirectSound_SetMixBinHeadroom),
        SYM(IDirectSound_CommitDeferredSettings), SYM(IDirectSound_SetDopplerFactor), SYM(IDirectSound_SetRolloffFactor),
        SYM(IDirectSound_SetDistanceFactor), SYM(IDirectSound_SetVelocity), SYM(IDirectSound_SetI3DL2Listener),
        SYM(IDirectSound_SetPosition), SYM(IDirectSound_SetOrientation), SYM(IDirectSound_SynchPlayback), SYM(DirectSoundDoWork),
        SYM(IDirectSound_DownloadEffectsImage), SYM(IDirectSound_GetEffectData), SYM(IDirectSound_CommitEffectData),
        SYM(IDirectSound_GetOutputLevels), SYM(IDirectSoundBuffer_SetPlayRegion), SYM(IDirectSoundBuffer_SetAllParameters),
        SYM(IDirectSoundBuffer_SetFormat), SYM(IDirectSoundBuffer_SetOutputBuffer), SYM(IDirectSoundBuffer_SetRolloffCurve),
        SYM(IDirectSoundBuffer_StopEx),
        SYM(XAudioDownloadEffectsImage), SYM(DirectSoundCreateBuffer), SYM(IDirectSound_CreateSoundBuffer),
        SYM(IDirectSoundBuffer_Release), SYM(IDirectSoundBuffer_Unlock), SYM(IDirectSoundBuffer_SetBufferData),
        SYM(IDirectSoundBuffer_Play), SYM(IDirectSoundBuffer_Stop), SYM(IDirectSoundBuffer_SetLoopRegion),
        SYM(IDirectSoundBuffer_GetStatus), SYM(IDirectSoundBuffer_GetCurrentPosition), SYM(IDirectSoundBuffer_SetCurrentPosition),
        SYM(IDirectSoundBuffer_Lock), SYM(IDirectSoundBuffer_SetVolume), SYM(IDirectSoundBuffer_SetFrequency),
        SYM(IDirectSoundBuffer_SetFilter), SYM(IDirectSoundBuffer_SetHeadroom), SYM(IDirectSoundBuffer_SetMixBinVolumes),
        SYM(IDirectSoundBuffer_SetMixBins), SYM(IDirectSoundBuffer_SetMaxDistance), SYM(IDirectSoundBuffer_SetMinDistance),
        SYM(IDirectSoundBuffer_SetPosition), SYM(IDirectSoundBuffer_SetVelocity), SYM(IDirectSoundBuffer_SetI3DL2Source),
        SYM(DirectSoundCreateStream), SYM(CDirectSoundStream_AddRef), SYM(CDirectSoundStream_SetFormat),
        SYM(CDirectSoundStream_Release), SYM(CDirectSoundStream_GetInfo), SYM(CDirectSoundStream_GetStatus),
        SYM(CDirectSoundStream_Process), SYM(CDirectSoundStream_Discontinuity), SYM(CDirectSoundStream_Flush),
        SYM(CDirectSoundStream_FlushEx), SYM(CDirectSoundStream_Pause), SYM(CDirectSoundStream_SetVolume),
        SYM(CDirectSoundStream_SetFrequency), SYM(CDirectSoundStream_SetFilter), SYM(CDirectSoundStream_SetHeadroom),
        SYM(CDirectSoundStream_SetMixBinVolumes), SYM(CDirectSoundStream_SetMixBins), SYM(CDirectSoundStream_SetMaxDistance),
        SYM(CDirectSoundStream_SetMinDistance), SYM(CDirectSoundStream_SetPosition), SYM(CDirectSoundStream_SetVelocity),
        SYM(CDirectSoundStream_SetI3DL2Source), SYM(IDirectSoundStream_SetEG), SYM(IDirectSoundBuffer_Pause),
        SYM(CDirectSoundStream_SetRolloffCurve)};
    std::string left;
    // the library's own section: the WMA decoder shares the export ids and touches no DSOUND object
    uint32_t lo = 0, hi = 0;
    for (auto& s : g_image->sections)
        if (SYM(DirectSoundCreate) - s.va < s.vsize) lo = s.va, hi = s.va + s.vsize;
    for (auto& s : g_image->sections)
        for (uint32_t o = 0; s.name == ".XTLID" && o + 8 <= std::min(s.vsize, s.rsize); o += 8) {
            uint32_t id = g_image->U32(s.raw + o), va = g_image->U32(s.raw + o + 4);
            auto done = [&](uint32_t f) { return std::find(std::begin(handled), std::end(handled), f) != std::end(handled); };
            // a jmp to a host version is covered by it
            if (id >> 16 != 0xB || va < lo || va >= hi || !dispatch::Find(va) || done(va) ||  // 0xB: DSOUND's library id
                (G<uint8_t>(va) == 0xE9 && done(va + 5 + R32(va + 1))))
                continue;
            char b[24];
            std::snprintf(b, sizeof b, " %05X at %08X", id, va);
            left += b;
        }
    if (!left.empty()) LOGW("dsound: exports running their original code:%s", left.c_str());
}

HLE_STDCALL(DirectSoundCreate, 3) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Start();
    if (!g_ds) {
        AuditExports();
        g_ds = mem::AllocSystem(0x40);
        std::memset(HostPtr(g_ds), 0, 0x40);
    }
    W32(a[1], g_ds);
    return 0;
}
HLE_STDCALL(IDirectSound_Release, 1) { return 0; }
HLE_STDCALL(DirectSoundUseFullHRTF, 0) { return 0; }
HLE_STDCALL(DirectSoundUseLightHRTF, 0) { return 0; }
HLE_STDCALL(IDirectSound_GetSpeakerConfig, 2) {
    W32(a[1], 0);  // DSSPEAKER_STEREO
    return 0;
}
HLE_STDCALL(IDirectSound_SetEffectData, 6) {
    // (this, effect, offset, data, size, apply): the title tunes the DSP effects here. Logged to learn what it sends; a
    // block the size of the I3DL2 listener (12 dwords) with room-like values is the reverb of the room, taken as such
    static int told = 0;
    uint32_t effect = a[1], offset = a[2], data = a[3], size = a[4];
    if (told++ < 80) {
        char buf[400];
        int len = snprintf(buf, sizeof(buf), "dsound: effect data %u offset %u size %u:", effect, offset, size);
        for (uint32_t i = 0; i < 12 && i * 4 < size && len < 360; i++)
            len += snprintf(buf + len, sizeof(buf) - size_t(len), " %d/%g", int32_t(R32(data + i * 4)), F32(R32(data + i * 4)));
        LOGR("%s", buf);
    }
    if (data && size == 48 && !g_reverb_locked) {
        int32_t room = int32_t(R32(data));
        float decay = F32(R32(data + 12));
        if (room >= -10000 && room <= 1000 && std::isfinite(decay) && decay >= 0.05f && decay <= 30.0f) {
            std::lock_guard<std::recursive_mutex> lock(g_lock);
            g_reverb.Set(room, decay, F32(R32(data + 16)), int32_t(R32(data + 20)), F32(R32(data + 24)),
                         int32_t(R32(data + 28)), F32(R32(data + 32)));
        }
    }
    return 0;
}
HLE_STDCALL(IDirectSound_EnableHeadphones, 2) { return 0; }
HLE_STDCALL(IDirectSound_SetMixBinHeadroom, 3) { return 0; }
HLE_STDCALL(IDirectSound_CommitDeferredSettings, 1) { return 0; }
HLE_STDCALL(IDirectSound_SetDopplerFactor, 3) { return 0; }
HLE_STDCALL(IDirectSound_SetRolloffFactor, 3) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    float f = F32(a[1]);
    if (std::isfinite(f) && f >= 0 && f <= 10) g_rolloff = f;
    LOG_FIRST(4, "dsound: rolloff factor %.3f", f);
    return 0;
}
HLE_STDCALL(IDirectSound_SetDistanceFactor, 3) { return 0; }
HLE_STDCALL(IDirectSound_SetVelocity, 5) { return 0; }
HLE_STDCALL(IDirectSound_SetI3DL2Listener, 3) {
    if (uint32_t l = a[1]; l && !g_reverb_locked) {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        g_reverb.Set(int32_t(R32(l)), F32(R32(l + 12)), F32(R32(l + 16)), int32_t(R32(l + 20)), F32(R32(l + 24)),
                     int32_t(R32(l + 28)), F32(R32(l + 32)));
        static int told = 0;
        if (told++ < 40) LOGR("dsound: reverb room %d decay %.2f hf ratio %.2f reflections %d (%.3f s) reverb %d (%.3f s)",
                              int32_t(R32(l)), F32(R32(l + 12)), F32(R32(l + 16)), int32_t(R32(l + 20)), F32(R32(l + 24)),
                              int32_t(R32(l + 28)), F32(R32(l + 32)));
    }
    return 0;
}
HLE_STDCALL(IDirectSound_SetPosition, 5) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    for (int i = 0; i < 3; i++) g_listener[i] = F32(a[1 + i]);
    return 0;
}
HLE_STDCALL(IDirectSound_SetOrientation, 8) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    for (int i = 0; i < 3; i++) {
        g_front[i] = F32(a[1 + i]);
        g_top[i] = F32(a[4 + i]);
    }
    return 0;
}
HLE_STDCALL(DirectSoundDoWork, 0) {
    g_pumped = NowMs();
    std::vector<Done> done;
    {
        std::lock_guard<std::recursive_mutex> lock(g_lock);
        done.assign(g_done.begin(), g_done.end());
        g_done.clear();
    }
    Run(ctx, done);
    return 0;
}
// starts together every voice played or unpaused with the synch playback flag
HLE_STDCALL(IDirectSound_SynchPlayback, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    for (auto& [obj, v] : g_voices) v.sync = false;
    return 0;
}

// Effects run on the console's DSP and are not reproduced.
// the descriptor keeps the image's effect list so the title finds its indices, every segment points at zeroed memory
static uint32_t EffectsDesc(const std::vector<uint8_t>& img, const char* name) {
    XDK_REQUIRE("dsound", XDK_KEY(EffectsImage_Skip0), XDK_KEY(EffectsImage_Skip1), XDK_KEY(EffectsImage_Desc));
    auto u32 = [&](size_t o) { uint32_t v = 0; if (o + 4 <= img.size()) std::memcpy(&v, &img[o], 4); return v; };
    size_t t = XDK_EffectsImage_Desc + size_t(u32(XDK_EffectsImage_Skip0) + u32(XDK_EffectsImage_Skip1)) * 4;
    uint32_t count = std::min<uint32_t>(u32(t), 64);
    LOGI("dsound: effects image %s, %u effects", name, count);
    uint32_t desc = mem::AllocSystem(8 + std::max(count, 1u) * 32);
    W32(desc, count);
    W32(desc + 4, u32(t + 4));
    for (uint32_t i = 0; i < count; i++) {
        size_t m = t + 8 + i * 32;
        uint32_t d = desc + 8 + i * 32;
        for (int k = 0; k < 4; k++) {
            uint32_t size = u32(m + k * 8 + 4);
            uint32_t seg = mem::AllocSystem(std::max(size, 0x40u));
            std::memset(HostPtr(seg), 0, std::max(size, 0x40u));
            W32(d + k * 8, seg);
            W32(d + k * 8 + 4, size);
        }
    }
    return desc;
}

HLE_STDCALL(XAudioDownloadEffectsImage, 4) {
    std::string name = ReadCString(a[0]);
    std::vector<uint8_t> img;
    if (a[2] & 1) {
        for (auto& sec : g_image->sections)
            if (sec.name == name) img.assign(static_cast<uint8_t*>(HostPtr(sec.va)), static_cast<uint8_t*>(HostPtr(sec.va)) + sec.vsize);
    } else {
        ReadGameFile(name, img);
    }
    uint32_t desc = EffectsDesc(img, name.c_str());
    if (a[3]) W32(a[3], desc);
    return 0;
}

// (this, image, size, location, descriptor out): the title loaded the image itself
HLE_STDCALL(IDirectSound_DownloadEffectsImage, 5) {
    std::vector<uint8_t> img(static_cast<uint8_t*>(HostPtr(a[1])), static_cast<uint8_t*>(HostPtr(a[1])) + a[2]);
    uint32_t desc = EffectsDesc(img, "from memory");
    if (a[4]) W32(a[4], desc);
    return 0;
}
// (this, effect, offset, data, size): SetEffectData keeps nothing, so effect memory reads as zero
HLE_STDCALL(IDirectSound_GetEffectData, 5) {
    if (a[3]) std::memset(HostPtr(a[3]), 0, a[4]);
    return 0;
}
HLE_STDCALL(IDirectSound_CommitEffectData, 1) { return 0; }
// (this, DSOUTPUTLEVELS out, reset peaks): 16 dwords of meters, silent
HLE_STDCALL(IDirectSound_GetOutputLevels, 3) {
    if (a[1]) std::memset(HostPtr(a[1]), 0, 64);
    return 0;
}

// buffers

HLE_STDCALL(DirectSoundCreateBuffer, 2) { return NewBuffer(a[0], a[1]); }
HLE_STDCALL(IDirectSound_CreateSoundBuffer, 4) { return NewBuffer(a[1], a[2]); }

#define BUFFER(name, nargs)                                 \
    static uint32_t buf_##name(Voice& v, const Args& a);    \
    HLE_STDCALL(IDirectSoundBuffer_##name, nargs) {         \
        std::lock_guard<std::recursive_mutex> lock(g_lock); \
        Voice* v = Find(a[0]);                              \
        return v ? buf_##name(*v, a) : 0x80004005u;         \
    }                                                       \
    static uint32_t buf_##name([[maybe_unused]] Voice& v, [[maybe_unused]] const Args& a)

BUFFER(Release, 1) {
    if (--v.refs > 0) return uint32_t(v.refs);
    if (v.owned) mem::FreeContiguous(v.owned);
    uint32_t obj = v.obj;
    g_voices.erase(obj);
    mem::FreeSystem(obj);
    return 0;
}
BUFFER(Unlock, 5) { return 0; }
BUFFER(SetBufferData, 3) {
    if (TraceAudio()) LOG_FIRST(40, "audio: buffer %08X data %08X bytes %u", v.obj, a[1], a[2]);
    if (a[1] == v.data && a[2] == v.bytes) {
        if (TraceAudio() && v.pos > 0)
            LOGR("audio: buffer %08X same data again at frame %.0f of %u%s", v.obj, v.pos, FramesOf(v, v.bytes),
                 v.playing ? " playing" : "");
        return 0;
    }
    // the library frees its own memory, and makes it again for a size with no data
    if (v.owned && v.owned != a[1]) {
        mem::FreeContiguous(v.owned);
        v.owned = 0;
    }
    v.data = a[1];
    if (!a[1] && a[2]) v.data = v.owned = mem::AllocContiguous(a[2], 0, 0xFFFFFFFF, 4);
    v.bytes = v.play_len = v.loop_len = a[2];
    v.play_start = v.loop_start = 0;
    v.pos = 0;
    v.block = -1;
    return 0;
}
BUFFER(Play, 4) {
    if (TraceAudio()) {
        uint32_t zero = 0, n = std::min<uint32_t>(v.bytes, 8192);
        for (uint32_t i = 0; i < n; i++) zero += (R16(v.data + (i & ~1u)) >> ((i & 1) * 8) & 0xFF) == 0;
        LOGR("audio: play %08X flags %X bytes %u data %08X%s, %u of the first %u bytes zero", v.obj, a[3], v.bytes, v.data,
             v.paused ? " paused" : "", zero, n);
    }
    // TimeSplitters 2 sets the pitch of its voices as the rate in the sound's own header times a factor (0x1C62FE..0x1C636E);
    // for some character sounds that rate is 0x6B72616D, the letters "mark", where the header lacks one: played at that
    // speed the cry ended in a millisecond. A frequency no sound has is the sound's own rate, or 22050.
    if (v.freq && (v.freq < 1000 || v.freq > 200000)) {
        uint32_t own = v.data >= 0x20 ? R32(v.data - 0x20 + 4) : 0;
        static int told = 0;
        if (told++ < 10) {
            LOGR("audio: buffer %08X asked for a frequency of %u: played at %u", v.obj, v.freq, own >= 8000 && own <= 48000 ? own : 22050u);
            // the bytes before the data (the header the game reads the rate from) and the first of the data
            for (uint32_t at : {v.data - 0x20, v.data, v.data + 0x20})
                LOGR("audio:   %08X: %08X %08X %08X %08X %08X %08X %08X %08X", at, R32(at), R32(at + 4), R32(at + 8), R32(at + 12),
                     R32(at + 16), R32(at + 20), R32(at + 24), R32(at + 28));
        }
        v.freq = own >= 8000 && own <= 48000 ? own : 22050;
    }
    if (!v.playing) v.paused = false;  // a stopped buffer starts on a new voice, which holds no pause
    v.playing = true;
    v.looping = a[3] & 1;
    if (a[3] & 2) v.pos = 0;
    v.sync = a[3] & 4;
    return 0;
}
BUFFER(Stop, 1) {
    if (TraceAudio() && v.playing) LOGR("audio: stop %08X at frame %.0f of %u", v.obj, v.pos, FramesOf(v, v.bytes));
    v.playing = v.paused = false;
    return 0;
}
// (this, DSBPAUSE_RESUME 0 / PAUSE 1 / SYNCHPLAYBACK 2), as the stream's. The XDK pauses only a buffer that plays
// (CMcpxBuffer_Pause, TimeSplitters 2's 0x204A1F): TimeSplitters 2 pauses every voice of its pool still marked
// playing when the game pauses (0x1C6C30), sounds that ended among them; a pause kept on those, never resumed once the
// pool let them go, silenced every later sound of theirs (cries, deaths, the pause menu's own)
BUFFER(Pause, 2) {
    if (TraceAudio()) LOGR("audio: pause %08X %u%s", v.obj, a[1], v.playing ? " playing" : "");
    if (a[1] == 1 && !v.playing) return 0;
    v.paused = a[1] == 1;
    v.sync = a[1] == 2;
    return 0;
}
// (this, time low, time high, flags): stops now, the release envelope is not reproduced
BUFFER(StopEx, 4) {
    v.playing = v.paused = false;
    return 0;
}
// (this, start, length) in bytes, 0 length plays to the end. The loop becomes the whole region.
BUFFER(SetPlayRegion, 3) {
    if (a[1] > v.bytes || a[2] > v.bytes - a[1]) return 0x88780032u;  // DSERR_INVALIDCALL
    v.play_start = a[1];
    v.play_len = v.loop_len = a[2] ? a[2] : v.bytes - a[1];
    v.loop_start = 0;
    v.pos = 0;
    v.block = -1;
    return 0;
}
BUFFER(SetLoopRegion, 3) {
    if (a[1] > v.play_len || a[2] > v.play_len - a[1]) return 0x88780032u;
    v.loop_start = a[1];
    v.loop_len = a[2] ? a[2] : v.play_len - a[1];
    return 0;
}
BUFFER(GetStatus, 2) {
    // DSBSTATUS_PLAYING 1, or DSBSTATUS_PAUSED 2 in its place (CMcpxBuffer_GetStatus, 0x204A64); DSBSTATUS_LOOPING 4
    W32(a[1], v.playing ? (v.paused ? 2u : 1u) | (v.looping ? 4u : 0u) : 0u);
    return 0;
}
BUFFER(GetCurrentPosition, 3) {
    uint32_t p = BytesOf(v, uint32_t(v.pos));
    if (a[1]) W32(a[1], p);
    if (a[2]) W32(a[2], v.play_len ? (p + BytesOf(v, 64 * 4)) % v.play_len : 0);
    return 0;
}
BUFFER(SetCurrentPosition, 2) {
    v.pos = FramesOf(v, a[1]);
    return 0;
}
BUFFER(Lock, 8) {
    uint32_t off = a[1], n = a[2];
    if (a[7] & 1) n = v.bytes;  // DSBLOCK_ENTIREBUFFER
    if (!v.bytes) return 0x80004005u;  // E_FAIL
    off %= v.bytes;
    uint32_t first = std::min(n, v.bytes - off);
    W32(a[3], v.data + off);
    W32(a[4], first);
    if (a[5]) W32(a[5], n > first ? v.data : 0);
    if (a[6]) W32(a[6], n > first ? n - first : 0);
    return 0;
}
BUFFER(SetVolume, 2) {
    v.volume = Db(int32_t(a[1]));
    return 0;
}
BUFFER(SetFrequency, 2) {
    if (TraceAudio()) LOG_FIRST(80, "audio: buffer %08X frequency %u (its rate %u)", v.obj, a[1], v.rate);
    v.freq = a[1];
    return 0;
}
BUFFER(SetFilter, 2) { return 0; }
BUFFER(SetHeadroom, 2) {
    v.headroom = Db(-int32_t(a[1]));
    return 0;
}
BUFFER(SetMixBinVolumes, 2) {
    SetBinVolumes(v, a[1]);
    return 0;
}
BUFFER(SetMixBins, 2) {
    SetBins(v, a[1]);
    return 0;
}
BUFFER(SetMaxDistance, 3) {
    v.max_dist = F32(a[1]);
    return 0;
}
BUFFER(SetMinDistance, 3) {
    v.min_dist = F32(a[1]);
    return 0;
}
BUFFER(SetPosition, 5) {
    v.placed = true;
    for (int i = 0; i < 3; i++) v.p[i] = F32(a[1 + i]);
    return 0;
}
BUFFER(SetVelocity, 5) { return 0; }
BUFFER(SetI3DL2Source, 3) {
    if (a[1]) {
        v.has_room = true;
        v.room_mb = int32_t(R32(a[1] + 8));
        static int told = 0;
        if (told++ < 40) LOGR("dsound: reverb source %08X room %d", v.obj, v.room_mb);
    }
    LOG_FIRST(20, "dsound: reverb source %08X direct %d hf %d room %d hf %d", v.obj, int32_t(R32(a[1])), int32_t(R32(a[1] + 4)),
              int32_t(R32(a[1] + 8)), int32_t(R32(a[1] + 12)));
    return 0;
}
// (this, DS3DBUFFER, apply): position at +4, distances at +0x34 and +0x38, mode at +0x3C
BUFFER(SetAllParameters, 3) {
    v.placed = true;
    for (int i = 0; i < 3; i++) v.p[i] = F32(R32(a[1] + 4 + i * 4));
    v.min_dist = F32(R32(a[1] + 0x34));
    v.max_dist = F32(R32(a[1] + 0x38));
    v.mode = R32(a[1] + 0x3C);
    return 0;
}
// (this, points, count, apply)
BUFFER(SetRolloffCurve, 4) {
    v.curve.clear();
    for (uint32_t i = 0; a[1] && i < std::min<uint32_t>(a[2], 64); i++) v.curve.push_back(F32(R32(a[1] + i * 4)));
    return 0;
}
BUFFER(SetOutputBuffer, 2) { return 0; }  // the voice keeps its own mix bins
BUFFER(SetFormat, 2) {
    ReadFormat(v, a[1]);
    v.freq = 0;
    v.block = -1;
    return 0;
}

// streams, the object carries the XDK's own vtable

HLE_STDCALL(DirectSoundCreateStream, 2) {
    XDK_REQUIRE("dsound", XDK_KEY(CDirectSoundStream_Vtable), XDK_KEY(CDirectSoundStream_Vtable2),
                XDK_DATA(DSound_StreamVtable), XDK_DATA(DSound_StreamVtable2));
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Start();
    uint32_t desc = a[0];
    uint32_t obj = mem::AllocSystem(0x80);
    std::memset(HostPtr(obj), 0, 0x80);
    W32(obj + XDK_CDirectSoundStream_Vtable, kStreamVtable);
    W32(obj + XDK_CDirectSoundStream_Vtable2, kStreamVtable2);
    Voice& v = g_voices[obj];
    v.obj = obj;
    v.stream = true;
    uint32_t flags = R32(desc);
    v.max_packets = std::max<uint32_t>(R32(desc + 4), 1);
    ReadFormat(v, R32(desc + 8));
    v.callback = R32(desc + 12);
    v.context = R32(desc + 16);
    SetBins(v, R32(desc + 20));
    v.is3d = flags & 0x10;  // DSBCAPS_CTRL3D
    v.headroom = DefaultHeadroom(flags);
    if (TraceAudio()) LOGR("audio: stream %08X flags %X packets %u tag %X ch %u rate %u callback %08X", obj, flags, v.max_packets, v.tag, v.channels, v.rate, v.callback);
    W32(a[1], obj);
    return 0;
}

#define STREAM(name, nargs)                                     \
    static uint32_t str_##name(Voice& v, const Args& a);        \
    HLE_STDCALL(CDirectSoundStream_##name, nargs) {             \
        uint32_t r;                                             \
        std::vector<Done> flushed;                              \
        {                                                       \
            std::lock_guard<std::recursive_mutex> lock(g_lock); \
            Voice* v = Find(a[0]);                              \
            r = v ? str_##name(*v, a) : 0x80004005u;            \
            flushed.swap(t_flushed);                            \
        }                                                       \
        Run(ctx, flushed);                                      \
        return r;                                               \
    }                                                           \
    static uint32_t str_##name([[maybe_unused]] Voice& v, [[maybe_unused]] const Args& a)

// the stream's callbacks still queued and those of its flushed packets, in order, go to the calling thread
static void FlushStream(Voice& v) {
    for (auto& pk : v.packets) Complete(v, pk, kFlushed);
    v.packets.clear();
    v.pos = 0;
    for (auto it = g_done.begin(); it != g_done.end();) {
        if (it->voice != v.obj) {
            ++it;
            continue;
        }
        t_flushed.push_back(*it);
        it = g_done.erase(it);
    }
    Deactivate(v);
}

STREAM(AddRef, 1) { return uint32_t(++v.refs); }
STREAM(SetFormat, 2) {
    FlushStream(v);
    ReadFormat(v, a[1]);
    v.freq = 0;  // the XDK sets the pitch back to the new format's own rate
    v.block = -1;
    return 0;
}
STREAM(Release, 1) {
    if (--v.refs > 0) return uint32_t(v.refs);
    FlushStream(v);
    uint32_t obj = v.obj;
    g_voices.erase(obj);
    mem::FreeSystem(obj);
    return 0;
}
STREAM(GetInfo, 2) {
    uint32_t info = a[1];
    W32(info, 0x3);  // fixed sample size, fixed packet alignment
    W32(info + 4, v.tag == 0x69 ? 36u * v.channels : v.align);
    W32(info + 8, 0);
    W32(info + 12, 0);
    return 0;
}
// DSSTREAMSTATUS_READY (XMEDIASTATUS_ACCEPT_INPUT) 1, _PLAYING 0x10000, _PAUSED 0x20000, _STARVED 0x40000: titles
// wait for a stream to play (TimeSplitters FP holds its cutscenes until their audio does)
STREAM(GetStatus, 2) {
    uint32_t s = v.packets.size() < v.max_packets ? 1u : 0u;
    if (v.paused) s |= 0x20000;
    else if (v.active && !v.packets.empty()) s |= 0x10000;
    else if (v.active) s |= 0x40000;
    W32(a[1], s);
    return 0;
}
STREAM(Process, 3) {
    uint32_t in = a[1];
    if (!in) return 0;
    Packet pk{R32(in), R32(in + 4), R32(in + 8), R32(in + 12), R32(in + 16)};
    if (pk.completed_ptr) W32(pk.completed_ptr, 0);
    if (pk.status_ptr) W32(pk.status_ptr, kPacketPending);
    if (TraceAudio()) {
        static std::map<uint32_t, int> traced;
        if (traced[v.obj]++ < 3) LOGR("audio: stream %08X packet %u bytes", v.obj, pk.size);
    }
    v.packets.push_back(pk);
    v.active = true;
    v.ending = false;
    return 0;
}
STREAM(Discontinuity, 1) {
    if (!v.packets.empty()) v.ending = true;
    else if (v.active) FlushStream(v);
    return 0;
}
STREAM(Flush, 1) {
    FlushStream(v);
    return 0;
}
STREAM(FlushEx, 4) {
    if (a[3] && !v.active) return 0;
    FlushStream(v);
    return 0;
}
// (this, points, count, apply). TimeSplitters FP gives every voice stream (0x299A3) the curve 0.5 0.25 0.125 0 over
// 1 to 100 units: a voice ten units off plays at 0.82, where the inverse distance law it went by unread left 0.1
STREAM(SetRolloffCurve, 4) {
    v.curve.clear();
    for (uint32_t i = 0; a[1] && i < std::min<uint32_t>(a[2], 64); i++) v.curve.push_back(F32(R32(a[1] + i * 4)));
    return 0;
}
STREAM(Pause, 2) {
    v.paused = a[1] == 1;
    v.sync = a[1] == 2;
    return 0;
}
STREAM(SetVolume, 2) {
    v.volume = Db(int32_t(a[1]));
    return 0;
}
STREAM(SetFrequency, 2) {
    v.freq = a[1];
    return 0;
}
STREAM(SetFilter, 2) { return 0; }
// the library's own writes voice registers a host stream does not have; envelopes are not played
HLE_STDCALL(IDirectSoundStream_SetEG, 2) {
    if (TraceAudio() && a[1]) {
        uint32_t e = a[1];
        LOG_FIRST(200, "audio: eg %08X kind %u mode %u delay %u attack %u hold %u decay %u release %u sustain %u pitch %d filter %d",
                  a[0], R32(e), R32(e + 4), R32(e + 8), R32(e + 12), R32(e + 16), R32(e + 20), R32(e + 24), R32(e + 28),
                  int32_t(R32(e + 32)), int32_t(R32(e + 36)));
    }
    return 0;
}
STREAM(SetHeadroom, 2) {
    v.headroom = Db(-int32_t(a[1]));
    return 0;
}
STREAM(SetMixBinVolumes, 2) {
    SetBinVolumes(v, a[1]);
    return 0;
}
STREAM(SetMixBins, 2) {
    SetBins(v, a[1]);
    return 0;
}
STREAM(SetMaxDistance, 3) {
    v.max_dist = F32(a[1]);
    return 0;
}
STREAM(SetMinDistance, 3) {
    v.min_dist = F32(a[1]);
    return 0;
}
STREAM(SetPosition, 5) {
    v.placed = true;
    for (int i = 0; i < 3; i++) v.p[i] = F32(a[1 + i]);
    return 0;
}
STREAM(SetVelocity, 5) { return 0; }
STREAM(SetI3DL2Source, 3) { return 0; }
