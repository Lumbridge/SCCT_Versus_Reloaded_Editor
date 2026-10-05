#include "../Reloaded.Editor/UndoHistoryModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace UndoHistory;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}

// A stand-in for the editor's buffer: Undo and Redo move UndoCount, and a
// refusal (an operation recording, or nothing left) leaves it alone.
struct Buffer
{
    Snapshot s;
    int refuseAfter = 1 << 30;
    int calls = 0;
    bool Step(bool redo)
    {
        if (calls++ >= refuseAfter || s.recording) return false;
        if (redo) { if (s.undoCount == 0) return false; --s.undoCount; }
        else { if (s.undoCount >= Count(s)) return false; ++s.undoCount; }
        return true;
    }
};

int main()
{
    try
    {
        Snapshot s;
        s.titles = {"Move actors", "Add actor", "Change property", "Create or resize blockout", "Turn actors"};

        // Rows: one before the oldest step, then one per step.
        Check(Rows(s) == 6 && Current(s) == 5, "all applied: current is the last row");
        Check(!Undone(s, 5) && !Undone(s, 0), "nothing undone");
        Check(RowText(s, 0) == "Start (before the oldest step)", "start row");
        Check(RowText(s, 1) == "1.  Move actors" && RowText(s, 5) == "5.  Turn actors", "numbered titles");
        Check(RowText(s, 6).empty(), "past the end");

        // Two undone: they are below the current point and greyed.
        s.undoCount = 2;
        Check(Current(s) == 3 && Undone(s, 4) && Undone(s, 5) && !Undone(s, 3), "undone rows follow the current one");
        Check(StepsTo(s, 1) == -2 && StepsTo(s, 5) == 2 && StepsTo(s, 3) == 0 && StepsTo(s, 0) == -3, "steps to a row");
        Check(StepsTo(s, 99) == 2 && StepsTo(s, -4) == -3, "rows outside the list clamp");

        // A count the editor reports past the list never goes below the start.
        Snapshot odd = s; odd.undoCount = 9;
        Check(Current(odd) == 0, "undo count beyond the list");

        // Jumping walks one step at a time and lands on the row.
        Buffer b; b.s = s;
        auto r = Jump(StepsTo(b.s, 1), [&](bool redo) { return b.Step(redo); });
        Check(r.done == 2 && !r.stopped && Current(b.s) == 1 && b.calls == 2, "undo two steps");
        Check(JumpReport(r) == "Undid 2 steps.", "undo report");
        r = Jump(StepsTo(b.s, 5), [&](bool redo) { return b.Step(redo); });
        Check(r.done == 4 && Current(b.s) == 5 && JumpReport(r) == "Redid 4 steps.", "redo to the end");
        r = Jump(StepsTo(b.s, 4), [&](bool redo) { return b.Step(redo); });
        Check(r.done == 1 && Current(b.s) == 4 && JumpReport(r) == "Undid 1 step.", "one step");

        // A refusal stops the walk where it is, without trying further.
        b.calls = 0; b.refuseAfter = 2;
        r = Jump(StepsTo(b.s, 0), [&](bool redo) { return b.Step(redo); });
        Check(r.stopped && r.done == 2 && Current(b.s) == 2 && b.calls == 3, "stopped after two");
        Check(JumpReport(r) == "Undid 2 of 4 steps; the editor would not go further.", "stopped report");
        b.calls = 0; b.refuseAfter = 1 << 30; b.s.recording = true;
        r = Jump(StepsTo(b.s, 5), [&](bool redo) { return b.Step(redo); });
        Check(r.stopped && r.done == 0 && Current(b.s) == 2, "refused while recording");
        Check(JumpReport(r) == "The editor would not redo that step; nothing changed.", "refusal report");
        r = Jump(0, [&](bool) { Check(false, "no step for zero"); return true; });
        Check(r.done == 0 && !r.stopped && JumpReport(r) == "Already at that step.", "zero steps");

        // Long lists: a thousand steps undo to the start in a thousand calls.
        Buffer longer;
        for (int i = 0; i < 1000; ++i) longer.s.titles.push_back("Step " + std::to_string(i));
        r = Jump(StepsTo(longer.s, 0), [&](bool redo) { return longer.Step(redo); });
        Check(r.done == 1000 && Current(longer.s) == 0 && longer.calls == 1000, "a thousand steps");
        Check(RowText(longer.s, 1000) == "1000.  Step 999", "last of a thousand");

        // Titles.
        Check(CleanTitle("  Move\tactors\r\n") == "Move actors", "control characters and ends");
        Check(CleanTitle("") == "(untitled)" && CleanTitle(" \t ") == "(untitled)", "empty title");
        Check(CleanTitle(std::string(400, 'x')).size() == 160 && CleanTitle(std::string(400, 'x')).ends_with("..."), "long title");

        // Memory.
        Check(Bytes(512) == "512 B" && Bytes(1536) == "1.5 KB" && Bytes(3 * 1024 * 1024) == "3.0 MB", "byte sizes");
        Check(MemoryText(4 * 1024 * 1024, 16 * 1024 * 1024) == "Memory 4.0 MB of 16.0 MB (25%)", "memory against the limit");
        Check(MemoryText(2048, 0) == "Memory 2.0 KB", "limit unknown");

        // Status line.
        Check(StatusText(s).starts_with("5 steps, 2 can be redone.\r\nMemory 0 B."), "status with redo");
        Snapshot empty;
        Check(StatusText(empty).starts_with("No undo steps yet."), "empty buffer");
        empty.resetReason = "Map load";
        Check(StatusText(empty).starts_with("History cleared: Map load."), "reset reason");
        Snapshot one; one.titles = {"Add actor"}; one.recording = true; one.limit = 100;
        Check(StatusText(one) == "1 step. An operation is recording.\r\nMemory 0 B of 100 B (0%).", "one step, recording");

        // Snapshots compare by value, so an unchanged buffer is not redrawn.
        Snapshot copy = s;
        Check(copy == s, "equal snapshots");
        copy.titles.back() = "Other";
        Check(!(copy == s), "a changed title differs");

        // Checkpoints follow a stand-in buffer whose prints are made up ids.
        {
            using P = Checkpoints::Prints;
            Checkpoints c;
            P buf = {11, 12, 13, 14};
            Check(c.Empty() && c.NextName() == "Checkpoint 1", "no checkpoints yet");
            Check(c.Set(buf, 2, "  before vent\trework  ") && c.NameAt(2) == "before vent rework", "named and cleaned");
            Check(!c.Set(buf, 2, "   ") && !c.Set(buf, 5, "x") && !c.Set(buf, -1, "x"), "blank names and rows off the list refused");
            Check(c.Set(buf, 0, "clean slate") && c.Set(buf, 4, "Checkpoint 1") && c.NextName() == "Checkpoint 2", "start row and next name");
            auto list = c.List();
            Check(list.size() == 3 && list[0] == std::pair<int, std::string>{0, "clean slate"} && list[1].first == 2 && list[2].first == 4, "listed by row");
            Check(c.Set(buf, 2, "vents v2") && c.Size() == 3 && c.NameAt(2) == "vents v2", "setting a marked row renames it");
            Check(Checkpoints::Name(std::string(100, 'n')).size() == Checkpoints::kNameLimit, "long name cut");

            // Undo two (undoCount 2), then a new step: 13 and 14 go, and the checkpoint on 14 with them.
            P after = {11, 12, 15};
            Check(c.Observe(buf) == 0, "nothing moved");
            Check(c.Began(buf, 2, after, 0) == 1 && c.NameAt(2) == "vents v2" && c.NameAt(0) == "clean slate" && c.NameAt(4).empty(),
                  "a dropped redo step takes its checkpoint");
            buf = after;
            // Full buffer: one step trimmed off the front as the next is added. Rows shift down; the start goes.
            after = {12, 15, 16};
            Check(c.Began(buf, 0, after, 0) == 1 && c.NameAt(1) == "vents v2" && c.NameAt(0).empty(), "trim moves rows and ends the start checkpoint");
            buf = after;
            Check(c.Set(buf, 3, "latest") && c.NameAt(3) == "latest", "checkpoint on the newest step");
            after = {16, 17};
            Check(c.Began(buf, 0, after, 0) == 1 && c.NameAt(1) == "latest" && c.Size() == 1, "two trimmed at once");
            buf = after;

            // Everything undone then a new step: the start checkpoint survives (nothing trimmed).
            Check(c.Set(buf, 0, "start"), "start again");
            after = {18};
            Check(c.Began(buf, 2, after, 0) == 1 && c.NameAt(0) == "start" && c.Size() == 1, "start kept over a replaced buffer");
            buf = after;

            // Prints that do not fit what Begin can do: everything goes rather than point at the wrong step.
            Check(c.Set(buf, 1, "one") && c.Began(buf, 0, P{99, 19}, 0) == 2 && c.Empty(), "a changed survivor resets");
            Check(c.Set(buf, 1, "one") && c.Began(buf, 0, {18, 19}, 1) == 1, "redo left after Begin resets");
            buf = {18, 19};
            Check(c.Set(buf, 1, "one") && c.Began(buf, 0, {18, 19, 20, 21}, 0) == 1, "two steps added resets");
            buf = {18, 19, 20, 21};
            Check(c.Set(buf, 2, "two") && c.Observe({18, 19, 21, 20}) == 1 && c.Empty(), "reordered buffer resets");
            buf = {18, 19, 21, 20};
            Check(c.Set(buf, 2, "two") && c.Observe(buf) == 0 && c.Reset() == 1 && c.Empty(), "reset clears");
            Check(c.Began({}, 0, {30}, 0) == 0 && c.Empty(), "Begin without checkpoints");

            // Undo and redo leave checkpoints where they are.
            buf = {30, 31, 32};
            c.Set(buf, 3, "end");
            Check(c.Observe(buf) == 0 && c.NameAt(3) == "end", "unchanged prints");
            Check(c.Remove(3) && !c.Remove(3) && c.Empty(), "removed");

            // Wording.
            Snapshot w;
            w.titles = {"Move actors", "Add actor", "Turn actors"};
            w.undoCount = 1;
            Check(MarkedRowText(w, 2, "vents") == "2.  Add actor   [vents]" && MarkedRowText(w, 1, "") == "1.  Move actors", "marked rows");
            Check(JumpItemText(w, 2, "vents") == "vents  (step 2: Add actor, current)", "jump item at the current row");
            Check(JumpItemText(w, 3, "x") == "x  (step 3: Turn actors, undone)" && JumpItemText(w, 0, "s") == "s  (start)", "undone and start");
            Check(CheckpointsGoneText(0).empty() && CheckpointsGoneText(1) == "A checkpoint went with its step."
                      && CheckpointsGoneText(3) == "3 checkpoints went with their steps.", "gone text");

            // Jumping to a checkpoint uses the same stepping.
            Buffer jb;
            jb.s = w;
            auto jr = Jump(StepsTo(jb.s, 0), [&](bool redo) { return jb.Step(redo); });
            Check(jr.done == 2 && Current(jb.s) == 0, "jump to the start checkpoint");
        }

        std::cout << checks << " undo history checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
