#pragma once
// The pure part of the Animation Browser's import options (Merge sequences,
// Overwrite existing sequences, Keep Notifies): which checkboxes apply, the
// skeleton check a merge needs, and what happens to each sequence in the
// file. No engine and no Windows, so the tests compile it alone.
#include <cstddef>
#include <string>
#include <vector>

namespace AnimImportOptions
{
    struct Options
    {
        bool merge = false;      // add the file's sequences to the existing set
        bool overwrite = false;  // replace sequences of the same name (merge only)
        bool keepNotifies = false;
    };

    // Overwrite only means something when merging (a plain import replaces the
    // whole set anyway), and Keep Notifies only when a sequence can be replaced.
    inline bool OverwriteApplies(bool merge) { return merge; }
    inline bool KeepNotifiesApplies(bool merge, bool overwrite) { return !merge || overwrite; }
    inline Options Normalize(Options o)
    {
        if (!OverwriteApplies(o.merge)) o.overwrite = false;
        if (!KeepNotifiesApplies(o.merge, o.overwrite)) o.keepNotifies = false;
        return o;
    }

    // FNames compare without case.
    inline bool SameName(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
            if (x != y) return false;
        }
        return true;
    }
    inline int IndexOf(const std::vector<std::string>& names, const std::string& name)
    {
        for (size_t i = 0; i < names.size(); ++i)
            if (SameName(names[i], name)) return static_cast<int>(i);
        return -1;
    }

    // Each motion chunk stores one track per reference bone, in bone order, so
    // sequences can only share a set whose bones match one for one. Returns an
    // empty string when they do, otherwise the reason.
    inline std::string CompareSkeletons(const std::vector<std::string>& existing,
                                        const std::vector<std::string>& incoming)
    {
        if (existing.size() != incoming.size())
            return "The file's skeleton has " + std::to_string(incoming.size()) +
                   " bones but the animation set has " + std::to_string(existing.size()) + ".";
        for (size_t i = 0; i < existing.size(); ++i)
            if (!SameName(existing[i], incoming[i]))
                return "Bone " + std::to_string(i) + " is '" + incoming[i] + "' in the file but '" +
                       existing[i] + "' in the animation set.";
        return {};
    }

    enum class Mode
    {
        Create,      // no set of that name yet: the engine's own import
        ReplaceAll,  // set exists, not merging: the file's sequences replace all of it
        Merge,       // set exists, merging: sequences are added (and maybe replaced)
    };
    enum class Action { Append, Replace, Skip };

    struct Step
    {
        int incoming = -1;  // index in the file's (imported) sequences
        Action action = Action::Skip;
        int existing = -1;  // index in the set (Replace), or of the earlier same-named sequence (Skip)
        bool keepNotifies = false;
        bool duplicate = false;  // Skip because the file repeats an earlier name
    };

    // Replace-all with Keep Notifies: the old sequence whose notifies move onto
    // the new sequence of the same name.
    struct Carry
    {
        int incoming = -1, existing = -1;
    };

    struct Plan
    {
        Mode mode = Mode::Create;
        Options options;
        std::string error;  // non-empty: refuse, change nothing
        std::vector<Step> steps;  // Merge
        std::vector<Carry> carries;  // ReplaceAll
        int appended = 0, replaced = 0, skipped = 0, notifiesKept = 0;
        int existingCount = 0, incomingCount = 0;

        bool Refused() const { return !error.empty(); }
        // Anything in the existing set gets thrown away or rewritten.
        bool Destructive() const { return mode == Mode::ReplaceAll || replaced > 0; }
        bool ChangesSomething() const { return mode != Mode::Merge || appended > 0 || replaced > 0; }
    };

    struct Set
    {
        std::vector<std::string> sequences, bones;
        std::vector<int> notifyCounts;  // per sequence; may be empty
    };

    inline int NotifyCount(const Set& s, int index)
    {
        return index >= 0 && static_cast<size_t>(index) < s.notifyCounts.size() ? s.notifyCounts[static_cast<size_t>(index)] : 0;
    }

    inline Plan MakePlan(bool existingSet, const Set& existing, const Set& incoming, Options requested)
    {
        Plan plan;
        plan.options = Normalize(requested);
        plan.existingCount = static_cast<int>(existing.sequences.size());
        plan.incomingCount = static_cast<int>(incoming.sequences.size());
        if (!existingSet)
        {
            plan.mode = Mode::Create;
            return plan;
        }
        if (incoming.sequences.empty())
        {
            plan.error = "The file has no animation sequences.";
            return plan;
        }
        if (!plan.options.merge)
        {
            plan.mode = Mode::ReplaceAll;
            if (plan.options.keepNotifies)
            {
                std::vector<bool> used(existing.sequences.size(), false);
                for (size_t i = 0; i < incoming.sequences.size(); ++i)
                {
                    const int j = IndexOf(existing.sequences, incoming.sequences[i]);
                    if (j < 0 || used[static_cast<size_t>(j)]) continue;
                    used[static_cast<size_t>(j)] = true;
                    plan.carries.push_back({static_cast<int>(i), j});
                    plan.notifiesKept += NotifyCount(existing, j);
                }
            }
            return plan;
        }

        plan.mode = Mode::Merge;
        plan.error = CompareSkeletons(existing.bones, incoming.bones);
        if (plan.Refused()) return plan;
        for (size_t i = 0; i < incoming.sequences.size(); ++i)
        {
            Step step;
            step.incoming = static_cast<int>(i);
            std::vector<std::string> earlier(incoming.sequences.begin(), incoming.sequences.begin() + static_cast<std::ptrdiff_t>(i));
            const int repeat = IndexOf(earlier, incoming.sequences[i]);
            const int j = IndexOf(existing.sequences, incoming.sequences[i]);
            if (repeat >= 0)
            {
                step.action = Action::Skip;
                step.existing = repeat;
                step.duplicate = true;
                ++plan.skipped;
            }
            else if (j < 0)
            {
                step.action = Action::Append;
                ++plan.appended;
            }
            else if (plan.options.overwrite)
            {
                step.action = Action::Replace;
                step.existing = j;
                step.keepNotifies = plan.options.keepNotifies;
                if (step.keepNotifies) plan.notifiesKept += NotifyCount(existing, j);
                ++plan.replaced;
            }
            else
            {
                step.action = Action::Skip;
                step.existing = j;
                ++plan.skipped;
            }
            plan.steps.push_back(step);
        }
        return plan;
    }

    inline std::string Plural(int n, const char* one, const char* many)
    {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    inline std::string NameList(const std::vector<std::string>& names, size_t limit = 8)
    {
        std::string out;
        for (size_t i = 0; i < names.size() && i < limit; ++i) out += (i ? ", " : "") + names[i];
        if (names.size() > limit) out += ", ... (" + std::to_string(names.size() - limit) + " more)";
        return out;
    }

    // Text for the Yes/No box shown before a destructive import.
    inline std::string ConfirmText(const Plan& plan, const std::string& setName, const Set& incoming)
    {
        std::string text;
        if (plan.mode == Mode::ReplaceAll)
        {
            text = "Animation set '" + setName + "' already exists with " + Plural(plan.existingCount, "sequence", "sequences") +
                   ".\n\nImporting without 'Merge sequences into existing' replaces all of them with the " +
                   Plural(plan.incomingCount, "sequence", "sequences") + " in the file.";
            if (plan.options.keepNotifies)
                text += plan.carries.empty()
                    ? "\n\nNo sequence in the file has the name of an existing one, so no notifies are kept."
                    : "\n\nNotifies are kept on " + Plural(static_cast<int>(plan.carries.size()), "sequence", "sequences") +
                      " with matching names (" + Plural(plan.notifiesKept, "notify", "notifies") + ").";
            else
                text += "\n\nAll existing notifies are discarded.";
        }
        else
        {
            std::vector<std::string> replaced, added;
            for (const Step& s : plan.steps)
            {
                if (s.action == Action::Replace) replaced.push_back(incoming.sequences[static_cast<size_t>(s.incoming)]);
                if (s.action == Action::Append) added.push_back(incoming.sequences[static_cast<size_t>(s.incoming)]);
            }
            text = "Merge into animation set '" + setName + "':\n\n";
            if (!replaced.empty())
                text += "Overwrite " + Plural(static_cast<int>(replaced.size()), "sequence", "sequences") + ": " + NameList(replaced) +
                        (plan.options.keepNotifies ? " (keeping " + Plural(plan.notifiesKept, "notify", "notifies") + ")"
                                                   : " (their notifies are discarded)") + "\n";
            if (!added.empty())
                text += "Add " + Plural(static_cast<int>(added.size()), "sequence", "sequences") + ": " + NameList(added) + "\n";
            if (plan.skipped) text += "Skip " + Plural(plan.skipped, "sequence", "sequences") + " (repeated in the file)\n";
        }
        if (text.empty() || text.back() != '\n') text += "\n";
        return text + "\nThis cannot be undone. Continue?";
    }

    // One-line result for the log, and the message when a merge changes nothing.
    inline std::string Summary(const Plan& plan)
    {
        switch (plan.mode)
        {
        case Mode::Create: return "created a new animation set";
        case Mode::ReplaceAll:
            return "replaced " + Plural(plan.existingCount, "sequence", "sequences") + " with " +
                   std::to_string(plan.incomingCount) + (plan.options.keepNotifies ? ", kept " + Plural(plan.notifiesKept, "notify", "notifies") : std::string{});
        case Mode::Merge:
            return "added " + std::to_string(plan.appended) + ", overwrote " + std::to_string(plan.replaced) +
                   (plan.options.keepNotifies && plan.replaced ? " (kept " + Plural(plan.notifiesKept, "notify", "notifies") + ")" : std::string{}) +
                   ", skipped " + std::to_string(plan.skipped);
        }
        return {};
    }

    inline std::string NothingToMergeText(const Plan& plan, const std::string& setName)
    {
        return "Every sequence in the file already exists in animation set '" + setName + "' (" +
               Plural(plan.skipped, "sequence", "sequences") + " skipped), so nothing was merged.\n\n"
               "Tick 'Overwrite existing sequences' to replace them.";
    }

    // The result of applying a Merge plan to the two sets' sequence lists: what
    // the set should contain afterwards. The native code checks the live arrays
    // against it.
    inline std::vector<std::string> MergedSequences(const Plan& plan, const Set& existing, const Set& incoming)
    {
        std::vector<std::string> out = existing.sequences;
        if (plan.mode == Mode::ReplaceAll) return incoming.sequences;
        if (plan.mode != Mode::Merge) return out;
        for (const Step& s : plan.steps)
            if (s.action == Action::Append) out.push_back(incoming.sequences[static_cast<size_t>(s.incoming)]);
        return out;
    }
}
