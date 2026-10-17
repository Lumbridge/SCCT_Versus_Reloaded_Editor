#include "../Reloaded.Editor/SaveBackupModel.h"
#include <iostream>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace SaveBackups;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
std::set<std::string> Set(const std::vector<std::string>& names) { return { names.begin(), names.end() }; }

constexpr std::uint64_t kMB = 1024 * 1024;

void Targets()
{
    Check(SaveTarget("MAP SAVE FILE=\"C:\\Game\\Packages\\MapsEd\\Moon Base.sdc\"") == "C:\\Game\\Packages\\MapsEd\\Moon Base.sdc", "a quoted path, with a space");
    Check(SaveTarget("MAP SAVE FILE=..\\Packages\\MapsEd\\MoonD.sdc") == "..\\Packages\\MapsEd\\MoonD.sdc", "an unquoted path");
    Check(SaveTarget("MAP SAVE FILE=MoonD.sdc AUTOSAVE=1") == "MoonD.sdc", "an unquoted value ends at a blank");
    Check(SaveTarget("MAP SAVE QUIET=1 FILE=\"MoonD.sdc\" PACKAGE=MyLevel") == "MoonD.sdc", "other parameters before and after");
    Check(SaveTarget("map save file=MoonD.sdc") == "MoonD.sdc", "lower case");
    Check(SaveTarget("  MAP\tSAVE  File=\"MoonD.sdc") == "MoonD.sdc", "blanks around the words, an unclosed quote");
    Check(!SaveTarget("MAP SAVE"), "no FILE=");
    Check(!SaveTarget("MAP SAVE PACKAGE=MyLevel"), "no FILE= among other parameters");
    Check(!SaveTarget("MAP SAVE FILE="), "an empty FILE=");
    Check(!SaveTarget("MAP SAVE FILE=\"\""), "an empty quoted FILE=");
    Check(!SaveTarget("MAP LOAD FILE=\"MoonD.sdc\""), "a load is not a save");
    Check(!SaveTarget("MAP SAVEAS FILE=MoonD.sdc") && !SaveTarget("MAPS SAVE FILE=MoonD.sdc"), "only whole words");
    Check(!SaveTarget("OBJ SAVEPACKAGE PACKAGE=MoonD-i FILE=\"..\\Packages\\Textures\\MoonD-i.utc\""), "a package save is not a map save");
    Check(!SaveTarget(""), "an empty command");

    Check(LoadTarget("MAP LOAD FILE=\"C:\\G\\System\\ReloadedEditor\\Backups\\MoonD\\MoonD_20261017_143205.sdc\"")
          == "C:\\G\\System\\ReloadedEditor\\Backups\\MoonD\\MoonD_20261017_143205.sdc", "the file a load opens");
    Check(LoadTarget("map load file=MoonD.sdc") == "MoonD.sdc", "a load, lower case and unquoted");
    Check(!LoadTarget("MAP SAVE FILE=\"MoonD.sdc\"") && !LoadTarget("MAP LOAD"), "a save is not a load, nor a load without FILE=");

    // Play Level's own command, and the copy the engine writes instead.
    Check(IsPlayLevelName(*SaveTarget("MAP SAVE FILE=..\\Packages\\MapsEd\\Autoplay.sdc")), "Play Level's save names MapsEd's Autoplay.sdc");
    Check(IsPlayLevelName("C:/G/Packages/Maps/AUTOPLAY.SDC") && IsPlayLevelName("Autoplay.sds"), "any case, slash and extension");
    Check(!IsPlayLevelName("..\\Packages\\MapsEd\\MoonD.sdc") && !IsPlayLevelName("Auto0.sdc"), "a map, an autosave");
    Check(!IsPlayLevelName("Autoplay2.sdc") && !IsPlayLevelName("MyAutoplay.sdc") && !IsPlayLevelName("C:\\Autoplay.d\\MoonD.sdc"),
          "only the file name Autoplay.*");
}

void Names()
{
    const Stamp stamp{ 2026, 10, 17, 14, 32, 5 };
    Check(BackupName("MoonD", ".sdc", stamp) == "MoonD_20261017_143205.sdc", "the name carries the date and time");
    Check(BackupName("ShipD", ".sdc", { 2027, 1, 2, 3, 4, 5 }) == "ShipD_20270102_030405.sdc", "every field two digits");

    const auto read = ParseBackup("MoonD_20261017_143205.sdc");
    Check(read && read->first == "MoonD" && read->second.year == 2026 && read->second.month == 10 && read->second.day == 17
          && read->second.hour == 14 && read->second.minute == 32 && read->second.second == 5, "a name reads back");
    const auto underscores = ParseBackup(BackupName("Moon_Base_v2", ".sdc", stamp));
    Check(underscores && underscores->first == "Moon_Base_v2" && Order(underscores->second) == Order(stamp), "a map name with underscores of its own");
    const auto path = ParseBackup("C:\\G\\System\\ReloadedEditor\\Backups\\MoonD\\MoonD_20261017_143205.sdc");
    Check(path && path->first == "MoonD", "a path is read by its file name");
    Check(ParseBackup("MoonD_20240229_000000.sdc").has_value(), "29 February in a leap year");

    Check(!ParseBackup("MoonD.sdc"), "a map, not a backup");
    Check(!ParseBackup("_20261017_143205.sdc"), "no map name");
    Check(!ParseBackup("MoonD_2026101_143205.sdc"), "a short date");
    Check(!ParseBackup("MoonD-20261017_143205.sdc") && !ParseBackup("MoonD_20261017-143205.sdc"), "the separators are underscores");
    Check(!ParseBackup("MoonD_2026a017_143205.sdc"), "a letter in the date");
    Check(!ParseBackup("MoonD_20261317_143205.sdc") && !ParseBackup("MoonD_20261032_143205.sdc")
          && !ParseBackup("MoonD_20250229_143205.sdc") && !ParseBackup("MoonD_20260431_120000.sdc"), "invalid dates");
    Check(!ParseBackup("MoonD_20261017_243205.sdc") && !ParseBackup("MoonD_20261017_146005.sdc")
          && !ParseBackup("MoonD_20261017_143260.sdc"), "invalid times");
    Check(Order({ 2026, 10, 17, 14, 32, 6 }) > Order(stamp) && Order({ 2026, 10, 18, 0, 0, 0 }) > Order(stamp)
          && Order({ 2025, 12, 31, 23, 59, 59 }) < Order(stamp), "later stamps order later");
}

void Copies()
{
    const std::vector<File> existing = {
        { "MoonD", "MoonD_20261017_120000.sdc", 50 * kMB, 1000 },
        { "MoonD", "MoonD_20261017_130000.sdc", 52 * kMB, 2000 },
        { "MoonD", "notes.txt", 52 * kMB, 3000 },
    };
    Check(!Needed(existing, 52 * kMB, 2000), "the newest backup already is the file on disk");
    Check(Needed(existing, 53 * kMB, 2000), "a different size");
    Check(Needed(existing, 52 * kMB, 2001), "a different write time");
    Check(Needed(existing, 50 * kMB, 1000), "the same as an older backup only");
    Check(Needed(existing, 52 * kMB, 3000), "a file that is not a backup is not compared");
    Check(Needed({}, 52 * kMB, 2000), "no backups yet");

    // Written before or in the second of its name: RE+'s own copy. Written
    // later: saved over since.
    const File copy{ "MoonD", "MoonD_20261017_143205.sdc", kMB, 0, { 2026, 10, 17, 14, 2, 40 } };
    Check(!SavedOver(copy), "a copy keeps the earlier write time of its map");
    Check(!SavedOver({ "MoonD", "MoonD_20261017_143205.sdc", kMB, 0, { 2026, 10, 17, 14, 32, 5 } }), "written in the second of its name");
    Check(SavedOver({ "MoonD", "MoonD_20261017_143205.sdc", kMB, 0, { 2026, 10, 17, 14, 32, 6 } })
          && SavedOver({ "MoonD", "C:\\G\\System\\ReloadedEditor\\Backups\\MoonD\\MoonD_20261017_143205.sdc", kMB, 0, { 2026, 10, 20, 9, 0, 0 } }),
          "written later: saved over");
    Check(!SavedOver({ "MoonD", "notes.txt", kMB, 0, { 2027, 1, 1, 0, 0, 0 } }) && !SavedOver({ "MoonD", "MoonD_20261017_143205.sdc", kMB, 0 }),
          "not a backup, or no write time given");
}

void Pruning()
{
    // Six of MoonD at 10 MB, two of ShipD at 20 MB, and a stray file.
    std::vector<File> files;
    for (int hour = 10; hour < 16; ++hour)
        files.push_back({ "MoonD", "MoonD_20261017_" + std::to_string(hour) + "0000.sdc", 10 * kMB, 0 });
    files.push_back({ "ShipD", "ShipD_20261016_090000.sdc", 20 * kMB, 0 });
    files.push_back({ "ShipD", "ShipD_20261017_090000.sdc", 20 * kMB, 0 });
    files.push_back({ "MoonD", "readme.txt", 500 * kMB, 0 });

    Check(Prune(files, 5, 2048 * kMB) == std::vector<std::string>{ "MoonD_20261017_100000.sdc" }, "the newest five of each map stay");
    Check(Prune(files, 2, 2048 * kMB).size() == 4 && !Set(Prune(files, 2, 2048 * kMB)).count("MoonD_20261017_150000.sdc"), "keep two");
    Check(Prune(files, 10, 2048 * kMB).empty(), "keep more than there are: nothing goes");
    Check(Prune(files, 0, 2048 * kMB).size() == 6, "keep is at least one: a map's newest stays");

    // 90 MB in all after the per-map rule; a cap of 65 MB takes the oldest
    // across maps until it fits: ShipD's older (09:00 on the 16th), then,
    // passing ShipD's newest, MoonD's 11:00.
    const auto capped = Prune(files, 5, 65 * kMB);
    Check(Set(capped) == std::set<std::string>{ "MoonD_20261017_100000.sdc", "ShipD_20261016_090000.sdc", "MoonD_20261017_110000.sdc" },
          "the total cap goes oldest first across maps");
    Check(Set(Prune(files, 5, 75 * kMB)) == std::set<std::string>{ "MoonD_20261017_100000.sdc", "ShipD_20261016_090000.sdc" },
          "it stops once the rest fits");
    const auto none = Set(Prune(files, 5, 0));
    Check(none.size() == 6 && !none.count("MoonD_20261017_150000.sdc") && !none.count("ShipD_20261017_090000.sdc"),
          "a cap nothing fits under leaves each map its newest");
    Check(!none.count("readme.txt"), "a file that is not a backup is never deleted");

    // The open map was loaded from a backup: a save over its map must not
    // remove it first.
    Check(Prune(files, 5, 2048 * kMB, "C:\\G\\System\\ReloadedEditor\\Backups\\MoonD\\MoonD_20261017_100000.sdc").empty(),
          "the backup the open map came from stays past keep");
    Check(Prune(files, 5, 2048 * kMB, "c:/g/system/reloadededitor/backups/moond/MOOND_20261017_100000.SDC").empty(),
          "it is matched by file name, whatever the case and slashes");
    Check(Set(Prune(files, 5, 65 * kMB, "C:\\G\\System\\ReloadedEditor\\Backups\\ShipD\\ShipD_20261016_090000.sdc"))
              == std::set<std::string>{ "MoonD_20261017_100000.sdc", "MoonD_20261017_110000.sdc", "MoonD_20261017_120000.sdc",
                                        "MoonD_20261017_130000.sdc" },
          "under the total cap it stays and counts, and the next oldest go instead");
    Check(Prune(files, 5, 2048 * kMB, "C:\\G\\Packages\\MapsEd\\MoonD.sdc") == std::vector<std::string>{ "MoonD_20261017_100000.sdc" },
          "an open map from MapsEd protects nothing");

    // MoonD's oldest was opened and saved over the next day: it holds the
    // user's work, so it is neither removed nor counted.
    auto edited = files;
    edited[0].writtenLocal = { 2026, 10, 18, 9, 0, 0 };
    Check(Prune(edited, 5, 2048 * kMB).empty(), "a backup saved over stays past keep, and the five others are all kept");
    Check(Set(Prune(edited, 2, 2048 * kMB)) == std::set<std::string>{ "MoonD_20261017_110000.sdc", "MoonD_20261017_120000.sdc", "MoonD_20261017_130000.sdc" },
          "keep two of the rest");
    Check(Set(Prune(edited, 5, 65 * kMB)) == std::set<std::string>{ "ShipD_20261016_090000.sdc", "MoonD_20261017_110000.sdc" },
          "the total cap neither counts nor removes it");
    auto newest = files;
    newest[5].writtenLocal = { 2026, 10, 17, 16, 30, 0 };
    Check(Set(Prune(newest, 2, 2048 * kMB)) == std::set<std::string>{ "MoonD_20261017_100000.sdc", "MoonD_20261017_110000.sdc", "MoonD_20261017_120000.sdc" },
          "the newest saved over: the two newest of the rest stay");

    const std::vector<File> folded = {
        { "MoonD", "MoonD_20261017_100000.sdc", kMB, 0 },
        { "moond", "MoonD_20261017_110000.sdc", kMB, 0 },
    };
    Check(Prune(folded, 1, 2048 * kMB) == std::vector<std::string>{ "MoonD_20261017_100000.sdc" }, "map folders match whatever their case");
    Check(Prune({}, 5, 0).empty(), "no backups");
}

void Paths()
{
    Check(IsBackupPath("C:\\G\\System\\ReloadedEditor\\Backups\\MoonD\\x.sdc"), "a backup, backslashes");
    Check(IsBackupPath("C:/G/System/ReloadedEditor/Backups/MoonD/x.sdc"), "a backup, forward slashes");
    Check(IsBackupPath("c:\\g\\system\\reloadededitor\\BACKUPS\\MoonD\\x.sdc") && IsBackupPath("C:\\G\\System\\ReloadedEditor\\\\Backups/MoonD/x.sdc"),
          "any case, doubled and mixed slashes");
    Check(!IsBackupPath("C:\\G\\Packages\\MapsEd\\MoonD.sdc") && !IsBackupPath("..\\Packages\\MapsEd\\Auto0.sdc"), "MapsEd maps");
    Check(!IsBackupPath("C:\\G\\Backups\\MoonD.sdc") && !IsBackupPath("C:\\G\\System\\ReloadedEditor\\snapshot-backups\\MoonD-i.utc"),
          "a Backups folder elsewhere, another ReloadedEditor folder");
    Check(!IsBackupPath("C:\\G\\System\\ReloadedEditor\\Backups"), "the folder itself");
    Check(!IsBackupPath(""), "an empty path");
}

int main()
{
    try
    {
        Targets();
        Names();
        Copies();
        Pruning();
        Paths();
        std::cout << "SaveBackupModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "SaveBackupModelTests FAILED: " << e.what() << "\n";
        return 1;
    }
}
