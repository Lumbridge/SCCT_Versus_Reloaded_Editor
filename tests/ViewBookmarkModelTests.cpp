#include "../Reloaded.Editor/ViewBookmarkModel.h"
#include <iostream>
#include <source_location>
#include <string>
using namespace Workflow;
using namespace Workflow::Bookmarks;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
template <class F> void Reject(F f, std::source_location where = std::source_location::current())
{
    bool caught = false;
    try { f(); } catch (const std::exception&) { caught = true; }
    if (!caught) throw std::runtime_error("invalid input accepted at line " + std::to_string(where.line()));
}

Json Cameras(double x)
{
    return Json::array({ { { "slot", 0 }, { "position", { x, 0.0, 64.0 } }, { "rotation", { 0, 16384, 0 } }, { "mode", 5 }, { "flags", 0 }, { "FovAngle", 90.0 } } });
}
Json View(const char* id, const char* name)
{
    return { { "id", id }, { "name", name }, { "modified", "1" }, { "cameras", Cameras(0) },
             { "actors", Json::array({ { { "path", "MyLevel.Light0" }, { "class", "Engine.Light" }, { "hidden", true } } }) }, { "groups", "None" } };
}

void Slots()
{
    Check(SlotOf(View("a", "Lobby")) == 0, "an ordinary view has no slot");
    auto entry = View("a", "Lobby");
    entry["slot"] = 0; Check(SlotOf(entry) == 0, "slot 0 is no key");
    entry["slot"] = 10; Check(SlotOf(entry) == 0, "slot 10 is no key");
    entry["slot"] = -3; Check(SlotOf(entry) == 0, "a negative slot is no key");
    entry["slot"] = "3"; Check(SlotOf(entry) == 0, "a text slot is not one");
    entry["slot"] = 3.5; Check(SlotOf(entry) == 0, "a fractional slot is not one");
    entry["slot"] = 3; Check(SlotOf(entry) == 3, "slot 3");
    entry["slot"] = 9; Check(SlotOf(entry) == 9, "slot 9, the last key");
    Check(SlotOf(Json()) == 0 && SlotOf(Json::array()) == 0, "not an entry at all");
    Check(Label(3) == "Bookmark 3", "the new entry's name");
    Reject([] { Label(0); });
    Reject([] { Label(10); });
}

void Finding()
{
    auto views = Json::array({ View("a", "Lobby"), View("b", "Roof"), View("c", "Vault") });
    views[1]["slot"] = 4; views[2]["slot"] = 2;
    Check(FindSlot(views, 4) == 1, "slot 4 is the second entry");
    Check(FindSlot(views, 2) == 2, "slot 2 is the third entry");
    Check(FindSlot(views, 1) == -1, "a free slot");
    Check(FindSlot(Json::array(), 5) == -1, "no views");
    Reject([&] { FindSlot(views, 0); });
    Reject([&] { FindSlot(views, 10); });
    Reject([] { FindSlot(Json::object(), 1); });
}

void Restoring()
{
    const auto only = CamerasOnly(View("a", "Lobby"));
    Check(only.at("cameras") == Cameras(0), "the cameras are kept");
    Check(only.at("actors").is_array() && only.at("actors").empty(), "no actor visibility to restore");
    Check(!only.contains("groups"), "hidden groups are left as they are");
    Check(!only.contains("id") && !only.contains("name") && only.size() == 2, "nothing else");
    Reject([] { CamerasOnly(Json::object()); });
}

void Storing()
{
    const auto ordinary = Json::array({ View("a", "Lobby"), View("b", "Roof") });

    // A free slot appends a camera-only entry named after it.
    auto views = Assign(ordinary, 3, Cameras(100), "new1", "500");
    Check(views.size() == 3, "appended");
    const auto& added = views[2];
    Check(added.at("id") == "new1" && added.at("name") == "Bookmark 3" && added.at("modified") == "500", "new id, name and time");
    Check(SlotOf(added) == 3 && added.at("cameras") == Cameras(100), "slot 3 with the cameras");
    Check(added.at("actors").is_array() && added.at("actors").empty() && !added.contains("groups"), "cameras only");
    Check(views[0] == ordinary[0] && views[1] == ordinary[1], "ordinary views untouched by a new bookmark");

    // The same slot again: the entry keeps its id and a renamed name.
    views[2]["name"] = "Atrium overview";
    views = Assign(views, 3, Cameras(200), "new2", "600");
    Check(views.size() == 3, "updated in place, nothing appended");
    Check(views[2].at("id") == "new1" && views[2].at("name") == "Atrium overview", "id and name kept");
    Check(views[2].at("cameras") == Cameras(200) && views[2].at("modified") == "600" && SlotOf(views[2]) == 3, "cameras and time refreshed, slot kept");
    Check(views[0] == ordinary[0] && views[1] == ordinary[1], "ordinary views untouched by an update");

    // Another slot is a second bookmark.
    views = Assign(views, 7, Cameras(300), "new3", "700");
    Check(views.size() == 4 && SlotOf(views[3]) == 7 && FindSlot(views, 3) == 2, "two bookmarks side by side");

    // A slot held twice (a library edited by hand): the first keeps it, the other loses only the slot.
    auto doubled = Json::array({ View("a", "Lobby"), View("b", "Roof"), View("c", "Vault") });
    doubled[1]["slot"] = 5; doubled[2]["slot"] = 5;
    doubled = Assign(doubled, 5, Cameras(400), "new4", "800");
    Check(doubled.size() == 3 && SlotOf(doubled[1]) == 5 && doubled[1].at("id") == "b", "the first holder takes the cameras");
    Check(!doubled[2].contains("slot") && doubled[2].at("name") == "Vault" && doubled[2].at("cameras") == Cameras(0), "the duplicate loses its slot and nothing else");
    Check(doubled[0] == View("a", "Lobby"), "the ordinary view is untouched");

    // An out-of-range slot on an ordinary view is not this key's and stays.
    auto odd = Json::array({ View("a", "Lobby") });
    odd[0]["slot"] = 12;
    odd = Assign(odd, 2, Cameras(500), "new5", "900");
    Check(odd.size() == 2 && odd[0].at("slot") == 12, "a view with no usable slot is left alone");

    // A map with no views list yet.
    Check(Assign(Json(), 1, Cameras(0), "new6", "1").size() == 1, "null views start a list");
    Reject([&] { Assign(ordinary, 0, Cameras(0), "x", "1"); });
    Reject([&] { Assign(ordinary, 10, Cameras(0), "x", "1"); });
    Reject([] { Assign(Json::object(), 1, Cameras(0), "x", "1"); });
    Reject([&] { Assign(ordinary, 1, Json::object(), "x", "1"); });
}

int main()
{
    try
    {
        Slots(); Finding(); Restoring(); Storing();
        std::cout << "View bookmark model tests passed (" << checks << " checks)." << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}
