#pragma once
// The pure part of the Sound Browser's search and Favorites: a sound's
// package, group and name, the search match, the package list with counts,
// the rows the list shows for a filter and sort order, and the saved list's
// add/remove merge. No engine and no Windows, so the tests compile it alone.
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace SoundFavorites
{
    // Sounds are named by full object path, Package[.Group...].Sound.
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
    inline bool SameName(const std::string& a, const std::string& b) { return Lower(a) == Lower(b); }

    // A search matches when every space-separated word of it appears in the
    // path, ignoring case; an empty search matches everything.
    inline bool Matches(const std::string& path, const std::string& query)
    {
        const std::string haystack = Lower(path);
        size_t at = 0;
        while (at < query.size())
        {
            const size_t start = query.find_first_not_of(" \t", at);
            if (start == std::string::npos) break;
            size_t end = query.find_first_of(" \t", start);
            if (end == std::string::npos) end = query.size();
            if (haystack.find(Lower(query.substr(start, end - start))) == std::string::npos) return false;
            at = end;
        }
        return true;
    }
    inline bool IsBlank(const std::string& query)
    {
        return query.find_first_not_of(" \t") == std::string::npos;
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
        for (Sort s : kSorts) if (SameName(key, SortKey(s))) return s;
        return Sort::Package;
    }

    // The saved list, in the order the favourites were added, without
    // repeats (compared without case) or empty entries.
    inline std::vector<std::string> Dedupe(const std::vector<std::string>& paths)
    {
        std::vector<std::string> out;
        for (const auto& p : paths)
            if (!p.empty() && std::none_of(out.begin(), out.end(), [&](const std::string& o) { return SameName(o, p); }))
                out.push_back(p);
        return out;
    }
    inline bool Contains(const std::vector<std::string>& paths, const std::string& path)
    {
        return std::any_of(paths.begin(), paths.end(), [&](const std::string& p) { return SameName(p, path); });
    }
    // Adding and removing start from the list as saved now, so two open
    // editors keep each other's changes. Adding what is already there, or
    // removing what is gone, leaves the list as it is.
    inline std::vector<std::string> Add(std::vector<std::string> saved, const std::string& path, size_t limit)
    {
        saved = Dedupe(saved);
        if (!path.empty() && !Contains(saved, path) && saved.size() < limit) saved.push_back(path);
        return saved;
    }
    inline std::vector<std::string> Remove(std::vector<std::string> saved, const std::string& path)
    {
        saved = Dedupe(saved);
        saved.erase(std::remove_if(saved.begin(), saved.end(), [&](const std::string& p) { return SameName(p, path); }), saved.end());
        return saved;
    }

    struct Favorite
    {
        std::string path;
        bool loaded = false; // the sound is in memory and can be played
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
            auto it = std::find_if(out.begin(), out.end(), [&](const Package& p) { return SameName(p.name, package); });
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
        size_t item = 0; // index into the sounds or favourites the rows were made from
        std::string label;
    };

    // In package order a row shows the full path, so each package's sounds
    // sit together; by name it leads with the sound name and its package.
    inline std::string Label(const std::string& path, Sort sort, bool loaded)
    {
        const auto p = Split(path);
        std::string label = path;
        if (sort == Sort::Name && p.package != path)
            label = p.name + "  (" + p.package + (p.group.empty() ? "" : "." + p.group) + ")";
        if (!loaded) label += "  [not loaded]";
        return label;
    }

    inline std::string OrderKey(const std::string& path, bool byName)
    {
        const auto p = Split(path);
        return byName ? Lower(p.name) + '\x01' + Lower(p.package) + '\x01' + Lower(p.group)
                      : Lower(p.package) + '\x01' + Lower(p.group) + '\x01' + Lower(p.name);
    }

    // The Favorites rows for one package (empty: every package) that match
    // the search, in the given order. Favourites that are not loaded say so,
    // and come after the loaded ones.
    inline std::vector<Row> FavoriteRows(const std::vector<Favorite>& favorites, const std::string& package, Sort sort,
                                         const std::string& query)
    {
        std::vector<size_t> order;
        for (size_t i = 0; i < favorites.size(); ++i)
            if ((package.empty() || SameName(Split(favorites[i].path).package, package)) && Matches(favorites[i].path, query))
                order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (favorites[a].loaded != favorites[b].loaded) return favorites[a].loaded;
            switch (sort)
            {
            case Sort::Package: return OrderKey(favorites[a].path, false) < OrderKey(favorites[b].path, false);
            case Sort::Name: return OrderKey(favorites[a].path, true) < OrderKey(favorites[b].path, true);
            case Sort::Newest: return a > b;
            case Sort::Oldest: return a < b;
            }
            return false;
        });
        std::vector<Row> rows;
        for (size_t i : order) rows.push_back({i, Label(favorites[i].path, sort, favorites[i].loaded)});
        return rows;
    }

    // The sounds matching a search, as loaded favourites, for Packages().
    inline std::vector<Favorite> Matching(const std::vector<std::string>& sounds, const std::string& query)
    {
        std::vector<Favorite> out;
        for (const auto& s : sounds)
            if (Matches(s, query)) out.push_back({s, true});
        return out;
    }

    // The search rows over every loaded sound: those in one package (empty:
    // every package) matching the search, by package or by name. Sounds have
    // no age, so the newest and oldest orders fall back to package order.
    inline std::vector<Row> SearchRows(const std::vector<std::string>& sounds, const std::string& package,
                                       const std::string& query, Sort sort)
    {
        const bool byName = sort == Sort::Name;
        std::vector<size_t> order;
        for (size_t i = 0; i < sounds.size(); ++i)
            if ((package.empty() || SameName(Split(sounds[i]).package, package)) && Matches(sounds[i], query))
                order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return OrderKey(sounds[a], byName) < OrderKey(sounds[b], byName);
        });
        std::vector<Row> rows;
        for (size_t i : order) rows.push_back({i, Label(sounds[i], byName ? Sort::Name : Sort::Package, true)});
        return rows;
    }
}
