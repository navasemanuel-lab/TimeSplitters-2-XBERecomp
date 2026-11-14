// The game's speed. TimeSplitters 2 steps its world by the vertical blanks its blank callback (0x1C89D2) counted at
// 0x47C5E8 since the last frame (0x1ADF80, the steps at 0x348898, 1 to 3), and a frame that saw none steps it once
// all the same: on the console the swap waits for a blank, so a frame never sees none. Here the frame cap and the blank
// thread both run at 60 Hz without being locked together: a frame drawn before the next blank took a step of its own,
// and the whole game ran fast. The frame waits for that blank, as the console's swap did.
#include <cstdint>

#include "app/app.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kClock = 0x47C5E8;      // the blanks counted
constexpr uint32_t kLastClock = 0x48A98C;  // the count the last frame took, moved there before the call to read it

}  // namespace

// 0x1AE009, the frame's read of the count
void SoundPackTick();  // sound_pack.cpp
void VideoMenuInit();  // video_menu.cpp
void BetaRebornTick(); // pak_replace.cpp

void FrameBlank(X86Context&, uint8_t*) {
    // in the menus a draw left out for its pipeline blinks: they wait for it (the level may leave the draw out)
    gpu::g_wait_pipelines.store(kernel::R32(0x4A46EC) >= 100, std::memory_order_relaxed);
    // the text language: the game reads its table index (0x3864FC, into the arrays at 0x335EA8: English, French, German,
    // Italian, Spanish) wherever it needs a line, and it was left at English whatever the console setting (XC_LANGUAGE)
    // said; the setting's index is put there
    {
        static const uint32_t kXc[] = {1, 4, 3, 6, 5};
        uint32_t want = 0;
        for (uint32_t i = 0; i < 5; i++)
            if (kXc[i] == app::Get().language) want = i;
        if (kernel::R32(0x3864FC) != want) {
            static int said = 0;
            if (said++ < 4) LOGR("language: table index %u -> %u", kernel::R32(0x3864FC), want);
            kernel::W32(0x3864FC, want);
        }
    }
    SoundPackTick();
    VideoMenuInit();
    BetaRebornTick();
    for (int i = 0; i < 4 && d3d::g_blanking.load(); i++) {
        uint32_t seen = d3d::BlankSeen();  // before the look, so a blank in between is not slept through
        if (kernel::R32(kClock) != kernel::R32(kLastClock)) return;
        d3d::WaitBlank(seen);
    }
}
