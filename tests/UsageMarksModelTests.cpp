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

    // A usage that became read-only (its Skins slot now holds a shader using the
    // asset) loses its check but keeps its scope.
    after = Json::array({Usage("Map.A", "StaticMesh", false), before[1], before[2]});
    carried = UsageMarks::Carry(before, marks, after, false);
    Check(Same(carried[0], false, true), "a carried row that became read-only is unchecked");
    Check(Same(carried[1], false, false) && Same(carried[2], true, false), "the other rows keep their marks");
}

void Rebuilding()
{
    // Brush A's faces 10 to 12, face 12 unchecked by hand, and an actor row unchecked.
    const Json brush = {{"path", "Map.Brush4"}, {"class", "Engine.Brush"}};
    const Json before = Json::array({Usage("Map.A", "StaticMesh"), Surface(10, brush), Surface(11, brush), Surface(12, brush)});
    const std::vector<Mark> marks = {{false, false}, {true, false}, {true, false}, {false, false}};
    // A build that adds faces earlier in the BSP moves them to 12 to 14.
    const Json after = Json::array({Usage("Map.A", "StaticMesh"), Surface(12, brush), Surface(13, brush), Surface(14, brush)});

    // Carried by number, face 12's unchecked mark would land on what was face 10.
    auto carried = UsageMarks::Carry(before, marks, after, false);
    Check(Same(carried[1], false, false), "without a build the numbers are trusted");

    carried = UsageMarks::Carry(before, marks, after, false, true);
    Check(carried.size() == 4, "one mark per row after a build");
    Check(Same(carried[0], false, false), "after a build an actor row keeps its marks");
    Check(Same(carried[1], true, false) && Same(carried[2], true, false) && Same(carried[3], true, false), "after a build, scope off: every surface row is checked again");
    carried = UsageMarks::Carry(before, marks, after, true, true);
    Check(Same(carried[1], false, false) && Same(carried[2], false, false) && Same(carried[3], false, false), "after a build, scope on: surface rows are unchecked and unscoped");
    // The same numbers after a build still name new faces.
    carried = UsageMarks::Carry(before, marks, before, false, true);
    Check(Same(carried[0], false, false) && Same(carried[3], true, false), "after a build a surface number is not matched");
}

int main()
{
    try
    {
        Keys();
        Defaults();
        Carrying();
        Rebuilding();
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Usage marks model tests passed (" << checks << " checks)\n";
}
