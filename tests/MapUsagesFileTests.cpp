// Reads real saved maps: the OffsE fixture in tests/maps, and optionally a
// folder given on the command line:
//   MapUsagesFileTests.exe [<folder>... <Package.Group.Name> [autosaves]]
// prints what Find Usages in All Maps would list for that folder and how long
// the scan took. Run from the repository root.
#include "../Reloaded.Editor/MapUsages.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <source_location>
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

void TestFixture()
{
    const std::filesystem::path source = "tests/maps/OffsE/MapsEd/OffsE.sdc", playable = "tests/maps/OffsE/Maps/OffsE.sdc";
    const auto package = ReadPackage(source);
    const auto p = Snapshot::Parse(package);
    Check(p.version == 300 && p.imports.size() == 356 && p.exports.size() == 5651, "compressed source map decodes to its tables");

    auto r = Scan(package, SplitPath("Missile.PC.Bunk_PC_applicAllum"));
    Check(r.objects.size() == 1 && r.users.size() == 1 && r.users.at("StaticMeshActor.StaticMesh") == 71, "a mesh placed 71 times");
    r = Scan(package, SplitPath("engine.s_lightboth"));
    Check(r.users.at("Light.Texture") == 56, "an editor sprite texture on 56 lights");
    r = Scan(package, SplitPath("Oilrig_SM.Third Floor.oiltanks"));
    Check(r.users.at("StaticMeshActor.StaticMesh") == 14, "a mesh in a group whose name has a space");
    r = Scan(package, SplitPath("Oilrig_SM"));
    Check(r.objects.size() > 5 && r.references > 14, "a whole package");
    Check(r.unparsed * 100 < p.exports.size(), "almost every property list is read");
    Check(!Scan(package, SplitPath("Oilrig_SM.Pipes.NoSuchMesh")).Found(), "an object the map does not import");

    const auto runtime = ReadPackage(playable);
    Check(Snapshot::Parse(runtime).version == 300, "the playable copy decodes too");

    // A damaged copy is refused with a reason instead of being misread.
    auto damaged = std::filesystem::temp_directory_path() / "MapUsagesDamaged.sdc";
    {
        std::ifstream in(source, std::ios::binary);
        Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        bytes.resize(bytes.size() / 3);
        std::ofstream out(damaged, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    Reject([&] { ReadPackage(damaged); });
    Reject([&] { ReadPackage("tests/maps/OffsE/README.md"); });
    Reject([&] { ReadPackage("tests/maps/OffsE/NoSuch.sdc"); });

    // The folder walk: one map that uses the mesh, one unreadable file.
    std::atomic<bool> cancel{ false };
    size_t calls = 0;
    const auto files = std::vector<std::pair<std::filesystem::path, std::string>>{ {source, "MapsEd"}, {damaged, "MapsEd"}, {playable, "Maps"} };
    const auto rows = Scan(files, SplitPath("Missile.PC.Bunk_PC_applicAllum"), cancel, [&](size_t, size_t total, const std::string&) { ++calls; Check(total == 3, "progress total"); });
    Check(calls == 3 && rows.size() == 3, "both copies of OffsE and the damaged file are listed");
    Check(rows[0].error.empty() && rows[1].error.empty() && !rows[2].error.empty() && rows[2].file == "MapUsagesDamaged.sdc", "unreadable files last");
    cancel = true;
    Check(Scan(files, SplitPath("Missile"), cancel, nullptr).empty(), "a cancelled scan stops");
    std::filesystem::remove(damaged);
}

int main(int argc, char** argv)
{
    try
    {
        TestFixture();
        std::cout << "MapUsagesFileTests: " << checks << " checks passed\n";
        if (argc >= 3)
        {
            const auto start = std::chrono::steady_clock::now();
            std::vector<std::string> args(argv + 1, argv + argc);
            const bool autosaves = args.back() == "autosaves";
            if (autosaves) args.pop_back();
            const std::string query = args.back();
            args.pop_back();
            std::vector<Folder> folders;
            for (const auto& folder : args) folders.push_back({ folder, std::filesystem::path(folder).filename().string() });
            const auto files = MapFiles(folders, autosaves);
            std::atomic<bool> cancel{ false };
            size_t unparsed = 0;
            const auto rows = Scan(files, SplitPath(query), cancel, nullptr);
            for (const auto& r : rows) unparsed += r.result.unparsed;
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << Report(query, rows, files.size());
            std::cout << files.size() << " maps scanned in " << seconds << " s; " << unparsed << " unreadable property lists in matching maps\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
