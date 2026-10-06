#pragma once
#include "MapPackage.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Optimise Map Assets: a map that uses a few meshes or textures from a big asset
// pack needs the whole pack shipped with it. The editor finds what the map uses
// (MapOptimiseNative.inl); this works out, from the packs' own tables, how much
// of each pack that is, and plans moving just those assets into the map (or into
// a small package of its own) for a release copy.
namespace MapOptimise
{
inline std::string Fold(std::string s)
{
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    return s;
}
inline std::string RootPackage(const std::string &path)
{
    return path.substr(0, path.find('.'));
}
// An asset the map uses, as the editor names it: Package.Group.Name.
struct Asset
{
    std::string path, className;
};
struct UsedAsset
{
    std::string path, className;
    std::uint64_t size = 0;
    bool found = false; // Listed in the pack's file.
};
struct Pack
{
    std::string name, file; // file: empty when no package file was found
    std::uint64_t fileSize = 0, usedSize = 0;
    std::vector<UsedAsset> assets;
    bool suggested = false;
    bool installed = false; // The same file is in the base install: players have it.
};

// An asset's size is its own export and everything inside it (a mesh's
// collision model, a material's modifiers), as stored in the pack.
class Sizes
{
    std::map<std::string, std::uint64_t> exports; // folded path -> size
  public:
    explicit Sizes(const MapPackage::Tables &tables)
    {
        for (const auto &e : tables.exports)
            exports[Fold(e.path)] += e.size;
    }
    std::optional<std::uint64_t> Of(const std::string &path) const
    {
        const auto key = Fold(path);
        auto it = exports.find(key);
        if (it == exports.end())
            return std::nullopt;
        std::uint64_t total = it->second;
        const auto prefix = key + ".";
        for (++it; it != exports.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
            total += it->second;
        return total;
    }
};

// Keeps assets that are not inside another listed asset: moving the owner
// moves what is inside it.
inline std::vector<Asset> TopLevel(std::vector<Asset> assets)
{
    std::sort(assets.begin(), assets.end(), [](const Asset &a, const Asset &b) { return Fold(a.path) < Fold(b.path); });
    std::vector<Asset> out;
    for (const auto &a : assets)
    {
        if (!out.empty())
        {
            const auto last = Fold(out.back().path), key = Fold(a.path);
            if (key == last || key.compare(0, last.size() + 1, last + ".") == 0)
                continue;
        }
        out.push_back(a);
    }
    return out;
}

// One row per pack the map uses, biggest saving first. tables gives a pack's
// file and tables, or nothing when no file was found. A pack is suggested when
// the map uses less than half of it.
inline std::vector<Pack> Report(const std::vector<Asset> &used,
                                const std::function<std::optional<std::pair<std::string, MapPackage::Tables>>(
                                    const std::string &package)> &tables)
{
    std::map<std::string, std::vector<Asset>> byPack;
    for (const auto &a : TopLevel(used))
        byPack[RootPackage(a.path)].push_back(a);
    std::vector<Pack> packs;
    for (const auto &[name, assets] : byPack)
    {
        Pack pack;
        pack.name = name;
        auto found = tables(name);
        std::optional<Sizes> sizes;
        if (found)
        {
            pack.file = found->first;
            pack.fileSize = found->second.fileSize;
            sizes.emplace(found->second);
        }
        for (const auto &a : assets)
        {
            UsedAsset u{a.path, a.className};
            if (sizes)
                if (auto size = sizes->Of(a.path))
                {
                    u.size = *size;
                    u.found = true;
                }
            pack.usedSize += u.size;
            pack.assets.push_back(std::move(u));
        }
        pack.suggested = found && pack.fileSize > 0 && pack.usedSize * 2 < pack.fileSize;
        packs.push_back(std::move(pack));
    }
    std::stable_sort(packs.begin(), packs.end(), [](const Pack &a, const Pack &b) {
        const auto sa = a.fileSize > a.usedSize ? a.fileSize - a.usedSize : 0;
        const auto sb = b.fileSize > b.usedSize ? b.fileSize - b.usedSize : 0;
        return sa > sb;
    });
    return packs;
}

// Packs players already have (the same file is in the base install) are never
// shipped, so moving their assets saves nothing and only makes the map bigger:
// they are not suggested, and go last.
inline void MarkInstalled(std::vector<Pack> &packs, const std::function<bool(const Pack &)> &installed)
{
    for (auto &p : packs)
        if (!p.file.empty() && installed(p))
        {
            p.installed = true;
            p.suggested = false;
        }
    std::stable_partition(packs.begin(), packs.end(), [](const Pack &p) { return !p.installed; });
}

inline std::string Size(std::uint64_t bytes)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    if (bytes >= 1024ull * 1024)
        out.precision(1), out << bytes / (1024.0 * 1024.0) << " MB";
    else if (bytes >= 1024)
        out << (bytes + 1023) / 1024 << " KB";
    else
        out << bytes << " bytes";
    return out.str();
}

// A package or map name: letters, digits and underscores, as package file names
// and object paths need.
inline bool ValidName(const std::string &name)
{
    if (name.empty() || name.size() > 48)
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

// Moving Pack.Group.Name into a destination package keeps where it came from as
// groups (Destination.Pack.Group.Name), so names cannot clash and the browsers
// still show the pack it came from.
struct Move
{
    std::string path, oldPackage, oldGroup, name, newPackage, newGroup;
    std::string NewPath() const
    {
        return newPackage + "." + newGroup + "." + name;
    }
};
inline Move PlanMove(const std::string &path, const std::string &destination)
{
    const auto first = path.find('.'), last = path.rfind('.');
    if (first == std::string::npos || first == 0 || last + 1 >= path.size())
        throw std::runtime_error("Not an asset path: " + path);
    if (!ValidName(destination))
        throw std::runtime_error("Not a package name: " + destination);
    for (char c : path)
        if (c == '"' || c == ' ' || c == '\r' || c == '\n')
            throw std::runtime_error("An asset name the editor cannot move: " + path);
    Move m;
    m.path = path;
    m.oldPackage = path.substr(0, first);
    m.oldGroup = last > first ? path.substr(first + 1, last - first - 1) : std::string();
    m.name = path.substr(last + 1);
    m.newPackage = destination;
    m.newGroup = m.oldPackage + (m.oldGroup.empty() ? "" : "." + m.oldGroup);
    return m;
}
// The stock rename command (the Texture Browser's Rename uses it): it makes the
// destination package and groups as needed.
inline std::string RenameCommand(const Move &m)
{
    return "OBJ RENAME OLDNAME=\"" + m.name + "\" OLDGROUP=\"" + m.oldGroup + "\" OLDPACKAGE=\"" + m.oldPackage +
           "\" NEWNAME=\"" + m.name + "\" NEWGROUP=\"" + m.newGroup + "\" NEWPACKAGE=\"" + m.newPackage + "\"";
}

// Objects a saved file still takes from the moved packs: imports whose root is
// one of them, packages (the packs and groups themselves) and classes left out.
inline std::vector<std::string> StillImported(const MapPackage::Tables &saved, const std::vector<std::string> &packs)
{
    std::vector<std::string> out;
    for (const auto &i : saved.imports)
    {
        if (Fold(i.className) == "package" || Fold(i.className) == "class")
            continue;
        const auto root = Fold(RootPackage(i.path));
        if (std::any_of(packs.begin(), packs.end(), [&](const std::string &p) { return Fold(p) == root; }))
            out.push_back(i.path);
    }
    return out;
}
// Every package a saved file imports from, except the game's own.
inline std::vector<std::string> ImportedPackages(const MapPackage::Tables &saved)
{
    std::vector<std::string> out;
    for (const auto &i : saved.imports)
    {
        if (i.path.find('.') != std::string::npos)
            continue;
        if (MapPackage::RuntimePackage(i.path) || Fold(i.path) == Fold(saved.package))
            continue;
        if (std::none_of(out.begin(), out.end(), [&](const std::string &p) { return Fold(p) == Fold(i.path); }))
            out.push_back(i.path);
    }
    return out;
}

inline std::string Text(const std::vector<Pack> &packs)
{
    std::ostringstream out;
    out << "Optimise Map Assets: what the map uses from each package\r\n\r\n";
    for (const auto &p : packs)
    {
        out << p.name << (p.file.empty() ? " (no package file found)" : " (" + p.file + ")") << ": "
            << p.assets.size() << " asset(s), " << Size(p.usedSize) << " of " << Size(p.fileSize)
            << (p.suggested ? "  [suggested]" : "") << (p.installed ? "  [in the base install]" : "") << "\r\n";
        for (const auto &a : p.assets)
            out << "  " << a.path << " (" << a.className << ", " << (a.found ? Size(a.size) : "size unknown") << ")\r\n";
        out << "\r\n";
    }
    out << "Assets loaded only by a script's string path are not found by the scan.\r\n";
    return out.str();
}
} // namespace MapOptimise
