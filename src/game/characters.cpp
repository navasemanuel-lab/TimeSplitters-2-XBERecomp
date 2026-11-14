// Characters that are in the game's tables but not in the Arcade's Select Character list: Gasmask Special, the twenty
// NeoTokyo pedestrians and X-Ray Skel (the robots, golems and the like are left out: their skeletons are not the biped one the
// screen's animation is made for, and its update writes through a bad bone pointer). The character table (0x2A9C40, 0x44 bytes a character: name text id, model
// indexes, stats) has all of them; the list is another table, 0x2BA840 (127 rows of 0x20 bytes: character, portrait
// texture, backdrop texture, lock descriptor kind), and a character with no row there is not offered. The table has no room
// after it, so a copy of it with the extra rows added is made in memory of our own, and the instructions that reach the table
// or its end are pointed at the copy ([[imm]] in config/game.toml: the table's address, its 127 rows and 0xFE0 bytes). The
// lock cache the Select Character screen keeps for the rows (0x35BE98, a dword each) is moved the same way.
#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace {
constexpr uint32_t kRows = 0x2BA840, kRow = 0x20, kCount = 127;

// character, portrait texture (the chrimage_* of the table at 0x2C10E8: 469 - its index back from it), backdrop texture
struct Extra {
    uint32_t chr, portrait, back;
};
constexpr Extra kExtras[] = {
    {94, 438, 480},                                                    // Gasmask Special
    {63, 442, 473},                                                    // X-Ray Skel
    {70, 467, 478},  {71, 468, 478}, {72, 469, 478}, {73, 467, 478}, {74, 468, 478},  // NeoTokyo male pedestrians
    {75, 469, 478},  {76, 467, 478}, {77, 468, 478}, {78, 469, 478}, {79, 467, 478},
    {80, 468, 478},  {81, 469, 478}, {82, 467, 478}, {83, 468, 478}, {84, 469, 478},  // female
    {85, 467, 478},  {86, 468, 478}, {87, 469, 478}, {88, 467, 478}, {89, 468, 478},
};
constexpr uint32_t kExtraCount = sizeof(kExtras) / sizeof(kExtras[0]);
static_assert(kCount + kExtraCount == 149, "the [[imm]] row count in config/game.toml (0x95, 0x12A0 bytes, 0x254 cache bytes)");
uint32_t g_table = 0, g_locks = 0;

void Make() {
    g_table = mem::AllocSystem((kCount + kExtraCount) * kRow);
    g_locks = mem::AllocSystem((kCount + kExtraCount) * 4);
    for (uint32_t w = 0; w < kCount * kRow / 4; w++) kernel::W32(g_table + w * 4, kernel::R32(kRows + w * 4));
    for (uint32_t i = 0; i < kExtraCount; i++) {
        uint32_t row[8] = {kExtras[i].chr, kExtras[i].portrait, kExtras[i].back, 1, 0, 0, 0, 0};
        for (uint32_t w = 0; w < 8; w++) kernel::W32(g_table + (kCount + i) * kRow + w * 4, row[w]);
    }
    for (uint32_t i = 0; i < kCount + kExtraCount; i++) kernel::W32(g_locks + i * 4, 0);
    // the Consultant (character 92) has the table's last row, a spare with no lock kind: it is offered with the suit's portrait
    kernel::W32(g_table + 126 * kRow + 4, 416);  // chrimage_suit
    kernel::W32(g_table + 126 * kRow + 12, 1);
    LOGR("characters: the character list copied with %u more rows (%08X)", kExtraCount, g_table);
}
}  // namespace

// the copy of the table and of its lock cache, for the recompiled instructions that [[imm]] points at them
extern "C" uint32_t GameCharTable() {
    if (!g_table) Make();
    return g_table;
}

extern "C" uint32_t GameCharLocks() {
    if (!g_table) Make();
    return g_locks;
}
