// The Story levels' opening camera. Siberia (Dam, level 35) starts with the camera flying over the dam and the base to
// the tunnel where the player stands, with the caption "1990 Oblask Dam, Siberia": its Story setup (the level table's row
// +0x10, 0xDEAE0 gets it) has a camera list, the pointer at +0x84 and the count at +0x88, of cameradata (16 bytes: the
// points' address, their number, the seconds it holds still, the caption's text number; a point is 28 bytes, position,
// direction, speed). 0xA8110 starts the level: with a count above zero it begins the camera sequence (camera mode 1 with
// the list), without it the play at once. The retail game keeps the captions of five levels in its text (1048 to 1052:
// Siberia, Chicago, Notre Dame, NeoTokyo, Robot Factory), and the camera data of two more still stands in the image,
// unused, its count zeroed: Notre Dame (one path of two points) and Robot Factory (a still of two seconds, then paths of
// four and of three points). Given their count back they play as Siberia's does, with the caption added to the first.
// Chicago's and NeoTokyo's data is not in the image.
#include <cstdint>

#include "common/options.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {
constexpr uint32_t kLevel = 0x4A46EC, kMode = 0x4AD80C, kStoryMode = 4;
constexpr uint32_t kCamerasAt = 0x84, kCameraCountAt = 0x88, kTextAt = 12;

struct Intro {
    uint32_t level, setup, cameras, count, text;
};
// the Story setup of each level (0x2D0F48's row +0x10), its cameradata array, how many entries, the caption (text number)
constexpr Intro kIntros[] = {
    // Notre Dame (34, setup 0x3069E8, array 0x306938) is left out: its cut path goes through the walls
    {41, 0x31BC40, 0x31B7BC, 3, 1052},  // Robot Factory: 2315 Robot Factory
};

void Restore() {
    uint32_t level = kernel::R32(kLevel);
    if (kernel::R32(kMode) != kStoryMode) return;
    for (const Intro& i : kIntros) {
        if (i.level != level) continue;
        bool on = game::Opt().level_intros;
        uint32_t count = on ? i.count : 0;
        if (kernel::R32(i.setup + kCameraCountAt) == count) return;
        kernel::W32(i.setup + kCamerasAt, on ? i.cameras : 0);
        kernel::W32(i.setup + kCameraCountAt, count);
        if (on && !kernel::R32(i.cameras + kTextAt)) kernel::W32(i.cameras + kTextAt, i.text);
        LOGR("intro: level %u opens with its camera sequence (%u cameras)", level, count);
        return;
    }
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_000A8110);
X86_FUNC(sub_000A8110) {
    Restore();
    __imp__sub_000A8110(ctx, base);
}
