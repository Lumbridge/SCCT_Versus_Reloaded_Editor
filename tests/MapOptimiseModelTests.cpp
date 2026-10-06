// Built by tools\test_map_package.cmd (needs zlib for MapPackage.cpp).
#include "../Reloaded.Editor/MapOptimiseModel.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
using namespace MapOptimise;
using Bytes = std::vector<unsigned char>;
int checks = 0;
void Check(bool value, const char *message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void Reject(F f, const char *message)
{
    bool failed = false;
    try
    {
        f();
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    Check(failed, message);
}
void Put(Bytes &b, size_t at, unsigned n)
{
    for (int i = 0; i < 4; ++i)
        b[at + i] = static_cast<unsigned char>(n >> (8 * i));
}
void Int(Bytes &b, int n)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<unsigned char>(static_cast<unsigned>(n) >> (8 * i)));
}
// Unreal's compact index.
void Compact(Bytes &b, int n)
{
    unsigned v = static_cast<unsigned>(n < 0 ? -n : n);
    unsigned char first = static_cast<unsigned char>((n < 0 ? 0x80 : 0) | (v & 0x3f));
    v >>= 6;
    if (v)
        first |= 0x40;
    b.push_back(first);
    while (v)
    {
        unsigned char next = static_cast<unsigned char>(v & 0x7f);
        v >>= 7;
        if (v)
            next |= 0x80;
        b.push_back(next);
    }
}
struct Imp
{
    int classPackage, className, outer, name;
};
struct Exp
{
    int type, outer, name, size;
};
// A version 300 package: names XORed with their offset, a name written as index
// and hash, exports pointing at data blocks of their size.
Bytes Package(const std::vector<std::string> &names, const std::vector<Imp> &imports, const std::vector<Exp> &exports)
{
    Bytes b(64);
    Put(b, 0, 0x9e2a83c1);
    Put(b, 4, 300);
    Put(b, 8, 1);
    Put(b, 12, static_cast<unsigned>(names.size()));
    Put(b, 16, 64);
    for (const auto &name : names)
    {
        b.push_back(static_cast<unsigned char>(name.size() + 1));
        for (char c : name)
            b.push_back(static_cast<unsigned char>(c ^ (b.size() & 255)));
        b.push_back(static_cast<unsigned char>(b.size() & 255));
        b.resize(b.size() + 4);
    }
    std::vector<size_t> data;
    for (const auto &e : exports)
    {
        data.push_back(b.size());
        b.resize(b.size() + static_cast<size_t>(e.size));
    }
    Put(b, 28, static_cast<unsigned>(imports.size()));
    Put(b, 32, static_cast<unsigned>(b.size()));
    for (const auto &i : imports)
    {
        Compact(b, i.classPackage);
        Compact(b, 77);
        Compact(b, i.className);
        Compact(b, 1234567);
        Int(b, i.outer);
        Compact(b, i.name);
        Compact(b, 9);
    }
    Put(b, 20, static_cast<unsigned>(exports.size()));
    Put(b, 24, static_cast<unsigned>(b.size()));
    for (size_t k = 0; k < exports.size(); ++k)
    {
        const auto &e = exports[k];
        Compact(b, e.type);
        Compact(b, 0);
        Int(b, e.outer);
        Compact(b, e.name);
        Compact(b, 55555);
        Int(b, 0x000f0004);
        Compact(b, e.size);
        if (e.size > 0)
            Compact(b, static_cast<int>(data[k]));
    }
    return b;
}
int main()
{
    try
    {
        auto root = fs::temp_directory_path() /
                    ("MapOptimiseTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        struct Cleanup
        {
            fs::path root;
            ~Cleanup()
            {
                std::error_code e;
                fs::remove_all(root, e);
            }
        } cleanup{root};

        // Pack.usx: Props.Crate (a mesh holding its collision model), Props.Barrel,
        // and Walls.Metal (a texture). Names: 0 Core 1 Package 2 Engine 3 Class
        // 4 StaticMesh 5 Texture 6 Model 7 Props 8 Walls 9 Crate 10 Barrel 11 Metal 12 Collision
        const std::vector<std::string> names = {"Core", "Package", "Engine", "Class", "StaticMesh", "Texture", "Model",
                                                "Props", "Walls", "Crate", "Barrel", "Metal", "Collision"};
        // Imports: 1 Engine (package), 2 Engine.StaticMesh, 3 Engine.Texture, 4 Engine.Model,
        // 5 Core (package), 6 Core.Package
        const std::vector<Imp> imports = {{0, 1, 0, 2}, {0, 3, -1, 4}, {0, 3, -1, 5},
                                          {0, 3, -1, 6}, {0, 1, 0, 0},  {0, 3, -5, 1}};
        // Exports: 1 Props (package), 2 Walls, 3 Crate, 4 Crate.Collision, 5 Barrel, 6 Metal
        const std::vector<Exp> exports = {{-6, 0, 7, 10}, {-6, 0, 8, 10},     {-2, 1, 9, 300},
                                          {-4, 3, 12, 50}, {-2, 1, 10, 1000}, {-3, 2, 11, 5000}};
        auto pack = root / "Pack.usx";
        {
            auto bytes = Package(names, imports, exports);
            std::ofstream(pack, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        }
        auto tables = MapPackage::ReadTables(pack);
        Check(tables.package == "Pack" && tables.fileSize == fs::file_size(pack), "package named after its file");
        Check(tables.exports.size() == 6 && tables.exports[3].path == "Pack.Props.Crate.Collision" &&
                  tables.exports[3].className == "Model" && tables.exports[3].size == 50,
              "export paths, classes and sizes");
        Check(tables.exports[0].className == "Package" && tables.exports[5].path == "Pack.Walls.Metal", "groups");
        Check(tables.imports.size() == 6 && tables.imports[1].path == "Engine.StaticMesh" &&
                  tables.imports[1].className == "Class" && tables.imports[0].className == "Package",
              "import paths");

        Sizes sizes(tables);
        Check(sizes.Of("Pack.Props.Crate") == 350u, "an asset's size includes what is inside it");
        Check(sizes.Of("pack.props.barrel") == 1000u, "paths compare without case");
        Check(!sizes.Of("Pack.Props.Cr"), "a partial name is not an asset");
        Check(sizes.Of("Pack.Props") == 1360u, "a group holds its assets");

        // The map uses the crate (and, through it, its collision) and the metal.
        auto lookup = [&](const std::string &name) -> std::optional<std::pair<std::string, MapPackage::Tables>> {
            if (Fold(name) == "pack")
                return std::make_pair(pack.string(), tables);
            return std::nullopt;
        };
        auto report = Report({{"Pack.Props.Crate", "StaticMesh"},
                              {"Pack.Props.Crate.Collision", "Model"},
                              {"Pack.Walls.Metal", "Texture"},
                              {"Other.Thing", "Texture"}},
                             lookup);
        Check(report.size() == 2 && report[0].name == "Pack" && report[1].name == "Other", "one row per pack");
        Check(report[0].assets.size() == 2 && report[0].usedSize == 5350 && report[0].fileSize == tables.fileSize,
              "used size counts each asset once");
        Check(report[0].suggested == (5350 * 2 < tables.fileSize), "suggested when under half the pack is used");
        Check(report[1].file.empty() && !report[1].suggested && !report[1].assets[0].found,
              "a pack with no file is reported, not suggested");
        Check(Text(report).find("Pack.Walls.Metal (Texture, 5 KB)") != std::string::npos, "report text");
        auto top = TopLevel({{"A.B", "x"}, {"A.B.C", "y"}, {"A.BC", "z"}, {"a.b", "x"}});
        Check(top.size() == 2 && top[0].path == "A.B" && top[1].path == "A.BC", "inner and repeated assets dropped");

        // Moves keep the pack and group as groups in the destination.
        auto m = PlanMove("Pack.Props.Crate", "MyMap");
        Check(m.oldPackage == "Pack" && m.oldGroup == "Props" && m.name == "Crate" && m.newGroup == "Pack.Props" &&
                  m.NewPath() == "MyMap.Pack.Props.Crate",
              "move plan");
        auto flat = PlanMove("Pack.Crate", "MyMap_Assets");
        Check(flat.oldGroup.empty() && flat.newGroup == "Pack" && flat.NewPath() == "MyMap_Assets.Pack.Crate",
              "an asset outside any group");
        Check(RenameCommand(m) == "OBJ RENAME OLDNAME=\"Crate\" OLDGROUP=\"Props\" OLDPACKAGE=\"Pack\" NEWNAME=\"Crate\" "
                                  "NEWGROUP=\"Pack.Props\" NEWPACKAGE=\"MyMap\"",
              "stock rename command");
        Reject([] { PlanMove("Crate", "MyMap"); }, "a bare name is not an asset path");
        Reject([] { PlanMove("Pack.Props.Crate", "My Map"); }, "a destination with a space");
        Reject([] { PlanMove("Pack.Props.Cr\"ate", "MyMap"); }, "a quote cannot reach the command");
        Check(ValidName("Map_Release2") && !ValidName("") && !ValidName("a.b") && !ValidName(std::string(49, 'a')),
              "names");

        // A saved map that still takes the barrel from the pack.
        MapPackage::Tables saved;
        saved.package = "MyMap_Release";
        saved.imports = {{"Engine", "Package", "Core"},
                         {"Engine.StaticMeshActor", "Class", "Core"},
                         {"Pack", "Package", "Core"},
                         {"Pack.Props", "Package", "Core"},
                         {"Pack.Props.Barrel", "StaticMesh", "Engine"},
                         {"Stock", "Package", "Core"},
                         {"Stock.Rock", "StaticMesh", "Engine"}};
        Check(StillImported(saved, {"pack"}) == std::vector<std::string>{"Pack.Props.Barrel"},
              "assets still taken from a moved pack");
        Check(StillImported(saved, {"Other"}).empty(), "other packs are not moved");
        Check(ImportedPackages(saved) == std::vector<std::string>({"Pack", "Stock"}), "packages still needed");

        std::cout << "MapOptimiseModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}
