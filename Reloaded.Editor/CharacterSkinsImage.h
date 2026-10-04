#pragma once
// Character Skins images: the stock character textures read straight from
// SPersoTextures.utx and written as 32-bit TGA, which paint programs open and the
// editor's texture importer reads back. Pure: no engine, so the tests compile it alone.
#include "LevelSnapshotModel.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace CharacterSkins
{
using Bytes = Snapshot::Bytes;

struct Image
{
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba; // top-down rows, R G B A
};

// The top mip of a texture export: format (Snapshot::kFormat*), size and its bytes.
struct Mip
{
    int format = -1, width = 0, height = 0;
    Bytes data;
};

inline Mip ReadMip(const Bytes& package, const std::string& name)
{
    const auto p = Snapshot::Parse(package);
    const auto* e = p.Find(name, "Texture");
    if (!e) throw std::runtime_error("SPersoTextures has no texture " + name + ".");
    const auto t = Snapshot::DescribeTexture(package, p, *e);
    // DescribeTexture stops after the mip count; walk the same property list to find it.
    Snapshot::Reader r(package, e->offset);
    r.version = p.version;
    for (;;)
    {
        if (r.Name(p.names) == "None") break;
        const auto info = r.U8();
        const int type = info & 0xf, sizeCode = (info >> 4) & 7;
        size_t size = 0;
        switch (sizeCode)
        {
        case 0: size = 1; break; case 1: size = 2; break; case 2: size = 4; break; case 3: size = 12; break; case 4: size = 16; break;
        case 5: size = r.U8(); break; case 6: size = r.U16(); break; default: size = r.U32(); break;
        }
        if (type == 3) continue;
        if (type == 10) r.Name(p.names);
        if (info & 0x80) { const auto index = r.U8(); if (index >= 0x80) r.U8(); if (index >= 0xc0) r.U16(); }
        r.pos += size;
    }
    if (r.Compact() < 1) throw std::runtime_error(name + " has no image data.");
    r.U32(); // lazy array end offset
    const int bytes = r.Compact();
    const size_t end = static_cast<size_t>(e->offset) + e->size;
    if (bytes <= 0 || r.pos + bytes > end) throw std::runtime_error(name + "'s image data runs past its export.");
    Mip m;
    m.format = t.format;
    m.width = t.width;
    m.height = t.height;
    m.data.assign(package.begin() + r.pos, package.begin() + r.pos + bytes);
    return m;
}

namespace Detail
{
inline void Color565(std::uint16_t c, std::uint8_t out[3])
{
    out[0] = static_cast<std::uint8_t>(((c >> 11) & 31) * 255 / 31);
    out[1] = static_cast<std::uint8_t>(((c >> 5) & 63) * 255 / 63);
    out[2] = static_cast<std::uint8_t>((c & 31) * 255 / 31);
}
// One DXT colour block into a 4x4 RGBA tile; threeColour allows DXT1's transparent entry.
inline void ColourBlock(const std::uint8_t* b, bool dxt1, std::uint8_t tile[16][4])
{
    const std::uint16_t c0 = static_cast<std::uint16_t>(b[0] | (b[1] << 8)), c1 = static_cast<std::uint16_t>(b[2] | (b[3] << 8));
    std::uint8_t palette[4][4]{};
    Color565(c0, palette[0]);
    Color565(c1, palette[1]);
    for (int i = 0; i < 4; ++i) palette[i][3] = 255;
    for (int k = 0; k < 3; ++k)
    {
        if (!dxt1 || c0 > c1)
        {
            palette[2][k] = static_cast<std::uint8_t>((2 * palette[0][k] + palette[1][k]) / 3);
            palette[3][k] = static_cast<std::uint8_t>((palette[0][k] + 2 * palette[1][k]) / 3);
        }
        else
        {
            palette[2][k] = static_cast<std::uint8_t>((palette[0][k] + palette[1][k]) / 2);
            palette[3][k] = 0;
        }
    }
    if (dxt1 && c0 <= c1) palette[3][3] = 0;
    const std::uint32_t bits = b[4] | (b[5] << 8) | (b[6] << 16) | (static_cast<std::uint32_t>(b[7]) << 24);
    for (int i = 0; i < 16; ++i)
        for (int k = 0; k < 4; ++k) tile[i][k] = palette[(bits >> (2 * i)) & 3][k];
}
} // namespace Detail

inline Image Decode(const Mip& m)
{
    if (m.width <= 0 || m.height <= 0 || m.width > 8192 || m.height > 8192) throw std::runtime_error("Implausible texture size.");
    Image image;
    image.width = m.width;
    image.height = m.height;
    image.rgba.assign(static_cast<size_t>(m.width) * m.height * 4, 0);
    const size_t pixels = static_cast<size_t>(m.width) * m.height;
    if (m.format == Snapshot::kFormatRgba8)
    {
        if (m.data.size() < pixels * 4) throw std::runtime_error("RGBA8 texture data is short.");
        for (size_t i = 0; i < pixels; ++i) // stored B G R A
        {
            image.rgba[i * 4] = m.data[i * 4 + 2];
            image.rgba[i * 4 + 1] = m.data[i * 4 + 1];
            image.rgba[i * 4 + 2] = m.data[i * 4];
            image.rgba[i * 4 + 3] = m.data[i * 4 + 3];
        }
        return image;
    }
    const bool dxt1 = m.format == Snapshot::kFormatDxt1, dxt3 = m.format == Snapshot::kFormatDxt3, dxt5 = m.format == Snapshot::kFormatDxt5;
    if (!dxt1 && !dxt3 && !dxt5) throw std::runtime_error("Unsupported texture format " + std::to_string(m.format) + ".");
    const int blocksWide = (m.width + 3) / 4, blocksHigh = (m.height + 3) / 4;
    const size_t blockSize = dxt1 ? 8 : 16;
    if (m.data.size() < static_cast<size_t>(blocksWide) * blocksHigh * blockSize) throw std::runtime_error("DXT texture data is short.");
    for (int by = 0; by < blocksHigh; ++by)
        for (int bx = 0; bx < blocksWide; ++bx)
        {
            const std::uint8_t* block = &m.data[(static_cast<size_t>(by) * blocksWide + bx) * blockSize];
            std::uint8_t tile[16][4];
            Detail::ColourBlock(dxt1 ? block : block + 8, dxt1, tile);
            if (dxt3)
                for (int i = 0; i < 16; ++i)
                {
                    const int nibble = (block[i / 2] >> (4 * (i & 1))) & 15;
                    tile[i][3] = static_cast<std::uint8_t>(nibble * 17);
                }
            else if (dxt5)
            {
                const int a0 = block[0], a1 = block[1];
                int alpha[8] = {a0, a1};
                for (int i = 2; i < 8; ++i)
                    alpha[i] = a0 > a1 ? ((8 - i) * a0 + (i - 1) * a1) / 7 : (i < 6 ? ((6 - i) * a0 + (i - 1) * a1) / 5 : (i == 6 ? 0 : 255));
                std::uint64_t bits = 0;
                for (int i = 0; i < 6; ++i) bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
                for (int i = 0; i < 16; ++i) tile[i][3] = static_cast<std::uint8_t>(alpha[(bits >> (3 * i)) & 7]);
            }
            for (int i = 0; i < 16; ++i)
            {
                const int x = bx * 4 + (i & 3), y = by * 4 + i / 4;
                if (x >= m.width || y >= m.height) continue;
                for (int k = 0; k < 4; ++k) image.rgba[(static_cast<size_t>(y) * m.width + x) * 4 + k] = tile[i][k];
            }
        }
    return image;
}

// Uncompressed 32-bit TGA, bottom-up rows with an 8-bit alpha channel.
inline Bytes Tga(const Image& image)
{
    if (image.rgba.size() != static_cast<size_t>(image.width) * image.height * 4 || image.width > 65535 || image.height > 65535)
        throw std::runtime_error("Image pixels do not match its size.");
    Bytes out(18, 0);
    out[2] = 2; // uncompressed true colour
    out[12] = static_cast<std::uint8_t>(image.width);
    out[13] = static_cast<std::uint8_t>(image.width >> 8);
    out[14] = static_cast<std::uint8_t>(image.height);
    out[15] = static_cast<std::uint8_t>(image.height >> 8);
    out[16] = 32;
    out[17] = 8; // alpha bits, bottom-left origin
    out.reserve(out.size() + image.rgba.size());
    for (int y = image.height - 1; y >= 0; --y)
        for (int x = 0; x < image.width; ++x)
        {
            const auto* p = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
            out.push_back(p[2]);
            out.push_back(p[1]);
            out.push_back(p[0]);
            out.push_back(p[3]);
        }
    return out;
}

// Image files the editor's texture importer reads.
inline bool Importable(const std::string& extension)
{
    std::string e;
    for (char c : extension) e += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e == ".tga" || e == ".bmp" || e == ".pcx" || e == ".dds";
}
} // namespace CharacterSkins
