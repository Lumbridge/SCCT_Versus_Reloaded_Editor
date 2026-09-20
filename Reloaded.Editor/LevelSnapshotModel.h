#pragma once
// The level snapshot the game shows in map selection is the "Menu" texture of
// the map's interface package, Packages/Textures/<Map>-i.utc (a zlib
// container around an ordinary package) or <Map>-i.utx. This header is the
// pure part of setting it: the BMP the editor's texture importer reads, the
// package tables needed to check what the editor wrote back, and the name of
// everything involved. No engine, no zlib, so the tests compile it alone.
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace Snapshot
{
    using Bytes = std::vector<std::uint8_t>;

    constexpr int kSize = 256; // The stock Menu textures are 256 x 256.
    constexpr std::uint32_t kPackageMagic = 0x9e2a83c1;
    constexpr int kFormatDxt1 = 3, kFormatRgba8 = 5, kFormatDxt3 = 7, kFormatDxt5 = 8;

    // The interface package's name for a map file's stem.
    inline std::string PackageName(const std::string& mapStem)
    {
        if (mapStem.empty()) throw std::runtime_error("Save the map first: the snapshot is stored next to the map's name.");
        if (mapStem.find_first_of(" \t\"'\\/:") != std::string::npos)
            throw std::runtime_error("The map's file name cannot contain spaces or quotes; the game names the snapshot package after it.");
        return mapStem + "-i";
    }

    // A 24-bit bottom-up BMP from top-down BGR rows, as the editor's TEXTURE
    // IMPORT reads it (a 24-bit BMP becomes an RGBA8 texture).
    inline Bytes Bmp24(int width, int height, const Bytes& bgrTopDown)
    {
        if (width <= 0 || height <= 0 || bgrTopDown.size() != static_cast<size_t>(width) * height * 3)
            throw std::runtime_error("Snapshot pixels do not match their size.");
        const size_t stride = (static_cast<size_t>(width) * 3 + 3) & ~size_t(3);
        const std::uint32_t dataSize = static_cast<std::uint32_t>(stride * height), fileSize = 54 + dataSize;
        Bytes out(fileSize, 0);
        auto put32 = [&](size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) out[at + i] = static_cast<std::uint8_t>(v >> (8 * i)); };
        auto put16 = [&](size_t at, std::uint16_t v) { out[at] = static_cast<std::uint8_t>(v); out[at + 1] = static_cast<std::uint8_t>(v >> 8); };
        out[0] = 'B'; out[1] = 'M';
        put32(2, fileSize); put32(10, 54);
        put32(14, 40); put32(18, static_cast<std::uint32_t>(width)); put32(22, static_cast<std::uint32_t>(height));
        put16(26, 1); put16(28, 24); put32(34, dataSize); put32(38, 2835); put32(42, 2835);
        for (int y = 0; y < height; ++y)
            std::memcpy(&out[54 + stride * (height - 1 - y)], &bgrTopDown[static_cast<size_t>(y) * width * 3], static_cast<size_t>(width) * 3);
        return out;
    }

    // The .utc container: uncompressed size, compressed size, then a zlib
    // stream. A plain package (magic first) is returned as is by the caller.
    struct Container { bool compressed = false; std::uint32_t uncompressedSize = 0, compressedSize = 0; size_t dataOffset = 0; };
    inline std::uint32_t Le32(const Bytes& b, size_t at)
    {
        if (at + 4 > b.size()) throw std::runtime_error("Truncated package.");
        return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
    }
    inline Container Describe(const Bytes& file)
    {
        Container c;
        if (file.size() < 12) throw std::runtime_error("The package file is too short.");
        if (Le32(file, 0) == kPackageMagic) return c;
        c.uncompressedSize = Le32(file, 0);
        c.compressedSize = Le32(file, 4);
        c.dataOffset = 8;
        if (file[8] != 0x78 || c.compressedSize > file.size() - 8 || c.uncompressedSize < 64)
            throw std::runtime_error("Not an Unreal package or a compressed .utc.");
        c.compressed = true;
        return c;
    }
    inline Bytes Contain(const Bytes& package, const Bytes& zlibStream)
    {
        Bytes out(8);
        const std::uint32_t sizes[2] = { static_cast<std::uint32_t>(package.size()), static_cast<std::uint32_t>(zlibStream.size()) };
        for (int k = 0; k < 2; ++k) for (int i = 0; i < 4; ++i) out[k * 4 + i] = static_cast<std::uint8_t>(sizes[k] >> (8 * i));
        out.insert(out.end(), zlibStream.begin(), zlibStream.end());
        return out;
    }

    // Package tables. Names are XOR-coded by file position from version 175 on,
    // and every name reference carries the saving process's name number too.
    struct Import { std::string classPackage, className, name; int outer = 0; };
    struct Export { std::string name; int classIndex = 0, outer = 0; std::uint32_t flags = 0, size = 0, offset = 0; std::string className; };
    struct Texture { int width = 0, height = 0, format = -1, mips = 0; };
    struct Package
    {
        int version = 0;
        std::vector<std::string> names;
        std::vector<Import> imports;
        std::vector<Export> exports;
        const Export* Find(const std::string& name, const std::string& className) const
        {
            for (const auto& e : exports)
                if (Fold(e.name) == Fold(name) && Fold(e.className) == Fold(className)) return &e;
            return nullptr;
        }
        static std::string Fold(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
    };
    struct Reader
    {
        const Bytes& b; size_t pos = 0; int version = 0;
        explicit Reader(const Bytes& bytes, size_t at = 0) : b(bytes), pos(at) {}
        std::uint8_t U8() { if (pos >= b.size()) throw std::runtime_error("Truncated package table."); return b[pos++]; }
        std::uint16_t U16() { std::uint16_t v = U8(); return static_cast<std::uint16_t>(v | (U8() << 8)); }
        std::uint32_t U32() { std::uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(U8()) << (8 * i); return v; }
        int Compact()
        {
            auto byte = U8();
            const bool negative = (byte & 0x80) != 0;
            std::uint32_t value = byte & 0x3f;
            bool more = (byte & 0x40) != 0;
            for (int shift = 6, i = 1; more; shift += 7, ++i)
            {
                if (i >= 5) throw std::runtime_error("Invalid compact index.");
                byte = U8();
                value |= static_cast<std::uint32_t>(byte & 0x7f) << shift;
                more = (byte & 0x80) != 0;
            }
            if (value > 0x7fffffff) throw std::runtime_error("Compact index out of range.");
            return negative ? -static_cast<int>(value) : static_cast<int>(value);
        }
        std::string Name(const std::vector<std::string>& names)
        {
            const int index = Compact();
            Compact(); // The saving process's name number; the string is what counts.
            if (index < 0 || static_cast<size_t>(index) >= names.size()) throw std::runtime_error("Invalid name reference.");
            return names[static_cast<size_t>(index)];
        }
    };
    inline Package Parse(const Bytes& b)
    {
        Reader r(b);
        if (r.U32() != kPackageMagic) throw std::runtime_error("Not an Unreal package.");
        Package p;
        p.version = r.U16();
        r.U16(); r.U32();
        const auto nameCount = r.U32(), nameOffset = r.U32(), exportCount = r.U32(), exportOffset = r.U32(), importCount = r.U32(), importOffset = r.U32();
        if (nameCount > 1000000 || exportCount > 1000000 || importCount > 1000000) throw std::runtime_error("Implausible package table counts.");
        if (nameOffset >= b.size() || exportOffset > b.size() || importOffset > b.size()) throw std::runtime_error("Package table offset is outside the file.");
        r.pos = nameOffset;
        for (std::uint32_t i = 0; i < nameCount; ++i)
        {
            const int length = r.Compact();
            if (length <= 0 || length > 1024) throw std::runtime_error("Invalid name length.");
            std::string name;
            for (int j = 0; j < length; ++j)
            {
                const auto at = r.pos;
                const char c = static_cast<char>(r.U8() ^ (p.version >= 175 ? static_cast<std::uint8_t>(at & 255) : 0));
                if (j == length - 1 ? c != 0 : c == 0) throw std::runtime_error("Invalid name terminator.");
                if (j < length - 1) name += c;
            }
            r.U32();
            p.names.push_back(name);
        }
        r.pos = importOffset;
        for (std::uint32_t i = 0; i < importCount; ++i)
        {
            Import imp;
            imp.classPackage = r.Name(p.names);
            imp.className = r.Name(p.names);
            imp.outer = static_cast<int>(r.U32());
            imp.name = r.Name(p.names);
            p.imports.push_back(imp);
        }
        r.pos = exportOffset;
        for (std::uint32_t i = 0; i < exportCount; ++i)
        {
            Export e;
            e.classIndex = r.Compact();
            r.Compact();
            e.outer = static_cast<int>(r.U32());
            e.name = r.Name(p.names);
            e.flags = r.U32();
            e.size = static_cast<std::uint32_t>(r.Compact());
            e.offset = e.size ? static_cast<std::uint32_t>(r.Compact()) : 0;
            if (e.size && (e.offset >= b.size() || e.size > b.size() - e.offset)) throw std::runtime_error("Export data is outside the file.");
            if (e.classIndex < 0)
            {
                const size_t importIndex = static_cast<size_t>(-e.classIndex - 1);
                if (importIndex >= p.imports.size()) throw std::runtime_error("Export class refers outside the import table.");
                e.className = p.imports[importIndex].name;
            }
            else if (e.classIndex > 0)
                e.className = "(exported class)";
            p.exports.push_back(e);
        }
        return p;
    }

    // Size, format and mip count of a texture export, from its property list
    // and the mip array that follows it.
    inline Texture DescribeTexture(const Bytes& b, const Package& p, const Export& e)
    {
        Texture t;
        Reader r(b, e.offset);
        r.version = p.version;
        const size_t end = static_cast<size_t>(e.offset) + e.size;
        for (;;)
        {
            const auto name = r.Name(p.names);
            if (name == "None") break;
            const auto info = r.U8();
            const int type = info & 0xf, sizeCode = (info >> 4) & 7;
            size_t size = 0;
            switch (sizeCode)
            {
            case 0: size = 1; break; case 1: size = 2; break; case 2: size = 4; break; case 3: size = 12; break; case 4: size = 16; break;
            case 5: size = r.U8(); break; case 6: size = r.U16(); break; default: size = r.U32(); break;
            }
            if (type == 3) continue; // A bool's value is the array bit.
            if (type == 10) r.Name(p.names); // Struct name.
            if (info & 0x80) { const auto index = r.U8(); if (index >= 0x80) r.U8(); if (index >= 0xc0) r.U16(); }
            if (r.pos + size > end) throw std::runtime_error("Texture property runs past its export.");
            const auto value = static_cast<int>(Le32(b, r.pos) & (size >= 4 ? 0xffffffffu : (1u << (8 * size)) - 1));
            if (name == "USize" && type == 2) t.width = value;
            else if (name == "VSize" && type == 2) t.height = value;
            else if (name == "Format" && type == 1) t.format = value & 0xff;
            r.pos += size;
        }
        t.mips = r.Compact();
        if (t.mips < 0 || t.mips > 16) throw std::runtime_error("Implausible mip count.");
        return t;
    }

    // What the saved package must contain for the game to show the snapshot,
    // and for nothing else of the package to have been lost on the way.
    struct Verified { Texture menu; size_t exportCount = 0; };
    inline Verified Verify(const Bytes& package, size_t previousExportCount)
    {
        const auto p = Parse(package);
        const auto* menu = p.Find("Menu", "Texture");
        if (!menu) throw std::runtime_error("The saved package has no Menu texture.");
        Verified v;
        v.menu = DescribeTexture(package, p, *menu);
        v.exportCount = p.exports.size();
        if (v.menu.width != kSize || v.menu.height != kSize)
            throw std::runtime_error("The Menu texture is " + std::to_string(v.menu.width) + " x " + std::to_string(v.menu.height) + ", not " + std::to_string(kSize) + " x " + std::to_string(kSize) + ".");
        if (v.menu.mips < 1) throw std::runtime_error("The Menu texture has no image data.");
        if (v.exportCount < previousExportCount)
            throw std::runtime_error("The saved package holds " + std::to_string(v.exportCount) + " objects where the previous one held " + std::to_string(previousExportCount) + "; the loading screens or map settings would be lost.");
        return v;
    }
}
