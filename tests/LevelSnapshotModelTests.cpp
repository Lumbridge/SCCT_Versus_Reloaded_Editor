#include "../Reloaded.Editor/LevelSnapshotModel.h"
#include <iostream>
#include <source_location>
#include <string>
using namespace Snapshot;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
template <class F> void Reject(F f, std::source_location where = std::source_location::current())
{
    bool caught = false;
    try { f(); } catch (const std::exception&) { caught = true; }
    if (!caught) throw std::runtime_error("invalid input accepted at line " + std::to_string(where.line()));
}

// A package writer independent of the header under test: the SCCT layout
// (version 300 names XOR-coded by position, name references carrying a
// number) with a texture export as the editor saves one.
struct Builder
{
    Bytes b;
    int version;
    std::vector<std::string> names;
    explicit Builder(int version) : version(version) {}
    void U8(unsigned v) { b.push_back(static_cast<std::uint8_t>(v)); }
    void U16(unsigned v) { U8(v); U8(v >> 8); }
    void U32(std::uint32_t v) { for (int i = 0; i < 4; ++i) U8(v >> (8 * i)); }
    static void CompactInto(Bytes& out, int v)
    {
        unsigned a = static_cast<unsigned>(v < 0 ? -v : v);
        std::uint8_t first = static_cast<std::uint8_t>((v < 0 ? 0x80 : 0) | (a & 0x3f));
        a >>= 6;
        if (a) first |= 0x40;
        out.push_back(first);
        while (a) { std::uint8_t c = a & 0x7f; a >>= 7; if (a) c |= 0x80; out.push_back(c); }
    }
    void Compact(int v) { CompactInto(b, v); }
    int Name(const std::string& n)
    {
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return static_cast<int>(i);
        names.push_back(n);
        return static_cast<int>(names.size() - 1);
    }
    void NameRef(Bytes& out, const std::string& n, int number = 4242) { CompactInto(out, Name(n)); CompactInto(out, number); }
};
struct Sample { std::string name; int width, height, format; Bytes data; };
Bytes TextureBlob(Builder& w, const Sample& t, bool extras)
{
    Bytes blob;
    auto intProperty = [&](const char* name, int value) { w.NameRef(blob, name); blob.push_back(0x22); for (int i = 0; i < 4; ++i) blob.push_back(static_cast<std::uint8_t>(value >> (8 * i))); };
    auto byteProperty = [&](const char* name, int value) { w.NameRef(blob, name); blob.push_back(0x01); blob.push_back(static_cast<std::uint8_t>(value)); };
    if (extras)
    {
        w.NameRef(blob, "MipZero"); blob.push_back(0x2a); w.NameRef(blob, "Color"); for (int i = 0; i < 4; ++i) blob.push_back(0x11);
        w.NameRef(blob, "bAlphaTexture"); blob.push_back(0x83); // A set bool: type 3, array bit as the value.
        w.NameRef(blob, "LastUpdateTime"); blob.push_back(0x24); for (int i = 0; i < 4; ++i) blob.push_back(0);
    }
    intProperty("USize", t.width); intProperty("VSize", t.height); intProperty("UClamp", t.width); intProperty("VClamp", t.height);
    byteProperty("Format", t.format); byteProperty("UBits", 8); byteProperty("VBits", 8);
    w.NameRef(blob, "None");
    Builder::CompactInto(blob, 1); // One mip.
    for (int i = 0; i < 4; ++i) blob.push_back(0);
    Builder::CompactInto(blob, static_cast<int>(t.data.size()));
    blob.insert(blob.end(), t.data.begin(), t.data.end());
    for (int v : { t.width, t.height }) for (int i = 0; i < 4; ++i) blob.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    blob.push_back(8); blob.push_back(8);
    return blob;
}
// Exports: the textures, then a MapSettings object whose class is imported
// from SBase, exactly as a stock <Map>-i package is laid out.
Bytes MakePackage(int version, const std::vector<Sample>& textures, bool mapSettings, bool extras = true)
{
    Builder w(version);
    w.Name("None");
    std::vector<Bytes> blobs;
    for (const auto& t : textures) blobs.push_back(TextureBlob(w, t, extras));
    if (mapSettings) { Bytes blob; w.NameRef(blob, "None"); blobs.push_back(blob); }
    struct ImportEntry { std::string classPackage, className, name; int outer; };
    std::vector<ImportEntry> imports = { {"Core", "Class", "Texture", -2}, {"Core", "Package", "Engine", 0}, {"Core", "Class", "SMapSettings", -4}, {"Core", "Package", "SBase", 0} };
    for (const auto& i : imports) { w.Name(i.classPackage); w.Name(i.className); w.Name(i.name); }
    for (const auto& t : textures) w.Name(t.name);
    w.Name("MapSettings");
    w.Name("Test-i");
    // Header, names, blobs, imports, exports: offsets need the name table's size first.
    Bytes nameTable;
    size_t position = 64;
    for (const auto& n : w.names)
    {
        Bytes entry;
        Builder::CompactInto(entry, static_cast<int>(n.size() + 1));
        position += entry.size();
        for (size_t i = 0; i <= n.size(); ++i) { const std::uint8_t c = i < n.size() ? static_cast<std::uint8_t>(n[i]) : 0; entry.push_back(version >= 175 ? static_cast<std::uint8_t>(c ^ (position & 255)) : c); ++position; }
        for (int i = 0; i < 4; ++i) entry.push_back(0x10);
        position += 4;
        nameTable.insert(nameTable.end(), entry.begin(), entry.end());
    }
    std::vector<std::uint32_t> offsets;
    std::uint32_t at = static_cast<std::uint32_t>(64 + nameTable.size());
    for (const auto& blob : blobs) { offsets.push_back(at); at += static_cast<std::uint32_t>(blob.size()); }
    Bytes importTable;
    for (const auto& i : imports)
    {
        w.NameRef(importTable, i.classPackage); w.NameRef(importTable, i.className);
        for (int k = 0; k < 4; ++k) importTable.push_back(static_cast<std::uint8_t>(i.outer >> (8 * k)));
        w.NameRef(importTable, i.name);
    }
    Bytes exportTable;
    for (size_t i = 0; i < blobs.size(); ++i)
    {
        const bool isTexture = i < textures.size();
        Builder::CompactInto(exportTable, isTexture ? -1 : -3);
        Builder::CompactInto(exportTable, 0);
        for (int k = 0; k < 4; ++k) exportTable.push_back(0);
        w.NameRef(exportTable, isTexture ? textures[i].name : "MapSettings");
        const std::uint32_t flags = isTexture ? 0xf0004 : 0xf0100;
        for (int k = 0; k < 4; ++k) exportTable.push_back(static_cast<std::uint8_t>(flags >> (8 * k)));
        Builder::CompactInto(exportTable, static_cast<int>(blobs[i].size()));
        Builder::CompactInto(exportTable, static_cast<int>(offsets[i]));
    }
    w.U32(kPackageMagic); w.U16(version); w.U16(0); w.U32(1);
    w.U32(static_cast<std::uint32_t>(w.names.size())); w.U32(64);
    w.U32(static_cast<std::uint32_t>(blobs.size())); w.U32(static_cast<std::uint32_t>(at + importTable.size()));
    w.U32(static_cast<std::uint32_t>(imports.size())); w.U32(at);
    for (int i = 0; i < 4; ++i) w.U32(0x11111111u * (i + 1)); // GUID.
    w.U32(1); w.U32(static_cast<std::uint32_t>(blobs.size())); w.U32(static_cast<std::uint32_t>(w.names.size()));
    Check(w.b.size() == 64, "header size");
    w.b.insert(w.b.end(), nameTable.begin(), nameTable.end());
    for (const auto& blob : blobs) w.b.insert(w.b.end(), blob.begin(), blob.end());
    w.b.insert(w.b.end(), importTable.begin(), importTable.end());
    w.b.insert(w.b.end(), exportTable.begin(), exportTable.end());
    return w.b;
}
Sample Menu(int size = kSize, int format = kFormatRgba8) { return { "Menu", size, size, format, Bytes(static_cast<size_t>(size) * size * (format == kFormatRgba8 ? 4 : 1), 0x7f) }; }
Sample Loading() { return { "LoadingDEF0", 512, 512, kFormatDxt1, Bytes(131072, 1) }; }

int main()
{
    try
    {
        // Names.
        Check(PackageName("Hotel2") == "Hotel2-i", "package name");
        Reject([] { PackageName(""); });
        Reject([] { PackageName("My Map"); });
        Reject([] { PackageName("a\"b"); });

        // The BMP the importer reads: bottom-up rows padded to four bytes.
        const Bytes pixels = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 }; // 2 x 2 BGR, top-down.
        const Bytes bmp = Bmp24(2, 2, pixels);
        Check(bmp.size() == 54 + 16, "bmp size");
        Check(bmp[0] == 'B' && bmp[1] == 'M' && Le32(bmp, 2) == 70 && Le32(bmp, 10) == 54 && Le32(bmp, 14) == 40, "bmp headers");
        Check(Le32(bmp, 18) == 2 && Le32(bmp, 22) == 2 && bmp[28] == 24 && Le32(bmp, 34) == 16, "bmp dimensions");
        Check(bmp[54] == 7 && bmp[55] == 8 && bmp[56] == 9 && bmp[57] == 10 && bmp[58] == 11 && bmp[59] == 12 && bmp[60] == 0 && bmp[61] == 0, "bottom row first, padded");
        Check(bmp[62] == 1 && bmp[63] == 2 && bmp[64] == 3 && bmp[67] == 6 && bmp[68] == 0, "top row last");
        Reject([&] { Bmp24(2, 3, pixels); });
        Reject([&] { Bmp24(0, 0, {}); });

        // The .utc container around a package.
        const Bytes plain = MakePackage(300, { Menu(), Loading() }, true);
        Check(!Describe(plain).compressed, "plain package");
        const Bytes stream = { 0x78, 0x9c, 1, 2, 3 };
        const Bytes container = Contain(plain, stream);
        const auto described = Describe(container);
        Check(described.compressed && described.uncompressedSize == plain.size() && described.compressedSize == 5 && described.dataOffset == 8, "container header");
        Reject([] { Describe(Bytes(20, 0x41)); });
        Reject([] { Describe(Bytes{ 1, 2, 3 }); });

        // Tables of a stock-shaped package.
        const auto p = Parse(plain);
        Check(p.version == 300 && p.imports.size() == 4 && p.exports.size() == 3, "table counts");
        Check(p.imports[0].className == "Class" && p.imports[0].name == "Texture" && p.imports[0].outer == -2, "texture import");
        Check(p.imports[3].classPackage == "Core" && p.imports[3].className == "Package" && p.imports[3].name == "SBase", "package import");
        Check(p.exports[0].name == "Menu" && p.exports[0].className == "Texture" && p.exports[0].flags == 0xf0004, "menu export");
        Check(p.exports[2].name == "MapSettings" && p.exports[2].className == "SMapSettings", "map settings export");
        Check(p.Find("menu", "texture") == &p.exports[0] && !p.Find("Menu", "SMapSettings") && !p.Find("Briefing", "Texture"), "find by name and class");
        Check(std::find(p.names.begin(), p.names.end(), "Test-i") != p.names.end(), "names decoded through the position cipher");
        const auto menu = DescribeTexture(plain, p, p.exports[0]);
        Check(menu.width == kSize && menu.height == kSize && menu.format == kFormatRgba8 && menu.mips == 1, "menu texture described past struct, bool and float properties");
        const auto loading = DescribeTexture(plain, p, p.exports[1]);
        Check(loading.width == 512 && loading.format == kFormatDxt1, "loading texture described");
        const auto old = Parse(MakePackage(171, { Menu(128, kFormatDxt5) }, false, false));
        Check(old.version == 171 && old.exports.size() == 1 && old.names[0] == "None", "pre-175 names are plain");
        Check(DescribeTexture(MakePackage(171, { Menu(128, kFormatDxt5) }, false, false), old, old.exports[0]).width == 128, "pre-175 texture");

        // What must be true of the saved file.
        const auto verified = Verify(plain, 3);
        Check(verified.exportCount == 3 && verified.menu.width == kSize, "verified");
        Verify(plain, 0);
        Reject([&] { Verify(plain, 4); });                                                   // Something was lost.
        Reject([&] { Verify(MakePackage(300, { Loading() }, true), 0); });                   // No Menu.
        Reject([&] { Verify(MakePackage(300, { Menu(128) }, true), 0); });                   // Wrong size.
        Reject([&] { Verify(Bytes(plain.begin(), plain.begin() + 200), 0); });               // Truncated.
        Reject([&] { Bytes broken = plain; broken[0] = 0; Verify(broken, 0); });             // Not a package.
        Reject([&] { Bytes broken = plain; broken[28] = 0xff; broken[29] = 0xff; Parse(broken); }); // Export table off the end.

        // Compact indices of every width.
        for (int value : { 0, 1, 63, 64, 8191, 8192, 65536, 1289462, -1, -64, -8192 })
        {
            Bytes encoded;
            Builder::CompactInto(encoded, value);
            Reader r(encoded);
            Check(r.Compact() == value && r.pos == encoded.size(), "compact round trip");
        }
        Reject([] { Bytes bad = { 0x40, 0x80, 0x80, 0x80, 0x80, 0x80 }; Reader r(bad); r.Compact(); });
    }
    catch (const std::exception& e)
    {
        std::cout << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "LevelSnapshotModelTests: " << checks << " checks passed\n";
    return 0;
}
