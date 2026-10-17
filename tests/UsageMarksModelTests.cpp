#include "../Reloaded.Editor/UsageMarksModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace Workflow;
using UsageMarks::Mark;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
Json Usage(const char* path, const char* property, bool replaceable = true, bool selected = false)
{
    return {{"actor", {{"path", path}, {"class", "Engine.StaticMeshActor"}}}, {"property", property}, {"direct", replaceable},
            {"replaceable", replaceable}, {"selected", selected}, {"surface", -1}};
}
Json Surface(int index, const Json& actor = Json{})
{
    return {{"actor", actor}, {"property", "BSP surface " + std::to_string(index)}, {"surface", index}, {"direct", true},
            {"replaceable", true}, {"selected", false}};
}
bool Same(const Mark& mark, bool checked, bool scoped) { return mark.checked == checked && mark.scoped == scoped; }

void Keys()
{
    Check(UsageMarks::Key(Usage("Map.StaticMeshActor3", "StaticMesh")) == "map.staticmeshactor3|StaticMesh|-1", "actor row key");
    Check(UsageMarks::Key(Usage("MAP.StaticMeshActor3", "StaticMesh")) == UsageMarks::Key(Usage("map.staticmeshactor3", "StaticMesh")), "actor paths ignore case");
    Check(UsageMarks::Key(Usage("Map.A", "StaticMesh")) != UsageMarks::Key(Usage("Map.A", "Skins")), "another property is another usage");
    Check(UsageMarks::Key(Surface(12)) == "bsp|BSP surface 12|12", "a BSP row without an actor");
    Check(UsageMarks::Key(Surface(12)) != UsageMarks::Key(Surface(13)), "surface numbers tell BSP rows apart");
    const Json brush = {{"path", "Map.Brush4"}, {"class", "Engine.Brush"}};
    Check(UsageMarks::Key(Surface(12, brush)) == "map.brush4|BSP surface 12|12", "a BSP row keyed by its brush");
}

void Defaults()
{
    Check(Same(UsageMarks::Default(Usage("Map.A", "StaticMesh"), false), true, false), "replaceable, scope off: checked");
    Check(Same(UsageMarks::Default(Usage("Map.A", "StaticMesh", false), false), false, false), "read-only: unchecked");
    Check(Same(UsageMarks::Default(Usage("Map.A", "StaticMesh", true, true), false), true, true), "selected rows are scoped");
    Check(Same(UsageMarks::Default(Usage("Map.A", "StaticMesh"), true), false, false), "scope on: unselected rows unchecked");
    Check(Same(UsageMarks::Default(Usage("Map.A", "StaticMesh", true, true), true), true, true), "scope on: selected rows checked");
    Check(Same(UsageMarks::Default(Usage("Map.A", "StaticMesh", false, true), true), false, true), "scope on: read-only stays unchecked");
}

void Carrying()
{
    const Json before = Json::array({Usage("Map.A", "StaticMesh"), Usage("Map.B", "StaticMesh"), Usage("Map.C", "StaticMesh")});
    const std::vector<Mark> marks = {{true, true}, {false, false}, {true, false}};

    // Listed again in another order: each keeps its own marks, the unchecked one unchecked.
    auto after = Json::array({before[2], before[0], before[1]});
    auto carried = UsageMarks::Carry(before, marks, after, false);
    Check(carried.size() == 3, "one mark per row");
    Check(Same(carried[0], true, false) && Same(carried[1], true, true) && Same(carried[2], false, false), "marks follow reordered rows");
    // The row's own selected flag is not read again: a click since Refresh does not scope it.
    after[2]["selected"] = true;
    Check(Same(UsageMarks::Carry(before, marks, after, true)[2], false, false), "a carried row ignores the live selection");

    // A usage that appeared since: outside the captured selection.
    after = Json::array({before[0], Usage("Map.D", "StaticMesh", true, true), Usage("Map.E", "Skins", false)});
    carried = UsageMarks::Carry(before, marks, after, true);
    Check(Same(carried[1], false, false), "scope on: a new row is unchecked and unscoped");
    carried = UsageMarks::Carry(before, marks, after, false);
    Check(Same(carried[1], true, false), "scope off: a new replaceable row is checked");
    Check(Same(carried[2], false, false), "a new read-only row is unchecked");

    // Rows that went (replaced, or the actor deleted) leave no mark behind.
    after = Json::array({before[0], before[2]});
    carried = UsageMarks::Carry(before, marks, after, false);
    Check(carried.size() == 2 && Same(carried[0], true, true) && Same(carried[1], true, false), "removed rows vanish");
    Check(UsageMarks::Carry(before, marks, Json::array(), false).empty(), "nothing listed, nothing marked");

    // The same key twice pairs up in order; a third copy is new.
    const Json twice = Json::array({Surface(7), Surface(7)});
    carried = UsageMarks::Carry(twice, {{false, false}, {true, false}}, Json::array({Surface(7), Surface(7), Surface(7)}), false);
    Check(Same(carried[0], false, false) && Same(carried[1], true, false) && Same(carried[2], true, false), "duplicate keys pair up in order");

    // Fewer marks than rows: the rows without one count as not listed.
    carried = UsageMarks::Carry(before, {{false, false}}, before, false);
    Check(Same(carried[0], false, false) && Same(carried[1], true, false) && Same(carried[2], true, false), "rows without marks are new");
}

int main()
{
    try
    {
        Keys();
        Defaults();
        Carrying();
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Usage marks model tests passed (" << checks << " checks)\n";
}
