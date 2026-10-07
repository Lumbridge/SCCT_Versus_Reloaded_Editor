#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

// Large address aware: ChaosTheory_Editor.exe is a 32-bit program without the PE
// flag that lets it use more than 2 GB, and a big map with a few 100 MB static mesh
// packages runs out ("ReadFile failed ... Invalid access to memory location" while
// a package loads). With IMAGE_FILE_LARGE_ADDRESS_AWARE it gets 4 GB on 64-bit
// Windows. Engine-independent: the bytes of the executable in, the same bytes with
// one bit set out.
namespace LargeAddress
{
constexpr uint16_t kFlag = 0x0020;   // IMAGE_FILE_LARGE_ADDRESS_AWARE
constexpr uint16_t kI386 = 0x014c;   // IMAGE_FILE_MACHINE_I386

inline uint32_t Read32(const std::vector<unsigned char>& b, size_t at)
{
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}
inline uint16_t Read16(const std::vector<unsigned char>& b, size_t at) { return static_cast<uint16_t>(b[at] | (b[at + 1] << 8)); }

// Where IMAGE_FILE_HEADER.Characteristics sits in a 32-bit x86 executable.
inline size_t CharacteristicsOffset(const std::vector<unsigned char>& image)
{
    if (image.size() < 0x40 || image[0] != 'M' || image[1] != 'Z') throw std::runtime_error("Not an executable.");
    const uint32_t pe = Read32(image, 0x3c);
    if (pe < 0x40 || pe > image.size() - 24) throw std::runtime_error("The executable has no PE header.");
    if (image[pe] != 'P' || image[pe + 1] != 'E' || image[pe + 2] != 0 || image[pe + 3] != 0)
        throw std::runtime_error("The executable has no PE header.");
    if (Read16(image, pe + 4) != kI386) throw std::runtime_error("Not a 32-bit x86 executable.");
    return pe + 22;
}
inline bool IsLargeAddressAware(const std::vector<unsigned char>& image)
{
    return (Read16(image, CharacteristicsOffset(image)) & kFlag) != 0;
}
// Sets the flag; returns false when it was already set. Nothing else changes.
inline bool MakeLargeAddressAware(std::vector<unsigned char>& image)
{
    const size_t at = CharacteristicsOffset(image);
    const uint16_t value = Read16(image, at);
    if (value & kFlag) return false;
    const uint16_t next = static_cast<uint16_t>(value | kFlag);
    image[at] = static_cast<unsigned char>(next & 0xff);
    image[at + 1] = static_cast<unsigned char>(next >> 8);
    return true;
}
// The file the unflagged editor is kept as: ChaosTheory_Editor.original.exe, or the
// first of .before-large-address.exe, .before-large-address-2.exe, ... not taken.
inline std::string BackupName(const std::function<bool(const std::string&)>& exists)
{
    if (!exists("ChaosTheory_Editor.original.exe")) return "ChaosTheory_Editor.original.exe";
    if (!exists("ChaosTheory_Editor.before-large-address.exe")) return "ChaosTheory_Editor.before-large-address.exe";
    for (int n = 2; n < 1000; ++n)
    {
        const auto name = "ChaosTheory_Editor.before-large-address-" + std::to_string(n) + ".exe";
        if (!exists(name)) return name;
    }
    throw std::runtime_error("Too many editor backups in the System folder.");
}
} // namespace LargeAddress
