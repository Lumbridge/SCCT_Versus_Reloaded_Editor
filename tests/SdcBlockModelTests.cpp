// Which block headers of a compressed package the readers accept. Pure: no
// zlib and no files. RecoveredAssetPackageTests.cpp, MapUsagesFileTests.cpp
// and MapOptimiseModelTests.cpp decode real blocks against the same rules.
#include "../Reloaded.Editor/SdcBlockModel.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

using namespace SdcBlock;

namespace
{
    int checks = 0;
    void Expect(bool value, const char* message)
    {
        ++checks;
        if (value) return;
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
    constexpr std::uint32_t kMb = 1024u * 1024u;
    constexpr std::uint64_t kPlenty = 4096ull * kMb;
}

int main()
{
    // The game's writer starts a block every 15 MB; a map saved by the editor
    // is one block, here with the sizes of a real 88 MB map.
    Expect(Check(15 * kMb, 3 * kMb, 40 * kMb, 0) == Fault::None, "a 15 MB block");
    Expect(Check(15 * kMb, 3 * kMb, 3 * kMb, 30 * kMb) == Fault::None, "the last of several blocks");
    Expect(Check(91879601u, 33451103u, 33451103u, 0) == Fault::None, "an 88 MB map in one block");
    Expect(Check(64 * kMb + 1, 20 * kMb, kPlenty, 0) == Fault::None, "just past the old 64 MB block limit");
    Expect(Check(600 * kMb, 200 * kMb, kPlenty, 0) == Fault::None, "a block far larger than any real map");
    Expect(Check(80 * kMb, 80 * kMb + 12345, kPlenty, 0) == Fault::None, "a block that did not compress");

    // Sizes of zero.
    Expect(Check(0, 100, kPlenty, 0) == Fault::EmptySize && Check(100, 0, kPlenty, 0) == Fault::EmptySize, "empty sizes");

    // The compressed bytes must be in the file.
    Expect(Check(1000, 500, 500, 0) == Fault::None, "a block ending with the file");
    Expect(Check(1000, 500, 499, 0) == Fault::PastEnd, "a block one byte longer than the file");
    Expect(Check(1000, 0xFFFFFFFFu, 33 * kMb, 0) == Fault::PastEnd, "a compressed size from a damaged header");
    Expect(Check(1000, 500, 0, 0) == Fault::PastEnd, "a header at the very end of the file");

    // The uncompressed size must be one the compressed bytes can produce.
    Expect(Check(1032 * 1000, 1000, kPlenty, 0) == Fault::None, "zlib's best ratio");
    Expect(Check(1032 * 1000 + 1, 1000, kPlenty, 0) == Fault::Overstated, "one byte more than zlib can produce");
    Expect(Check(0xFFFFFFFFu, 300000, kPlenty, 0) == Fault::Overstated, "4 GB declared for a 300 KB block");
    Expect(Check(0x9E2A83C1u, 300, kPlenty, 0) == Fault::Overstated, "a raw package read as a block header");

    // The whole package stays within the limit, however it is split.
    const auto limit = static_cast<std::uint32_t>(kMaxPackageBytes);
    Expect(Check(limit, 300 * kMb, kPlenty, 0) == Fault::None, "a package of exactly the limit");
    Expect(Check(limit + 1, 300 * kMb, kPlenty, 0) == Fault::TooLarge, "one byte over the limit");
    Expect(Check(0xFFFFFFFFu, 0xFFFFFFFFu, 2 * kPlenty, 0) == Fault::TooLarge, "the largest sizes a header can hold");
    Expect(Check(10, 10, kPlenty, kMaxPackageBytes - 10) == Fault::None, "blocks adding up to the limit");
    Expect(Check(11, 11, kPlenty, kMaxPackageBytes - 10) == Fault::TooLarge, "blocks adding up past the limit");
    Expect(Check(1, 1, kPlenty, kMaxPackageBytes + 5) == Fault::TooLarge, "a package already past the limit");

    Expect(!*Text(Fault::None), "no text for a good block");
    for (auto fault : { Fault::EmptySize, Fault::PastEnd, Fault::Overstated, Fault::TooLarge })
        Expect(std::strlen(Text(fault)) > 20, "every fault is explained");
    Expect(std::strstr(Text(Fault::TooLarge), "1 GB") != nullptr && kMaxPackageBytes == 1024ull * kMb, "the limit named in the text");

    std::printf("SdcBlockModelTests: %d checks passed\n", checks);
    return 0;
}
