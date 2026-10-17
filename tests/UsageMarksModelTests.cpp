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

    // A usage that was read-only (unchecked because of it, not by hand) and is
    // replaceable now (its Skins slot holds the asset itself) is marked as
    // Refresh would have marked it, from the scope it captured.
    const Json readOnly = Json::array({Usage("Map.A", "Skins", false), Usage("Map.B", "Skins", false, true), before[1]});
    const std::vector<Mark> readOnlyMarks = {{false, false}, {false, true}, {false, false}};
    after = Json::array({Usage("Map.A", "Skins"), Usage("Map.B", "Skins"), before[1]});
    carried = UsageMarks::Carry(readOnly, readOnlyMarks, after, false);
    Check(Same(carried[0], true, false) && Same(carried[1], true, true), "scope off: a row that became replaceable is checked");
    Check(Same(carried[2], false, false), "a row unchecked by hand stays unchecked");
    carried = UsageMarks::Carry(readOnly, readOnlyMarks, after, true);
    Check(Same(carried[0], false, false), "scope on: a row that became replaceable outside the captured selection is unchecked");
    Check(Same(carried[1], true, true), "scope on: a row that became replaceable inside the captured selection is checked");
    // Still read-only: nothing changes.
    carried = UsageMarks::Carry(readOnly, readOnlyMarks, readOnly, false);
    Check(Same(carried[0], false, false) && Same(carried[1], false, true), "a row that stays read-only stays unchecked");

    // Replaceable, read-only for a listing (a shader using the asset applied,
    // then undone), and replaceable again: a row left out by hand stays out.
    const Json direct = Json::array({Usage("Map.A", "Skins"), Usage("Map.B", "Skins", true, true)});
    const Json shared = Json::array({Usage("Map.A", "Skins", false), Usage("Map.B", "Skins", false, true)});
    const std::vector<Mark> directMarks = {{false, false}, {true, true}};
    auto through = UsageMarks::Carry(direct, directMarks, shared, false);
    Check(Same(through[0], false, false) && through[0].excluded, "a row unchecked by hand is remembered as left out while read-only");
    Check(Same(through[1], false, true) && !through[1].excluded, "a checked row that became read-only is not left out");
    // Still read-only for another listing: the exclusion is kept.
    through = UsageMarks::Carry(shared, through, shared, false);
    Check(through[0].excluded && !through[1].excluded, "the exclusion lasts while the row is read-only");
    carried = UsageMarks::Carry(shared, through, direct, false);
    Check(Same(carried[0], false, false), "a row left out by hand stays unchecked once replaceable again");
    Check(Same(carried[1], true, true), "a row that was checked comes back checked once replaceable again");
    Check(carried[0].excluded, "the exclusion stays with the unchecked row");
    // Checked by hand while read-only: the exclusion is taken back.
    auto handChecked = UsageMarks::Carry(direct, directMarks, shared, false);
    handChecked[0].checked = true;
    carried = UsageMarks::Carry(shared, handChecked, direct, false);
    Check(Same(carried[0], true, false) && !carried[0].excluded, "checking a read-only row takes its exclusion back");
    // Refresh forgets the exclusion.
    Check(!UsageMarks::Default(direct[0], false).excluded, "Refresh marks a row afresh");
}

void Returning()
{
    // A's mesh row unchecked by hand. Another mesh set on A, or A deleted,
    // takes it off the list with B's checked row; Undo brings them back.
    const Json three = Json::array({Usage("Map.A", "StaticMesh"), Usage("Map.B", "StaticMesh"), Usage("Map.C", "StaticMesh", true, true)});
    const std::vector<Mark> marks = {{false, false}, {true, false}, {true, true}};
    const Json onlyC = Json::array({three[2]});
    std::multiset<std::string> away;
    auto gone = UsageMarks::Carry(three, marks, onlyC, false, false, &away);
    Check(gone.size() == 1 && Same(gone[0], true, true), "the row still listed keeps its marks");
    Check(away.size() == 1 && away.count(UsageMarks::Key(three[0])) == 1, "a row left out by hand is remembered when it leaves the list");
    auto carried = UsageMarks::Carry(onlyC, gone, three, false, false, &away);
    Check(Same(carried[0], false, false) && carried[0].excluded, "a row left out by hand comes back unchecked");
    Check(Same(carried[1], true, false) && !carried[1].excluded, "a checked row that left the list comes back as a new one");
    Check(Same(carried[2], true, true), "the row that stayed keeps its marks");
    Check(away.empty(), "the row that came back takes its entry");
    carried = UsageMarks::Carry(three, carried, three, false, false, &away);
    Check(Same(carried[0], false, false) && Same(carried[1], true, false), "it stays unchecked at the next listing");
    // Scope on: it comes back unchecked as any new row does, and still left out.
    away = {UsageMarks::Key(three[0])};
    carried = UsageMarks::Carry(onlyC, gone, three, true, false, &away);
    Check(Same(carried[0], false, false) && carried[0].excluded, "scope on: a row left out by hand comes back left out");

    // Back read-only (its slot holds a shader using the asset), then replaceable:
    // still left out.
    away = {UsageMarks::Key(Usage("Map.A", "Skins[0]"))};
    const Json sharedA = Json::array({Usage("Map.A", "Skins[0]", false)});
    const Json directA = Json::array({Usage("Map.A", "Skins[0]")});
    carried = UsageMarks::Carry(Json::array(), {}, sharedA, false, false, &away);
    Check(Same(carried[0], false, false) && carried[0].excluded, "a row left out by hand comes back read-only and left out");
    carried = UsageMarks::Carry(sharedA, carried, directA, false, false, &away);
    Check(Same(carried[0], false, false), "it stays unchecked once it can be replaced");

    // Without the list of rows away, a row that comes back is a new one.
    carried = UsageMarks::Carry(onlyC, gone, three, false);
    Check(Same(carried[0], true, false), "nothing remembered: a returning row is new");

    // One entry per row: two left out with the same key, one comes back.
    const Json twice = Json::array({Surface(7), Surface(7), Surface(8)});
    away.clear();
    gone = UsageMarks::Carry(twice, {{false, false}, {false, false}, {true, false}}, Json::array({Surface(8)}), false, false, &away);
    Check(away.count(UsageMarks::Key(Surface(7))) == 2, "each row left out has its own entry");
    carried = UsageMarks::Carry(Json::array({Surface(8)}), gone, Json::array({Surface(7), Surface(8)}), false, false, &away);
    Check(Same(carried[0], false, false) && Same(carried[1], true, false) && away.size() == 1, "a row that comes back takes one entry");

    // A build drops the surfaces away: their numbers name other faces now, so
    // the listed surfaces come in left out. Actor rows away stay remembered.
    const Json brush = {{"path", "Map.Brush4"}, {"class", "Engine.Brush"}};
    away = {UsageMarks::Key(Surface(12, brush)), UsageMarks::Key(three[0])};
    const Json faces = Json::array({Surface(10, brush), Surface(11, brush)});
    const Json moved = Json::array({Surface(12, brush), Surface(13, brush)});
    carried = UsageMarks::Carry(faces, {{true, false}, {true, false}}, moved, false, true, &away);
    Check(Same(carried[0], false, false) && Same(carried[1], false, false) && carried[0].excluded && carried[1].excluded,
          "after a build, a face left out that had left the list leaves every surface row unchecked");
    Check(away.size() == 1 && away.count(UsageMarks::Key(three[0])) == 1, "a build forgets the surfaces away, not the actor rows");
    carried = UsageMarks::Carry(faces, {{true, false}, {true, false}}, moved, false, false, &away);
    Check(Same(carried[0], true, false) && Same(carried[1], true, false), "without a build an actor row away does not reach the faces");
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

    // Face 12 was left out by hand and which face it is now cannot be told, so
    // no surface row comes back checked.
    carried = UsageMarks::Carry(before, marks, after, false, true);
    Check(carried.size() == 4, "one mark per row after a build");
    Check(Same(carried[0], false, false), "after a build an actor row keeps its marks");
    Check(Same(carried[1], false, false) && Same(carried[2], false, false) && Same(carried[3], false, false), "after a build, a surface unchecked by hand leaves every surface row unchecked");
    carried = UsageMarks::Carry(before, marks, after, true, true);
    Check(Same(carried[1], false, false) && Same(carried[2], false, false) && Same(carried[3], false, false), "after a build, scope on: surface rows are unchecked and unscoped");
    // The same numbers after a build still name new faces.
    carried = UsageMarks::Carry(before, marks, before, false, true);
    Check(Same(carried[0], false, false) && Same(carried[1], false, false) && Same(carried[2], false, false) && Same(carried[3], false, false),
          "after a build a surface number is not matched");
    // Nothing left out, and face 11's old mark scoped: a match would keep the
    // scope, a new face is unscoped.
    const Json scopedFaces = Json::array({Surface(10, brush), Surface(11, brush)});
    const std::vector<Mark> scopedMarks = {{true, false}, {true, true}};
    const Json movedFaces = Json::array({Surface(11, brush), Surface(12, brush)});
    carried = UsageMarks::Carry(scopedFaces, scopedMarks, movedFaces, false, true);
    Check(Same(carried[0], true, false) && Same(carried[1], true, false), "after a build, scope off: a reused surface number comes in as a new face");
    carried = UsageMarks::Carry(scopedFaces, scopedMarks, movedFaces, true, true);
    Check(Same(carried[0], false, false) && Same(carried[1], false, false), "after a build, scope on: a reused surface number is outside the captured selection");
    carried = UsageMarks::Carry(scopedFaces, scopedMarks, movedFaces, true);
    Check(Same(carried[0], true, true) && Same(carried[1], false, false), "without a build a surface number keeps its scope");

    // Every replaceable surface was checked: an unchecked actor row, or a
    // surface unchecked only because it is read-only, leaves nothing out.
    Json shared = Surface(9, brush);
    shared["replaceable"] = false;
    const Json allChecked = Json::array({Usage("Map.A", "StaticMesh"), shared, Surface(10, brush), Surface(11, brush)});
    const std::vector<Mark> allMarks = {{false, false}, {false, false}, {true, false}, {true, false}};
    carried = UsageMarks::Carry(allChecked, allMarks, after, false, true);
    Check(Same(carried[0], false, false), "the unchecked actor row keeps its mark");
    Check(Same(carried[1], true, false) && Same(carried[2], true, false) && Same(carried[3], true, false), "after a build, scope off: surfaces come back checked when none was left out");
    carried = UsageMarks::Carry(allChecked, allMarks, after, true, true);
    Check(Same(carried[1], false, false) && Same(carried[2], false, false) && Same(carried[3], false, false), "after a build, scope on: surfaces come back unchecked");
    // Without a build an unchecked surface only keeps its own mark, and a face
    // that appeared since comes in checked.
    Json withNewFace = before;
    withNewFace.push_back(Surface(15, brush));
    carried = UsageMarks::Carry(before, marks, withNewFace, false);
    Check(Same(carried[1], true, false) && Same(carried[3], false, false), "without a build only the unchecked surface stays unchecked");
    Check(Same(carried[4], true, false), "without a build a new surface is checked");
    // A face left out by hand, then read-only when the build ran, still leaves
    // every surface row unchecked.
    Json sharedFace = Surface(12, brush);
    sharedFace["replaceable"] = false;
    const Json leftShared = Json::array({Surface(10, brush), Surface(11, brush), sharedFace});
    const std::vector<Mark> leftSharedMarks = {{true, false}, {true, false}, {false, false, true}};
    carried = UsageMarks::Carry(leftShared, leftSharedMarks, after, false, true);
    Check(Same(carried[1], false, false) && Same(carried[2], false, false) && Same(carried[3], false, false), "after a build, a surface left out while read-only leaves every surface row unchecked");

    // Faces 10 to 12, 12 unchecked by hand; a shader using the asset on 12; a
    // build moves them to 12 to 14, 14 (the old 12) read-only; 14 replaceable
    // again: the face left out stays unchecked, and so do the others.
    const Json faces = Json::array({Surface(10, brush), Surface(11, brush), Surface(12, brush)});
    Json shaded = faces;
    shaded[2]["replaceable"] = false;
    Json built14 = Json::array({Surface(12, brush), Surface(13, brush), Surface(14, brush)});
    built14[2]["replaceable"] = false;
    Json direct14 = built14;
    direct14[2]["replaceable"] = true;
    auto spell = UsageMarks::Carry(faces, {{true, false}, {true, false}, {false, false}}, shaded, false);
    Check(Same(spell[2], false, false) && spell[2].excluded, "the face left out is remembered while read-only");
    spell = UsageMarks::Carry(shaded, spell, built14, false, true);
    Check(Same(spell[0], false, false) && Same(spell[1], false, false) && Same(spell[2], false, false), "after the build every surface row is unchecked");
    Check(spell[0].excluded && spell[1].excluded && spell[2].excluded, "after the build every surface row counts as left out");
    spell = UsageMarks::Carry(built14, spell, direct14, false);
    Check(Same(spell[0], false, false) && Same(spell[1], false, false) && Same(spell[2], false, false), "a face read-only at the build stays unchecked once it can be replaced");
    // A face left out before a build with nothing read-only: the same.
    spell = UsageMarks::Carry(faces, {{true, false}, {true, false}, {false, false}}, built14, false, true);
    Check(spell[2].excluded, "a read-only face after a build that left one out counts as left out");
    // Nothing left out: a read-only face after a build comes back checked once it
    // can be replaced, as a new usage does.
    spell = UsageMarks::Carry(faces, {{true, false}, {true, false}, {true, false}}, built14, false, true);
    Check(!spell[2].excluded, "a build that left nothing out marks nothing left out");
    spell = UsageMarks::Carry(built14, spell, direct14, false);
    Check(Same(spell[2], true, false), "a read-only face comes back checked when nothing was left out");
    // A surface left out by hand does not reach a new actor row after a build.
    const Json withMesh = Json::array({after[1], Usage("Map.F", "StaticMesh")});
    carried = UsageMarks::Carry(before, marks, withMesh, false, true);
    Check(Same(carried[0], false, false) && Same(carried[1], true, false), "after a build a new actor row is checked as usual");
}

int main()
{
    try
    {
        Keys();
        Defaults();
        Carrying();
        Returning();
        Rebuilding();
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Usage marks model tests passed (" << checks << " checks)\n";
}
