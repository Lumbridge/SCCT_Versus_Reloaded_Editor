#pragma once
// The pure part of offering recovery after a crash: the session marker each
// running editor keeps under System\ReloadedEditor\Sessions, telling a marker
// left by a session that ended abnormally from one of another editor still
// running, the autosaves and crash report that belong to that session, and
// the text and choices of the start-up offer. Times are FILETIME ticks (100 ns
// since 1601), as Windows reports both process start and file write times. No
// Windows calls, so the tests compile it alone.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace Sessions
{
    // What a running editor writes about itself. A clean exit deletes it, so
    // one found by the next start belongs to a session that crashed, hung and
    // was ended, or was killed.
    struct Marker
    {
        unsigned long pid = 0;
        std::uint64_t created = 0; // the process's creation time: pid plus this names one process
        std::string map;           // the map open at the last update, empty for none
    };

    inline std::string MarkerFileName(unsigned long pid) { return "session-" + std::to_string(pid) + ".txt"; }

    inline std::string Format(const Marker& marker)
    {
        return "pid=" + std::to_string(marker.pid) + "\r\ncreated=" + std::to_string(marker.created) + "\r\nmap=" + marker.map + "\r\n";
    }

    // Unknown keys are ignored; a marker without its process is unusable.
    inline std::optional<Marker> Parse(const std::string& text)
    {
        Marker marker;
        bool pid = false, created = false;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto equals = line.find('=');
            if (equals == std::string::npos) continue;
            const auto key = line.substr(0, equals), value = line.substr(equals + 1);
            try
            {
                size_t used = 0;
                if (key == "pid") { marker.pid = std::stoul(value, &used); pid = used == value.size() && marker.pid != 0; }
                else if (key == "created") { marker.created = std::stoull(value, &used); created = used == value.size(); }
                else if (key == "map") marker.map = value;
            }
            catch (const std::exception&) { return std::nullopt; }
        }
        if (!pid || !created) return std::nullopt;
        return marker;
    }

    // liveCreated is the creation time of the process now running with the
    // marker's pid, if there is one. A different time means Windows reused
    // the pid for some other process, so the marker's own editor is gone.
    inline bool Abandoned(const Marker& marker, std::optional<std::uint64_t> liveCreated)
    {
        return !liveCreated || *liveCreated != marker.created;
    }

    // Of several abandoned sessions, the latest one is offered.
    inline std::optional<Marker> Latest(const std::vector<Marker>& markers)
    {
        if (markers.empty()) return std::nullopt;
        return *std::max_element(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.created < b.created; });
    }

    struct File
    {
        std::string path;
        std::uint64_t written = 0;
    };

    inline std::string FileName(const std::string& path)
    {
        const auto slash = path.find_last_of("\\/");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }
    inline std::string Lower(std::string text)
    {
        for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    // The stock autosave's own names, Auto0.sdc to Auto9.sdc (any number is
    // accepted); not Autoplay.sdc or a copy such as Auto0.sdc.bak.
    inline bool IsAutosaveName(const std::string& path)
    {
        const auto name = Lower(FileName(path));
        if (name.size() < 9 || name.compare(0, 4, "auto") != 0 || name.compare(name.size() - 4, 4, ".sdc") != 0) return false;
        const auto digits = name.substr(4, name.size() - 8);
        return std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; });
    }

    // The newest autosave written at or after notBefore; an older one is a
    // leftover from some earlier session and not worth offering.
    inline std::optional<File> NewestAutosave(const std::vector<File>& files, std::uint64_t notBefore = 0)
    {
        std::optional<File> best;
        for (const auto& file : files)
            if (IsAutosaveName(file.path) && file.written >= notBefore && (!best || file.written > best->written)) best = file;
        return best;
    }

    // CrashDiagnostics names its reports EditorCrash_<time>_pid<pid>.log; one
    // written before the session started belongs to an earlier process that
    // had the same pid.
    inline std::optional<File> CrashReportFor(const std::vector<File>& files, const Marker& marker)
    {
        const auto suffix = "_pid" + std::to_string(marker.pid) + ".log";
        std::optional<File> best;
        for (const auto& file : files)
        {
            const auto name = Lower(FileName(file.path));
            if (name.rfind("editorcrash_", 0) != 0 || name.size() < suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            if (file.written < marker.created) continue;
            if (!best || file.written > best->written) best = file;
        }
        return best;
    }

    // What the start-up offer can do. With both an autosave and the map the
    // answers are Yes (autosave), No (map) and Cancel; with one of them, Yes
    // opens it and No does nothing; with neither, the message only informs.
    enum class Choices { None, Inform, Autosave, Map, AutosaveOrMap };

    struct Offer
    {
        Choices choices = Choices::None;
        std::string text;
        std::string autosave; // what Yes opens when there is an autosave
        std::string map;      // the map as last saved, when it can be offered
    };

    // autosave: the newest autosave of the session, with its write time shown
    // as autosaveTime. mapExists: the session's map is still on disk. Nothing
    // is offered for a session that had nothing open, wrote no autosave and
    // left no crash report: there is nothing to say about it.
    inline Offer BuildOffer(const Marker& marker, const std::optional<File>& autosave, const std::string& autosaveTime,
                            bool mapExists, const std::optional<File>& crashReport)
    {
        Offer offer;
        const bool hasMap = !marker.map.empty() && mapExists && !(autosave && Lower(autosave->path) == Lower(marker.map));
        if (autosave) offer.autosave = autosave->path;
        if (hasMap) offer.map = marker.map;
        offer.choices = autosave && hasMap ? Choices::AutosaveOrMap : autosave ? Choices::Autosave : hasMap ? Choices::Map
                      : crashReport || !marker.map.empty() ? Choices::Inform : Choices::None;
        if (offer.choices == Choices::None) return offer;

        std::string text = "The last editor session did not close properly: it crashed, stopped responding or was ended.\r\n\r\n";
        text += marker.map.empty() ? std::string("No map was open in it.\r\n") : "Map open at the time:\r\n    " + marker.map + "\r\n";
        if (!marker.map.empty() && !mapExists) text += "    (that file is no longer there)\r\n";
        text += "\r\n";
        if (autosave)
            text += "Newest autosave from that session (" + autosaveTime + "):\r\n    " + autosave->path + "\r\n"
                    "An autosave opens under its own name, and the editor reuses Auto0 to Auto9 in turn: "
                    "use File > Save As to keep it.\r\n\r\n";
        else
            text += "The editor wrote no autosave during that session.\r\n\r\n";
        if (crashReport) text += "Crash report:\r\n    " + crashReport->path + "\r\n\r\n";

        const auto mapName = FileName(marker.map);
        switch (offer.choices)
        {
        case Choices::AutosaveOrMap:
            text += "Yes: open the autosave.\r\nNo: open " + mapName + " as it was last saved.\r\nCancel: open nothing.";
            break;
        case Choices::Autosave: text += "Open the autosave?"; break;
        case Choices::Map: text += "Open " + mapName + " as it was last saved?"; break;
        default: text += "There is nothing to reopen."; break;
        }
        offer.text = text;
        return offer;
    }
}
