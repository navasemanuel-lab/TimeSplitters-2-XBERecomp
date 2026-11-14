#pragma once
// The lines we add to the game's menus, in its text language: the game's table index (0x3864FC) is English, French,
// German, Italian, Spanish, as the arrays at 0x335EA8 are. Each text is given in the five; the one for the language chosen
// is put in the game's memory (once for each language) and its address returned. Latin-1, as the game's own text is.
#include <cstdint>
#include <cstring>
#include <string>

#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace menu_text {

inline uint32_t Index() {
    uint32_t i = kernel::R32(0x3864FC);
    return i < 5 ? i : 0;
}

// the text for the language, in the game's memory
inline uint32_t Address(const char* const (&texts)[5], uint32_t (&cache)[5]) {
    uint32_t i = Index();
    if (!cache[i]) {
        size_t n = std::strlen(texts[i]) + 1;
        cache[i] = mem::AllocSystem(uint32_t((n + 3) & ~3u));
        if (cache[i]) std::memcpy(kernel::HostPtr(cache[i]), texts[i], n);
    }
    return cache[i];
}

// a line of the game's own text table (the arrays at 0x335EA8), in the language chosen
inline std::string GameText(uint32_t id) {
    uint32_t array = kernel::R32(0x335EA8 + 4 * Index());
    uint32_t p = array ? kernel::R32(array + 4 * id) : 0;
    return p > 0x10000 ? kernel::ReadCString(p, 120) : std::string();
}

// the title of the profile screens ("Select Profile": ids 235 and 2410), whatever the language
inline bool IsProfileTitle(const std::string& s) { return !s.empty() && (s == GameText(235) || s == GameText(2410)); }

}  // namespace menu_text
