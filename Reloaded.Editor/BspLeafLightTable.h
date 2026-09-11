#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace BspLeafLightTable
{
    constexpr uint16_t None = 0xFFFF;
    constexpr size_t MaxSlots = 0x8000;

    struct Packed
    {
        std::vector<uint16_t> starts;
        std::vector<uint32_t> lights;
    };

    // FLeaf::iPermeating is read with MOVSX by both stock render paths.
    // Intern identical, ordered, null-terminated lists and their suffixes to keep the ordinary
    // file format usable without changing the game's index interpretation.
    // Failure leaves the caller's table untouched.
    inline bool Pack(std::span<const uint16_t> starts,
                     std::span<const uint32_t> lights, Packed& result,
                     std::string& error)
    {
        if (starts.size() > 1000000 || lights.size() > None)
        {
            error = "The BSP leaf-light table exceeds its native index capacity.";
            return false;
        }
        constexpr uint32_t invalid = (std::numeric_limits<uint32_t>::max)();
        // Give equal suffixes equal IDs in one backwards pass. ID 0 is the
        // terminator. A pair (light, following suffix ID) identifies a list.
        std::map<std::pair<uint32_t,uint32_t>, uint32_t> suffixes;
        std::vector<uint32_t> ids(lights.size(), invalid), lengths(lights.size());
        uint32_t next = invalid, length = 0;
        for (size_t i=lights.size(); i-- > 0;)
        {
            if (!lights[i]) { next=0; length=1; }
            else if (next != invalid)
            {
                const auto inserted = suffixes.emplace(std::make_pair(lights[i],next),
                    static_cast<uint32_t>(suffixes.size()+1));
                next = inserted.first->second;
                ++length;
            }
            ids[i]=next;
            lengths[i]=length;
        }
        std::vector<uint16_t> unique;
        std::vector<bool> seen(suffixes.size()+1);
        for (const uint16_t start : starts)
        {
            if (start == None) continue;
            if (start >= lights.size())
            {
                error = "The BSP contains an out-of-range leaf-light index.";
                return false;
            }
            if (ids[start] == invalid)
            {
                error = "The BSP contains an unterminated leaf-light list.";
                return false;
            }
            if (!seen[ids[start]]) { unique.push_back(start); seen[ids[start]]=true; }
        }
        // Write longer lists first, so a shorter suffix can point into one
        // already written. This cannot expand an already shared input table.
        std::stable_sort(unique.begin(), unique.end(), [&](uint16_t a,uint16_t b) {
            return lengths[a] > lengths[b];
        });
        Packed packed;
        std::vector<uint16_t> offsets(suffixes.size()+1, None);
        for (const auto start : unique)
        {
            if (offsets[ids[start]] != None) continue;
            if (lengths[start] > MaxSlots - packed.lights.size())
            {
                error = "The BSP still exceeds the stock game's leaf-light index limit after exact list sharing.";
                return false;
            }
            for (size_t i=start; i<static_cast<size_t>(start)+lengths[start]; ++i)
            {
                if (offsets[ids[i]] == None) offsets[ids[i]]=static_cast<uint16_t>(packed.lights.size());
                packed.lights.push_back(lights[i]);
            }
        }
        packed.starts.reserve(starts.size());
        for (const auto start : starts) packed.starts.push_back(start==None ? None : offsets[ids[start]]);
        result = std::move(packed);
        error.clear();
        return true;
    }
}
