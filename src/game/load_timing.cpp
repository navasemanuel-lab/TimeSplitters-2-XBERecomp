// Diagnostic: how long each step of the title's level set-up takes (the main loop 0x168E60 between its restart at
// 0x168E70 and the first world step at 0x168F84). Each hook writes the call it stands before, the time and the vertical
// blanks since the last mark, so the log shows where a load spends its seconds.
#include <chrono>
#include <cstdint>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {

void Mark(uint32_t at) {
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now();
    static uint32_t last_blank = 0;
    auto now = clock::now();
    uint32_t blank = kernel::R32(0x47C5E8);
    // the first world step is only the end of a set-up when a mark came before it
    static uint32_t since_restart = 0;
    if (at == 0x168F84) {
        if (!since_restart) return;
        since_restart = 0;
    } else {
        since_restart++;
    }
    LOGR("setup: before %08X: %.0f ms and %u blanks since the last step", at,
         std::chrono::duration<double, std::milli>(now - last).count(), blank - last_blank);
    last = now;
    last_blank = blank;
}

}  // namespace

void SetupMark_168EB5(X86Context&, uint8_t*) { Mark(0x168EB5); }
void SetupMark_168EBA(X86Context&, uint8_t*) { Mark(0x168EBA); }
void SetupMark_168EBF(X86Context&, uint8_t*) { Mark(0x168EBF); }
void SetupMark_168EC4(X86Context&, uint8_t*) { Mark(0x168EC4); }
void SetupMark_168EC9(X86Context&, uint8_t*) { Mark(0x168EC9); }
void SetupMark_168ECE(X86Context&, uint8_t*) { Mark(0x168ECE); }
void SetupMark_168EEB(X86Context&, uint8_t*) { Mark(0x168EEB); }
void SetupMark_168EF3(X86Context&, uint8_t*) { Mark(0x168EF3); }
void SetupMark_168EF8(X86Context&, uint8_t*) { Mark(0x168EF8); }
void SetupMark_168EFD(X86Context&, uint8_t*) { Mark(0x168EFD); }
void SetupMark_168F02(X86Context&, uint8_t*) { Mark(0x168F02); }
void SetupMark_168F0D(X86Context&, uint8_t*) { Mark(0x168F0D); }
void SetupMark_168F12(X86Context&, uint8_t*) { Mark(0x168F12); }
void SetupMark_168F17(X86Context&, uint8_t*) { Mark(0x168F17); }
void SetupMark_168F1C(X86Context&, uint8_t*) { Mark(0x168F1C); }
void SetupMark_168F21(X86Context&, uint8_t*) { Mark(0x168F21); }
void SetupMark_168F26(X86Context&, uint8_t*) { Mark(0x168F26); }
void SetupMark_168F2B(X86Context&, uint8_t*) { Mark(0x168F2B); }
void SetupMark_168F30(X86Context&, uint8_t*) { Mark(0x168F30); }
void SetupMark_168F35(X86Context&, uint8_t*) { Mark(0x168F35); }
void SetupMark_168F84(X86Context&, uint8_t*) { Mark(0x168F84); }

// 0xDCE87, the loading bar's redraw test (0xDCE30): a redraw draws a frame and swaps, which waits for a blank, and the
// bar went on at every second blank, so the load stopped for it about every 33 ms. The test compares the blank count (edx)
// with the count of the last redraw plus one (eax); with that plus 12 the bar moves five times a second.
void LoadingBarRate(X86Context& ctx, uint8_t*) { ctx.eax = kernel::R32(0x2CE410) + 12; }
