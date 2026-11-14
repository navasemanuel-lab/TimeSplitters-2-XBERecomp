// A screen's object kept in a list (the list at +0x64C of the object that calls them, from 0x87135) can be a number
// where a pointer should be: 0xD6F60 (object) then draws from it and reads its fields at +0x1C and +0x20, a read of
// address 0x1E that closed the game in Aztec Ruins. Such an object is skipped, so its frame is not drawn.
#include <cstdint>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_000D6F60);
X86_FUNC(sub_000D6F60) {
    uint32_t object = kernel::R32(ctx.esp + 4);
    if (object < 0x10000) {
        static int logged = 0;
        if (logged++ < 4) LOGR("guard: a screen object %08X that is no pointer is left out (0xD6F60)", object);
        ctx.esp += 4;  // its ret
        return;
    }
    __imp__sub_000D6F60(ctx, base);
}
