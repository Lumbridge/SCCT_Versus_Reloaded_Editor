#include "../Reloaded.Editor/MapUsagesModel.h"
#include <iostream>
#include <source_location>
#include <string>
using namespace MapUsages;
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

// A package writer independent of the header under test, in the SCCT layout:
// version 300, names XOR-coded by position, every name reference followed by
// a number that tag sizes do not count, tables after the export data.
struct Builder
{
    std::vector<std::string> names;
    static void Compact(Bytes& out, int v)
    {
        unsigned a = static_cast<unsigned>(v < 0 ? -v : v);
        std::uint8_t first = static_cast<std::uint8_t>((v < 0 ? 0x80 : 0) | (a & 0x3f));
        a >>= 6;
        if (a) first |= 0x40;
        out.push_back(first);
        while (a) { std::uint8_t c = a & 0x7f; a >>= 7; if (a) c |= 0x80; out.push_back(c); }
    }
    static void U32(Bytes& out, std::uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
    int Name(const std::string& n)
    {
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return static_cast<int>(i);
        names.push_back(n);
        return static_cast<int>(names.size() - 1);
    }
    // A four-byte number like the ones the editor writes, so a reader that
    // counted it against the tag size would be caught.
    void NameRef(Bytes& out, const std::string& n) { Compact(out, Name(n)); Compact(out, 0x13a2d9); }
    // Tag header; size counts value bytes without name numbers.
    void Tag(Bytes& out, const std::string& name, int type, size_t size, const std::string& structName = {})
    {
        NameRef(out, name);
        const int code = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : size == 12 ? 3 : size == 16 ? 4 : 5;
        out.push_back(static_cast<std::uint8_t>(type | (code << 4)));
        if (type == 10) NameRef(out, structName);
        if (code == 5) out.push_back(static_cast<std::uint8_t>(size));
    }
    void ObjectTag(Bytes& out, const std::string& name, int object)
    {
        Bytes value; Compact(value, object);
        Tag(out, name, 5, value.size());
        out.insert(out.end(), value.begin(), value.end());
    }
};
struct ImportEntry { std::string classPackage, className, name; int outer; };
struct ExportEntry { int classIndex; std::string name; std::uint32_t flags; Bytes blob; };
Bytes Assemble(Builder& w, const std::vector<ImportEntry>& imports, const std::vector<ExportEntry>& exports)
{
    w.Name("None");
    for (const auto& i : imports) { w.Name(i.classPackage); w.Name(i.className); w.Name(i.name); }
    for (const auto& e : exports) w.Name(e.name);
    Bytes nameTable;
    size_t position = 64;
    for (const auto& n : w.names)
    {
        Bytes entry;
        Builder::Compact(entry, static_cast<int>(n.size() + 1));
        position += entry.size();
        for (size_t i = 0; i <= n.size(); ++i) { const std::uint8_t c = i < n.size() ? static_cast<std::uint8_t>(n[i]) : 0; entry.push_back(static_cast<std::uint8_t>(c ^ (position & 255))); ++position; }
        Builder::U32(entry, 0x70010);
        position += 4;
        nameTable.insert(nameTable.end(), entry.begin(), entry.end());
    }
    Bytes data;
    std::vector<std::uint32_t> offsets;
    const auto dataStart = static_cast<std::uint32_t>(64 + nameTable.size());
    for (const auto& e : exports) { offsets.push_back(dataStart + static_cast<std::uint32_t>(data.size())); data.insert(data.end(), e.blob.begin(), e.blob.end()); }
    Bytes importTable;
    for (const auto& i : imports)
    {
        w.NameRef(importTable, i.classPackage); w.NameRef(importTable, i.className);
        Builder::U32(importTable, static_cast<std::uint32_t>(i.outer));
        w.NameRef(importTable, i.name);
    }
    Bytes exportTable;
    for (size_t i = 0; i < exports.size(); ++i)
    {
        Builder::Compact(exportTable, exports[i].classIndex);
        Builder::Compact(exportTable, 0);
        Builder::U32(exportTable, 0);
        w.NameRef(exportTable, exports[i].name);
        Builder::U32(exportTable, exports[i].flags);
        Builder::Compact(exportTable, static_cast<int>(exports[i].blob.size()));
        if (!exports[i].blob.empty()) Builder::Compact(exportTable, static_cast<int>(offsets[i]));
    }
    const auto importOffset = dataStart + static_cast<std::uint32_t>(data.size());
    const auto exportOffset = importOffset + static_cast<std::uint32_t>(importTable.size());
    Bytes b;
    Builder::U32(b, 0x9e2a83c1);
    b.push_back(44); b.push_back(1); b.push_back(0); b.push_back(0); // Version 300, licensee 0.
    Builder::U32(b, 0);
    for (auto v : { static_cast<std::uint32_t>(w.names.size()), 64u, static_cast<std::uint32_t>(exports.size()), exportOffset, static_cast<std::uint32_t>(imports.size()), importOffset })
        Builder::U32(b, v);
    b.resize(64, 0);
    b.insert(b.end(), nameTable.begin(), nameTable.end());
    b.insert(b.end(), data.begin(), data.end());
    b.insert(b.end(), importTable.begin(), importTable.end());
    b.insert(b.end(), exportTable.begin(), exportTable.end());
    return b;
}

// Imports, numbered as the package refers to them:
//  -1 Engine (package)            -2 Engine.StaticMeshActor (class)
//  -3 Shared (package)            -4 Shared.Walls (package)
//  -5 Shared.Walls.Brick (texture) -6 Shared.Walls.Pillar (static mesh)
//  -7 Other (package)             -8 Other.Walls (package)
//  -9 Other.Walls.Brick (texture) -10 Engine.Shader (class)
//  -11 Shared.Third Floor (package) -12 Shared.Third Floor.Tank (mesh)
//  -13 Engine.Brush (class)       -14 Shared.Walls.Unused (texture)
//  -15 Engine.Polys (class)
std::vector<ImportEntry> Imports()
{
    return {
        {"Core", "Package", "Engine", 0}, {"Core", "Class", "StaticMeshActor", -1},
        {"Core", "Package", "Shared", 0}, {"Core", "Package", "Walls", -3},
        {"Engine", "Texture", "Brick", -4}, {"Engine", "StaticMesh", "Pillar", -4},
        {"Core", "Package", "Other", 0}, {"Core", "Package", "Walls", -7},
        {"Engine", "Texture", "Brick", -8}, {"Core", "Class", "Shader", -1},
        {"Core", "Package", "Third Floor", -3}, {"Engine", "StaticMesh", "Tank", -11},
        {"Core", "Class", "Brush", -1}, {"Engine", "Texture", "Unused", -4},
        {"Core", "Class", "Polys", -1},
    };
}
// An actor as the editor saves one: state frame, then tags.
Bytes Actor(Builder& w, int mesh, bool skins)
{
    Bytes b;
    Builder::Compact(b, -2); Builder::Compact(b, -2);
    for (int i = 0; i < 8; ++i) b.push_back(0xff);
    Builder::U32(b, 0x20);
    Builder::Compact(b, -1); // Code offset, present because the node is set.
    w.Tag(b, "Location", 10, 12, "Vector"); for (int i = 0; i < 12; ++i) b.push_back(0);
    w.ObjectTag(b, "StaticMesh", mesh);
    w.ObjectTag(b, "Level", 1);
    // Region: a tagged struct whose nested name numbers the size leaves out.
    {
        Bytes inner;
        w.ObjectTag(inner, "Zone", 1);
        w.Tag(inner, "iLeaf", 2, 4); Builder::U32(inner, 7);
        w.Tag(inner, "ZoneNumber", 1, 1); inner.push_back(1);
        w.NameRef(inner, "None");
        const size_t logical = 3 + 6 + 3 + 1; // Zone, iLeaf, ZoneNumber, None without their 4-byte numbers.
        w.Tag(b, "Region", 10, logical, "PointRegion");
        b.insert(b.end(), inner.begin(), inner.end());
    }
    w.Tag(b, "bHidden", 3, 0 + 1); // Bool, no value bytes.
    if (skins)
    {
        Bytes value; Builder::Compact(value, 2); Builder::Compact(value, -5); Builder::Compact(value, -9);
        w.Tag(b, "Skins", 9, value.size()); b.insert(b.end(), value.begin(), value.end());
    }
    // Tag: a name value, whose size of 1 leaves out its number.
    w.Tag(b, "Tag", 6, 1); w.NameRef(b, "Pillar");
    // An array of names: count + name references.
    {
        Bytes value; Builder::Compact(value, 1); w.NameRef(value, "Brick");
        w.Tag(b, "Groups", 9, 2); b.insert(b.end(), value.begin(), value.end());
    }
    // An array of tagged structs holding an object.
    {
        Bytes value; Builder::Compact(value, 1); w.ObjectTag(value, "Material", -5); w.NameRef(value, "None");
        w.Tag(b, "Layers", 9, 1 + 3 + 1); // Count, the Material tag, None.
        b.insert(b.end(), value.begin(), value.end());
    }
    w.Tag(b, "Label", 13, 4); b.push_back(3); b.push_back('a'); b.push_back('b'); b.push_back(0);
    w.NameRef(b, "None");
    b.push_back(0x55); // Native data after the list is not read.
    return b;
}

void TestSplitPath()
{
    auto parts = SplitPath("  Shared.Walls.Brick ");
    Check(parts.size() == 3 && parts[0] == "Shared" && parts[2] == "Brick", "path splits on dots");
    parts = SplitPath("Texture'Shared.Walls.Brick'");
    Check(parts.size() == 3 && parts[1] == "Walls", "class-quoted path is unwrapped");
    parts = SplitPath("Oilrig_SM.Third Floor.oiltanks");
    Check(parts.size() == 3 && parts[1] == "Third Floor", "groups may contain spaces");
    Reject([] { SplitPath(""); });
    Reject([] { SplitPath("Shared..Brick"); });
    Reject([] { SplitPath("Shared.Walls."); });
    Reject([] { SplitPath("Shared.Wa\"lls"); });
}

void TestMatching()
{
    Builder w;
    const auto bytes = Assemble(w, Imports(), { {-2, "StaticMeshActor0", 0x02070001, Actor(w, -6, false)} });
    const auto p = Snapshot::Parse(bytes);
    Check(Join(Chain(p, -5)) == "Shared.Walls.Brick", "import chain");
    Check(Join(Chain(p, -12)) == "Shared.Third Floor.Tank", "import chain with a spaced group");
    auto m = MatchImports(p, SplitPath("Shared.Walls.Brick"));
    Check(m.size() == 1 && m[0] == 4, "full name matches its import only");
    m = MatchImports(p, SplitPath("shared.WALLS.brick"));
    Check(m.size() == 1 && m[0] == 4, "matching ignores case");
    Check(MatchImports(p, SplitPath("Brick")).empty(), "a bare object name is not a match");
    Check(MatchImports(p, SplitPath("Walls.Brick")).empty(), "a chain must start at the package");
    Check(MatchImports(p, SplitPath("Shared.Brick")).empty(), "a skipped group is not a match");
    Check(MatchImports(p, SplitPath("Shared.Walls.Brick.Extra")).empty(), "a longer chain is not a match");
    Check(MatchImports(p, SplitPath("Shared.Walls.Bric")).empty(), "a name prefix is not a match");
    m = MatchImports(p, SplitPath("Shared.Walls"));
    Check(m.size() == 4, "a group matches itself and its three objects");
    m = MatchImports(p, SplitPath("Shared"));
    Check(m.size() == 7, "a package matches itself and everything inside");
    m = MatchImports(p, SplitPath("Other.Walls.Brick"));
    Check(m.size() == 1 && m[0] == 8, "a same-named object in another package is distinct");
    Check(MatchImports(p, SplitPath("Missing.Thing")).empty(), "absent package");
}

void TestScan()
{
    Builder w;
    Bytes shader;
    w.ObjectTag(shader, "Diffuse", -5);
    w.NameRef(shader, "None");
    Bytes broken; // Tag with an unknown name index: the export cannot be read.
    Builder::Compact(broken, 5000); Builder::Compact(broken, 0);
    Bytes brush; w.NameRef(brush, "None"); brush.push_back(0x85); // Native brush data is not read.
    // A brush's faces: two quads, one textured with the brick, one with none.
    Bytes faces; w.NameRef(faces, "None");
    Builder::U32(faces, 2); Builder::U32(faces, 2);
    for (int material : { -5, 0 })
    {
        Builder::Compact(faces, 4);
        for (int i = 0; i < 4 * 3 + 4 * 3; ++i) Builder::U32(faces, 0x42000000); // Base, normal, U, V, four vertices.
        Builder::U32(faces, 0x10); // Flags.
        Builder::Compact(faces, 0); // Actor.
        Builder::Compact(faces, material);
        w.NameRef(faces, "None"); // Item name.
        Builder::Compact(faces, material ? 0 : 1); Builder::Compact(faces, -1); // Link, brush poly.
        Builder::U32(faces, 0x42000000); Builder::U32(faces, 0x42000000);
    }
    Bytes badFaces = faces; badFaces.push_back(0); // One byte more than the faces account for.
    const auto bytes = Assemble(w, Imports(), {
        {-2, "StaticMeshActor0", 0x02070001, Actor(w, -6, true)},
        {-2, "StaticMeshActor1", 0x02070001, Actor(w, -6, false)},
        {-2, "StaticMeshActor2", 0x02070001, Actor(w, -12, false)},
        {-10, "Shader0", 0x70004, shader},
        {-13, "Brush0", 0x70004, brush},
        {-10, "Shader1", 0x70004, broken},
        {-10, "Shader2", 0x70004, {}},
        {-15, "Polys0", 0x340001, faces},
    });

    auto r = Scan(bytes, SplitPath("Shared.Walls.Pillar"));
    Check(r.Found() && r.objects.size() == 1 && r.objects[0] == "Shared.Walls.Pillar", "mesh found");
    Check(r.references == 2 && r.users.at("StaticMeshActor.StaticMesh") == 2, "two actors use the mesh");
    Check(r.unparsed == 1, "one export could not be read");
    Check(r.unreferenced.empty(), "the mesh is referenced");
    Check(Summary(r) == "StaticMeshActor.StaticMesh x2", "summary");

    r = Scan(bytes, SplitPath("Shared.Walls.Brick"));
    Check(r.references == 6, "texture: one skin, three struct-array layers, one shader, one brush face");
    Check(r.users.at("StaticMeshActor.Layers.Material") == 3 && r.users.at("StaticMeshActor.Skins") == 1 && r.users.at("Shader.Diffuse") == 1 && r.users.at(kBrushFaces) == 1, "texture users by class and property");
    Check(Summary(r) == "StaticMeshActor.Layers.Material x3, Brush faces x1, Shader.Diffuse x1, StaticMeshActor.Skins x1", "summary order: most used first, then by name");
    {
        Builder v;
        const auto damaged = Assemble(v, Imports(), { {-15, "Polys0", 0x340001, badFaces} });
        const auto q = Snapshot::Parse(damaged);
        std::vector<std::pair<std::string, int>> refs;
        Check(!PropertyReferences(damaged, q, q.exports[0], refs, true) && refs.empty(), "faces that do not fill their export are refused");
    }

    r = Scan(bytes, SplitPath("Other.Walls.Brick"));
    Check(r.references == 1 && r.users.at("StaticMeshActor.Skins") == 1, "array element in another package");

    r = Scan(bytes, SplitPath("Shared.Walls.Unused"));
    Check(r.Found() && r.references == 0 && r.unreferenced.size() == 1, "imported but in no property list");
    Check(Summary(r) == "only in native data (built BSP surfaces, a map-local mesh)", "native-only usage is explained");

    r = Scan(bytes, SplitPath("Shared"));
    Check(r.objects.size() == 4 && r.packages == 3 && r.references == 2 + 1 + 6, "whole package: pillar x2, tank x1, brick x6");
    Check(r.unreferenced.size() == 1 && r.unreferenced[0] == "Shared.Walls.Unused", "only the unused texture is unreferenced; packages and groups are not counted");
    Check(Summary(r).find("; 1 object(s) only in native data") != std::string::npos, "package summary");
    r = Scan(bytes, SplitPath("Shared.Third Floor"));
    Check(r.Found() && r.objects.size() == 1 && r.users.at("StaticMeshActor.StaticMesh") == 1, "a group with a space");

    r = Scan(bytes, SplitPath("Missing.Thing"));
    Check(!r.Found() && r.references == 0 && r.unparsed == 0, "no match reads no properties");

    // The tag reader must not be fooled by name numbers: a size that counted
    // them would be wrong by four bytes per name reference.
    Builder plain;
    const auto single = Assemble(plain, Imports(), { {-2, "A", 0x02070001, Actor(plain, -6, true)} });
    const auto p = Snapshot::Parse(single);
    std::vector<std::pair<std::string, int>> refs;
    Check(PropertyReferences(single, p, p.exports[0], refs), "actor properties parse");
    std::vector<std::pair<std::string, int>> expected = { {"StaticMesh", -6}, {"Level", 1}, {"Region.Zone", 1}, {"Skins", -5}, {"Skins", -9},
        {"Layers.Material", -5} };
    Check(refs == expected, "references in order, with struct members");

    // Truncated and foreign files are refused, not misread.
    Bytes cut(bytes.begin(), bytes.begin() + bytes.size() / 2);
    Reject([&] { Scan(cut, SplitPath("Shared")); });
    Bytes junk(200, 0x41);
    Reject([&] { Scan(junk, SplitPath("Shared")); });
}

void TestReport()
{
    Row a{ "AquaD.sdc", "MapsEd", {}, {}, "x" };
    a.result.objects = { "Shared.Walls.Pillar" };
    a.result.users["StaticMeshActor.StaticMesh"] = 4;
    a.result.references = 4;
    Row b{ "BankD.sdc", "MapsEd", {}, {}, "y" };
    b.result.objects = { "Shared.Walls.Pillar" };
    b.result.users["StaticMeshActor.StaticMesh"] = 9;
    b.result.references = 9;
    Row c{ "Bad.sdc", "Maps", {}, "a compressed block is damaged", "z" };
    std::vector<Row> rows = { c, a, b };
    std::sort(rows.begin(), rows.end(), RowOrder);
    Check(rows[0].file == "BankD.sdc" && rows[1].file == "AquaD.sdc" && rows[2].file == "Bad.sdc", "most references first, unreadable last");
    const auto text = Report("Shared.Walls.Pillar", rows, 12);
    Check(text.rfind("Usages of Shared.Walls.Pillar: 2 of 12 maps\r\n", 0) == 0, "report header");
    Check(text.find("BankD.sdc\tMapsEd\t9\t1\tStaticMeshActor.StaticMesh x9\r\n") != std::string::npos, "report row");
    Check(text.find("Bad.sdc\tMaps\t-\t-\tCould not read: a compressed block is damaged\r\n") != std::string::npos, "unreadable row");
}

void TestFiles()
{
    Check(ScanFile("AquaD.sdc", false) && ScanFile("aquad.SDC", false), ".sdc maps are scanned");
    Check(!ScanFile("Auto3.sdc", false) && ScanFile("Auto3.sdc", true), "autosaves only when asked");
    Check(ScanFile("Autoplay.sdc", false) && ScanFile("Auto10.sdc", false), "maps that merely start with Auto are scanned");
    Check(!ScanFile("Cistern.sdc.bak-20260928-230254", true) && !ScanFile("notes.txt", true) && !ScanFile(".sdc", true), "other files are not");
}

int main()
{
    try
    {
        TestSplitPath();
        TestMatching();
        TestScan();
        TestReport();
        TestFiles();
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "MapUsagesModelTests: " << checks << " checks passed\n";
    return 0;
}
