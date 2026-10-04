#pragma once
// The pure part of the Static Mesh and Texture Browsers' Favorites: a
// favourite's package, group and name, the package list with counts, and the
// rows shown for a package filter and sort order. No engine and no Windows, so
// the tests compile it alone.
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace Favorites
{
    // Favourites are stored as full object paths, Package[.Group...].Name.
    struct Parts
    {
        std::string package, group, name;
    };
    inline Parts Split(const std::string& path)
    {
        const size_t first = path.find('.'), last = path.rfind('.');
        if (first == std::string::npos) return {path, {}, path};
        return {path.substr(0, first), last > first ? path.substr(first + 1, last - first - 1) : std::string{}, path.substr(last + 1)};
    }

    inline std::string Lower(std::string s)
    {
        for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    }

    enum class Sort { Package, Name, Newest, Oldest };
    constexpr Sort kSorts[] = {Sort::Package, Sort::Name, Sort::Newest, Sort::Oldest};
    inline const char* SortLabel(Sort s)
    {
        switch (s)
        {
        case Sort::Package: return "Sort: Package";
        case Sort::Name: return "Sort: Name";
        case Sort::Newest: return "Sort: Newest first";
        case Sort::Oldest: return "Sort: Oldest first";
        }
        return "";
    }
    inline const char* SortKey(Sort s)
    {
        switch (s)
        {
        case Sort::Package: return "Package";
        case Sort::Name: return "Name";
        case Sort::Newest: return "Newest";
        case Sort::Oldest: return "Oldest";
        }
        return "";
    }
    inline Sort SortFromKey(const std::string& key)
    {
        for (Sort s : kSorts) if (Lower(key) == Lower(SortKey(s))) return s;
        return Sort::Package;
    }

    struct Favorite
    {
        std::string path;
        bool loaded = false; // the object is in memory and can be shown
    };

    struct Package
    {
        std::string name; // as first favourited; compared without case
        size_t count = 0, loaded = 0;
    };

    // Every package with favourites, in name order, with how many of each
    // are favourites and how many of those are loaded.
    inline std::vector<Package> Packages(const std::vector<Favorite>& favorites)
    {
        std::vector<Package> out;
        for (const auto& f : favorites)
        {
            const auto package = Split(f.path).package;
            auto it = std::find_if(out.begin(), out.end(), [&](const Package& p) { return Lower(p.name) == Lower(package); });
            if (it == out.end()) it = out.insert(out.end(), Package{package});
            ++it->count;
            if (f.loaded) ++it->loaded;
        }
        std::stable_sort(out.begin(), out.end(), [](const Package& a, const Package& b) { return Lower(a.name) < Lower(b.name); });
        return out;
    }

    inline std::string PackageLabel(const Package& p)
    {
        return p.name + " (" + std::to_string(p.count) + ")";
    }
    inline std::string AllLabel(size_t count)
    {
        return "All packages (" + std::to_string(count) + ")";
    }

    struct Row
    {
        size_t favorite = 0; // index into the favourites, which are in the order they were added
        std::string label;
    };

    // The rows for one package (empty: every package) in the given order. In
    // package order a row shows its full path, so each package's objects sit
    // together; by name it leads with the object's name. Favourites that are not
    // loaded say so, and come after the loaded ones.
    inline std::vector<Row> Rows(const std::vector<Favorite>& favorites, const std::string& package, Sort sort)
    {
        std::vector<size_t> order;
        for (size_t i = 0; i < favorites.size(); ++i)
            if (package.empty() || Lower(Split(favorites[i].path).package) == Lower(package)) order.push_back(i);
        auto key = [&](size_t i, bool byName) {
            const auto p = Split(favorites[i].path);
            return byName ? Lower(p.name) + '\x01' + Lower(p.package) + '\x01' + Lower(p.group)
                          : Lower(p.package) + '\x01' + Lower(p.group) + '\x01' + Lower(p.name);
        };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (favorites[a].loaded != favorites[b].loaded) return favorites[a].loaded;
            switch (sort)
            {
            case Sort::Package: return key(a, false) < key(b, false);
            case Sort::Name: return key(a, true) < key(b, true);
            case Sort::Newest: return a > b;
            case Sort::Oldest: return a < b;
            }
            return false;
        });
        std::vector<Row> rows;
        for (size_t i : order)
        {
            const auto p = Split(favorites[i].path);
            std::string label = favorites[i].path;
            if (sort == Sort::Name && p.package != favorites[i].path)
                label = p.name + "  (" + p.package + (p.group.empty() ? "" : "." + p.group) + ")";
            if (!favorites[i].loaded) label += "  [not loaded]";
            rows.push_back({i, label});
        }
        return rows;
    }

    // The Texture Browser draws loaded favourites as thumbnails, which need
    // the material itself, and lists the ones that are not loaded apart.
    struct TileView
    {
        std::vector<size_t> tiles; // favourite indices, in drawing order
        std::vector<Row> notLoaded;
    };
    inline TileView Tiles(const std::vector<Favorite>& favorites, const std::string& package, Sort sort)
    {
        TileView view;
        for (auto& row : Rows(favorites, package, sort))
        {
            if (favorites[row.favorite].loaded) view.tiles.push_back(row.favorite);
            else view.notLoaded.push_back(std::move(row));
        }
        return view;
    }
}
