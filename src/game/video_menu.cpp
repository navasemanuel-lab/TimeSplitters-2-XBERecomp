// Audio Options and Video Options: the Options menu's "Audio/Video Options" holds only sound rows and subtitles, so it is
// named Audio Options, and a Video Options line follows it that opens the settings panel on its graphics tab (the video
// settings are there). The Options menu is built at 0x95F80: each line is an item of the menu library (0xF0EC0 makes it in
// a menu, +0x20 its text, +0x30 what it does when chosen, +0x34 and +0x38 its place from 0xF0500); the new one is made
// after Audio's, before the next line (Credits) is, as the others are.
#include <cstring>

#include "common/panel.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "menu_text.h"
#include "xdk/hle.h"

namespace dispatch {
uint32_t HostThunk(X86Func* fn);  // cpu/dispatch.cpp: a guest address that calls host code
}

namespace {
constexpr uint32_t kMakeItem = 0xF0EC0, kPlaceItem = 0xF0500;

uint32_t g_text = 0, g_chosen = 0;

// the title the game has for Audio/Video Options, in place: shorter, so it fits, the rest of its room zeros
void Rename(uint32_t va, const char* from, const char* to) {
    size_t n = std::strlen(from);
    uint8_t* p = static_cast<uint8_t*>(kernel::HostPtr(va));
    if (std::memcmp(p, from, n) != 0) return;
    std::memset(p, 0, n + 1);
    std::memcpy(p, to, std::strlen(to));
}

// (menu): the line's action
void VideoChosen(X86Context& ctx, uint8_t*) {
    panel::OpenTab(0);
    kernel::Return(ctx, 0, 0);
}
}  // namespace

// each frame, the first one renames the titles, before any menu has measured them
void VideoMenuInit() {
    // the titles are the game's own again and no Video Options line is added: the video settings are in the F1 panel only
    static bool done = true;
    if (done) return;
    done = true;
    Rename(0x245174, "Audio/Video Options", "Audio Options");
    Rename(0x2866A0, "Opzioni audio/video", "Opzioni audio");
    Rename(0x29BC4C, "Opciones Audio/Video", "Opciones Audio");
    Rename(0x29B154, "Opciones Audio/Vídeo", "Opciones Audio");
    Rename(0x25BD74, "Options audio / vidéo", "Options audio");
    Rename(0x271244, "Sound-/Grafikoptionen", "Soundoptionen");
}

// before the Options menu's third line (0x96084): esi is the menu, ebp the place number the lines take
void OptionsVideoRow(X86Context& ctx, uint8_t*) {
    return;  // no extra line: the Options menu is the game's own (the video settings are in the F1 panel)
    static const char* const kText[5] = {"Video Options", "Options vidéo", "Videooptionen", "Opzioni video", "Opciones Vídeo"};
    static uint32_t cache[5] = {};
    if (!g_chosen) g_chosen = dispatch::HostThunk(VideoChosen);
    g_text = menu_text::Address(kText, cache);
    if (!g_text) return;
    X86Context saved = ctx;
    uint32_t item = kernel::CallGuest(ctx, kMakeItem, {saved.esi, 0x23, 0x100});
    if (item) {
        kernel::W32(item + 0x20, g_text);
        kernel::W32(item + 0x1C, 0x15);
        kernel::W32(item + 0x24, 3);
        kernel::W32(item + 0x34, kernel::CallGuest(ctx, kPlaceItem, {item}));
        kernel::W32(item + 0x38, saved.ebp);
        kernel::W32(item + 0x30, g_chosen);
        static int logged = 0;
        if (logged++ < 2) LOGR("video: a Video Options line added to Options (item %08X)", item);
    }
    ctx = saved;
}
