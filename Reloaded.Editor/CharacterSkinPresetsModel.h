#pragma once
// Character Skin presets: a named look for ONE team (its two material slots, its
// model swap and that model's goggle light offset) kept in a library and applied to
// any map through the Character Skins Import and Apply paths. Applying a spy preset
// changes only the spy values and leaves the mercs as the map has them, so spies and
// mercs can wear different presets. Pure: no Windows, no engine, so the tests
// compile it alone.
//
// Entry, the contract with the browser and the editor side:
//   {"id": "builtin.<team>.<slug>" (compiled in, never written) | 32 lower-case hex (user),
//    "team": "spy"|"merc",
//    "name", "category", "description",
//    "builtin": true|false, "readonly": true|false (in memory only, never written),
//    "modified": epoch milliseconds as text (user entries),
//    "slots": {"SpyBody"|"SpyHead" (spy) or "MercBody"|"MercHead" (merc):
//               {"recipe": {...}}  a camouflage made from the stock texture (Recolour)
//             | {"image": "<Slot>.tga"}  a picture kept beside the entry
//             | {"path": "Package.Group.Name"}  a material in a shared package},
//              a slot left out keeps the stock look,
//    "models": {"SpyModel" or "MercModel": "Package.Mesh"} (optional),
//    "mesh": {"name": "HazmatMerc", "materials": ["HazmatSuit", "HazmatGear"]} (optional):
//            a model the preset carries, instead of naming one in "models": <name>.psk
//            rigged to the team's stock skeleton and one <material>.tga per PSK material,
//            in the DLL for a built-in, beside the entry for the user's own. Applying
//            imports them into the map (see MeshImportName),
//    "goggles": {"SpyGoggleOffset" or "MercGoggleOffset": [x,y,z]} (optional)}
// User file, <Editor::Directory()>/skin_presets.json:
//   {"version":2,"presets":[user entries],"hiddenBuiltins":[built-in ids the user hid],
//    "imports":{"<texture name a preset's picture was imported under>": "<preset id>"}}
// with each user entry's pictures in <Editor::Directory()>/skin_presets/<id>/<Slot>.tga
// and its model's files beside them.
// Version 1 entries dressed both teams; Migrate splits each into a spy and a merc entry.
// Shared preset file (*.skinpreset): {"version":2,"format":"RE+ skin preset",
//   "preset":{entry without id, modified and flags},"images":{"<Slot>":"<base64 TGA>"},
//   "files":{"<model file>":"<base64>"} for a preset carrying a model};
// a version 1 file (both teams) reads back as two entries.
#include "CharacterSkinsImage.h"
#include "CharacterSkinsModel.h"
#include "EmitterLibraryModel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace CharacterSkins::Presets
{
using Json = Workflow::Json;
using Workflow::Fold;

constexpr const char* FileName = "skin_presets.json";
constexpr const char* ImageFolder = "skin_presets";
constexpr const char* ShareFormat = "RE+ skin preset";
constexpr const char* ShareExtension = ".skinpreset";
constexpr int MaxSide = 2048;
constexpr int FileVersion = 2;  // skin_presets.json
constexpr int ShareVersion = 2; // *.skinpreset

// The two teams a preset can belong to, and the Character Skins values each owns.
struct Team
{
    const char* id;     // "spy" | "merc", as written in entries
    const char* label;  // "Spy"
    const char* plural; // "Spies"
    const char* body;   // slot properties
    const char* head;
    const char* model;   // model property
    const char* goggles; // goggle offset property
};
inline constexpr std::array<Team, 2> Teams = {{
    {"spy", "Spy", "Spies", "SpyBody", "SpyHead", "SpyModel", "SpyGoggleOffset"},
    {"merc", "Merc", "Mercs", "MercBody", "MercHead", "MercModel", "MercGoggleOffset"},
}};
inline const Team* FindTeam(const std::string& id)
{
    for (const auto& team : Teams)
        if (id == team.id) return &team;
    return nullptr;
}
// The team owning a slot, model or goggle property; null for anything else.
inline const Team* TeamOfProperty(const std::string& property)
{
    for (const auto& team : Teams)
        if (property == team.body || property == team.head || property == team.model || property == team.goggles) return &team;
    return nullptr;
}
inline bool Owns(const Team& team, const std::string& property) { return TeamOfProperty(property) == &team; }
// The team of a (valid) entry.
inline const Team& TeamOf(const Json& entry)
{
    const auto* team = entry.is_object() && entry.contains("team") && entry.at("team").is_string() ? FindTeam(entry.at("team").get<std::string>()) : nullptr;
    if (!team) throw std::runtime_error("The skin preset belongs to no team.");
    return *team;
}

inline const std::vector<std::string>& DefaultCategories()
{
    static const std::vector<std::string> categories = {"Woodland & Jungle", "Desert", "Snow & Arctic", "Urban & Night", "Fun", "Models", "Other"};
    return categories;
}
inline bool IsBuiltinId(const std::string& id) { return Workflow::EmitterLibrary::IsBuiltinId(id); }
inline bool IsUserId(const std::string& id) { return Workflow::EmitterLibrary::IsUserId(id); }
inline const Slot* FindSlot(const std::string& property)
{
    for (const auto& slot : Slots)
        if (property == slot.property) return &slot;
    return nullptr;
}
inline const ModelSlot* FindModel(const std::string& property)
{
    for (const auto& model : Models)
        if (property == model.property) return &model;
    return nullptr;
}
inline const ModelSlot* FindGoggles(const std::string& property)
{
    for (const auto& model : Models)
        if (property == model.goggles) return &model;
    return nullptr;
}
// The picture file of a slot kept beside a user entry.
inline std::string ImageFile(const std::string& property) { return property + ".tga"; }

// ---------------------------------------------------------------------------
// Models a preset carries ("mesh"): a PSK and one TGA per material.
constexpr size_t MaxModelMaterials = 8;
// A model or material name: what the editor names the imported objects after.
inline bool ValidModelName(const std::string& name)
{
    if (name.empty() || name.size() > 40 || !std::isalpha(static_cast<unsigned char>(name[0]))) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}
// A model name made from a file name: other characters become underscores.
inline std::string ModelNameFrom(const std::string& stem)
{
    std::string out;
    for (unsigned char c : stem) out += std::isalnum(c) ? static_cast<char>(c) : '_';
    if (out.empty() || !std::isalpha(static_cast<unsigned char>(out[0]))) out = "Model_" + out;
    if (out.size() > 40) out.resize(40);
    return out;
}
inline bool HasMesh(const Json& entry) { return entry.is_object() && entry.contains("mesh"); }
inline std::string MeshFile(const Json& entry) { return entry.at("mesh").at("name").get<std::string>() + ".psk"; }
inline std::string MaterialFile(const std::string& material) { return material + ".tga"; }
// Every file of an entry's model: the PSK first, then the pictures in material order.
inline std::vector<std::string> MeshFiles(const Json& entry)
{
    std::vector<std::string> out;
    if (!HasMesh(entry)) return out;
    out.push_back(MeshFile(entry));
    for (const auto& material : entry.at("mesh").at("materials")) out.push_back(MaterialFile(material.get<std::string>()));
    return out;
}
// The material names of a PSK (ActorX skeletal mesh), in order. Throws when the bytes are
// not one: chunks of a 32-byte header (20-byte id, flags, record size, record count).
inline std::vector<std::string> PskMaterials(const Bytes& psk)
{
    auto i32 = [&](size_t at) { std::int32_t v; std::memcpy(&v, psk.data() + at, 4); return v; };
    if (psk.size() < 32 || std::memcmp(psk.data(), "ACTRHEAD", 8) != 0) throw std::runtime_error("This is not a PSK skeletal mesh (no ACTRHEAD).");
    std::vector<std::string> materials;
    bool points = false, faces = false, bones = false;
    size_t at = 0;
    while (at + 32 <= psk.size())
    {
        const std::string id(reinterpret_cast<const char*>(psk.data() + at), strnlen(reinterpret_cast<const char*>(psk.data() + at), 20));
        const std::int32_t size = i32(at + 24), count = i32(at + 28);
        if (size < 0 || count < 0 || static_cast<std::uint64_t>(size) * static_cast<std::uint64_t>(count) > psk.size() - at - 32)
            throw std::runtime_error("The PSK is damaged (chunk " + id + ").");
        const size_t body = at + 32;
        if (id == "PNTS0000") points = count > 0;
        else if (id == "FACE0000") faces = count > 0;
        else if (id == "REFSKELT") bones = count > 0;
        else if (id == "MATT0000")
        {
            if (size < 64) throw std::runtime_error("The PSK's materials are damaged.");
            for (std::int32_t m = 0; m < count; ++m)
            {
                const char* name = reinterpret_cast<const char*>(psk.data() + body + static_cast<size_t>(m) * size);
                materials.emplace_back(name, strnlen(name, 64));
            }
        }
        at = body + static_cast<size_t>(size) * static_cast<size_t>(count);
    }
    if (at != psk.size()) throw std::runtime_error("The PSK is damaged (it ends inside a chunk).");
    if (!points || !faces) throw std::runtime_error("The PSK has no geometry.");
    if (!bones) throw std::runtime_error("The PSK has no skeleton; a character model must be rigged to the team's stock skeleton.");
    if (materials.empty()) throw std::runtime_error("The PSK names no materials.");
    if (materials.size() > MaxModelMaterials) throw std::runtime_error("The PSK has more than " + std::to_string(MaxModelMaterials) + " materials.");
    return materials;
}

// ---------------------------------------------------------------------------
// Recipes: camouflage painted over the stock texture. The stock picture keeps its
// folds, straps and seams as shading, and its lit parts (goggle lenses, lamps, the
// merc's skin and orange goggles) are left as they are.
struct Rgb { double r = 0, g = 0, b = 0; };
struct Recipe
{
    std::string pattern;      // blotch, digital, tiger, stripes or solid
    std::vector<Rgb> colours; // the first is the ground colour
    double scale = 64;        // size of a patch, in texels of a 512-texel texture
    double cell = 6;          // digital: size of a square, in the same texels
    unsigned seed = 1;
    double contrast = 1;      // how strongly the stock shading shows (0.2 .. 2)
};
inline const std::vector<std::string>& Patterns()
{
    static const std::vector<std::string> patterns = {"blotch", "digital", "tiger", "stripes", "solid"};
    return patterns;
}
inline Rgb ParseColour(const std::string& text)
{
    if (text.size() != 7 || text[0] != '#') throw std::runtime_error("Colours are written #rrggbb, not " + text + ".");
    int value[3]{};
    for (int k = 0; k < 3; ++k)
        for (int d = 0; d < 2; ++d)
        {
            const char c = text[1 + 2 * k + d];
            int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0) throw std::runtime_error("Colours are written #rrggbb, not " + text + ".");
            value[k] = value[k] * 16 + digit;
        }
    return {value[0] / 255.0, value[1] / 255.0, value[2] / 255.0};
}
inline Recipe ReadRecipe(const Json& json)
{
    if (!json.is_object()) throw std::runtime_error("A recipe is not an object.");
    Recipe r;
    r.pattern = json.value("pattern", std::string{});
    if (std::find(Patterns().begin(), Patterns().end(), r.pattern) == Patterns().end())
        throw std::runtime_error("Unknown camouflage pattern '" + r.pattern + "'.");
    if (!json.contains("colours") || !json.at("colours").is_array() || json.at("colours").empty() || json.at("colours").size() > 6)
        throw std::runtime_error("A recipe needs 1 to 6 colours.");
    for (const auto& c : json.at("colours"))
    {
        if (!c.is_string()) throw std::runtime_error("Colours are written #rrggbb.");
        r.colours.push_back(ParseColour(c.get<std::string>()));
    }
    auto number = [&](const char* key, double fallback, double low, double high) {
        if (!json.contains(key)) return fallback;
        const auto& v = json.at(key);
        if (!v.is_number() || !std::isfinite(v.get<double>()) || v.get<double>() < low || v.get<double>() > high)
            throw std::runtime_error(std::string("A recipe's ") + key + " must be a number from " + Number(low) + " to " + Number(high) + ".");
        return v.get<double>();
    };
    r.scale = number("scale", 64, 4, 512);
    r.cell = number("cell", 6, 1, 64);
    r.seed = static_cast<unsigned>(number("seed", 1, 0, 1000000));
    r.contrast = number("contrast", 1, 0.2, 2);
    return r;
}

namespace Detail
{
inline double Hash(int x, int y, unsigned seed)
{
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xffffff) / static_cast<double>(0x1000000);
}
inline double Smooth(double t) { return t * t * (3 - 2 * t); }
inline double ValueNoise(double x, double y, unsigned seed)
{
    const double fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const double tx = Smooth(x - fx), ty = Smooth(y - fy);
    const double a = Hash(ix, iy, seed), b = Hash(ix + 1, iy, seed), c = Hash(ix, iy + 1, seed), d = Hash(ix + 1, iy + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}
// Three octaves of value noise, about 0..1 around 0.5.
inline double Fractal(double x, double y, unsigned seed)
{
    return (ValueNoise(x, y, seed) * 4 + ValueNoise(x * 2.03, y * 2.03, seed + 101) * 2 + ValueNoise(x * 4.11, y * 4.11, seed + 202)) / 7;
}
inline double Luma(const std::uint8_t* p) { return (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) / 255; }
inline double Ramp(double v, double low, double high) { return std::clamp((v - low) / (high - low), 0.0, 1.0); }
} // namespace Detail

// The colour index of the pattern at (u, v), in texels of a 512-texel texture.
inline size_t PatternIndex(const Recipe& r, double u, double v)
{
    const size_t n = r.colours.size();
    if (n <= 1 || r.pattern == "solid") return 0;
    if (r.pattern == "stripes")
    {
        const auto band = static_cast<long long>(std::floor((u + v) / r.scale));
        return static_cast<size_t>(((band % static_cast<long long>(n)) + static_cast<long long>(n)) % static_cast<long long>(n));
    }
    double x = u / r.scale, y = v / r.scale;
    if (r.pattern == "digital")
    {
        x = std::floor(u / r.cell) * r.cell / r.scale;
        y = std::floor(v / r.cell) * r.cell / r.scale;
    }
    else if (r.pattern == "tiger")
    {
        // Long horizontal brush strokes, bent by a slow wave.
        const double bend = Detail::Fractal(x * 0.5, y * 0.5, r.seed + 977) - 0.5;
        x = x * 0.3;
        y = y * 1.8 + bend * 2.5;
    }
    size_t index = 0;
    // Each further colour is a layer of patches over the earlier ones; later
    // layers cover less, so the last colour (usually the darkest) is a fleck.
    for (size_t k = 1; k < n; ++k)
        if (Detail::Fractal(x + k * 17.31, y - k * 9.77, r.seed * 131 + static_cast<unsigned>(k)) > 0.5 + 0.035 * (k - 1)) index = k;
    return index;
}

// How much of a stock texel is kept as it is: lit, saturated parts (lenses,
// lamps, the orange goggles) and skin.
inline double Kept(const std::uint8_t* p)
{
    const double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
    const double high = (std::max)({r, g, b}), low = (std::min)({r, g, b});
    const double saturation = high > 0 ? (high - low) / high : 0;
    const double lit = Detail::Ramp(saturation, 0.35, 0.6) * Detail::Ramp(high, 0.3, 0.55);
    // Skin: warm (red above green above blue), not grey, not dark.
    const bool warm = r > g && g > b && r - b > 0.12;
    const double skin = warm ? Detail::Ramp(saturation, 0.18, 0.3) * Detail::Ramp(high, 0.3, 0.45) * (1 - Detail::Ramp(saturation, 0.75, 0.9)) : 0;
    return (std::max)(lit, skin);
}

// The stock picture repainted with the recipe; alpha is kept.
inline Image Recolour(const Image& stock, const Recipe& r)
{
    if (stock.width <= 0 || stock.height <= 0 || stock.rgba.size() != static_cast<size_t>(stock.width) * stock.height * 4)
        throw std::runtime_error("The stock picture is empty.");
    if (r.colours.empty()) throw std::runtime_error("A recipe needs a colour.");
    const size_t pixels = static_cast<size_t>(stock.width) * stock.height;
    std::vector<double> kept(pixels);
    double sum = 0, weight = 0;
    for (size_t i = 0; i < pixels; ++i)
    {
        kept[i] = Kept(&stock.rgba[i * 4]);
        sum += Detail::Luma(&stock.rgba[i * 4]) * (1 - kept[i]);
        weight += 1 - kept[i];
    }
    // The cloth's own brightness becomes the camouflage's: a texel as bright as
    // the cloth's average shows the colour as it is.
    const double reference = weight > 0 && sum / weight > 0.01 ? sum / weight : 0.12;
    const double su = 512.0 / stock.width, sv = 512.0 / stock.height;
    Image out = stock;
    for (int y = 0; y < stock.height; ++y)
        for (int x = 0; x < stock.width; ++x)
        {
            const size_t i = static_cast<size_t>(y) * stock.width + x;
            const auto* p = &stock.rgba[i * 4];
            const auto& c = r.colours[PatternIndex(r, (x + 0.5) * su, (y + 0.5) * sv)];
            const double shade = std::pow(std::clamp(Detail::Luma(p) / reference, 0.15, 2.4), 0.8 * r.contrast);
            const double paint[3] = {c.r * shade, c.g * shade, c.b * shade};
            for (int k = 0; k < 3; ++k)
            {
                const double mixed = p[k] / 255.0 * kept[i] + std::clamp(paint[k], 0.0, 1.0) * (1 - kept[i]);
                out.rgba[i * 4 + k] = static_cast<std::uint8_t>(std::lround(std::clamp(mixed, 0.0, 1.0) * 255));
            }
        }
    return out;
}

// A picture shrunk by averaging, for thumbnails.
inline Image Shrink(const Image& image, int width, int height)
{
    if (image.width <= 0 || image.height <= 0 || width <= 0 || height <= 0) return {};
    Image out{width, height, std::vector<std::uint8_t>(static_cast<size_t>(width) * height * 4)};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const int x0 = x * image.width / width, x1 = (std::max)(x0 + 1, (x + 1) * image.width / width);
            const int y0 = y * image.height / height, y1 = (std::max)(y0 + 1, (y + 1) * image.height / height);
            double total[4]{};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx)
                    for (int k = 0; k < 4; ++k) total[k] += image.rgba[(static_cast<size_t>(sy) * image.width + sx) * 4 + k];
            const double count = static_cast<double>(x1 - x0) * (y1 - y0);
            for (int k = 0; k < 4; ++k) out.rgba[(static_cast<size_t>(y) * width + x) * 4 + k] = static_cast<std::uint8_t>(std::lround(total[k] / count));
        }
    return out;
}

// ---------------------------------------------------------------------------
// Pictures: 32-bit or 24-bit uncompressed TGA, the files the editor imports.
inline bool PowerOfTwo(int v) { return v > 0 && (v & (v - 1)) == 0; }
inline Image ReadTga(const Bytes& data)
{
    if (data.size() < 18) throw std::runtime_error("The picture is not a TGA file.");
    const int idLength = data[0], colourMap = data[1], type = data[2];
    const int width = data[12] | (data[13] << 8), height = data[14] | (data[15] << 8), bits = data[16];
    if (colourMap != 0 || type != 2 || (bits != 24 && bits != 32))
        throw std::runtime_error("The picture must be an uncompressed 24 or 32-bit TGA file.");
    if (!PowerOfTwo(width) || !PowerOfTwo(height) || width > MaxSide || height > MaxSide)
        throw std::runtime_error("The picture's sides must be powers of two up to " + std::to_string(MaxSide) + ".");
    const size_t step = static_cast<size_t>(bits / 8), start = 18 + static_cast<size_t>(idLength);
    if (data.size() < start + static_cast<size_t>(width) * height * step) throw std::runtime_error("The TGA file is cut short.");
    const bool topDown = (data[17] & 0x20) != 0;
    Image image{width, height, std::vector<std::uint8_t>(static_cast<size_t>(width) * height * 4)};
    for (int row = 0; row < height; ++row)
    {
        const int y = topDown ? row : height - 1 - row;
        for (int x = 0; x < width; ++x)
        {
            const auto* p = &data[start + (static_cast<size_t>(row) * width + x) * step];
            auto* q = &image.rgba[(static_cast<size_t>(y) * width + x) * 4];
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
            q[3] = step == 4 ? p[3] : 255;
        }
    }
    return image;
}

// The name a picture is imported under in the map's CharacterSkins group: the slot
// and a hash of the file, so applying a preset again reuses the texture it imported
// the first time instead of adding another.
inline std::uint32_t Fnv(const Bytes& data)
{
    std::uint32_t h = 2166136261u;
    for (auto b : data) h = (h ^ b) * 16777619u;
    return h;
}
// The names a carried model is imported under: its mesh and each material with the PSK's
// hash, so applying the same model again reuses what it imported and a changed model
// arrives beside the old one instead of over it.
inline std::string ModelStamp(const Bytes& psk)
{
    char hex[9]{};
    std::snprintf(hex, sizeof(hex), "%08x", Fnv(psk));
    return hex;
}
inline std::string MeshImportName(const std::string& name, const Bytes& psk) { return name + "_" + ModelStamp(psk); }
inline std::string MaterialImportName(const std::string& material, const Bytes& psk) { return material + "_" + ModelStamp(psk); }
inline std::string ImportName(const std::string& property, const Bytes& tga)
{
    if (!FindSlot(property)) throw std::runtime_error("Unknown Character Skins slot " + property + ".");
    static const char* hex = "0123456789abcdef";
    std::string name = property + "_";
    const auto h = Fnv(tga);
    for (int shift = 28; shift >= 0; shift -= 4) name += hex[(h >> shift) & 15];
    return name;
}

inline std::string Base64(const Bytes& data)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3)
    {
        const std::uint32_t v = (data[i] << 16) | ((i + 1 < data.size() ? data[i + 1] : 0) << 8) | (i + 2 < data.size() ? data[i + 2] : 0);
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += i + 1 < data.size() ? table[(v >> 6) & 63] : '=';
        out += i + 2 < data.size() ? table[v & 63] : '=';
    }
    return out;
}
inline Bytes FromBase64(const std::string& text)
{
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        return c == '+' ? 62 : c == '/' ? 63 : -1;
    };
    if (text.size() % 4) throw std::runtime_error("A picture in the preset file is damaged.");
    Bytes out;
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4)
    {
        int v[4];
        for (int k = 0; k < 4; ++k)
        {
            v[k] = text[i + k] == '=' && i + 4 == text.size() && k >= 2 ? 0 : value(text[i + k]);
            if (v[k] < 0) throw std::runtime_error("A picture in the preset file is damaged.");
        }
        const std::uint32_t n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
        out.push_back(static_cast<std::uint8_t>(n >> 16));
        if (text[i + 2] != '=') out.push_back(static_cast<std::uint8_t>(n >> 8));
        if (text[i + 3] != '=') out.push_back(static_cast<std::uint8_t>(n));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Entries.
inline std::string CleanCategory(const std::string& category, const Json& entries = Json::array())
{
    auto value = Workflow::EmitterLibrary::CleanText(category);
    if (value.empty()) return "Other";
    if (value.size() > 48) throw std::runtime_error("Enter a category of at most 48 characters.");
    for (const auto& known : DefaultCategories())
        if (Fold(known) == Fold(value)) return known;
    if (entries.is_array())
        for (const auto& entry : entries)
            if (entry.is_object() && entry.contains("category") && entry.at("category").is_string() &&
                Fold(entry.at("category").get<std::string>()) == Fold(value))
                return entry.at("category").get<std::string>();
    return value;
}
// Defaults first, then the other categories entries use, alphabetically, then Other.
inline Json Categories(const Json& entries)
{
    std::vector<std::string> extra;
    std::set<std::string> seen;
    for (const auto& known : DefaultCategories()) seen.insert(Fold(known));
    if (entries.is_array())
        for (const auto& entry : entries)
            if (entry.is_object() && entry.contains("category") && entry.at("category").is_string())
            {
                auto category = entry.at("category").get<std::string>();
                if (!category.empty() && seen.insert(Fold(category)).second) extra.push_back(category);
            }
    std::sort(extra.begin(), extra.end(), [](const std::string& a, const std::string& b) { return Fold(a) < Fold(b); });
    Json result = Json::array();
    for (const auto& known : DefaultCategories())
        if (known != "Other") result.push_back(known);
    for (const auto& category : extra) result.push_back(category);
    result.push_back("Other");
    return result;
}

// Throws a sentence naming what is wrong.
inline void Validate(const Json& entry)
{
    auto fail = [&](const std::string& why) {
        std::string name = entry.is_object() && entry.contains("name") && entry.at("name").is_string() ? " '" + entry.at("name").get<std::string>() + "'" : "";
        throw std::runtime_error("Skin preset" + name + " is invalid: " + why);
    };
    if (!entry.is_object()) fail("it is not an object.");
    auto text = [&](const char* key, bool required) -> std::string {
        if (!entry.contains(key))
        {
            if (required) fail(std::string("its ") + key + " is missing.");
            return {};
        }
        if (!entry.at(key).is_string()) fail(std::string("its ") + key + " is not text.");
        return entry.at(key).get<std::string>();
    };
    const auto id = text("id", true);
    const bool builtin = IsBuiltinId(id), user = IsUserId(id);
    if (!builtin && !user) fail("its id is not a built-in or user id.");
    if (entry.contains("builtin") && (!entry.at("builtin").is_boolean() || entry.at("builtin").get<bool>() != builtin)) fail("its built-in flag does not match its id.");
    if (entry.contains("readonly") && (!entry.at("readonly").is_boolean() || entry.at("readonly").get<bool>() != builtin)) fail("its read-only flag does not match its id.");
    try
    {
        Workflow::EmitterLibrary::CleanName(text("name", true));
        CleanCategory(text("category", true));
        Workflow::EmitterLibrary::CleanDescription(text("description", false));
    }
    catch (const std::exception& e)
    {
        fail(e.what());
    }
    const auto modified = text("modified", user);
    if (!modified.empty() && (modified.size() > 20 || modified.find_first_not_of("0123456789") != std::string::npos)) fail("its modified time is not a number.");
    const auto* team = FindTeam(text("team", true));
    if (!team) fail("its team must be spy or merc.");
    if (builtin && id.rfind(std::string("builtin.") + team->id + ".", 0) != 0) fail("its built-in id does not name its team.");
    auto other = [&](const std::string& property) { fail(property + " belongs to the other team; a " + team->id + " preset holds only " + team->id + " values."); };
    if (!entry.contains("slots") || !entry.at("slots").is_object()) fail("its slots are missing.");
    size_t used = 0;
    for (auto it = entry.at("slots").begin(); it != entry.at("slots").end(); ++it)
    {
        if (!FindSlot(it.key())) fail("it has an unknown slot " + it.key() + ".");
        if (!Owns(*team, it.key())) other(it.key());
        const auto& value = it.value();
        if (!value.is_object() || value.size() != 1) fail("slot " + it.key() + " must hold one recipe, image or path.");
        if (value.contains("recipe"))
        {
            if (user) fail("slot " + it.key() + " has a recipe; only built-in presets are painted from recipes.");
            try { ReadRecipe(value.at("recipe")); }
            catch (const std::exception& e) { fail("slot " + it.key() + ": " + e.what()); }
        }
        else if (value.contains("image"))
        {
            if (!user) fail("slot " + it.key() + " has an image; built-in presets are painted from recipes.");
            if (value.at("image") != Json(ImageFile(it.key()))) fail("slot " + it.key() + "'s image must be " + ImageFile(it.key()) + ".");
        }
        else if (value.contains("path"))
        {
            if (!value.at("path").is_string() || value.at("path").get<std::string>().empty() || !ValidPath(value.at("path").get<std::string>()))
                fail("slot " + it.key() + "'s path is not a material path.");
            if (Fold(value.at("path").get<std::string>()).rfind("mylevel.", 0) == 0)
                fail("slot " + it.key() + " names a texture stored inside a map; keep it as an image instead.");
        }
        else
            fail("slot " + it.key() + " must hold a recipe, an image or a path.");
        ++used;
    }
    if (entry.contains("models"))
    {
        if (!entry.at("models").is_object()) fail("its models are not an object.");
        for (auto it = entry.at("models").begin(); it != entry.at("models").end(); ++it)
        {
            if (!FindModel(it.key())) fail("it has an unknown model " + it.key() + ".");
            if (!Owns(*team, it.key())) other(it.key());
            if (!it.value().is_string() || !ValidPath(it.value().get<std::string>())) fail("model " + it.key() + " is not a mesh path.");
            if (!it.value().get<std::string>().empty()) ++used;
        }
    }
    if (entry.contains("mesh"))
    {
        const auto& mesh = entry.at("mesh");
        if (!mesh.is_object() || !mesh.contains("name") || !mesh.at("name").is_string() || !ValidModelName(mesh.at("name").get<std::string>()))
            fail("its model needs a name of letters, digits and underscores.");
        if (!mesh.contains("materials") || !mesh.at("materials").is_array() || mesh.at("materials").empty() || mesh.at("materials").size() > MaxModelMaterials)
            fail("its model needs 1 to " + std::to_string(MaxModelMaterials) + " materials.");
        std::set<std::string> seen;
        for (const auto& material : mesh.at("materials"))
            if (!material.is_string() || !ValidModelName(material.get<std::string>()) || !seen.insert(Fold(material.get<std::string>())).second)
                fail("its model's materials must be distinct names of letters, digits and underscores.");
        if (entry.contains("models") && entry.at("models").is_object() && !entry.at("models").value(team->model, std::string{}).empty())
            fail("it both names a model and carries one; keep one.");
        ++used;
    }
    if (entry.contains("goggles"))
    {
        if (!entry.at("goggles").is_object()) fail("its goggle light offsets are not an object.");
        for (auto it = entry.at("goggles").begin(); it != entry.at("goggles").end(); ++it)
        {
            if (!FindGoggles(it.key())) fail("it has an unknown goggle offset " + it.key() + ".");
            if (!Owns(*team, it.key())) other(it.key());
            const auto& v = it.value();
            if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number()) fail("goggle offset " + it.key() + " needs X, Y and Z.");
            Offset o;
            try { o = CheckOffset({v[0].get<double>(), v[1].get<double>(), v[2].get<double>()}); }
            catch (const std::exception& e) { fail(e.what()); }
            if (o.x || o.y || o.z) ++used;
        }
    }
    if (!used) fail("it changes nothing: every slot and model is stock.");
}

// The image slots of an entry: the pictures kept beside it.
inline std::vector<std::string> ImageSlots(const Json& entry)
{
    std::vector<std::string> out;
    if (entry.is_object() && entry.contains("slots") && entry.at("slots").is_object())
        for (const auto& slot : Slots)
            if (entry.at("slots").contains(slot.property) && entry.at("slots").at(slot.property).contains("image")) out.push_back(slot.property);
    return out;
}

namespace Detail
{
inline Json Recipe(const std::string& pattern, const std::vector<std::string>& colours, double scale, unsigned seed, double cell = 6, double contrast = 1)
{
    Json r = {{"pattern", pattern}, {"colours", colours}, {"scale", scale}, {"seed", seed}, {"contrast", contrast}};
    if (pattern == "digital") r["cell"] = cell;
    return r;
}
inline std::string BuiltinId(const Team& team, const std::string& name) { return std::string("builtin.") + team.id + "." + Workflow::EmitterLibrary::Slug(name); }
inline Json Builtin(const Team& team, const std::string& name, const std::string& category, const std::string& description, const Json& recipe)
{
    return {{"id", BuiltinId(team, name)}, {"team", team.id}, {"name", name}, {"category", category}, {"description", description},
            {"slots", {{team.body, {{"recipe", recipe}}}, {team.head, {{"recipe", recipe}}}}}};
}
} // namespace Detail

// The presets that ship with RE+: camouflage painted over the stock textures when
// applied, so nothing but the DLL is needed. Every look comes as a spy preset and a
// merc preset. Never written; hiding one only hides it.
inline const Json& Builtins()
{
    static const Json builtins = [] {
        using Detail::Builtin;
        using Detail::Recipe;
        struct Look { const char* name; const char* category; const char* description; Json recipe; };
        const std::vector<Look> looks = {
            {"Woodland", "Woodland & Jungle", "Classic four-colour woodland: khaki, green, brown and black patches. For forests and overgrown maps.",
             Recipe("blotch", {"#7d7a52", "#4c5a30", "#5c4530", "#1f1f1a"}, 70, 11)},
            {"Jungle Tiger Stripe", "Woodland & Jungle", "Tiger stripe in deep jungle greens: long dark brush strokes over olive.",
             Recipe("tiger", {"#55652d", "#2d3b1a", "#7a843f", "#141710"}, 60, 23)},
            {"Desert", "Desert", "Sand, tan and earth patches for dunes, dust and dry stone.", Recipe("blotch", {"#c4aa7c", "#9a7d52", "#e0d0aa", "#6d583c"}, 80, 31)},
            {"Arid Digital", "Desert", "Pixelated desert camouflage in sand and khaki squares.", Recipe("digital", {"#bfa678", "#d8c79f", "#8f7550", "#a68f68"}, 56, 37, 6)},
            {"Snow", "Snow & Arctic", "Arctic white with grey patches and a few dark flecks, for snowy and icy maps.",
             Recipe("blotch", {"#e6eaee", "#c0c8d0", "#8d969f", "#4e555c"}, 85, 41)},
            {"Arctic Digital", "Snow & Arctic", "Pixelated snow camouflage: white and pale blue-grey squares.", Recipe("digital", {"#f0f3f5", "#b9c3cc", "#dce2e8", "#7e8994"}, 60, 43, 5)},
            {"Urban Night", "Urban & Night", "Dark digital greys and charcoal for streets, rooftops and night missions.",
             Recipe("digital", {"#3a3f46", "#23262b", "#545b64", "#14161a"}, 60, 53, 8)},
            {"Midnight Blue", "Urban & Night", "Near-black navy digital camouflage that disappears in moonlit shadow.",
             Recipe("digital", {"#1c2433", "#101722", "#2e3a52", "#0a0d13"}, 56, 59, 7)},
            // Red vs Blue of version 1, one colour per preset so either team can take either.
            {"Team Red", "Fun", "Bold red team colours. Give the other team Team Blue and every fight reads at a glance.",
             Recipe("blotch", {"#a32020", "#701414", "#c84040"}, 90, 61)},
            {"Team Blue", "Fun", "Bold blue team colours. Give the other team Team Red and every fight reads at a glance.",
             Recipe("blotch", {"#2246a8", "#152d70", "#4068cc"}, 90, 67)},
            {"Hazard Stripes", "Fun", "Yellow and black warning stripes. Nobody will miss you.", Recipe("stripes", {"#e8c018", "#1a1a1a"}, 24, 71)},
            {"Solid Gold", "Fun", "Polished gold from head to toe, for the winners.", Recipe("solid", {"#d8aa3c"}, 64, 73, 6, 1.4)},
            {"Bubblegum", "Fun", "Pink, cyan and violet candy camouflage.", Recipe("blotch", {"#f08cc0", "#68d0e8", "#ffffff", "#b050c8"}, 70, 79)},
        };
        Json list = Json::array();
        for (const auto& team : Teams)
            for (const auto& look : looks) list.push_back(Builtin(team, look.name, look.category, look.description, look.recipe));
        // Models RE+ carries in the DLL (tools/models; the bundles in Reloaded.Editor.rc).
        list.push_back({{"id", "builtin.merc.hazmat_suit"}, {"team", "merc"}, {"name", "Hazmat Suit"}, {"category", "Models"},
                        {"description", "A yellow Level A hazmat suit with a wide visor, taped seams and black gloves and boots. "
                                        "Imported into the map when applied; the mercs keep their own animations."},
                        {"slots", Json::object()},
                        {"mesh", {{"name", "HazmatMerc"}, {"materials", {"HazmatSuit", "HazmatGear"}}}},
                        {"goggles", {{"MercGoggleOffset", {-0.1, -2.8, -0.8}}}}});
        for (auto& entry : list)
        {
            Validate(entry);
            entry["builtin"] = true;
            entry["readonly"] = true;
        }
        return list;
    }();
    return builtins;
}
// The built-in ids of version 1 (one preset for both teams), and the team presets
// that replace each: hiding an old one hides both of its parts.
inline std::vector<std::string> ReplacedBuiltin(const std::string& id)
{
    if (!IsBuiltinId(id) || id.find('.', 8) != std::string::npos) return {};
    const auto slug = id.substr(8);
    if (slug == "red_vs_blue") return {"builtin.spy.team_red", "builtin.merc.team_blue"};
    return {"builtin.spy." + slug, "builtin.merc." + slug};
}

inline Json EmptyDocument() { return {{"version", FileVersion}, {"presets", Json::array()}, {"hiddenBuiltins", Json::array()}, {"imports", Json::object()}}; }
inline const char* DamagedFile()
{
    return "The skin preset file (skin_presets.json) is damaged or from a newer version, so your presets are not listed. Restore a valid copy or move it aside before saving presets.";
}

namespace Detail
{
// A user id made from another, so migrating the same file twice gives the same ids.
inline std::string DerivedId(const std::string& id, const std::string& salt)
{
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (std::uint64_t seed : {1469598103934665603ull, 1099511628211ull * 7919})
    {
        std::uint64_t h = seed;
        for (unsigned char c : id + ":" + salt) h = (h ^ c) * 1099511628211ull;
        for (int shift = 60; shift >= 0; shift -= 4) out += hex[(h >> shift) & 15];
    }
    return out;
}
// The part of a version 1 entry (both teams) that belongs to one team; null when that
// team keeps the stock look.
inline Json TeamPart(const Json& entry, const Team& team)
{
    Json part = entry;
    part["team"] = team.id;
    for (const char* key : {"slots", "models", "goggles"})
    {
        if (!entry.contains(key) || !entry.at(key).is_object()) continue;
        Json kept = Json::object();
        for (auto it = entry.at(key).begin(); it != entry.at(key).end(); ++it)
            if (Owns(team, it.key()) && !(std::string(key) == "models" && it.value() == Json(""))) kept[it.key()] = it.value();
        part[key] = kept;
    }
    if (part.contains("models") && part.at("models").empty()) part.erase("models");
    if (part.contains("goggles") && part.at("goggles").empty()) part.erase("goggles");
    bool any = part.contains("slots") && part.at("slots").is_object() && !part.at("slots").empty();
    if (part.contains("models"))
        for (const auto& value : part.at("models"))
            any = any || (value.is_string() && !value.get<std::string>().empty());
    if (part.contains("goggles"))
        for (const auto& value : part.at("goggles"))
            any = any || (value.is_array() && std::any_of(value.begin(), value.end(), [](const Json& v) { return !v.is_number() || v.get<double>() != 0; }));
    return any ? part : Json();
}
} // namespace Detail

// A picture to copy when a version 1 entry is split: the merc part gets its own id,
// so its pictures move to its own folder (the spy part keeps the old id and folder).
struct PictureCopy { std::string from, to, property; };
struct Migration
{
    Json document;
    std::vector<PictureCopy> copies;
    bool changed = false;
};
// Brings a version 1 file to version 2: each entry that dressed both teams becomes a
// spy entry and a merc entry with the same name, category, description and date,
// a hidden version 1 built-in hides both of its team presets. Entries already with
// a team, and entries too damaged to split, are kept as they are. A version 2 file
// comes back unchanged.
inline Migration Migrate(Json document)
{
    Migration result;
    if (!document.is_object() || document.value("version", 0) != 1)
    {
        result.document = std::move(document);
        return result;
    }
    result.changed = true;
    Json presets = Json::array();
    std::set<std::string> ids;
    if (document.contains("presets") && document.at("presets").is_array())
        for (const auto& entry : document.at("presets"))
            if (entry.is_object() && entry.contains("id") && entry.at("id").is_string()) ids.insert(entry.at("id").get<std::string>());
    if (document.contains("presets") && document.at("presets").is_array())
        for (const auto& entry : document.at("presets"))
        {
            const bool splittable = entry.is_object() && !entry.contains("team") && entry.contains("id") && entry.at("id").is_string() &&
                                    IsUserId(entry.at("id").get<std::string>());
            if (!splittable)
            {
                presets.push_back(entry);
                continue;
            }
            const auto id = entry.at("id").get<std::string>();
            const auto spy = Detail::TeamPart(entry, Teams[0]), merc = Detail::TeamPart(entry, Teams[1]);
            if (spy.is_null() && merc.is_null())
            {
                presets.push_back(entry); // nothing to split: kept, and reported as it was
                continue;
            }
            if (!spy.is_null()) presets.push_back(spy);
            if (!merc.is_null())
            {
                auto part = merc;
                if (!spy.is_null())
                {
                    std::string mercId = Detail::DerivedId(id, "merc");
                    for (int n = 2; ids.count(mercId); ++n) mercId = Detail::DerivedId(id, "merc" + std::to_string(n));
                    ids.insert(mercId);
                    part["id"] = mercId;
                    for (const auto& slot : Slots)
                        if (part.at("slots").contains(slot.property) && part.at("slots").at(slot.property).is_object() &&
                            part.at("slots").at(slot.property).contains("image"))
                            result.copies.push_back({id, mercId, slot.property});
                }
                presets.push_back(part);
            }
        }
    Json hidden = Json::array();
    std::set<std::string> seen;
    if (document.contains("hiddenBuiltins") && document.at("hiddenBuiltins").is_array())
        for (const auto& id : document.at("hiddenBuiltins"))
        {
            if (!id.is_string()) continue;
            auto replaced = ReplacedBuiltin(id.get<std::string>());
            if (replaced.empty()) replaced.push_back(id.get<std::string>());
            for (const auto& r : replaced)
                if (seen.insert(r).second) hidden.push_back(r);
        }
    result.document = document;
    result.document["version"] = FileVersion;
    result.document["presets"] = presets;
    result.document["hiddenBuiltins"] = hidden;
    if (!result.document.contains("imports")) result.document["imports"] = Json::object();
    return result;
}

// The user file checked, a version 1 file migrated in memory (the editor writes it back
// and copies its pictures: see Migrate).
inline Json Document(Json document)
{
    if (document.is_object() && document.value("version", 0) == 1) document = Migrate(std::move(document)).document;
    if (!document.is_object() || document.value("version", 0) != FileVersion) throw std::runtime_error(DamagedFile());
    for (const char* key : {"presets", "hiddenBuiltins"})
        if (!document.contains(key)) document[key] = Json::array();
    if (!document.contains("imports")) document["imports"] = Json::object();
    if (!document.at("presets").is_array() || !document.at("hiddenBuiltins").is_array() || !document.at("imports").is_object()) throw std::runtime_error(DamagedFile());
    for (const auto& id : document.at("hiddenBuiltins"))
        if (!id.is_string()) throw std::runtime_error(DamagedFile());
    return document;
}
namespace Detail
{
inline void Unflag(Json& entry)
{
    if (entry.is_object())
        for (const char* key : {"builtin", "readonly"}) entry.erase(key);
}
inline Json::iterator FindUser(Json& list, const std::string& id)
{
    return std::find_if(list.begin(), list.end(), [&](const Json& e) { return e.is_object() && e.value("id", std::string{}) == id; });
}
} // namespace Detail
// Built-ins not hidden, then the valid user entries in file order.
inline Json Merge(const Json& builtins, const Json& file)
{
    const auto document = Document(file);
    std::set<std::string> hidden, ids;
    for (const auto& id : document.at("hiddenBuiltins")) hidden.insert(id.get<std::string>());
    Json result = Json::array();
    for (auto entry : builtins)
    {
        const auto id = entry.at("id").get<std::string>();
        if (hidden.count(id) || !ids.insert(id).second) continue;
        entry["builtin"] = true;
        entry["readonly"] = true;
        result.push_back(std::move(entry));
    }
    for (auto entry : document.at("presets"))
    {
        try
        {
            Detail::Unflag(entry);
            Validate(entry);
        }
        catch (const std::exception&)
        {
            continue;
        }
        const auto id = entry.at("id").get<std::string>();
        if (!IsUserId(id) || !ids.insert(id).second) continue;
        entry["builtin"] = false;
        entry["readonly"] = false;
        result.push_back(std::move(entry));
    }
    return result;
}
// One sentence per user entry Merge leaves out.
inline Json Problems(const Json& file)
{
    const auto document = Document(file);
    Json result = Json::array();
    std::set<std::string> ids;
    for (auto entry : document.at("presets"))
    {
        try
        {
            Detail::Unflag(entry);
            Validate(entry);
            const auto id = entry.at("id").get<std::string>();
            if (!IsUserId(id)) throw std::runtime_error("Skin preset '" + entry.at("name").get<std::string>() + "' has a built-in id in the user file.");
            if (!ids.insert(id).second) throw std::runtime_error("Skin preset '" + entry.at("name").get<std::string>() + "' repeats the id of an earlier preset.");
        }
        catch (const std::exception& e)
        {
            result.push_back(e.what());
        }
    }
    return result;
}
inline const Json& Find(const Json& entries, const std::string& id)
{
    for (const auto& entry : entries)
        if (entry.value("id", std::string{}) == id) return entry;
    throw std::runtime_error("The selected skin preset no longer exists. Refresh the list.");
}

// A new user entry for one team from the Character Skins window's values: slots holds
// each slot's material path, models each model path, goggles each [x,y,z]; the other
// team's values are ignored. local names the slots whose material is stored inside
// the map: they become images, which the caller writes. Returns the unsaved entry (no id).
inline Json Capture(const Team& team, const Json& slots, const Json& models, const Json& goggles, const std::set<std::string>& local)
{
    Json entry = {{"team", team.id}, {"name", "New preset"}, {"category", "Other"}, {"description", ""}, {"slots", Json::object()}, {"models", Json::object()}, {"goggles", Json::object()}};
    bool any = false;
    for (const auto& slot : Slots)
    {
        if (!Owns(team, slot.property)) continue;
        const auto path = slots.is_object() ? slots.value(slot.property, std::string{}) : std::string{};
        if (path.empty()) continue;
        if (!ValidPath(path)) throw std::runtime_error(std::string(slot.label) + ": not a material path: " + path);
        entry["slots"][slot.property] = local.count(slot.property) ? Json{{"image", ImageFile(slot.property)}} : Json{{"path", path}};
        any = true;
    }
    for (const auto& model : Models)
    {
        if (!Owns(team, model.property)) continue;
        const auto path = models.is_object() ? models.value(model.property, std::string{}) : std::string{};
        if (!path.empty())
        {
            if (!ValidPath(path)) throw std::runtime_error(std::string(model.label) + ": not a model path: " + path);
            entry["models"][model.property] = path;
            any = true;
        }
        if (goggles.is_object() && goggles.contains(model.goggles))
        {
            const auto& v = goggles.at(model.goggles);
            if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number())
                throw std::runtime_error(std::string(model.label) + ": the goggle light offset needs numbers for X, Y and Z.");
            const Offset o = CheckOffset({v[0].get<double>(), v[1].get<double>(), v[2].get<double>()});
            if (o.x || o.y || o.z)
            {
                entry["goggles"][model.goggles] = Json::array({o.x, o.y, o.z});
                any = true;
            }
        }
    }
    if (!any)
        throw std::runtime_error(std::string(team.plural) + " wear the stock look, so there is nothing to save. Fill in a " + team.id + " slot or apply a " + team.id + " preset first.");
    return entry;
}

// The Character Skins values {slots, models, goggles} with one team dressed by entry
// and the other team's values left exactly as they were. paths holds the material
// path each of the entry's slots was imported or found under; a slot the entry does
// not hold, and a model it does not name, go back to stock. mesh is the path the
// entry's own model was imported under, when it carries one.
inline Json Dress(const Json& entry, Json settings, const std::map<std::string, std::string>& paths, const std::string& mesh = {})
{
    const auto& team = TeamOf(entry);
    for (const char* key : {"slots", "models", "goggles"})
        if (!settings.contains(key) || !settings.at(key).is_object()) settings[key] = Json::object();
    const auto slots = entry.value("slots", Json::object()), models = entry.value("models", Json::object()), goggles = entry.value("goggles", Json::object());
    for (const char* property : {team.body, team.head})
    {
        std::string path;
        if (slots.contains(property))
        {
            auto found = paths.find(property);
            if (found == paths.end() || found->second.empty()) throw std::runtime_error(std::string("The preset's ") + property + " has no material to apply.");
            path = found->second;
        }
        settings["slots"][property] = path;
    }
    if (HasMesh(entry))
    {
        if (mesh.empty()) throw std::runtime_error("The preset's model was not imported.");
        settings["models"][team.model] = mesh;
    }
    else
        settings["models"][team.model] = models.value(team.model, std::string{});
    settings["goggles"][team.goggles] = goggles.contains(team.goggles) ? goggles.at(team.goggles) : Json::array({0, 0, 0});
    return settings;
}

// The last part of an object path: the name a texture was imported under.
inline std::string ObjectName(const std::string& path)
{
    const auto dot = path.rfind('.');
    return dot == std::string::npos ? path : path.substr(dot + 1);
}
// What a team wears on the map, in words: the name of the listed preset its values
// match, "the stock look", or "its own skins". settings is the map's Character Skins
// values; imports maps the texture names preset pictures were imported under to the
// preset ids (the file's "imports"). Goggle offsets are not compared.
inline std::string Wearing(const Team& team, const Json& settings, const Json& entries, const Json& imports)
{
    auto value = [&](const char* key, const char* property) {
        return settings.is_object() && settings.contains(key) && settings.at(key).is_object() ? settings.at(key).value(property, std::string{}) : std::string{};
    };
    const std::string body = value("slots", team.body), head = value("slots", team.head), model = value("models", team.model);
    const ModelSlot* stock = FindModel(team.model);
    const bool stockModel = model.empty() || (stock && Fold(model) == Fold(stock->stockMesh));
    if (body.empty() && head.empty() && stockModel) return "the stock look";
    if (entries.is_array())
        for (const auto& entry : entries)
        {
            if (!entry.is_object() || entry.value("team", std::string{}) != team.id) continue;
            const auto slots = entry.value("slots", Json::object());
            auto matches = [&](const char* property, const std::string& path) {
                if (!slots.contains(property)) return path.empty();
                if (path.empty()) return false;
                const auto& slot = slots.at(property);
                if (slot.contains("path")) return Fold(slot.at("path").get<std::string>()) == Fold(path);
                const auto name = ObjectName(path);
                return imports.is_object() && imports.contains(name) && imports.at(name) == entry.at("id");
            };
            const auto presetModel = entry.value("models", Json::object()).value(team.model, std::string{});
            bool modelMatches = presetModel.empty() ? stockModel : Fold(presetModel) == Fold(model);
            if (HasMesh(entry))
            {
                const auto name = ObjectName(model);
                modelMatches = !stockModel && imports.is_object() && imports.contains(name) && imports.at(name) == entry.at("id");
            }
            if (matches(team.body, body) && matches(team.head, head) && modelMatches) return entry.at("name").get<std::string>();
        }
    return "their own skins";
}

// Adds entry with a fresh user id (or replaces the user entry with its id), after
// cleaning its texts. Returns the entry as stored.
inline Json Save(Json& file, Json entry)
{
    file = Document(std::move(file));
    if (!entry.is_object()) throw std::runtime_error("Nothing to save.");
    Detail::Unflag(entry);
    auto& list = file["presets"];
    entry["name"] = Workflow::EmitterLibrary::CleanName(entry.value("name", std::string{}));
    entry["category"] = CleanCategory(entry.value("category", std::string("Other")), list);
    entry["description"] = Workflow::EmitterLibrary::CleanDescription(entry.value("description", std::string{}));
    const auto id = entry.contains("id") && entry.at("id").is_string() ? entry.at("id").get<std::string>() : std::string{};
    auto existing = IsUserId(id) ? Detail::FindUser(list, id) : list.end();
    if (!IsUserId(id)) entry["id"] = Workflow::Id();
    entry["modified"] = Workflow::Timestamp();
    Validate(entry);
    if (existing != list.end()) *existing = entry;
    else list.push_back(entry);
    return entry;
}
// Name, category and description of a user entry.
inline Json Update(Json& file, const std::string& id, const Json& changes)
{
    file = Document(std::move(file));
    if (IsBuiltinId(id)) throw std::runtime_error("Built-in presets never change. Apply one and save it as your own preset to keep a changed copy.");
    if (!changes.is_object()) throw std::runtime_error("Choose what to change.");
    auto& list = file["presets"];
    auto existing = Detail::FindUser(list, id);
    if (existing == list.end()) throw std::runtime_error("The selected skin preset no longer exists. Refresh the list.");
    Json entry = *existing;
    for (auto it = changes.begin(); it != changes.end(); ++it)
    {
        if (!it.value().is_string()) throw std::runtime_error("Names, categories and descriptions are text.");
        const auto value = it.value().get<std::string>();
        if (it.key() == "name") entry["name"] = Workflow::EmitterLibrary::CleanName(value);
        else if (it.key() == "category") entry["category"] = CleanCategory(value, list);
        else if (it.key() == "description") entry["description"] = Workflow::EmitterLibrary::CleanDescription(value);
        else throw std::runtime_error("Only a preset's name, category and description can be changed.");
    }
    entry["modified"] = Workflow::Timestamp();
    Validate(entry);
    *existing = entry;
    return entry;
}
// A user entry is removed (true: its pictures may go too); a built-in is hidden.
inline bool Delete(Json& file, const Json& builtins, const std::string& id)
{
    file = Document(std::move(file));
    auto& list = file["presets"];
    auto existing = Detail::FindUser(list, id);
    if (IsUserId(id) && existing != list.end())
    {
        list.erase(existing);
        return true;
    }
    if (IsBuiltinId(id) && std::any_of(builtins.begin(), builtins.end(), [&](const Json& e) { return e.at("id") == id; }))
    {
        auto& hidden = file["hiddenBuiltins"];
        if (std::find(hidden.begin(), hidden.end(), Json(id)) == hidden.end()) hidden.push_back(id);
        return false;
    }
    throw std::runtime_error("The selected skin preset no longer exists. Refresh the list.");
}
inline void RestoreBuiltins(Json& file)
{
    file = Document(std::move(file));
    file["hiddenBuiltins"] = Json::array();
}

// A preset file to share: the entry without its identity, with its pictures inside.
// images holds the TGA bytes of each image slot.
inline Json ShareDocument(const Json& entry, const std::map<std::string, Bytes>& images, const std::map<std::string, Bytes>& files = {})
{
    Json preset = entry;
    Detail::Unflag(preset);
    preset.erase("id");
    preset.erase("modified");
    Json pictures = Json::object();
    for (const auto& property : ImageSlots(entry))
    {
        auto found = images.find(property);
        if (found == images.end()) throw std::runtime_error("The picture for " + property + " is missing from this preset.");
        ReadTga(found->second);
        pictures[property] = Base64(found->second);
    }
    Json document = {{"version", ShareVersion}, {"format", ShareFormat}, {"preset", preset}, {"images", pictures}};
    if (HasMesh(entry))
    {
        Json carried = Json::object();
        for (const auto& name : MeshFiles(entry))
        {
            auto found = files.find(name);
            if (found == files.end()) throw std::runtime_error("The model file " + name + " is missing from this preset.");
            carried[name] = Base64(found->second);
        }
        document["files"] = carried;
    }
    return document;
}
// A shared preset file read back: the entries (no id yet, user entries once saved)
// and their pictures, every one checked. A version 2 file holds one team's preset;
// a version 1 file (both teams) gives a spy entry and a merc entry. Recipe slots (a
// built-in shared as is) are painted by the caller and turned into images before saving.
struct Shared { Json entry; std::map<std::string, Bytes> images, files; };
inline std::vector<Shared> ReadShared(const Json& document)
{
    if (!document.is_object() || document.value("format", std::string{}) != ShareFormat)
        throw std::runtime_error("This is not an RE+ skin preset file.");
    const int version = document.value("version", 0);
    if (version != 1 && version != ShareVersion) throw std::runtime_error("This skin preset file is from a newer version of RE+.");
    if (!document.contains("preset") || !document.at("preset").is_object()) throw std::runtime_error("The skin preset file has no preset.");
    Json preset = document.at("preset");
    Detail::Unflag(preset);
    preset.erase("modified");
    std::vector<Json> entries;
    if (version == 1)
    {
        preset.erase("team");
        for (const auto& team : Teams)
            if (auto part = Detail::TeamPart(preset, team); !part.is_null()) entries.push_back(part);
        if (entries.empty()) throw std::runtime_error("The skin preset file's preset changes nothing.");
    }
    else
        entries.push_back(preset);
    const Json images = document.value("images", Json::object());
    if (!images.is_object()) throw std::runtime_error("The skin preset file's pictures are damaged.");
    std::vector<Shared> result;
    for (auto& entry : entries)
    {
        Shared shared;
        shared.entry = entry;
        for (const auto& property : ImageSlots(shared.entry))
        {
            if (!images.contains(property) || !images.at(property).is_string()) throw std::runtime_error("The skin preset file has no picture for " + property + ".");
            auto bytes = FromBase64(images.at(property).get<std::string>());
            ReadTga(bytes);
            shared.images[property] = std::move(bytes);
        }
        if (HasMesh(shared.entry) && shared.entry.at("mesh").is_object() && shared.entry.at("mesh").contains("name") &&
            shared.entry.at("mesh").at("name").is_string() && shared.entry.at("mesh").contains("materials") && shared.entry.at("mesh").at("materials").is_array())
        {
            const Json files = document.value("files", Json::object());
            for (const auto& name : MeshFiles(shared.entry))
            {
                if (!files.is_object() || !files.contains(name) || !files.at(name).is_string()) throw std::runtime_error("The skin preset file has no " + name + ".");
                shared.files[name] = FromBase64(files.at(name).get<std::string>());
            }
            const auto materials = PskMaterials(shared.files.at(MeshFile(shared.entry)));
            for (const auto& material : materials)
                if (!shared.files.count(MaterialFile(material))) throw std::runtime_error("The skin preset file's model has no picture for its material " + material + ".");
            for (const auto& material : shared.entry.at("mesh").at("materials")) ReadTga(shared.files.at(MaterialFile(material.get<std::string>())));
        }
        // Checked as a user entry would be, under a placeholder id.
        auto probe = shared.entry;
        probe["id"] = std::string(32, '0');
        probe["modified"] = "0";
        if (probe.contains("slots") && probe.at("slots").is_object())
            for (auto& [key, value] : probe["slots"].items())
                if (value.is_object() && value.contains("recipe")) value = {{"image", ImageFile(key)}};
        Validate(probe);
        result.push_back(std::move(shared));
    }
    return result;
}
// Remembers which preset each imported texture name came from (see Wearing).
inline void RecordImports(Json& file, const std::map<std::string, std::string>& names)
{
    file = Document(std::move(file));
    for (const auto& [name, id] : names) file["imports"][name] = id;
}
// A file name for a shared preset: the name with characters Windows refuses dropped.
inline std::string ShareFileName(const std::string& name)
{
    std::string out;
    for (unsigned char c : name)
        if (c >= 32 && !std::strchr("<>:\"/\\|?*", c)) out += static_cast<char>(c);
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    if (out.empty()) out = "Skin preset";
    return out + ShareExtension;
}

inline std::string PatternLabel(const std::string& pattern)
{
    if (pattern == "blotch") return "camouflage patches";
    if (pattern == "digital") return "digital camouflage";
    if (pattern == "tiger") return "tiger stripe";
    if (pattern == "stripes") return "stripes";
    if (pattern == "solid") return "solid colour";
    return pattern;
}
// A short summary for the browser's details box: the entry's team values only.
inline std::string Summary(const Json& entry)
{
    std::string out;
    const auto* team = entry.is_object() ? FindTeam(entry.value("team", std::string{})) : nullptr;
    for (const auto& slot : Slots)
    {
        if ((team && !Owns(*team, slot.property)) || HasMesh(entry)) continue; // a model wears its own textures
        out += std::string(slot.label) + ": ";
        const auto slots = entry.value("slots", Json::object());
        if (!slots.contains(slot.property)) out += "stock";
        else if (slots.at(slot.property).contains("recipe")) out += PatternLabel(slots.at(slot.property).at("recipe").value("pattern", std::string{}));
        else if (slots.at(slot.property).contains("image")) out += "own picture";
        else out += slots.at(slot.property).value("path", std::string{});
        out += "\r\n";
    }
    if (HasMesh(entry) && team)
    {
        const auto& mesh = entry.at("mesh");
        out += std::string(FindModel(team->model)->label) + ": its own, " + mesh.value("name", std::string{}) + " (" +
               std::to_string(mesh.value("materials", Json::array()).size()) + " textures), imported into the map when applied\r\n";
    }
    for (const auto& model : Models)
    {
        const auto models = entry.value("models", Json::object());
        const auto path = models.value(model.property, std::string{});
        const auto goggles = entry.value("goggles", Json::object());
        if (path.empty() && !goggles.contains(model.goggles)) continue;
        if (HasMesh(entry) && team && model.property == std::string(team->model)) out += "Goggle lights";
        else out += std::string(model.label) + ": " + (path.empty() ? std::string("stock") : path);
        if (goggles.contains(model.goggles))
        {
            const auto& g = goggles.at(model.goggles);
            out += (HasMesh(entry) && team && model.property == std::string(team->model) ? ": " : " (goggle lights ") + Number(g[0].get<double>()) + ", " +
                   Number(g[1].get<double>()) + ", " + Number(g[2].get<double>()) + (HasMesh(entry) && team && model.property == std::string(team->model) ? "" : ")");
        }
        out += "\r\n";
    }
    return out;
}
} // namespace CharacterSkins::Presets
