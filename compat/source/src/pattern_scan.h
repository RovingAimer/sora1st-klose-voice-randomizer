// Copyright (c) 2025 Hinkiii
// Copyright (c) 2026 Mod contributors
// SPDX-License-Identifier: MIT
// Mask comparison adapted from Hinkiii/sora1looseload FindPattern.
#pragma once
#include <windows.h>
#include <psapi.h>
#include <cstddef>
#include <cstdint>

namespace native {
template<std::size_t N>
unsigned char* FindPattern(const unsigned char (&pattern)[N], const char (&mask)[N+1]) noexcept {
    MODULEINFO image{};
    const auto module = GetModuleHandleW(nullptr);
    if (!module || !K32GetModuleInformation(GetCurrentProcess(), module, &image, sizeof(image))) return nullptr;
    auto cursor = reinterpret_cast<std::uintptr_t>(image.lpBaseOfDll);
    const auto end = cursor + image.SizeOfImage;
    // Read executable regions only; no executable name, file size or digest whitelist.
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region))) break;
        const auto limit = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (limit <= cursor) break;
        const auto stop = limit < end ? limit : end;
        if (region.State == MEM_COMMIT && !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (region.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(cursor);
            const auto size = stop-cursor;
            if (size >= N) for (std::size_t i=0; i<=size-N; ++i) {
                std::size_t j=0;
                while (j<N && (mask[j]=='?' || bytes[i+j]==pattern[j])) ++j;
                if (j==N) return const_cast<unsigned char*>(bytes+i);
            }
        }
        cursor = stop;
    }
    return nullptr;
}
}
