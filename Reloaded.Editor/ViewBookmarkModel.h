#pragma once
// Camera bookmarks: Ctrl+1 to Ctrl+9 over a level viewport store the viewport
// cameras as bookmark N of the open map, 1 to 9 go back to them. A bookmark is
// an ordinary entry of the map's Working Views (maps[map].views in
// library.json) that also carries "slot": N, and only cameras (its "actors"
// list is empty, so going back never touches hidden flags or groups). Working
// Views lists, renames and deletes it like any other view. This header is the
// pure part: which entry holds a slot and what storing into one does to the
// list. No Windows, no engine.
#include "WorkflowModel.h"
#include <stdexcept>
#include <string>

namespace Workflow::Bookmarks
{
    constexpr int kFirst = 1, kLast = 9;

    inline void CheckSlot(int slot)
    {
        if (slot < kFirst || slot > kLast) throw std::runtime_error("Camera bookmarks are numbered 1 to 9.");
    }
    // "Bookmark 3": the name a new bookmark gets.
    inline std::string Label(int slot)
    {
        CheckSlot(slot);
        return "Bookmark " + std::to_string(slot);
    }
    // The entry's bookmark number, or 0 for an ordinary view (or a slot no key reaches).
    inline int SlotOf(const Json& entry)
    {
        if (!entry.is_object()) return 0;
        const auto found = entry.find("slot");
        if (found == entry.end() || !found->is_number_integer()) return 0;
        const auto slot = found->get<long long>();
        return slot >= kFirst && slot <= kLast ? static_cast<int>(slot) : 0;
    }
    // The index of the entry holding the slot, or -1 when it is free.
    inline int FindSlot(const Json& views, int slot)
    {
        CheckSlot(slot);
        if (!views.is_array()) throw std::runtime_error("Invalid workflow entries.");
        for (size_t i = 0; i < views.size(); ++i)
            if (SlotOf(views[i]) == slot) return static_cast<int>(i);
        return -1;
    }
    // What going back to a view restores for a bookmark: its cameras only.
    inline Json CamerasOnly(const Json& captured)
    {
        return { { "cameras", captured.at("cameras") }, { "actors", Json::array() } };
    }
    // The views list with these cameras stored as the slot: the entry holding
    // it keeps its id and name and takes the cameras; a free slot appends a new
    // entry. Any other entry carrying the same slot loses it, so one key always
    // means one view. Ordinary views are never changed.
    inline Json Assign(Json views, int slot, const Json& cameras, const std::string& newId, const std::string& timestamp)
    {
        CheckSlot(slot);
        if (views.is_null()) views = Json::array();
        if (!views.is_array()) throw std::runtime_error("Invalid workflow entries.");
        if (!cameras.is_array()) throw std::runtime_error("No viewport cameras to store.");
        const int at = FindSlot(views, slot);
        for (size_t i = 0; i < views.size(); ++i)
            if (static_cast<int>(i) != at && SlotOf(views[i]) == slot) views[i].erase("slot");
        Json entry = { { "id", newId }, { "name", Label(slot) }, { "modified", timestamp }, { "slot", slot }, { "cameras", cameras }, { "actors", Json::array() } };
        if (at < 0) { views.push_back(std::move(entry)); return views; }
        auto& held = views[static_cast<size_t>(at)];
        entry["id"] = held.at("id");
        entry["name"] = held.at("name");
        held = std::move(entry);
        return views;
    }
}
