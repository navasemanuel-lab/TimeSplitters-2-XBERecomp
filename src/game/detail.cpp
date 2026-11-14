// The levels' detail textures (a second, fine grain texture over the floors and walls: textures/misc/*_dtl) on every
// piece of the level, not only the near ones.
//
// The game lays the detail texture (0x1C9662, stage 1, when the flag at 0x47C644 is set) on a piece of the level only
// while the camera is within a square distance of 20 of its box: 0x1C9362 (box, camera position, square distance) sets
// the flag. Walking, each piece took it or lost it at once, its look and brightness changing in steps across the walls
// and floors (Chicago's most of all). Far off its mip levels average out to the plain texture, so it is kept on.
#include <bit>

#include "kernel/kernel.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_001C9362);
X86_FUNC(sub_001C9362) {
    kernel::W32(ctx.esp + 0xC, std::bit_cast<uint32_t>(1e30f));
    __imp__sub_001C9362(ctx, base);
}
