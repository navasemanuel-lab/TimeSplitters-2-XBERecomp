// The CRT's sin, cos, sqrt, asin and acos. Each checks its argument in a helper call (0x1D4D44 or 0x1D4D01) and
// branches on the ZF that call returns; recompiled code does not carry flags across call and ret, so they read stale
// flags and now and then returned NaN. Everything that aims by an angle suffered: wall cameras and turrets hung straight
// down, the soldiers turned away from the player, models flickered. The register entries (the argument in st0, popped
// by their ret) are replaced through the SDK's crt_* symbols in config/game.toml; the stack entries (the argument above
// the return address, the result left in st0) here.
#include <cmath>

#include "xdk/hle.h"

#define STACK_ENTRY(addr, fn)                                  \
    X86_FUNC(sub_##addr) {                                     \
        double x = double(X86_MEMF64(ctx.esp + 4));            \
        x87_push(ctx, fn(x));                                  \
        ctx.esp += 4;                                          \
    }

STACK_ENTRY(001D3B84, std::sin)
STACK_ENTRY(001D3AD4, std::cos)
STACK_ENTRY(001D3C34, std::sqrt)
STACK_ENTRY(001D2910, std::acos)
STACK_ENTRY(001D35A8, std::asin)
