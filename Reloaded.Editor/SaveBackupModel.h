#pragma once
// The pure part of keeping the last saved versions of each map: the file a
// MAP SAVE command writes, a backup's name (the map's own name and the local
// time it was replaced) and how to read it back, whether the file on disk
// still needs a copy, and which old copies go. The copies live under
// System\ReloadedEditor\Backups\<Map>. No Windows calls, so the tests compile
// it alone.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace SaveBackups
{
    struct Stamp
    {
        int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    };

    // One backup on disk. map: the folder it is in, named after its map.
    // name: its file name or path, handed back by Prune as given. written:
    // its write time, which the copy keeps from the map it was taken from.
    struct File
    {
        std::string map, name;
        std::uint64_t size = 0, written = 0;
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
    inline bool Blank(char c) { return c == ' ' || c == '\t'; }

    // The engine's ParseCommand: the word in any case, not followed by a
    // letter or digit, with the blanks around it skipped.
    inline bool Word(const std::string& text, size_t& at, const char* word)
    {
        while (at < text.size() && Blank(text[at])) ++at;
        size_t length = 0;
        for (; word[length]; ++length)
            if (at + length >= text.size() || std::toupper(static_cast<unsigned char>(text[at + length])) != word[length]) return false;
        if (at + length < text.size() && std::isalnum(static_cast<unsigned char>(text[at + length]))) return false;
        at += length;
        while (at < text.size() && Blank(text[at])) ++at;
        return true;
    }

    // The file a MAP <verb> command names. FILE= is found as the engine's
    // Parse finds it: anywhere after the verb, in any case, its value quoted
    // or running to the next blank or comma.
    inline std::optional<std::string> CommandFile(const std::string& command, const char* verb)
    {
        size_t at = 0;
        if (!Word(command, at, "MAP") || !Word(command, at, verb)) return std::nullopt;
        std::string rest = command.substr(at);
        for (auto& c : rest) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const auto key = rest.find("FILE=");
        if (key == std::string::npos) return std::nullopt;
        const size_t start = at + key + 5;
        std::string value;
        if (start < command.size() && command[start] == '"')
        {
            const auto close = command.find('"', start + 1);
            value = command.substr(start + 1, close == std::string::npos ? std::string::npos : close - start - 1);
        }
        else
        {
            const auto end = command.find_first_of(" \t\r\n,", start);
            value = command.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }
        if (value.empty()) return std::nullopt;
        return value;
    }
    // The file a MAP SAVE writes, as the command names it.
    inline std::optional<std::string> SaveTarget(const std::string& command) { return CommandFile(command, "SAVE"); }
    // The file a MAP LOAD opens.
    inline std::optional<std::string> LoadTarget(const std::string& command) { return CommandFile(command, "LOAD"); }

    inline std::string Digits(int value, size_t width)
    {
        auto text = std::to_string(value);
        return text.size() < width ? std::string(width - text.size(), '0') + text : text;
    }

    // MoonD_20261017_143205.sdc for MoonD.sdc replaced at 14:32:05 on 17 October 2026.
    inline std::string BackupName(const std::string& stem, const std::string& ext, const Stamp& stamp)
    {
        return stem + "_" + Digits(stamp.year, 4) + Digits(stamp.month, 2) + Digits(stamp.day, 2) + "_"
             + Digits(stamp.hour, 2) + Digits(stamp.minute, 2) + Digits(stamp.second, 2) + ext;
    }

    inline bool Valid(const Stamp& stamp)
    {
        static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (stamp.year < 1 || stamp.month < 1 || stamp.month > 12 || stamp.day < 1
            || stamp.hour > 23 || stamp.minute > 59 || stamp.second > 59) return false;
        const bool leap = (stamp.year % 4 == 0 && stamp.year % 100 != 0) || stamp.year % 400 == 0;
        return stamp.day <= days[stamp.month - 1] + (stamp.month == 2 && leap ? 1 : 0);
    }

    // The map a backup was taken from and when, from its name. A map's name
    // may hold underscores of its own, so the fixed _YYYYMMDD_HHMMSS is read
    // from the end. A path is read by its file name.
    inline std::optional<std::pair<std::string, Stamp>> ParseBackup(const std::string& fileName)
    {
        auto name = FileName(fileName);
        if (const auto dot = name.find_last_of('.'); dot != std::string::npos) name.resize(dot);
        constexpr size_t kSuffix = 16; // _YYYYMMDD_HHMMSS
        if (name.size() <= kSuffix) return std::nullopt;
        const size_t at = name.size() - kSuffix;
        if (name[at] != '_' || name[at + 9] != '_') return std::nullopt;
        const auto number = [&](size_t from, size_t count, int& value)
        {
            value = 0;
            for (size_t i = at + from; i < at + from + count; ++i)
            {
                if (name[i] < '0' || name[i] > '9') return false;
                value = value * 10 + (name[i] - '0');
            }
            return true;
        };
        Stamp stamp;
        if (!number(1, 4, stamp.year) || !number(5, 2, stamp.month) || !number(7, 2, stamp.day)
            || !number(10, 2, stamp.hour) || !number(12, 2, stamp.minute) || !number(14, 2, stamp.second) || !Valid(stamp))
            return std::nullopt;
        return std::pair{ name.substr(0, at), stamp };
    }

    // Later stamps are larger. The stamp is local time, so in the hour that
    // repeats when the clocks go back a backup can order before those taken
    // in the first pass of that hour.
    inline std::uint64_t Order(const Stamp& stamp)
    {
        return ((((std::uint64_t(stamp.year) * 100 + stamp.month) * 100 + stamp.day) * 100 + stamp.hour) * 100 + stamp.minute) * 100
             + stamp.second;
    }

    // A copy is needed unless the map's newest backup already is the file on
    // disk, the same size and write time: a save that failed and is tried
    // again replaces nothing new.
    inline bool Needed(const std::vector<File>& existingForMap, std::uint64_t size, std::uint64_t written)
    {
        const File* newest = nullptr;
        std::uint64_t newestOrder = 0;
        for (const auto& file : existingForMap)
            if (const auto backup = ParseBackup(file.name))
            {
                const auto order = Order(backup->second);
                if (!newest || order > newestOrder || (order == newestOrder && file.name > newest->name)) { newest = &file; newestOrder = order; }
            }
        return !newest || newest->size != size || newest->written != written;
    }

    // The backups to delete: of each map all but the newest keep, then, while
    // the rest together take more than maxBytes, the oldest of any map. A
    // map's newest always stays, and a file not named as a backup is never
    // deleted or counted. inUse, the file the open map was loaded from, stays
    // too, as the save about to run may still read from it; it is matched by
    // file name, which carries its map's name.
    inline std::vector<std::string> Prune(std::vector<File> files, size_t keep, std::uint64_t maxBytes, const std::string& inUse = {})
    {
        struct Entry
        {
            std::string map, name;
            std::uint64_t size = 0, order = 0;
            bool used = false, newest = false, gone = false;
        };
        const auto used = Lower(FileName(inUse));
        std::vector<Entry> entries;
        for (auto& file : files)
            if (const auto backup = ParseBackup(file.name))
            {
                const bool open = !used.empty() && Lower(FileName(file.name)) == used;
                entries.push_back({ Lower(file.map), std::move(file.name), file.size, Order(backup->second), open });
            }
        // Newest first.
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
                  { return a.order != b.order ? a.order > b.order : a.name > b.name; });

        std::vector<std::string> removed;
        std::vector<std::pair<std::string, size_t>> seen;
        std::uint64_t total = 0;
        for (auto& entry : entries)
        {
            auto counted = std::find_if(seen.begin(), seen.end(), [&](const auto& map) { return map.first == entry.map; });
            if (counted == seen.end()) { seen.emplace_back(entry.map, 0); counted = seen.end() - 1; entry.newest = true; }
            if (++counted->second > (std::max)(keep, size_t(1)) && !entry.used) { entry.gone = true; removed.push_back(entry.name); }
            else total += entry.size;
        }
        for (auto entry = entries.rbegin(); entry != entries.rend() && total > maxBytes; ++entry)
            if (!entry->newest && !entry->gone && !entry->used)
            {
                entry->gone = true;
                total -= entry->size;
                removed.push_back(entry->name);
            }
        return removed;
    }

    // A file in a Backups folder of a ReloadedEditor folder, where RE+ keeps
    // them, whichever slashes the path uses.
    inline bool IsBackupPath(const std::string& path)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= path.size())
        {
            const auto slash = path.find_first_of("\\/", start);
            const auto end = slash == std::string::npos ? path.size() : slash;
            if (end > start) parts.push_back(Lower(path.substr(start, end - start)));
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        for (size_t i = 1; i + 1 < parts.size(); ++i)
            if (parts[i] == "backups" && parts[i - 1] == "reloadededitor") return true;
        return false;
    }
}
