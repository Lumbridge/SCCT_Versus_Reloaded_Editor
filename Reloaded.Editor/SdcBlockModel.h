#pragma once
// A compressed SCCT package (a saved .sdc map) is a run of blocks, each an
// 8-byte header (uncompressed size, compressed size) and one zlib stream. The
// game's writer starts a new block every 15 MB, but the editor merges a saved
// map into a single block (LightmapFix), so one block can be as large as the
// whole map: 88 MB for a large user map. This header decides whether a block
// header can be right before anything is allocated or read for it. No zlib,
// so the tests compile it alone.
#include <cstdint>

namespace SdcBlock
{
    // The largest package the readers accept, all blocks together. The
    // largest real files are about 88 MB (a decoded map) and 188 MB (a raw
    // mesh package); the editor and game are 32-bit processes.
    constexpr std::uint64_t kMaxPackageBytes = 1024ull * 1024 * 1024;
    // Deflate codes a 258-byte match in no fewer than 2 bits, so a zlib stream
    // never expands to more than 1032 times its own size.
    constexpr std::uint64_t kMaxInflateRatio = 1032;

    enum class Fault { None, EmptySize, PastEnd, Overstated, TooLarge };

    // fileBytesLeft: the bytes after this block's header; packageBytes: what
    // the earlier blocks decoded to.
    inline Fault Check(std::uint32_t uncompressed, std::uint32_t compressed,
                       std::uint64_t fileBytesLeft, std::uint64_t packageBytes)
    {
        if (uncompressed == 0 || compressed == 0) return Fault::EmptySize;
        if (compressed > fileBytesLeft) return Fault::PastEnd;
        if (uncompressed > compressed * kMaxInflateRatio) return Fault::Overstated;
        if (packageBytes > kMaxPackageBytes || uncompressed > kMaxPackageBytes - packageBytes) return Fault::TooLarge;
        return Fault::None;
    }

    inline const char* Text(Fault fault)
    {
        switch (fault)
        {
        case Fault::None: return "";
        case Fault::EmptySize: return "An SDC block declares an invalid or unsupported size.";
        case Fault::PastEnd: return "The SDC asset package ends inside a compressed block.";
        case Fault::Overstated: return "An SDC block declares more data than its compressed bytes can hold.";
        case Fault::TooLarge: return "The package exceeds the supported 1 GB package size.";
        }
        return "An SDC block header is invalid.";
    }
}
