#pragma once
// The pure part of the Static Mesh and Texture Browsers' Favorites and of the
// browsers' search: a favourite's package, group and name, the search match,
// the package list with counts, the rows shown for a package filter, sort
// order and search, and where the search row goes under a list. No engine and
// no Windows, so the tests compile it alone.
#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
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

    // The text a search looks in for an object: its path, unless the editor
    // adds more (the English for French names).
    inline std::function<std::string(const std::string&)>& SearchText()
    {
        static std::function<std::string(const std::string&)> text;
        return text;
    }

    inline bool IsBlank(const std::string& query)
    {
        return query.find_first_not_of(" \t") == std::string::npos;
    }

    // A search matches when every space-separated word of it appears in the
    // path (or its SearchText), ignoring case; an empty search matches everything.
    inline bool Matches(const std::string& path, const std::string& query)
    {
        if (IsBlank(query)) return true;
        const std::string haystack = Lower(SearchText() ? SearchText()(path) : path);
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

    // The words of a search, in lower case.
    inline std::vector<std::string> Words(const std::string& query)
    {
        std::vector<std::string> words;
        size_t at = 0;
        while ((at = query.find_first_not_of(" \t", at)) != std::string::npos)
        {
            const size_t end = (std::min)(query.find_first_of(" \t", at), query.size());
            words.push_back(Lower(query.substr(at, end - at)));
            at = end;
        }
        return words;
    }

    // Two searches match the same objects when they have the same words,
    // whatever their case and spacing; blank ones all match everything.
    inline bool SameSearch(const std::string& a, const std::string& b)
    {
        return Words(a) == Words(b);
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
        // Index into the favourites, which are in the order they were added,
        // or for search rows into the loaded objects searched.
        size_t favorite = 0;
        std::string label;
    };

    // In package order a row shows its full path, so each package's objects
    // sit together; by name it leads with the object's name.
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

    // The rows for one package (empty: every package) that match the search
    // (empty: all of them), in the given order. Favourites that are not loaded
    // say so, and come after the loaded ones.
    inline std::vector<Row> Rows(const std::vector<Favorite>& favorites, const std::string& package, Sort sort,
                                 const std::string& query = "")
    {
        std::vector<size_t> order;
        for (size_t i = 0; i < favorites.size(); ++i)
            if ((package.empty() || Lower(Split(favorites[i].path).package) == Lower(package)) && Matches(favorites[i].path, query))
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

    // An object is in a group when it sits in the group or in one inside it;
    // an empty group holds everything.
    inline bool InGroup(const std::string& path, const std::string& group)
    {
        const std::string in = Lower(Split(path).group), wanted = Lower(group);
        return wanted.empty() || in == wanted || in.rfind(wanted + '.', 0) == 0;
    }

    // The search rows over every loaded object: those in one package (empty:
    // every package) and group (empty: every group) matching the search, by
    // package or by name. Loaded objects have no age, so the newest and oldest
    // orders fall back to package order.
    inline std::vector<Row> SearchRows(const std::vector<std::string>& paths, const std::string& package,
                                       const std::string& query, Sort sort, const std::string& group = "")
    {
        const bool byName = sort == Sort::Name;
        // Keyed once each: a search can match every mesh in memory.
        std::vector<std::pair<std::string, size_t>> order;
        for (size_t i = 0; i < paths.size(); ++i)
            if ((package.empty() || Lower(Split(paths[i]).package) == Lower(package)) && InGroup(paths[i], group)
                && Matches(paths[i], query))
                order.push_back({OrderKey(paths[i], byName), i});
        std::sort(order.begin(), order.end());
        std::vector<Row> rows;
        for (const auto& entry : order)
            rows.push_back({entry.second, Label(paths[entry.second], byName ? Sort::Name : Sort::Package, true)});
        return rows;
    }

    // The search row under a browser's list, in the client coordinates of the
    // list's parent.
    struct Box
    {
        long left = 0, top = 0, right = 0, bottom = 0;
        bool operator==(const Box&) const = default;
    };
    inline bool Inside(const Box& box, const Box& client)
    {
        return box.left >= client.left && box.top >= client.top && box.right <= client.right && box.bottom <= client.bottom;
    }

    struct SearchRowPlace
    {
        Box list, row;
    };

    // The row takes the bottom of the list's place and sits right under what
    // is left of it, which keeps at least 20 pixels.
    inline SearchRowPlace PlaceSearchRow(const Box& list, long rowHeight, long gap)
    {
        SearchRowPlace place{list, list};
        place.list.bottom = (std::max)(list.top + 20, list.bottom - rowHeight - gap);
        place.row.top = place.list.bottom + gap;
        place.row.bottom = place.row.top + rowHeight;
        return place;
    }

    // Whether the list and the row move, and where to (place). A list that is
    // not where the last layout left it (lastList) has been placed by the
    // native layout, so it gives up its bottom again. One that is still there
    // keeps its height and has the row put back right under it, unless the row
    // would then fall outside the pane while the list fits: the native layout
    // has put the list back exactly there in a pane that shrank by the row's
    // height, so it gives up its bottom again too. A list that does not fit
    // either is not shortened further, which would only repeat on every check.
    inline bool LayOutSearchRow(const Box& list, const Box& lastList, const Box& row, const Box& client, long rowHeight,
                                long gap, SearchRowPlace& place)
    {
        SearchRowPlace next{list, {list.left, list.bottom + gap, list.right, list.bottom + gap + rowHeight}};
        if (!(list == lastList) || (Inside(list, client) && !Inside(next.row, client)))
            next = PlaceSearchRow(list, rowHeight, gap);
        // Nothing moves when both are there already, a list as short as it
        // goes with the row under it included.
        if (next.list == list && next.row == row)
            return false;
        place = next;
        return true;
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
