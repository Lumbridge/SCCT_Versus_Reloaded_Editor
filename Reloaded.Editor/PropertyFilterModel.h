#pragma once
// The pure part of the property filter in the native Properties windows:
// which rows of the property tree stay listed for a filter text, where the
// list goes under the filter bar, and when a row of a multi-selection shows
// "(multiple values)". No engine and no Windows, so the tests compile it alone.
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace PropertyFilter
{
    constexpr const char* kMultipleValues = "(multiple values)";

    inline std::string Lower(std::string s)
    {
        for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    }

    // The filter's words, lower case. Spaces separate words; every word has to
    // match, so "light col" finds LightColor and "events tag" finds the Tag row
    // under Events.
    inline std::vector<std::string> Words(const std::string& filter)
    {
        std::vector<std::string> words;
        std::string word;
        for (char c : filter)
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                if (!word.empty()) words.push_back(Lower(word));
                word.clear();
            }
            else word += c;
        }
        if (!word.empty()) words.push_back(Lower(word));
        return words;
    }

    inline bool Matches(const std::string& text, const std::vector<std::string>& words)
    {
        const std::string folded = Lower(text);
        for (const auto& w : words)
            if (folded.find(w) == std::string::npos) return false;
        return true;
    }

    // One row of the property list in list order: a category (depth 1), a
    // property under it (depth 2) or a struct member / array element shown
    // below an expanded property (depth 3 and deeper). Rows with no name of
    // their own (buttons, "new object" rows) take their place from depth only.
    struct Row
    {
        int depth = 1;
        std::string name;
    };

    // Which rows stay listed. A row matches when the names from its category
    // down to itself contain every word: a category name that matches keeps
    // all of its properties, "events tag" keeps Tag under Events, and a struct
    // member that matches ("x") keeps the property it belongs to. A row also
    // stays when anything below it matches, so every match keeps its path. An
    // empty filter keeps everything.
    inline std::vector<bool> Visible(const std::vector<Row>& rows, const std::string& filter)
    {
        const auto words = Words(filter);
        std::vector<bool> keep(rows.size(), words.empty());
        if (words.empty()) return keep;

        std::vector<size_t> ancestors;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            while (!ancestors.empty() && rows[ancestors.back()].depth >= rows[i].depth) ancestors.pop_back();
            std::string path;
            for (auto a : ancestors) path += rows[a].name + " ";
            path += rows[i].name;
            if (Matches(path, words))
            {
                keep[i] = true;
                for (auto a : ancestors) keep[a] = true;
            }
            ancestors.push_back(i);
        }
        return keep;
    }

    // Where the native list goes once the filter bar takes the top `bar`
    // pixels of the window. The editor places the list at the top and sizes it
    // to its rows, never past `limit` (the client height less the space kept
    // for a button under it); the list moves down by the bar and loses height
    // only where it would now run past that limit.
    struct Placement
    {
        int y = 0, height = 0;
    };
    inline Placement BelowBar(int y, int height, int bar, int limit)
    {
        Placement p{y, height};
        if (p.y < bar) p.y = bar;
        if (p.y + p.height > limit) p.height = (std::max)(0, limit - p.y);
        return p;
    }

    // A property row of a selection shows "(multiple values)" when the
    // selected objects disagree, which the editor reports by having no single
    // value to read. One object always has a value.
    inline bool ShowsMultipleValues(int objectCount, bool hasValue)
    {
        return objectCount > 1 && !hasValue;
    }
}
