#pragma once
// Character Skin presets: a named set of Character Skins (the four material slots,
// the two model swaps and their goggle light offsets) kept in a library and applied
// to any map through the Character Skins Import and Apply paths. Pure: no Windows,
// no engine, so the tests compile it alone.
//
// Entry, the contract with the browser and the editor side:
//   {"id": "builtin.<slug>" (compiled in, never written) | 32 lower-case hex (user),
//    "name", "category", "description",
//    "builtin": true|false, "readonly": true|false (in memory only, never written),
//    "modified": epoch milliseconds as text (user entries),
//    "slots": {"SpyBody"|"SpyHead"|"MercBody"|"MercHead":
//               {"recipe": {...}}  a camouflage made from the stock texture (Recolour)
//             | {"image": "<Slot>.tga"}  a picture kept beside the entry
//             | {"path": "Package.Group.Name"}  a material in a shared package},
//              a slot left out keeps the stock look,
//    "models": {"SpyModel"|"MercModel": "Package.Mesh"} (optional),
//    "goggles": {"SpyGoggleOffset"|"MercGoggleOffset": [x,y,z]} (optional)}
// User file, <Editor::Directory()>/skin_presets.json:
//   {"version":1,"presets":[user entries],"hiddenBuiltins":[built-in ids the user hid]}
// with each user entry's pictures in <Editor::Directory()>/skin_presets/<id>/<Slot>.tga.
// Shared preset file (*.skinpreset): {"version":1,"format":"RE+ skin preset",
//   "preset":{entry without id, modified and flags},"images":{"<Slot>":"<base64 TGA>"}}.
#include "CharacterSkinsImage.h"
#include "CharacterSkinsModel.h"
#include "EmitterLibraryModel.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
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

inline const std::vector<std::string>& DefaultCategories()
{
    static const std::vector<std::string> categories = {"Woodland & Jungle", "Desert", "Snow & Arctic", "Urban & Night", "Fun", "Other"};
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
    if (!entry.contains("slots") || !entry.at("slots").is_object()) fail("its slots are missing.");
    size_t used = 0;
    for (auto it = entry.at("slots").begin(); it != entry.at("slots").end(); ++it)
    {
        if (!FindSlot(it.key())) fail("it has an unknown slot " + it.key() + ".");
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
            if (!it.value().is_string() || !ValidPath(it.value().get<std::string>())) fail("model " + it.key() + " is not a mesh path.");
            if (!it.value().get<std::string>().empty()) ++used;
        }
    }
    if (entry.contains("goggles"))
    {
        if (!entry.at("goggles").is_object()) fail("its goggle light offsets are not an object.");
        for (auto it = entry.at("goggles").begin(); it != entry.at("goggles").end(); ++it)
        {
            if (!FindGoggles(it.key())) fail("it has an unknown goggle offset " + it.key() + ".");
            const auto& v = it.value();
            if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number()) fail("goggle offset " + it.key() + " needs X, Y and Z.");
            try { CheckOffset({v[0].get<double>(), v[1].get<double>(), v[2].get<double>()}); }
            catch (const std::exception& e) { fail(e.what()); }
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
inline Json Builtin(const std::string& name, const std::string& category, const std::string& description, const Json& spy, const Json& merc)
{
    return {{"id", Workflow::EmitterLibrary::BuiltinId(name)}, {"name", name}, {"category", category}, {"description", description},
            {"slots", {{"SpyBody", {{"recipe", spy}}}, {"SpyHead", {{"recipe", spy}}}, {"MercBody", {{"recipe", merc}}}, {"MercHead", {{"recipe", merc}}}}}};
}
} // namespace Detail

// The presets that ship with RE+: camouflage painted over the stock textures when
// applied, so nothing but the DLL is needed. Never written; hiding one only hides it.
inline const Json& Builtins()
{
    static const Json builtins = [] {
        using Detail::Builtin;
        using Detail::Recipe;
        Json list = Json::array();
        const auto woodland = Recipe("blotch", {"#7d7a52", "#4c5a30", "#5c4530", "#1f1f1a"}, 70, 11);
        list.push_back(Builtin("Woodland", "Woodland & Jungle", "Classic four-colour woodland: khaki, green, brown and black patches. For forests and overgrown maps.", woodland, woodland));
        const auto jungle = Recipe("tiger", {"#55652d", "#2d3b1a", "#7a843f", "#141710"}, 60, 23);
        list.push_back(Builtin("Jungle Tiger Stripe", "Woodland & Jungle", "Tiger stripe in deep jungle greens: long dark brush strokes over olive.", jungle, jungle));
        const auto desert = Recipe("blotch", {"#c4aa7c", "#9a7d52", "#e0d0aa", "#6d583c"}, 80, 31);
        list.push_back(Builtin("Desert", "Desert", "Sand, tan and earth patches for dunes, dust and dry stone.", desert, desert));
        const auto arid = Recipe("digital", {"#bfa678", "#d8c79f", "#8f7550", "#a68f68"}, 56, 37, 6);
        list.push_back(Builtin("Arid Digital", "Desert", "Pixelated desert camouflage in sand and khaki squares.", arid, arid));
        const auto snow = Recipe("blotch", {"#e6eaee", "#c0c8d0", "#8d969f", "#4e555c"}, 85, 41);
        list.push_back(Builtin("Snow", "Snow & Arctic", "Arctic white with grey patches and a few dark flecks, for snowy and icy maps.", snow, snow));
        const auto arctic = Recipe("digital", {"#f0f3f5", "#b9c3cc", "#dce2e8", "#7e8994"}, 60, 43, 5);
        list.push_back(Builtin("Arctic Digital", "Snow & Arctic", "Pixelated snow camouflage: white and pale blue-grey squares.", arctic, arctic));
        const auto urban = Recipe("digital", {"#3a3f46", "#23262b", "#545b64", "#14161a"}, 60, 53, 8);
        list.push_back(Builtin("Urban Night", "Urban & Night", "Dark digital greys and charcoal for streets, rooftops and night missions.", urban, urban));
        const auto blue = Recipe("digital", {"#1c2433", "#101722", "#2e3a52", "#0a0d13"}, 56, 59, 7);
        list.push_back(Builtin("Midnight Blue", "Urban & Night", "Near-black navy digital camouflage that disappears in moonlit shadow.", blue, blue));
        list.push_back(Builtin("Red vs Blue", "Fun", "Team colours: spies in red, mercs in blue, so every fight reads at a glance.",
                               Recipe("blotch", {"#a32020", "#701414", "#c84040"}, 90, 61), Recipe("blotch", {"#2246a8", "#152d70", "#4068cc"}, 90, 67)));
        const auto hazard = Recipe("stripes", {"#e8c018", "#1a1a1a"}, 24, 71);
        list.push_back(Builtin("Hazard Stripes", "Fun", "Yellow and black warning stripes. Nobody will miss you.", hazard, hazard));
        const auto gold = Recipe("solid", {"#d8aa3c"}, 64, 73, 6, 1.4);
        list.push_back(Builtin("Solid Gold", "Fun", "Polished gold from head to toe, for the winners.", gold, gold));
        const auto pink = Recipe("blotch", {"#f08cc0", "#68d0e8", "#ffffff", "#b050c8"}, 70, 79);
        list.push_back(Builtin("Bubblegum", "Fun", "Pink, cyan and violet candy camouflage.", pink, pink));
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

inline Json EmptyDocument() { return {{"version", 1}, {"presets", Json::array()}, {"hiddenBuiltins", Json::array()}}; }
inline const char* DamagedFile()
{
    return "The skin preset file (skin_presets.json) is damaged or from a newer version, so your presets are not listed. Restore a valid copy or move it aside before saving presets.";
}
inline Json Document(Json document)
{
    if (!document.is_object() || document.value("version", 0) != 1) throw std::runtime_error(DamagedFile());
    for (const char* key : {"presets", "hiddenBuiltins"})
        if (!document.contains(key)) document[key] = Json::array();
    if (!document.at("presets").is_array() || !document.at("hiddenBuiltins").is_array()) throw std::runtime_error(DamagedFile());
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

// A new user entry from the Character Skins window's values: slots holds each
// slot's material path, models each model path, goggles each [x,y,z]. local names
// the slots whose material is stored inside the map: they become images, which the
// caller writes. Returns the unsaved entry (no id).
inline Json Capture(const Json& slots, const Json& models, const Json& goggles, const std::set<std::string>& local)
{
    Json entry = {{"name", "New preset"}, {"category", "Other"}, {"description", ""}, {"slots", Json::object()}, {"models", Json::object()}, {"goggles", Json::object()}};
    bool any = false;
    for (const auto& slot : Slots)
    {
        const auto path = slots.is_object() ? slots.value(slot.property, std::string{}) : std::string{};
        if (path.empty()) continue;
        if (!ValidPath(path)) throw std::runtime_error(std::string(slot.label) + ": not a material path: " + path);
        entry["slots"][slot.property] = local.count(slot.property) ? Json{{"image", ImageFile(slot.property)}} : Json{{"path", path}};
        any = true;
    }
    for (const auto& model : Models)
    {
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
            if (o.x || o.y || o.z) entry["goggles"][model.goggles] = Json::array({o.x, o.y, o.z});
        }
    }
    if (!any) throw std::runtime_error("Every slot and model is stock, so there is nothing to save. Fill in a slot or apply a preset first.");
    return entry;
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
inline Json ShareDocument(const Json& entry, const std::map<std::string, Bytes>& images)
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
    return {{"version", 1}, {"format", ShareFormat}, {"preset", preset}, {"images", pictures}};
}
// A shared preset file read back: the entry (no id yet, a user entry once saved)
// and its pictures, every one checked. Recipe slots (a built-in shared as is) are
// painted by the caller and turned into images before saving.
struct Shared { Json entry; std::map<std::string, Bytes> images; };
inline Shared ReadShared(const Json& document)
{
    if (!document.is_object() || document.value("format", std::string{}) != ShareFormat)
        throw std::runtime_error("This is not an RE+ skin preset file.");
    if (document.value("version", 0) != 1) throw std::runtime_error("This skin preset file is from a newer version of RE+.");
    if (!document.contains("preset") || !document.at("preset").is_object()) throw std::runtime_error("The skin preset file has no preset.");
    Shared shared;
    shared.entry = document.at("preset");
    Detail::Unflag(shared.entry);
    shared.entry.erase("modified");
    const Json images = document.value("images", Json::object());
    if (!images.is_object()) throw std::runtime_error("The skin preset file's pictures are damaged.");
    for (const auto& property : ImageSlots(shared.entry))
    {
        if (!images.contains(property) || !images.at(property).is_string()) throw std::runtime_error("The skin preset file has no picture for " + property + ".");
        auto bytes = FromBase64(images.at(property).get<std::string>());
        ReadTga(bytes);
        shared.images[property] = std::move(bytes);
    }
    // Checked as a user entry would be, under a placeholder id.
    auto probe = shared.entry;
    probe["id"] = std::string(32, '0');
    probe["modified"] = "0";
    for (auto& [key, value] : probe["slots"].items())
        if (value.is_object() && value.contains("recipe")) value = {{"image", ImageFile(key)}};
    Validate(probe);
    return shared;
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
// A short summary for the browser's details box.
inline std::string Summary(const Json& entry)
{
    std::string out;
    for (const auto& slot : Slots)
    {
        out += std::string(slot.label) + ": ";
        const auto slots = entry.value("slots", Json::object());
        if (!slots.contains(slot.property)) out += "stock";
        else if (slots.at(slot.property).contains("recipe")) out += PatternLabel(slots.at(slot.property).at("recipe").value("pattern", std::string{}));
        else if (slots.at(slot.property).contains("image")) out += "own picture";
        else out += slots.at(slot.property).value("path", std::string{});
        out += "\r\n";
    }
    for (const auto& model : Models)
    {
        const auto models = entry.value("models", Json::object());
        const auto path = models.value(model.property, std::string{});
        if (path.empty()) continue;
        out += std::string(model.label) + ": " + path;
        const auto goggles = entry.value("goggles", Json::object());
        if (goggles.contains(model.goggles))
        {
            const auto& g = goggles.at(model.goggles);
            out += " (goggle lights " + Number(g[0].get<double>()) + ", " + Number(g[1].get<double>()) + ", " + Number(g[2].get<double>()) + ")";
        }
        out += "\r\n";
    }
    return out;
}
} // namespace CharacterSkins::Presets
