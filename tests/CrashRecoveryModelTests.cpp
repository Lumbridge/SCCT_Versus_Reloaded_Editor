#include "../Reloaded.Editor/CrashRecoveryModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace Sessions;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

constexpr std::uint64_t kStart = 134000000000000000ull; // a 2025 FILETIME
constexpr std::uint64_t kMinute = 600000000ull;

void Markers()
{
    Marker marker{ 4242, kStart, "C:\\Game\\Packages\\MapsEd\\Ship=D.sdc" };
    const auto read = Parse(Format(marker));
    Check(read && read->pid == 4242 && read->created == kStart && read->map == marker.map, "a marker reads back, '=' in the map included");
    Check(MarkerFileName(4242) == "session-4242.txt", "marker file name");

    const auto noMap = Parse("pid=7\ncreated=5\n");
    Check(noMap && noMap->map.empty(), "LF endings and no map");
    Check(Parse("pid=7\r\ncreated=5\r\nmap=\r\nfuture=1\r\n").has_value(), "unknown keys are ignored");
    Check(!Parse("created=5\r\nmap=x\r\n"), "no pid");
    Check(!Parse("pid=7\r\nmap=x\r\n"), "no creation time");
    Check(!Parse("pid=0\r\ncreated=5\r\n"), "pid 0");
    Check(!Parse("pid=7x\r\ncreated=5\r\n"), "a pid with trailing text");
    Check(!Parse("pid=7\r\ncreated=abc\r\n"), "a creation time that is not a number");
    Check(!Parse(""), "an empty file");

    Check(Abandoned(marker, std::nullopt), "no process with that pid: abandoned");
    Check(!Abandoned(marker, kStart), "the same process still running: another live editor");
    Check(Abandoned(marker, kStart + kMinute), "the pid reused by a later process: abandoned");

    Check(!Latest({}), "no markers");
    const auto latest = Latest({ { 1, kStart, "a" }, { 2, kStart + 3 * kMinute, "b" }, { 3, kStart + kMinute, "c" } });
    Check(latest && latest->pid == 2, "the latest session is offered");
}

void Autosaves()
{
    Check(IsAutosaveName("C:\\x\\Auto0.sdc") && IsAutosaveName("auto9.SDC") && IsAutosaveName("D:/m/Auto12.sdc"), "autosave names");
    Check(!IsAutosaveName("Autoplay.sdc") && !IsAutosaveName("Auto.sdc") && !IsAutosaveName("Auto0.sdc.bak")
          && !IsAutosaveName("MyAuto0.sdc") && !IsAutosaveName("Auto0.utx") && !IsAutosaveName("C:\\Auto0\\ShipD.sdc"), "not autosaves");

    const std::vector<File> files = {
        { "M\\Auto0.sdc", kStart - kMinute },
        { "M\\Auto1.sdc", kStart + 2 * kMinute },
        { "M\\Auto2.sdc", kStart + 7 * kMinute },
        { "M\\ShipD.sdc", kStart + 9 * kMinute },
        { "M\\Autoplay.sdc", kStart + 9 * kMinute },
    };
    const auto newest = NewestAutosave(files);
    Check(newest && newest->path == "M\\Auto2.sdc", "the newest autosave, ignoring other maps");
    Check(NewestAutosave(files, kStart)->path == "M\\Auto2.sdc", "the newest of the session");
    Check(NewestAutosave(files, kStart + 2 * kMinute)->path == "M\\Auto2.sdc", "written at the start counts");
    Check(!NewestAutosave(files, kStart + 8 * kMinute), "every autosave older than the session: none");
    Check(NewestAutosave({ { "M\\Auto0.sdc", kStart - kMinute } })->path == "M\\Auto0.sdc", "the menu command takes any age");
    Check(!NewestAutosave({}), "no files");
}

void CrashReports()
{
    const Marker marker{ 1234, kStart, "" };
    const std::vector<File> files = {
        { "D\\EditorCrash_20250101_120000_000_pid1234.log", kStart - kMinute }, // an earlier process with the same pid
        { "D\\EditorCrash_20250101_120500_000_pid1234.dmp", kStart + 5 * kMinute },
        { "D\\EditorCrash_20250101_120500_000_pid12345.log", kStart + 5 * kMinute },
        { "D\\BspCrash_20250101_120500_000_pid1234.log", kStart + 5 * kMinute },
    };
    Check(!CrashReportFor(files, marker), "no report of this session");
    auto withReport = files;
    withReport.push_back({ "D\\EditorCrash_20250101_120400_000_pid1234.log", kStart + 4 * kMinute });
    const auto report = CrashReportFor(withReport, marker);
    Check(report && report->path == "D\\EditorCrash_20250101_120400_000_pid1234.log", "this session's report");
}

void Offers()
{
    const Marker marker{ 1, kStart, "C:\\G\\Packages\\MapsEd\\ShipD.sdc" };
    const File autosave{ "C:\\G\\Packages\\MapsEd\\Auto3.sdc", kStart + 5 * kMinute };
    const File report{ "C:\\G\\System\\Diagnostics\\EditorCrash_x_pid1.log", kStart + 6 * kMinute };

    auto both = BuildOffer(marker, autosave, "14:32", true, report);
    Check(both.choices == Choices::AutosaveOrMap && both.autosave == autosave.path && both.map == marker.map, "autosave or map");
    Check(Has(both.text, marker.map) && Has(both.text, autosave.path) && Has(both.text, "14:32") && Has(both.text, report.path), "the text names map, autosave, time and report");
    Check(Has(both.text, "Yes: open the autosave") && Has(both.text, "No: open ShipD.sdc") && Has(both.text, "Save As"), "the text explains the answers");

    auto onlyAutosave = BuildOffer(marker, autosave, "14:32", false, std::nullopt);
    Check(onlyAutosave.choices == Choices::Autosave && onlyAutosave.map.empty(), "a deleted map is not offered");
    Check(Has(onlyAutosave.text, "no longer there") && !Has(onlyAutosave.text, "Crash report"), "a missing map is mentioned, no report");

    auto onlyMap = BuildOffer(marker, std::nullopt, "", true, std::nullopt);
    Check(onlyMap.choices == Choices::Map && onlyMap.autosave.empty() && Has(onlyMap.text, "wrote no autosave"), "no fresh autosave: the map only");

    const Marker untitled{ 1, kStart, "" };
    Check(BuildOffer(untitled, std::nullopt, "", false, std::nullopt).choices == Choices::None, "nothing open, nothing written: no offer");
    auto informed = BuildOffer(untitled, std::nullopt, "", false, report);
    Check(informed.choices == Choices::Inform && Has(informed.text, report.path) && Has(informed.text, "No map was open"), "a crash report alone is still reported");
    Check(BuildOffer(untitled, autosave, "14:32", false, std::nullopt).choices == Choices::Autosave, "an autosave with no map open");

    const Marker onAutosave{ 1, kStart, "C:\\G\\Packages\\MapsEd\\AUTO3.sdc" };
    Check(BuildOffer(onAutosave, autosave, "14:32", true, std::nullopt).choices == Choices::Autosave, "the map that is the autosave is offered once");
}

int main()
{
    try
    {
        Markers();
        Autosaves();
        CrashReports();
        Offers();
        std::cout << "CrashRecoveryModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "CrashRecoveryModelTests FAILED: " << e.what() << "\n";
        return 1;
    }
}
