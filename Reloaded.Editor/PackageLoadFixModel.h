#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// UObject::LoadPackage (ChaosTheory_Editor.exe 0x10FB1EB0) calls BeginLoad and then
// GetPackageLinker. When that finds no linker without throwing (a package with no file
// of its own, such as the map's MyLevel, gives the editor no error), LoadPackage
// returns NULL straight away, without the EndLoad its other two ways out (the package
// loaded, or an error caught) both make. GObjBeginLoadCount stays at 1 and the next
// engine tick fails check(GObjBeginLoadCount==0) in UObject::StaticTick
// (UnObj.cpp line 2106). The stock Sound Browser reloads every package it lists with
// LoadPackage(NULL, name, LOAD_NoWarn) when the browsers refresh after MAP LOAD, and it
// lists MyLevel once the map holds a sound of its own: such a map crashes the editor
// as soon as it is opened again, with or without RE+.
//
// The fix has two parts. The seven bytes after the call (add esp,1Ch; test esi,esi;
// je <return NULL>) become a jump to a stub that does the same and calls EndLoad
// before the NULL return, as the found-a-linker path does, so no failed LoadPackage
// can leave a load open. And the Sound Browser no longer asks for MyLevel at all
// (kSoundBrowserRefreshLoad). Engine-independent here: recognising the stock code,
// building the jump, and which package the browser skips.
namespace PackageLoadFix
{
constexpr uint32_t kLoadPackage = 0x10FB1EB0;
constexpr uint32_t kGetPackageLinker = 0x10FB10D0;
constexpr uint32_t kEndLoad = 0x10FA9C40;
constexpr uint32_t kCallLinker = 0x10FB1F12;      // call GetPackageLinker
constexpr uint32_t kSite = 0x10FB1F19;            // add esp,1Ch; test esi,esi; je kNullReturn
constexpr uint32_t kResume = 0x10FB1F20;          // test bl,10h: a linker was found
constexpr uint32_t kNullReturn = 0x10FB1EE0;      // xor eax,eax, then LoadPackage's epilogue
constexpr uint32_t kFoundEndLoadCall = 0x10FB1F2F; // the found-a-linker path's call EndLoad
constexpr size_t kSiteLength = 7;

// From kCallLinker: call GetPackageLinker; mov esi,eax; add esp,1Ch; test esi,esi;
// je kNullReturn; test bl,10h.
constexpr size_t kCodeLength = 17;
using Code = std::array<uint8_t, kCodeLength>;

inline uint32_t Read32(const uint8_t* at)
{
    return at[0] | (at[1] << 8) | (at[2] << 16) | (static_cast<uint32_t>(at[3]) << 24);
}

// Whether the five bytes at `at`, which sit at address `address`, are a call to `target`.
inline bool CallsTo(const uint8_t* at, uint32_t address, uint32_t target)
{
    return at[0] == 0xE8 && address + 5 + Read32(at + 1) == target;
}

// The stock bytes from kCallLinker.
inline Code Stock()
{
    const uint32_t call = kGetPackageLinker - (kCallLinker + 5);
    return {0xE8, static_cast<uint8_t>(call), static_cast<uint8_t>(call >> 8), static_cast<uint8_t>(call >> 16),
            static_cast<uint8_t>(call >> 24),
            0x8B, 0xF0,                                         // mov esi,eax
            0x83, 0xC4, 0x1C,                                   // add esp,1Ch
            0x85, 0xF6,                                         // test esi,esi
            0x74, static_cast<uint8_t>(kNullReturn - (kSite + 7)), // je kNullReturn
            0xF6, 0xC3, 0x10};                                  // test bl,10h
}

// jmp stub, then two nops, for the seven bytes at kSite.
inline std::array<uint8_t, kSiteLength> Jump(uint32_t stub)
{
    const uint32_t rel = stub - (kSite + 5);
    return {0xE9, static_cast<uint8_t>(rel), static_cast<uint8_t>(rel >> 8), static_cast<uint8_t>(rel >> 16),
            static_cast<uint8_t>(rel >> 24), 0x90, 0x90};
}

enum class State { Stock, Patched, Unknown };

// What the kCodeLength bytes read at kCallLinker are: the stock code, the stock code
// with the jump in (to any stub), or something else (another build of the editor, or
// someone else's patch), which is left alone.
inline State Classify(const uint8_t* code)
{
    const Code stock = Stock();
    bool same = true;
    for (size_t i = 0; i < kCodeLength; ++i) same = same && code[i] == stock[i];
    if (same) return State::Stock;
    const size_t site = kSite - kCallLinker;
    for (size_t i = 0; i < site; ++i)
        if (code[i] != stock[i]) return State::Unknown;
    for (size_t i = site + kSiteLength; i < kCodeLength; ++i)
        if (code[i] != stock[i]) return State::Unknown;
    if (code[site] == 0xE9 && code[site + 5] == 0x90 && code[site + 6] == 0x90) return State::Patched;
    return State::Unknown;
}

// Where a patched site jumps to.
inline uint32_t JumpTarget(const uint8_t* code)
{
    const uint8_t* at = code + (kSite - kCallLinker);
    return kSite + 5 + Read32(at + 1);
}

// The Sound Browser's package refresh (WBrowserSound::RefreshPackages, 0x10E7F410) calls
// LoadPackage here for each listed package. The map's own package is always in memory
// with its map and has no file: loading it can only fail, and the failure also lists
// "MyLevel" as a missing file in the editor's Load Errors window. The browser skips it,
// as the Texture Browser's refresh does (TextureBrowser.cpp).
constexpr uint32_t kSoundBrowserRefreshLoad = 0x10E7F510;

inline bool IsMapPackage(const char* name)
{
    const char* map = "MyLevel";
    if (!name) return false;
    for (; *name && *map; ++name, ++map)
    {
        const char a = (*name >= 'A' && *name <= 'Z') ? static_cast<char>(*name - 'A' + 'a') : *name;
        const char b = (*map >= 'A' && *map <= 'Z') ? static_cast<char>(*map - 'A' + 'a') : *map;
        if (a != b) return false;
    }
    return !*name && !*map;
}
}
