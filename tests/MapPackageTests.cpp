#include "../Reloaded.Editor/MapPackage.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <zlib.h>
namespace fs = std::filesystem;
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
unsigned Get(const Bytes &b, size_t at)
{
    unsigned n = 0;
    for (int i = 0; i < 4; ++i)
        n |= static_cast<unsigned>(b.at(at + i)) << (8 * i);
    return n;
}
Bytes Load(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void Save(const fs::path &path, const Bytes &bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}
Bytes Package(const std::vector<std::string> &imports, unsigned version = 175)
{
    Bytes b(64);
    Put(b, 0, 0x9e2a83c1);
    Put(b, 4, version);
    Put(b, 8, 1);
    Put(b, 12, static_cast<unsigned>(imports.size() + 2));
    Put(b, 16, 64);
    Put(b, 28, static_cast<unsigned>(imports.size()));
    auto names = std::vector<std::string>{"Core", "Package"};
    names.insert(names.end(), imports.begin(), imports.end());
    for (const auto &name : names)
    {
        b.push_back(static_cast<unsigned char>(name.size() + 1));
        for (char c : name)
            b.push_back(static_cast<unsigned char>(c ^ (version >= 175 ? b.size() : 0)));
        b.push_back(static_cast<unsigned char>(version >= 175 ? b.size() : 0));
        b.resize(b.size() + 4);
    }
    Put(b, 32, static_cast<unsigned>(b.size()));
    for (size_t i = 0; i < imports.size(); ++i)
    {
        Bytes row = {0, 0, 1, 0, 0, 0, 0, 0, static_cast<unsigned char>(i + 2), 0};
        b.insert(b.end(), row.begin(), row.end());
    }
    return b;
}
Bytes Compressed(const Bytes &b)
{
    uLongf size = compressBound(static_cast<uLong>(b.size()));
    Bytes out(size + 8);
    Check(compress2(out.data() + 8, &size, b.data(), static_cast<uLong>(b.size()), 6) == Z_OK, "compress fixture");
    out.resize(size + 8);
    Put(out, 0, static_cast<unsigned>(b.size()));
    Put(out, 4, size);
    return out;
}
int main(int argc, char **argv)
{
    try
    {
        if (argc >= 3)
        {
            auto plan = MapPackage::Inspect(argv[1], argv[2]);
            std::cout << MapPackage::Report(plan);
            if (argc >= 4)
                MapPackage::Write(plan, argv[3]);
            return plan.errors.empty() ? 0 : 1;
        }
        auto root = fs::temp_directory_path() /
                    ("MapPackageTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(root);
        struct Cleanup
        {
            fs::path root;
            ~Cleanup()
            {
                std::error_code e;
                fs::remove_all(root, e);
            }
        } cleanup{root};
        auto map = root / "Packages/Maps/Test.sdc", mesh = root / "Packages/StaticMeshes/Mesh.usx",
             texture = root / "Packages/Textures/Texture.utx";
        Save(map, Compressed(Package({"Mesh", "Engine"})));
        Save(mesh, Package({"Texture"}));
        Save(texture, Package({"Mesh"}));
        Check(MapPackage::Imports(map) == std::vector<std::string>({"Engine", "Mesh"}),
              "compressed import names decoded");
        auto plan = MapPackage::Inspect(root, map);
        Check(plan.errors.empty() && plan.files.size() == 3,
              "transitive cyclic dependencies terminate without duplicates");
        Check(plan.runtimePackages == std::vector<std::string>({"Engine"}),
              "native game package classified separately");
        Check(plan.files[1].requiredBy.size() == 2, "report records both parents of shared dependency");
        auto baseline = root / "base";
        fs::create_directories(baseline / "System");
        Save(baseline / "Packages/StaticMeshes/Mesh.usx", Load(mesh));
        Save(baseline / "Packages/Textures/Texture.utx", Package({"Other"}));
        auto matched = MapPackage::MatchBaseFiles(plan, baseline);
        Check(!matched[0] && matched[1] && !matched[2],
              "base comparison excludes identical assets only, always keeps map");
        fs::create_directory(root / "System");
        Reject([&] { MapPackage::MatchBaseFiles(plan, root); }, "cannot use same installation as its own base");
        auto zip = root / "result.zip";
        MapPackage::Write(plan, zip);
        auto original = Load(zip);
        Check(Get(original, 0) == 0x04034b50, "ZIP local header");
        Check(Get(original, original.size() - 22) == 0x06054b50 && original[original.size() - 12] == 4,
              "ZIP contains map, transitive assets and report");
        Reject([&] { MapPackage::Write(plan, zip); }, "existing ZIP rejected");
        Check(Load(zip) == original, "existing ZIP unchanged");
        plan.files[2].include = false;
        Check(MapPackage::Report(plan).find("REQUIRED, SUPPLY SEPARATELY: Packages/Textures/Texture.utx") !=
                  std::string::npos,
              "omitted dependency documented");
        MapPackage::Write(plan, root / "partial.zip");
        auto partial = Load(root / "partial.zip");
        Check(partial[partial.size() - 12] == 3, "unchecked dependency absent from ZIP");
        plan.files[0].include = false;
        Reject([&] { MapPackage::Write(plan, root / "nomap.zip"); }, "cannot omit playable map");
        plan.files[0].include = true;
        auto bad = plan;
        bad.files[1].destination = "../outside.usx";
        Reject([&] { MapPackage::Write(bad, root / "bad.zip"); }, "reject ZIP traversal");
        Save(texture, Package({"Missing"}));
        Reject([&] { MapPackage::Write(plan, root / "stale.zip"); },
               "changed omitted dependencies reject stale preview");
        Check(!fs::exists(root / "stale.zip"), "failed write publishes no archive");
        auto missing = MapPackage::Inspect(root, map);
        Check(!missing.errors.empty(), "missing transitive dependency reported");
        Reject([&] { MapPackage::Write(missing, root / "missing.zip"); }, "missing dependencies prevent ZIP");
        Save(texture, Package({"Mesh"}));
        Save(root / "Packages/Textures/Texture.utc", Package({"Mesh"}));
        Check(MapPackage::Inspect(root, map).files.size() == 4, "both texture representations included");
        Save(root / "Packages/Sounds/Mesh.uax", Package({}));
        Check(!MapPackage::Inspect(root, map).errors.empty(), "ambiguous package name rejected");
        auto invalid = root / "invalid.usx";
        auto bytes = Package({"Mesh"});
        bytes.resize(bytes.size() - 1);
        Save(invalid, bytes);
        Reject([&] { MapPackage::Imports(invalid); }, "truncated import table rejected");
        bytes = Package({"Mesh"});
        Put(bytes, 32, 0x7fffffff);
        Save(invalid, bytes);
        Reject([&] { MapPackage::Imports(invalid); }, "out-of-file import offset rejected");
        bytes = Package({"Mesh"});
        Put(bytes, 4, 999);
        Save(invalid, bytes);
        Reject([&] { MapPackage::Imports(invalid); }, "unknown format rejected");
        bytes = Compressed(Package({"Mesh"}));
        bytes.pop_back();
        Save(invalid, bytes);
        Reject([&] { MapPackage::Imports(invalid); }, "truncated compressed package rejected");
        Save(root / "Packages/MapsEd/Source.sdc", Package({}));
        Reject([&] { MapPackage::Inspect(root, root / "Packages/MapsEd/Source.sdc"); },
               "source maps cannot masquerade as playable maps");
        for (unsigned version : {171u, 172u, 173u, 174u, 175u, 300u})
        {
            Save(invalid, Package({"Engine"}, version));
            Check(MapPackage::Imports(invalid) == std::vector<std::string>({"Engine"}),
                  "supported package name encodings");
        }
        std::cout << "Map packaging: " << checks << " checks passed.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
