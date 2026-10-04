#include "../Reloaded.Editor/AnimImportOptionsModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace AnimImportOptions;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int main()
{
    try
    {
        // Checkbox rules.
        Check(!OverwriteApplies(false) && OverwriteApplies(true), "overwrite needs merge");
        Check(KeepNotifiesApplies(false, false), "keep notifies on a full replace");
        Check(!KeepNotifiesApplies(true, false), "merge-only replaces nothing");
        Check(KeepNotifiesApplies(true, true), "keep notifies on overwrite");
        Options o = Normalize({false, true, true});
        Check(!o.merge && !o.overwrite && o.keepNotifies, "overwrite dropped without merge");
        o = Normalize({true, false, true});
        Check(o.merge && !o.overwrite && !o.keepNotifies, "keep dropped for merge-only");

        // Names and skeletons.
        Check(SameName("David_Play", "david_play") && !SameName("david_play", "david_plays"), "FName comparison");
        const std::vector<std::string> fan = {"Dummy02", "Dummy01", "MIs_ventfan2", "MIS_vent_fan_1", "MIS_vent_axe"};
        Check(CompareSkeletons(fan, {"dummy02", "DUMMY01", "mis_ventfan2", "MIS_vent_fan_1", "MIS_vent_axe"}).empty(), "case-insensitive bones");
        Check(Has(CompareSkeletons(fan, {"Dummy02", "Dummy01"}), "2 bones but the animation set has 5"), "bone count");
        Check(Has(CompareSkeletons(fan, {"Dummy02", "Dummy01", "MIs_ventfan2", "MIS_vent_fan_1", "Other"}), "Bone 4 is 'Other'"), "bone name");
        Check(Has(CompareSkeletons(fan, {"Dummy01", "Dummy02", "MIs_ventfan2", "MIS_vent_fan_1", "MIS_vent_axe"}), "Bone 0"), "bone order matters");

        const Set existing{{"david_play", "david_stop"}, fan, {4, 3}};
        const Set incoming{{"David_Play", "new_spin"}, fan, {0, 0}};

        // No set of that name: the engine's import, nothing to merge.
        Plan p = MakePlan(false, {}, incoming, {true, true, true});
        Check(p.mode == Mode::Create && !p.Refused() && !p.Destructive() && p.ChangesSomething(), "create");

        // Merge only: new names appended, existing ones kept.
        p = MakePlan(true, existing, incoming, {true, false, false});
        Check(p.mode == Mode::Merge && !p.Refused(), "merge plan");
        Check(p.appended == 1 && p.replaced == 0 && p.skipped == 1 && !p.Destructive(), "merge counts");
        Check(p.steps.size() == 2 && p.steps[0].action == Action::Skip && p.steps[0].existing == 0 && !p.steps[0].duplicate, "existing name skipped");
        Check(p.steps[1].action == Action::Append, "new name appended");
        Check((MergedSequences(p, existing, incoming) == std::vector<std::string>{"david_play", "david_stop", "new_spin"}), "merged list");
        Check(Has(Summary(p), "added 1, overwrote 0, skipped 1"), "merge summary");

        // Merge with nothing new.
        p = MakePlan(true, existing, {{"DAVID_STOP"}, fan, {}}, {true, false, false});
        Check(!p.ChangesSomething() && p.skipped == 1, "nothing to merge");
        Check(Has(NothingToMergeText(p, "ventilateur_anm"), "Overwrite existing sequences"), "nothing-to-merge hint");

        // Overwrite keeping notifies.
        p = MakePlan(true, existing, incoming, {true, true, true});
        Check(p.appended == 1 && p.replaced == 1 && p.skipped == 0 && p.notifiesKept == 4 && p.Destructive(), "overwrite counts");
        Check(p.steps[0].action == Action::Replace && p.steps[0].existing == 0 && p.steps[0].keepNotifies, "replace step keeps notifies");
        std::string text = ConfirmText(p, "ventilateur_anm", incoming);
        Check(Has(text, "Overwrite 1 sequence: David_Play (keeping 4 notifies)") && Has(text, "Add 1 sequence: new_spin") && Has(text, "cannot be undone"), "overwrite confirm");
        Check(Has(Summary(p), "overwrote 1 (kept 4 notifies)"), "overwrite summary");

        // Overwrite discarding notifies.
        p = MakePlan(true, existing, {{"david_stop"}, fan, {}}, {true, true, false});
        Check(p.replaced == 1 && p.appended == 0 && p.notifiesKept == 0 && !p.steps[0].keepNotifies, "overwrite without keep");
        Check(Has(ConfirmText(p, "a", {{"david_stop"}, fan, {}}), "their notifies are discarded"), "discard confirm");
        Check((MergedSequences(p, existing, {{"david_stop"}, fan, {}}) == existing.sequences), "overwrite keeps order");

        // A name repeated in the file is only used once.
        const Set repeated{{"new_a", "NEW_A", "david_play", "David_Play"}, fan, {}};
        p = MakePlan(true, existing, repeated, {true, true, false});
        Check(p.appended == 1 && p.replaced == 1 && p.skipped == 2, "repeats skipped");
        Check(p.steps[1].duplicate && p.steps[1].existing == 0 && p.steps[3].duplicate && p.steps[3].existing == 2, "repeat points at first use");
        Check(Has(ConfirmText(p, "a", repeated), "Skip 2 sequences (repeated in the file)"), "repeat confirm");

        // Merging needs the same skeleton; nothing is planned otherwise.
        p = MakePlan(true, existing, {{"x"}, {"Dummy02", "Dummy01", "MIs_ventfan2", "MIS_vent_fan_1"}, {}}, {true, true, true});
        Check(p.Refused() && p.steps.empty() && Has(p.error, "4 bones"), "skeleton mismatch refused");
        p = MakePlan(true, existing, {{}, fan, {}}, {true, false, false});
        Check(p.Refused() && Has(p.error, "no animation sequences"), "empty file refused");

        // Replace all: any skeleton, notifies optionally carried by name.
        const Set other{{"david_stop", "other", "DAVID_STOP"}, {"Root"}, {}};
        p = MakePlan(true, existing, other, {false, true, true});
        Check(p.mode == Mode::ReplaceAll && !p.Refused() && p.Destructive() && !p.options.overwrite, "replace all");
        Check(p.carries.size() == 1 && p.carries[0].incoming == 0 && p.carries[0].existing == 1 && p.notifiesKept == 3, "carry by name, once");
        text = ConfirmText(p, "ventilateur_anm", other);
        Check(Has(text, "replaces all of them with the 3 sequences") && Has(text, "kept on 1 sequence with matching names (3 notifies)"), "replace confirm");
        Check((MergedSequences(p, existing, other) == other.sequences), "replace result");
        p = MakePlan(true, existing, other, {false, false, false});
        Check(p.carries.empty() && Has(ConfirmText(p, "a", other), "All existing notifies are discarded"), "replace without keep");
        p = MakePlan(true, existing, {{"zzz"}, fan, {}}, {false, false, true});
        Check(p.carries.empty() && Has(ConfirmText(p, "a", {{"zzz"}, fan, {}}), "no notifies are kept"), "nothing to carry");
        Check(Has(Summary(MakePlan(true, existing, other, {false, false, true})), "replaced 2 sequences with 3, kept 3 notifies"), "replace summary");

        // Long lists are shortened.
        Check(NameList({"a", "b", "c"}, 2) == "a, b, ... (1 more)", "name list");

        std::cout << "AnimImportOptionsModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
