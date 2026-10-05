#pragma once
// The pure part of the Favorites window: the favourites of the Texture, Static
// Mesh and Sound Browsers in one list, their tags, and the rows shown for a
// type, tag, package and search filter in a sort order. The favourite lists
// themselves stay in the browsers' own ini sections; tags live in a section of
// their own, one key per tag name and one per tagged favourite, so a change
// writes only the keys it touches and two open editors keep each other's.
// No engine and no Windows, so the tests compile it alone.
#include "FavoritesModel.h"
#include "SoundFavoritesModel.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace FavoritesWindow::Model
{
    using Favorites::Lower;
    using Favorites::Sort;

    enum class Kind { Material, Mesh, Sound };
    constexpr Kind kKinds[] = {Kind::Material, Kind::Mesh, Kind::Sound};
    constexpr size_t kKindCount = 3;
    constexpr size_t kMaxFavorites = 4096;
    constexpr size_t kMaxTagName = 64;
    constexpr const char* kTagSection = "FavoriteTags";
    constexpr const char* kViewSection = "FavoritesWindowView";

    inline size_t Index(Kind kind) { return static_cast<size_t>(kind); }
    inline const char* KindLabel(Kind kind)
    {
        switch (kind)
        {
        case Kind::Material: return "Material";
        case Kind::Mesh: return "Static mesh";
        case Kind::Sound: return "Sound";
        }
        return "";
    }
    inline const char* KindPlural(Kind kind)
    {
        switch (kind)
        {
        case Kind::Material: return "Materials";
        case Kind::Mesh: return "Static meshes";
        case Kind::Sound: return "Sounds";
        }
        return "";
    }
    // The name a favourite's tag key starts with, and the view's saved type.
    inline const char* KindKey(Kind kind)
    {
        switch (kind)
        {
        case Kind::Material: return "Material";
        case Kind::Mesh: return "Mesh";
        case Kind::Sound: return "Sound";
        }
        return "";
    }
    inline std::optional<Kind> KindFromKey(const std::string& key)
    {
        for (Kind k : kKinds)
            if (Lower(key) == Lower(KindKey(k))) return k;
        return std::nullopt;
    }
    // Each browser's own favourites section, kept exactly as the browser writes it.
    inline const char* ListSection(Kind kind)
    {
        switch (kind)
        {
        case Kind::Material: return "TextureBrowserFavorites";
        case Kind::Mesh: return "StaticMeshBrowserFavorites";
        case Kind::Sound: return "SoundBrowserFavorites";
        }
        return "";
    }

    // An ini section's lines, key and value, in file order.
    using Lines = std::vector<std::pair<std::string, std::string>>;

    inline const std::string* Find(const Lines& lines, const std::string& key)
    {
        for (const auto& line : lines)
            if (Lower(line.first) == Lower(key)) return &line.second;
        return nullptr;
    }

    // A browser's list: Count, then Favorite0..Count-1, read as the browsers
    // read it, without repeats or blanks.
    inline std::vector<std::string> ParseList(const Lines& lines)
    {
        const std::string* count = Find(lines, "Count");
        int n = 0;
        if (count)
            try { n = std::stoi(*count); } catch (const std::exception&) { n = 0; }
        n = std::max(0, std::min(n, static_cast<int>(kMaxFavorites)));
        std::vector<std::string> paths;
        for (int i = 0; i < n; ++i)
            if (const std::string* value = Find(lines, "Favorite" + std::to_string(i))) paths.push_back(*value);
        return SoundFavorites::Dedupe(paths);
    }
    // The section a browser writes for a list (it rewrites the whole section).
    inline Lines ListLines(const std::vector<std::string>& paths)
    {
        Lines lines{{"Count", std::to_string(paths.size())}};
        for (size_t i = 0; i < paths.size(); ++i) lines.push_back({"Favorite" + std::to_string(i), paths[i]});
        return lines;
    }

    // ------------------------------------------------------------------
    // Tags.

    inline std::string Trim(const std::string& text)
    {
        const size_t first = text.find_first_not_of(" \t");
        if (first == std::string::npos) return {};
        return text.substr(first, text.find_last_not_of(" \t") - first + 1);
    }
    // A tag name as typed, made safe for the ini: no separators, quotes,
    // brackets or control characters, single spaces, at most kMaxTagName
    // characters. Empty means the name cannot be used.
    inline std::string CleanTagName(const std::string& typed)
    {
        std::string out;
        for (char c : typed)
        {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 32 || c == '|' || c == '"' || c == '=' || c == '[' || c == ']' || c == ';') c = ' ';
            if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
            out.push_back(c);
        }
        out = Trim(out);
        if (out.size() > kMaxTagName) out = Trim(out.substr(0, kMaxTagName));
        return out;
    }

    inline std::string ItemKey(Kind kind, const std::string& path) { return std::string(KindKey(kind)) + ":" + path; }
    inline std::string TagKey(const std::string& name) { return "Tag:" + name; }

    inline std::vector<std::string> SplitTags(const std::string& value)
    {
        std::vector<std::string> out;
        size_t at = 0;
        while (at <= value.size())
        {
            size_t end = value.find('|', at);
            if (end == std::string::npos) end = value.size();
            const std::string name = Trim(value.substr(at, end - at));
            if (!name.empty() && std::none_of(out.begin(), out.end(), [&](const std::string& o) { return Lower(o) == Lower(name); }))
                out.push_back(name);
            at = end + 1;
        }
        return out;
    }
    inline std::string JoinTags(const std::vector<std::string>& names, const char* separator = "|")
    {
        std::string out;
        for (const auto& n : names) out += (out.empty() ? "" : separator) + n;
        return out;
    }

    struct Tags
    {
        std::vector<std::string> names;                         // as created, in file order
        std::map<std::string, std::vector<std::string>> members; // lower-case item key -> tag names
        std::map<std::string, std::string> keys;                 // lower-case item key -> the key as written

        bool Has(const std::string& name) const
        {
            return std::any_of(names.begin(), names.end(), [&](const std::string& n) { return Lower(n) == Lower(name); });
        }
        std::string Spelling(const std::string& name) const
        {
            for (const auto& n : names)
                if (Lower(n) == Lower(name)) return n;
            return name;
        }
    };

    inline Tags ParseTags(const Lines& lines)
    {
        Tags tags;
        for (const auto& [key, value] : lines)
        {
            if (Lower(key).rfind("tag:", 0) == 0)
            {
                const std::string name = CleanTagName(key.substr(4));
                if (!name.empty() && !tags.Has(name)) tags.names.push_back(name);
                continue;
            }
            const size_t colon = key.find(':');
            if (colon == std::string::npos || !KindFromKey(key.substr(0, colon)) || colon + 1 >= key.size()) continue;
            const auto names = SplitTags(value);
            if (names.empty()) continue;
            tags.members[Lower(key)] = names;
            tags.keys[Lower(key)] = key;
        }
        return tags;
    }

    // The favourite's tags that still exist, in the order the tags were created.
    inline std::vector<std::string> TagsOf(const Tags& tags, Kind kind, const std::string& path)
    {
        std::vector<std::string> out;
        const auto it = tags.members.find(Lower(ItemKey(kind, path)));
        if (it == tags.members.end()) return out;
        for (const auto& name : tags.names)
            if (std::any_of(it->second.begin(), it->second.end(), [&](const std::string& m) { return Lower(m) == Lower(name); }))
                out.push_back(name);
        return out;
    }

    // One ini key to write: a value, or nullopt to delete the key.
    struct Write
    {
        std::string key;
        std::optional<std::string> value;
        bool operator==(const Write&) const = default;
    };
    using Writes = std::vector<Write>;

    struct Item
    {
        Kind kind = Kind::Material;
        std::string path;
    };

    // The favourite's key as already written (so a rewrite replaces it), or a new one.
    inline std::string KeyFor(const Tags& tags, const Item& item)
    {
        const auto it = tags.keys.find(Lower(ItemKey(item.kind, item.path)));
        return it != tags.keys.end() ? it->second : ItemKey(item.kind, item.path);
    }
    inline std::vector<std::string> RawMembers(const Tags& tags, const Item& item)
    {
        const auto it = tags.members.find(Lower(ItemKey(item.kind, item.path)));
        return it == tags.members.end() ? std::vector<std::string>{} : it->second;
    }
    inline Write MembershipWrite(const Tags& tags, const Item& item, const std::vector<std::string>& names)
    {
        return {KeyFor(tags, item), names.empty() ? std::nullopt : std::optional<std::string>(JoinTags(names))};
    }

    // Each of these starts from the tags as saved now and returns only the
    // keys that change. They throw std::runtime_error with a message for the
    // status line when the change cannot be made.
    inline Writes CreateTag(const Tags& tags, const std::string& typed)
    {
        const std::string name = CleanTagName(typed);
        if (name.empty()) throw std::runtime_error("Type a name for the tag first.");
        if (tags.Has(name)) throw std::runtime_error("There is already a tag called " + tags.Spelling(name) + ".");
        return {{TagKey(name), std::string("1")}};
    }

    inline Writes RenameTag(const Tags& tags, const std::string& from, const std::string& typed)
    {
        if (!tags.Has(from)) throw std::runtime_error("The tag " + from + " no longer exists.");
        const std::string old = tags.Spelling(from);
        const std::string name = CleanTagName(typed);
        if (name.empty()) throw std::runtime_error("Type the tag's new name first.");
        if (name == old) return {};
        if (Lower(name) != Lower(old) && tags.Has(name)) throw std::runtime_error("There is already a tag called " + tags.Spelling(name) + ".");
        Writes writes{{TagKey(old), std::nullopt}, {TagKey(name), std::string("1")}};
        for (const auto& [lower, names] : tags.members)
        {
            if (std::none_of(names.begin(), names.end(), [&](const std::string& n) { return Lower(n) == Lower(old); })) continue;
            std::vector<std::string> renamed;
            for (const auto& n : names)
            {
                const std::string next = Lower(n) == Lower(old) ? name : n;
                if (std::none_of(renamed.begin(), renamed.end(), [&](const std::string& r) { return Lower(r) == Lower(next); }))
                    renamed.push_back(next);
            }
            writes.push_back({tags.keys.at(lower), JoinTags(renamed)});
        }
        return writes;
    }

    inline Writes DeleteTag(const Tags& tags, const std::string& name)
    {
        if (!tags.Has(name)) throw std::runtime_error("The tag " + name + " no longer exists.");
        Writes writes{{TagKey(tags.Spelling(name)), std::nullopt}};
        for (const auto& [lower, names] : tags.members)
        {
            std::vector<std::string> kept;
            for (const auto& n : names)
                if (Lower(n) != Lower(name)) kept.push_back(n);
            if (kept.size() == names.size()) continue;
            writes.push_back({tags.keys.at(lower), kept.empty() ? std::nullopt : std::optional<std::string>(JoinTags(kept))});
        }
        return writes;
    }

    inline Writes AddToTag(const Tags& tags, const std::vector<Item>& items, const std::string& name)
    {
        if (!tags.Has(name)) throw std::runtime_error("The tag " + name + " no longer exists.");
        const std::string spelled = tags.Spelling(name);
        Writes writes;
        for (const auto& item : items)
        {
            auto names = RawMembers(tags, item);
            if (std::any_of(names.begin(), names.end(), [&](const std::string& n) { return Lower(n) == Lower(spelled); })) continue;
            // Names of tags since deleted are dropped while the key is rewritten.
            names.erase(std::remove_if(names.begin(), names.end(), [&](const std::string& n) { return !tags.Has(n); }), names.end());
            names.push_back(spelled);
            writes.push_back(MembershipWrite(tags, item, names));
        }
        return writes;
    }

    inline Writes RemoveFromTag(const Tags& tags, const std::vector<Item>& items, const std::string& name)
    {
        Writes writes;
        for (const auto& item : items)
        {
            const auto names = RawMembers(tags, item);
            std::vector<std::string> kept;
            for (const auto& n : names)
                if (Lower(n) != Lower(name) && tags.Has(n)) kept.push_back(n);
            if (kept.size() != names.size()) writes.push_back(MembershipWrite(tags, item, kept));
        }
        return writes;
    }

    // A favourite that is removed takes its tags with it.
    inline Writes ForgetItems(const Tags& tags, const std::vector<Item>& items)
    {
        Writes writes;
        for (const auto& item : items)
            if (tags.members.count(Lower(ItemKey(item.kind, item.path)))) writes.push_back({KeyFor(tags, item), std::nullopt});
        return writes;
    }

    // What WritePrivateProfileString makes of a section, key by key: a key is
    // matched without case, replaced where it stands, deleted, or appended.
    inline Lines ApplyWrites(Lines lines, const Writes& writes)
    {
        for (const auto& w : writes)
        {
            auto it = std::find_if(lines.begin(), lines.end(), [&](const auto& l) { return Lower(l.first) == Lower(w.key); });
            if (!w.value)
            {
                if (it != lines.end()) lines.erase(it);
            }
            else if (it != lines.end())
                it->second = *w.value;
            else
                lines.push_back({w.key, *w.value});
        }
        return lines;
    }

    // ------------------------------------------------------------------
    // The list.

    struct Entry
    {
        Kind kind = Kind::Material;
        std::string path;
        bool loaded = false;
        size_t order = 0; // position in its browser's list, oldest first
        std::vector<std::string> tags;
    };

    using ListsByKind = std::array<std::vector<std::string>, kKindCount>;

    template <class LoadedFn>
    std::vector<Entry> Entries(const ListsByKind& lists, const Tags& tags, LoadedFn&& loaded)
    {
        std::vector<Entry> out;
        for (Kind kind : kKinds)
        {
            const auto& list = lists[Index(kind)];
            for (size_t i = 0; i < list.size(); ++i)
                out.push_back({kind, list[i], loaded(kind, list[i]), i, TagsOf(tags, kind, list[i])});
        }
        return out;
    }

    struct TagFilter
    {
        enum class Mode { All, Untagged, Tag } mode = Mode::All;
        std::string name;
        bool operator==(const TagFilter&) const = default;
    };

    struct Filter
    {
        std::optional<Kind> kind; // none: every type
        TagFilter tag;
        std::string package;      // empty: every package
        std::string query;
        Sort sort = Sort::Package;
    };

    inline bool HasTag(const Entry& e, const std::string& name)
    {
        return std::any_of(e.tags.begin(), e.tags.end(), [&](const std::string& t) { return Lower(t) == Lower(name); });
    }
    inline bool KindMatches(const Entry& e, const Filter& f) { return !f.kind || *f.kind == e.kind; }
    inline bool TagMatches(const Entry& e, const TagFilter& t)
    {
        switch (t.mode)
        {
        case TagFilter::Mode::All: return true;
        case TagFilter::Mode::Untagged: return e.tags.empty();
        case TagFilter::Mode::Tag: return HasTag(e, t.name);
        }
        return true;
    }
    // Every word of the search appears in the path (with the English for
    // French names, as the Sound Browser's search does), a tag or the type.
    inline bool QueryMatches(const Entry& e, const std::string& query)
    {
        if (SoundFavorites::IsBlank(query)) return true;
        const auto& text = SoundFavorites::SearchText();
        const std::string haystack = Lower((text ? text(e.path) : e.path) + " " + JoinTags(e.tags, " ") + " " + KindLabel(e.kind));
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
    inline bool PackageMatches(const Entry& e, const std::string& package)
    {
        return package.empty() || Lower(Favorites::Split(e.path).package) == Lower(package);
    }

    // The packages the package filter offers: those of the favourites the
    // other filters let through, with their counts.
    inline std::vector<Favorites::Package> Packages(const std::vector<Entry>& entries, const Filter& filter, size_t* total = nullptr)
    {
        std::vector<Favorites::Favorite> shown;
        for (const auto& e : entries)
            if (KindMatches(e, filter) && TagMatches(e, filter.tag) && QueryMatches(e, filter.query)) shown.push_back({e.path, e.loaded});
        if (total) *total = shown.size();
        return Favorites::Packages(shown);
    }

    // The tag list: every favourite, the untagged ones, then each tag, with
    // how many of the favourites of the chosen type are in it.
    struct TagRow
    {
        TagFilter filter;
        std::string label;
    };
    inline std::vector<TagRow> TagRows(const std::vector<Entry>& entries, const Tags& tags, const std::optional<Kind>& kind)
    {
        size_t all = 0, untagged = 0;
        std::vector<size_t> counts(tags.names.size(), 0);
        for (const auto& e : entries)
        {
            if (kind && *kind != e.kind) continue;
            ++all;
            if (e.tags.empty()) ++untagged;
            for (size_t i = 0; i < tags.names.size(); ++i)
                if (HasTag(e, tags.names[i])) ++counts[i];
        }
        std::vector<TagRow> rows{{{TagFilter::Mode::All, {}}, "All favourites (" + std::to_string(all) + ")"},
                                 {{TagFilter::Mode::Untagged, {}}, "Untagged (" + std::to_string(untagged) + ")"}};
        std::vector<size_t> order(tags.names.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return Lower(tags.names[a]) < Lower(tags.names[b]); });
        for (size_t i : order)
            rows.push_back({{TagFilter::Mode::Tag, tags.names[i]}, tags.names[i] + " (" + std::to_string(counts[i]) + ")"});
        return rows;
    }

    struct Row
    {
        size_t entry = 0; // index into the entries
        std::string name, type, location, tags;
    };

    inline std::string Location(const std::string& path)
    {
        const auto p = Favorites::Split(path);
        return p.package == path ? std::string{} : p.package + (p.group.empty() ? "" : "." + p.group);
    }

    // The rows the filter lets through, loaded favourites first. By package
    // they group by package, group and name; by name they lead with the
    // object's name. Newest and oldest keep each type together (each browser
    // only knows the order of its own list), materials first.
    inline std::vector<Row> Rows(const std::vector<Entry>& entries, const Filter& filter)
    {
        std::vector<size_t> order;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            if (KindMatches(e, filter) && TagMatches(e, filter.tag) && PackageMatches(e, filter.package) && QueryMatches(e, filter.query))
                order.push_back(i);
        }
        auto key = [&](size_t i, bool byName) {
            return SoundFavorites::OrderKey(entries[i].path, byName) + '\x01' + static_cast<char>('0' + Index(entries[i].kind));
        };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const auto& x = entries[a];
            const auto& y = entries[b];
            if (x.loaded != y.loaded) return x.loaded;
            switch (filter.sort)
            {
            case Sort::Package: return key(a, false) < key(b, false);
            case Sort::Name: return key(a, true) < key(b, true);
            case Sort::Newest:
                if (x.kind != y.kind) return Index(x.kind) < Index(y.kind);
                return x.order > y.order;
            case Sort::Oldest:
                if (x.kind != y.kind) return Index(x.kind) < Index(y.kind);
                return x.order < y.order;
            }
            return false;
        });
        std::vector<Row> rows;
        for (size_t i : order)
        {
            const auto& e = entries[i];
            Row row{i, Favorites::Split(e.path).name, KindLabel(e.kind), Location(e.path), JoinTags(e.tags, ", ")};
            if (!e.loaded) row.name += "  [not loaded]";
            rows.push_back(std::move(row));
        }
        return rows;
    }

    // The packages to load for favourites that are not in memory, one per
    // type and package (each type's packages live in their own folder).
    struct PackageLoad
    {
        Kind kind = Kind::Material;
        std::string package;
        bool operator==(const PackageLoad&) const = default;
    };
    inline std::vector<PackageLoad> MissingPackages(const std::vector<Entry>& entries, const std::set<std::string>& attempted = {})
    {
        std::vector<PackageLoad> out;
        std::set<std::string> seen;
        for (const auto& e : entries)
        {
            if (e.loaded) continue;
            const std::string package = Favorites::Split(e.path).package;
            const std::string id = std::string(KindKey(e.kind)) + ":" + Lower(package);
            if (package.empty() || attempted.count(id) || !seen.insert(id).second) continue;
            out.push_back({e.kind, package});
        }
        return out;
    }
    inline std::string AttemptKey(const PackageLoad& load) { return std::string(KindKey(load.kind)) + ":" + Lower(load.package); }

    // A one-line summary for the status bar.
    inline std::string Summary(const std::vector<Entry>& entries, size_t shown)
    {
        std::array<size_t, kKindCount> counts{};
        size_t notLoaded = 0;
        for (const auto& e : entries)
        {
            ++counts[Index(e.kind)];
            if (!e.loaded) ++notLoaded;
        }
        std::string text = std::to_string(shown) + " of " + std::to_string(entries.size()) + " favourites shown: " +
                           std::to_string(counts[0]) + " materials, " + std::to_string(counts[1]) + " static meshes, " +
                           std::to_string(counts[2]) + " sounds";
        if (notLoaded) text += "; " + std::to_string(notLoaded) + " not loaded";
        return text + ".";
    }

    // Where a tab the browser window adds goes while the Favorites tab is at
    // index favorites (-1: not there): never after it, so it stays the last
    // tab and the stock tabs keep their indices.
    inline int StockTabIndex(int requested, int favorites)
    {
        if (favorites < 0) return requested;
        return requested < 0 || requested > favorites ? favorites : requested;
    }
}
