#pragma once
// The pure part of Edit > Undo History: what the editor's transaction buffer
// looks like as a list, how far a row is from the current point, walking
// there one Undo or Redo at a time, and the panel's wording. No engine and no
// Windows, so the tests compile it alone.
//
// The buffer keeps its transactions oldest first; UndoCount of them, at the
// end, have been undone and wait for Redo. The list shows one row before the
// oldest step (the state the oldest step started from) and one row per step,
// so row N means "the first N steps applied" and the current row is
// count - undoCount.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace UndoHistory
{
    struct Snapshot
    {
        std::vector<std::string> titles; // oldest first
        int undoCount = 0;               // steps at the end waiting for Redo
        bool recording = false;          // a transaction is open (ActiveCount > 0)
        size_t used = 0, limit = 0;      // bytes the records hold, and the buffer's cap (0: unknown)
        std::string resetReason;         // why the buffer was last emptied, if it says

        bool operator==(const Snapshot&) const = default;
    };

    inline int Count(const Snapshot& s) { return static_cast<int>(s.titles.size()); }

    // How many steps are applied: the current row.
    inline int Current(const Snapshot& s) { return std::clamp(Count(s) - s.undoCount, 0, Count(s)); }

    inline int Rows(const Snapshot& s) { return Count(s) + 1; }

    // A row after the current one is a step that has been undone.
    inline bool Undone(const Snapshot& s, int row) { return row > Current(s); }

    // Steps from the current point to a row: negative undoes, positive redoes.
    inline int StepsTo(const Snapshot& s, int row) { return std::clamp(row, 0, Count(s)) - Current(s); }

    // A title as one readable line: control characters become spaces, the
    // ends are trimmed, and a very long one is cut.
    inline std::string CleanTitle(const std::string& title, size_t limit = 160)
    {
        std::string out;
        for (unsigned char c : title) out += c < 32 || c == 127 ? ' ' : static_cast<char>(c);
        const size_t first = out.find_first_not_of(' ');
        if (first == std::string::npos) return "(untitled)";
        out = out.substr(first, out.find_last_not_of(' ') - first + 1);
        if (out.size() > limit) out = out.substr(0, limit - 3) + "...";
        return out;
    }

    inline std::string RowText(const Snapshot& s, int row)
    {
        if (row <= 0) return "Start (before the oldest step)";
        if (row > Count(s)) return {};
        return std::to_string(row) + ".  " + CleanTitle(s.titles[row - 1]);
    }

    inline std::string Bytes(size_t bytes)
    {
        char text[32];
        if (bytes < 1024) std::snprintf(text, sizeof(text), "%zu B", bytes);
        else if (bytes < 1024 * 1024) std::snprintf(text, sizeof(text), "%.1f KB", bytes / 1024.0);
        else std::snprintf(text, sizeof(text), "%.1f MB", bytes / (1024.0 * 1024.0));
        return text;
    }

    inline std::string MemoryText(size_t used, size_t limit)
    {
        if (!limit) return "Memory " + Bytes(used);
        const int percent = static_cast<int>(std::min<size_t>(999, (used * 100 + limit / 2) / limit));
        return "Memory " + Bytes(used) + " of " + Bytes(limit) + " (" + std::to_string(percent) + "%)";
    }

    inline std::string StatusText(const Snapshot& s)
    {
        std::string text;
        if (s.titles.empty())
            text = s.resetReason.empty() ? "No undo steps yet." : "History cleared: " + CleanTitle(s.resetReason) + ".";
        else
        {
            text = std::to_string(Count(s)) + (Count(s) == 1 ? " step" : " steps");
            if (s.undoCount > 0) text += ", " + std::to_string(std::min(s.undoCount, Count(s))) + " can be redone";
            text += ".";
        }
        if (s.recording) text += " An operation is recording.";
        return text + "\r\n" + MemoryText(s.used, s.limit) + ".";
    }

    struct JumpResult
    {
        int wanted = 0; // signed: negative undoes
        int done = 0;   // steps taken, always >= 0
        bool stopped = false;
    };

    // Takes |steps| Undo (steps < 0) or Redo (steps > 0) steps, one at a time,
    // stopping at the first the editor refuses. step(redo) returns whether it
    // moved the buffer.
    template<class Step> JumpResult Jump(int steps, Step step)
    {
        JumpResult result;
        result.wanted = steps;
        const bool redo = steps > 0;
        for (int i = 0, n = steps < 0 ? -steps : steps; i < n; ++i)
        {
            if (!step(redo)) { result.stopped = true; break; }
            ++result.done;
        }
        return result;
    }

    inline std::string JumpReport(const JumpResult& r)
    {
        if (r.wanted == 0) return "Already at that step.";
        const bool redo = r.wanted > 0;
        const int wanted = redo ? r.wanted : -r.wanted;
        const std::string verb = redo ? "Redid" : "Undid";
        const auto plural = [](int n) { return std::to_string(n) + (n == 1 ? " step" : " steps"); };
        if (!r.stopped) return verb + " " + plural(wanted) + ".";
        if (r.done == 0) return std::string("The editor would not ") + (redo ? "redo" : "undo") + " that step; nothing changed.";
        return verb + " " + std::to_string(r.done) + " of " + plural(wanted) + "; the editor would not go further.";
    }
}
