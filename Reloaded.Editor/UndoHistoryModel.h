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

    // Named checkpoints: "before vent rework" on a row of the list. The
    // buffer has no ids of its own, so each transaction is given one when
    // first seen and the ids follow the buffer as it changes. A checkpoint
    // is tied to the transaction its row ends with (row N: the Nth step), or
    // to the start row; it lasts while that transaction is in the buffer and
    // goes when the transaction does: trimmed off the front when the buffer
    // is full, dropped from the redo end when a new step replaces undone
    // ones, or cleared by a reset (a map load). The start row's checkpoint
    // goes once anything is trimmed, since the start state is then gone.
    //
    // Prints are what the caller can read of each transaction, oldest first,
    // to tell them apart (the address of its title text, which stays put
    // while the transaction lives). Whenever the ids and the buffer's prints
    // disagree, the ids start afresh and every checkpoint goes: losing a
    // checkpoint is better than one that points at the wrong step.
    class Checkpoints
    {
    public:
        using Prints = std::vector<unsigned long long>;
        struct Mark
        {
            unsigned long long after = 0; // the transaction's id
            bool start = false;           // the start row instead
            std::string name;
        };
        static constexpr size_t kNameLimit = 60;

        bool Empty() const { return marks.empty(); }
        size_t Size() const { return marks.size(); }

        // Checks the ids against the buffer as it is now. Returns how many
        // checkpoints went because the two disagreed.
        size_t Observe(const Prints& now)
        {
            if (now == prints && ids.size() == prints.size()) return 0;
            return Fresh(now);
        }

        // The outermost UTransBuffer::Begin ran: it dropped the undone steps,
        // trimmed the oldest while the buffer was over its memory limit, and
        // added the new step at the end.
        size_t Began(const Prints& before, int undoBefore, const Prints& after, int undoAfter)
        {
            size_t gone = Observe(before);
            if (marks.empty()) return gone + Fresh(after);
            const int n = static_cast<int>(before.size());
            const int kept = n - std::clamp(undoBefore, 0, n);
            const int trimmed = kept + 1 - static_cast<int>(after.size());
            if (undoAfter != 0 || after.empty() || trimmed < 0 || trimmed > kept
                || !std::equal(before.begin() + trimmed, before.begin() + kept, after.begin()))
                return gone + Fresh(after);
            ids.erase(ids.begin() + kept, ids.end());
            ids.erase(ids.begin(), ids.begin() + trimmed);
            ids.push_back(next++);
            prints = after;
            if (trimmed > 0)
            {
                const auto was = marks.size();
                std::erase_if(marks, [](const Mark& m) { return m.start; });
                gone += was - marks.size();
            }
            return gone + Prune();
        }

        // The buffer was emptied.
        size_t Reset()
        {
            const size_t gone = marks.size();
            marks.clear();
            ids.clear();
            prints.clear();
            return gone;
        }

        // Names a row of the buffer as it is now, renaming a checkpoint
        // already there. False for an empty name or a row off the list.
        bool Set(const Prints& now, int row, const std::string& name)
        {
            Observe(now);
            const auto clean = Name(name);
            if (clean.empty() || row < 0 || row > static_cast<int>(ids.size())) return false;
            Remove(row);
            Mark m;
            m.start = row == 0;
            m.after = row == 0 ? 0 : ids[row - 1];
            m.name = clean;
            marks.push_back(m);
            return true;
        }

        bool Remove(int row)
        {
            const auto was = marks.size();
            std::erase_if(marks, [&](const Mark& m) { return RowOf(m) == row; });
            return marks.size() != was;
        }

        // The row a checkpoint is on, or -1 when its step is gone.
        int RowOf(const Mark& m) const
        {
            if (m.start) return 0;
            const auto at = std::find(ids.begin(), ids.end(), m.after);
            return at == ids.end() ? -1 : static_cast<int>(at - ids.begin()) + 1;
        }

        // Row and name of every checkpoint, oldest row first.
        std::vector<std::pair<int, std::string>> List() const
        {
            std::vector<std::pair<int, std::string>> out;
            for (const auto& m : marks)
                if (const int row = RowOf(m); row >= 0) out.emplace_back(row, m.name);
            std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            return out;
        }

        std::string NameAt(int row) const
        {
            for (const auto& m : marks)
                if (RowOf(m) == row) return m.name;
            return {};
        }

        // "Checkpoint 3": the first such name not in use.
        std::string NextName() const
        {
            for (int i = 1;; ++i)
            {
                const auto name = "Checkpoint " + std::to_string(i);
                if (std::none_of(marks.begin(), marks.end(), [&](const Mark& m) { return m.name == name; })) return name;
            }
        }

        // One line, trimmed, at most kNameLimit characters; empty when blank.
        static std::string Name(const std::string& text)
        {
            std::string out;
            for (unsigned char c : text) out += c < 32 || c == 127 ? ' ' : static_cast<char>(c);
            const size_t first = out.find_first_not_of(' ');
            if (first == std::string::npos) return {};
            out = out.substr(first, out.find_last_not_of(' ') - first + 1);
            if (out.size() > kNameLimit) out = out.substr(0, kNameLimit);
            return out;
        }

    private:
        Prints prints;
        std::vector<unsigned long long> ids; // one per transaction, oldest first
        unsigned long long next = 1;
        std::vector<Mark> marks;

        size_t Fresh(const Prints& now)
        {
            const size_t gone = marks.size();
            marks.clear();
            prints = now;
            ids.clear();
            for (size_t i = 0; i < now.size(); ++i) ids.push_back(next++);
            return gone;
        }
        size_t Prune()
        {
            const auto was = marks.size();
            std::erase_if(marks, [&](const Mark& m) { return RowOf(m) < 0; });
            return was - marks.size();
        }
    };

    // A row as the list and the copied text show it with its checkpoint.
    inline std::string MarkedRowText(const Snapshot& s, int row, const std::string& checkpoint)
    {
        auto text = RowText(s, row);
        if (!checkpoint.empty()) text += "   [" + checkpoint + "]";
        return text;
    }

    // A checkpoint in the quick-jump list: its name, then where it is.
    inline std::string JumpItemText(const Snapshot& s, int row, const std::string& name)
    {
        std::string where = row <= 0 ? "start" : "step " + std::to_string(row) + ": " + CleanTitle(s.titles[std::min(row, Count(s)) - 1], 48);
        if (row == Current(s)) where += ", current";
        else if (Undone(s, row)) where += ", undone";
        return name + "  (" + where + ")";
    }

    inline std::string CheckpointsGoneText(size_t gone)
    {
        if (!gone) return {};
        return gone == 1 ? "A checkpoint went with its step." : std::to_string(gone) + " checkpoints went with their steps.";
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
