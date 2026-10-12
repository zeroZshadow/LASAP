#pragma once
#include "lib.hpp"
#include <map>
#include <string>

const inline exl::util::ModuleInfo g_MainInfo = exl::util::GetMainModuleInfo();
const inline uintptr_t g_BaseAddress = g_MainInfo.m_Total.m_Start; // 0x8504000
const inline uintptr_t g_MainEnd = g_BaseAddress + g_MainInfo.m_Total.m_Size;

inline std::map<std::string, uintptr_t> g_symbols {
    {"FrameworkPtr",            g_BaseAddress + 0x01c97188},
    {"GlobalSave",              g_BaseAddress + 0x01cbb200},
    {"EventFlagsPtr",           g_BaseAddress + 0x01cc1120},
    {"InventoryPtr",            g_BaseAddress + 0x01cc1368},
    {"LevelIndexPtr",           g_BaseAddress + 0x01cc13c8},
    {"DungeonItemsPtr",         g_BaseAddress + 0x01cc11b0},
    {"PFXTiltShiftParam",       g_BaseAddress + 0x016cbd73}
};
