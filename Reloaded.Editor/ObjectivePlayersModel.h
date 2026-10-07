#pragma once
#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Objective player counts: objectives that are only in the match when the lobby has
// a number of players, e.g. a third laptop that only 3v3 games play for. The editor
// compiles the script below into the map package (MyLevel) and places one actor of
// it, which lists the objectives with a rule.
//
// The server decides once, as the map loads and before the missions prepare their
// objectives. The lobby's player count reaches the map as the URL option NBPlayers,
// which SGameInfo.InitGame also reads into PlayersToWait; both are known before any
// actor's PostBeginPlay. An objective outside its range comes out of its mission's
// Objectives list and is switched off with the game's own Desactivate (as the stock
// RandomObjectives choice does), which also hides its terminals, bomb targets and
// flags. The missions then need fewer completions where the remaining objectives
// could not otherwise reach their MinimumObjectives, so the match stays winnable.
// Clients are not touched: the server's mission is the one that scores.
namespace ObjectivePlayers
{
// Raise with every change to Script(), and give the class a new name: a map's actor
// of an older version is read and replaced instead of recompiling a class in use.
constexpr int Version = 1;
constexpr const char* ClassName = "SObjectivePlayers";
// Objectives one map can give a rule.
constexpr int Capacity = 64;
// The largest count the panel offers; the game's own limit is 16 (MaxPlayers).
constexpr int MostPlayers = 16;
// How deep missions may nest (top mission > zone > objective is 2).
constexpr int MaxDepth = 8;

// A rule: the objective is in the match with Minimum to Maximum players. 0 is no limit.
struct Rule
{
    int minimum = 0, maximum = 0;
    bool operator==(const Rule&) const = default;
};
inline bool Empty(const Rule& r) { return r.minimum == 0 && r.maximum == 0; }
inline bool Keeps(const Rule& r, int players)
{
    return (r.minimum == 0 || players >= r.minimum) && (r.maximum == 0 || players <= r.maximum);
}
inline void Check(const Rule& r)
{
    if (r.minimum < 0 || r.minimum > MostPlayers || r.maximum < 0 || r.maximum > MostPlayers)
        throw std::runtime_error("Player counts run from 1 to " + std::to_string(MostPlayers) + ", or Any.");
    if (r.minimum && r.maximum && r.minimum > r.maximum)
        throw std::runtime_error("The minimum (" + std::to_string(r.minimum) + " players) is more than the maximum (" +
                                 std::to_string(r.maximum) + ").");
}
// "6+ players", "up to 4 players", "6 players", "4-8 players"; empty for no rule.
inline std::string Short(const Rule& r)
{
    if (Empty(r)) return {};
    if (!r.maximum) return std::to_string(r.minimum) + "+ players";
    if (!r.minimum) return "up to " + std::to_string(r.maximum) + " players";
    if (r.minimum == r.maximum) return std::to_string(r.minimum) + " players";
    return std::to_string(r.minimum) + "-" + std::to_string(r.maximum) + " players";
}
// A sentence for the status line.
inline std::string Describe(const Rule& r)
{
    if (Empty(r)) return "in every match";
    if (!r.maximum) return "only in matches with " + std::to_string(r.minimum) + " or more players";
    if (!r.minimum) return "only in matches with up to " + std::to_string(r.maximum) + " players";
    if (r.minimum == r.maximum) return "only in matches with exactly " + std::to_string(r.minimum) + " players";
    return "only in matches with " + std::to_string(r.minimum) + " to " + std::to_string(r.maximum) + " players";
}
// The sheet's choices for one end of a range, and back.
inline std::vector<std::string> Choices()
{
    std::vector<std::string> out{"Any"};
    for (int n = 1; n <= MostPlayers; ++n) out.push_back(std::to_string(n));
    return out;
}
inline std::string ChoiceText(int count) { return count ? std::to_string(count) : std::string("Any"); }
inline int ChoiceCount(const std::string& text)
{
    if (text.empty() || text == "Any") return 0;
    size_t used = 0;
    int value = 0;
    try { value = std::stoi(text, &used); }
    catch (const std::exception&) { used = 0; }
    if (used != text.size() || value < 1 || value > MostPlayers)
        throw std::runtime_error("Choose a player count from 1 to " + std::to_string(MostPlayers) + ", or Any.");
    return value;
}

inline std::string Fold(std::string text)
{
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
// An object reference as the editor writes it, SObjective'MyLevel.SObjective12', as
// its path; a bare path stays as it is, and None is empty.
inline std::string ReferencePath(const std::string& reference)
{
    const auto open = reference.find('\'');
    auto path = open == std::string::npos ? reference : reference.substr(open + 1, reference.rfind('\'') - open - 1);
    return Fold(path) == "none" ? std::string() : path;
}

// The missions as the map has them, keyed by folded path: their Objectives (paths,
// missions among them) and MinimumObjectives.
struct Mission
{
    std::vector<std::string> objectives;
    int minimum = 0;
};
using Missions = std::map<std::string, Mission>;
using Rules = std::map<std::string, Rule>; // folded objective path -> rule

// A mission with no mission above it: the game mode's own, never left out.
inline std::vector<std::string> Roots(const Missions& missions)
{
    std::vector<std::string> out;
    for (const auto& [path, mission] : missions)
    {
        bool listed = false;
        for (const auto& [other, parent] : missions)
            for (const auto& child : parent.objectives)
                if (Fold(child) == path && other != path) listed = true;
        if (!listed) out.push_back(path);
    }
    return out;
}

// What a match with this many players plays: the script's Prune, step for step.
struct Outcome
{
    Missions missions;                  // after pruning
    std::vector<std::string> left;      // objectives (and missions) left out
};
namespace Detail
{
inline int Needs(const Mission& m) { return m.minimum > 0 ? m.minimum : static_cast<int>(m.objectives.size()); }
inline bool Excluded(const Rules& rules, const std::string& path, int players)
{
    const auto rule = rules.find(Fold(path));
    return rule != rules.end() && !Keeps(rule->second, players);
}
inline void Drop(Outcome& out, const std::string& path, int depth)
{
    out.left.push_back(path);
    const auto sub = out.missions.find(Fold(path));
    if (sub == out.missions.end() || depth >= MaxDepth) return;
    for (const auto& child : sub->second.objectives)
        if (!child.empty()) Drop(out, child, depth + 1);
}
inline void Prune(Outcome& out, const Rules& rules, const std::string& key, int players, int depth)
{
    auto& m = out.missions.at(key);
    int fewer = 0, capacity = 0;
    for (int i = static_cast<int>(m.objectives.size()) - 1; i >= 0; --i)
    {
        const auto child = m.objectives[i];
        if (child.empty()) continue;
        const auto sub = out.missions.find(Fold(child));
        const bool mission = sub != out.missions.end();
        if (Excluded(rules, child, players))
        {
            if (mission) fewer += Needs(sub->second);
            Drop(out, child, depth);
            out.missions.at(key).objectives.erase(out.missions.at(key).objectives.begin() + i);
            continue;
        }
        if (!mission || depth >= MaxDepth)
        {
            ++capacity;
            continue;
        }
        const int before = Needs(sub->second);
        Prune(out, rules, sub->first, players, depth + 1);
        const auto& after = out.missions.at(sub->first);
        if (after.objectives.empty())
        {
            fewer += before;
            Drop(out, child, depth);
            out.missions.at(key).objectives.erase(out.missions.at(key).objectives.begin() + i);
            continue;
        }
        fewer += before - Needs(after);
        capacity += Needs(after);
    }
    auto& self = out.missions.at(key);
    if (self.minimum > 0)
    {
        self.minimum = (std::min)(self.minimum - fewer, capacity);
        if (self.minimum < 1 && !self.objectives.empty()) self.minimum = 1;
        if (self.minimum < 0) self.minimum = 0;
    }
}
} // namespace Detail

inline Outcome Apply(const Missions& missions, const Rules& rules, int players)
{
    Outcome out{missions, {}};
    if (players <= 0) return out;
    for (const auto& root : Roots(missions)) Detail::Prune(out, rules, root, players, 0);
    return out;
}

// What goes wrong for some lobby size: a top mission left with nothing to do. One
// line per run of player counts that share the problem.
inline std::vector<std::string> Problems(const Missions& missions, const Rules& rules, const std::map<std::string, std::string>& names = {})
{
    std::vector<std::string> out;
    for (const auto& root : Roots(missions))
    {
        int first = 0;
        auto flush = [&](int last) {
            if (!first) return;
            const auto name = names.count(root) ? names.at(root) : root;
            const auto counts = first == last ? std::to_string(first) + " player" + (first == 1 ? "" : "s")
                                              : std::to_string(first) + " to " + std::to_string(last) + " players";
            out.push_back("With " + counts + ", " + name + " has no objectives left: the player rules leave nothing to play.");
            first = 0;
        };
        for (int n = 1; n <= MostPlayers; ++n)
        {
            const auto outcome = Apply(missions, rules, n);
            const bool empty = outcome.missions.at(root).objectives.empty() && !missions.at(root).objectives.empty();
            if (empty && !first) first = n;
            if (!empty) flush(n - 1);
        }
        flush(MostPlayers);
    }
    return out;
}
// The objectives a lobby size plays, for the status line: "6 players: 5 objectives, the spies need 3".
inline std::string Summary(const Missions& missions, const Rules& rules, int players)
{
    const auto outcome = Apply(missions, rules, players);
    int objectives = 0, needed = 0;
    for (const auto& root : Roots(missions))
    {
        const auto& top = outcome.missions.at(root);
        needed += Detail::Needs(top);
        std::vector<std::string> stack(top.objectives.begin(), top.objectives.end());
        for (int guard = 0; !stack.empty() && guard < 4096; ++guard)
        {
            const auto child = stack.back();
            stack.pop_back();
            const auto sub = outcome.missions.find(Fold(child));
            if (sub == outcome.missions.end()) ++objectives;
            else stack.insert(stack.end(), sub->second.objectives.begin(), sub->second.objectives.end());
        }
    }
    return std::to_string(players) + " player" + (players == 1 ? "" : "s") + ": " + std::to_string(objectives) + " objective" +
           (objectives == 1 ? "" : "s") + ", the spies need " + std::to_string(needed);
}

// The UnrealScript compiled into the map. Only the server's copy acts (Level.Game is
// None on clients). In an editor Play Level the lobby count is PlayLevelPlayers, and
// 0 there keeps every objective so a solo test is not left with less to do.
inline std::string Script()
{
    const auto cap = std::to_string(Capacity), depth = std::to_string(MaxDepth);
    return std::string(
        "//=============================================================================\r\n"
        "// ") + ClassName + ": objectives that are only in matches with some number of\r\n"
        "// players, written by RE+ Map Design. The server leaves the others out as the\r\n"
        "// map loads, before the missions prepare their objectives.\r\n"
        "//=============================================================================\r\n"
        "class " + ClassName + " extends Info\r\n"
        "\tplaceable;\r\n"
        "\r\n"
        "var() SObjective Objective[" + cap + "];\r\n"
        "var() byte MinPlayers[" + cap + "];\r\n"
        "var() byte MaxPlayers[" + cap + "];\r\n"
        "var() byte PlayLevelPlayers;\r\n"
        "var const int Version;\r\n"
        "var int Players, LeftOut;\r\n"
        "\r\n"
        "function PostBeginPlay()\r\n"
        "{\r\n"
        "\tlocal SMission M;\r\n"
        "\r\n"
        "\tif (Level.Game == None)\r\n"
        "\t\treturn;\r\n"
        "\tPlayers = CountPlayers();\r\n"
        "\tif (Players <= 0)\r\n"
        "\t\treturn;\r\n"
        "\tforeach AllActors(class'SMission', M)\r\n"
        "\t\tif (IsTop(M))\r\n"
        "\t\t\tPrune(M, 0);\r\n"
        "\tLog(\"" + ClassName + ": \" $ Players $ \" players, \" $ LeftOut $ \" objectives left out\");\r\n"
        "}\r\n"
        "\r\n"
        "// The lobby's player count, which the host puts in the map's URL.\r\n"
        "function int CountPlayers()\r\n"
        "{\r\n"
        "\tlocal string URL;\r\n"
        "\tlocal int n;\r\n"
        "\r\n"
        "\tURL = Level.GetLocalURL();\r\n"
        "\tif (InStr(Caps(URL), \"?EDITEUR=TRUE\") >= 0)\r\n"
        "\t\treturn PlayLevelPlayers;\r\n"
        "\tn = int(Level.Game.ParseOption(URL, \"NBPlayers\"));\r\n"
        "\tif (n <= 0 && SGameInfo(Level.Game) != None)\r\n"
        "\t\tn = SGameInfo(Level.Game).PlayersToWait;\r\n"
        "\treturn n;\r\n"
        "}\r\n"
        "\r\n"
        "function bool IsTop(SMission M)\r\n"
        "{\r\n"
        "\tlocal SMission Other;\r\n"
        "\tlocal int i;\r\n"
        "\r\n"
        "\tforeach AllActors(class'SMission', Other)\r\n"
        "\t\tif (Other != M)\r\n"
        "\t\t\tfor (i = 0; i < Other.Objectives.Length; i++)\r\n"
        "\t\t\t\tif (Other.Objectives[i] == M)\r\n"
        "\t\t\t\t\treturn false;\r\n"
        "\treturn true;\r\n"
        "}\r\n"
        "\r\n"
        "function bool Excluded(SObjective O)\r\n"
        "{\r\n"
        "\tlocal int i;\r\n"
        "\r\n"
        "\tfor (i = 0; i < " + cap + "; i++)\r\n"
        "\t\tif (Objective[i] == O)\r\n"
        "\t\t\treturn (MinPlayers[i] > 0 && Players < MinPlayers[i]) || (MaxPlayers[i] > 0 && Players > MaxPlayers[i]);\r\n"
        "\treturn false;\r\n"
        "}\r\n"
        "\r\n"
        "// How many completions a mission asks for.\r\n"
        "function int Needs(SMission M)\r\n"
        "{\r\n"
        "\tif (M.MinimumObjectives > 0)\r\n"
        "\t\treturn M.MinimumObjectives;\r\n"
        "\treturn M.Objectives.Length;\r\n"
        "}\r\n"
        "\r\n"
        "// Leaves out the objectives (and whole missions) this match is too small or too big\r\n"
        "// for, and asks each mission for no more completions than it can still give: as\r\n"
        "// many fewer as its zones lost, and never more than its objectives can reach.\r\n"
        "function Prune(SMission M, int Depth)\r\n"
        "{\r\n"
        "\tlocal int i, Fewer, Capacity, Before;\r\n"
        "\tlocal SObjective O;\r\n"
        "\tlocal SMission Sub;\r\n"
        "\r\n"
        "\tfor (i = M.Objectives.Length - 1; i >= 0; i--)\r\n"
        "\t{\r\n"
        "\t\tO = M.Objectives[i];\r\n"
        "\t\tif (O == None)\r\n"
        "\t\t\tcontinue;\r\n"
        "\t\tSub = SMission(O);\r\n"
        "\t\tif (Excluded(O))\r\n"
        "\t\t{\r\n"
        "\t\t\tif (Sub != None)\r\n"
        "\t\t\t\tFewer += Needs(Sub);\r\n"
        "\t\t\tM.Objectives.Remove(i, 1);\r\n"
        "\t\t\tDrop(O, Depth);\r\n"
        "\t\t\tcontinue;\r\n"
        "\t\t}\r\n"
        "\t\tif (Sub == None || Depth >= " + depth + ")\r\n"
        "\t\t{\r\n"
        "\t\t\tCapacity++;\r\n"
        "\t\t\tcontinue;\r\n"
        "\t\t}\r\n"
        "\t\tBefore = Needs(Sub);\r\n"
        "\t\tPrune(Sub, Depth + 1);\r\n"
        "\t\tif (Sub.Objectives.Length == 0)\r\n"
        "\t\t{\r\n"
        "\t\t\tFewer += Before;\r\n"
        "\t\t\tM.Objectives.Remove(i, 1);\r\n"
        "\t\t\tDrop(Sub, Depth);\r\n"
        "\t\t\tcontinue;\r\n"
        "\t\t}\r\n"
        "\t\tFewer += Before - Needs(Sub);\r\n"
        "\t\tCapacity += Needs(Sub);\r\n"
        "\t}\r\n"
        "\tif (M.MinimumObjectives > 0)\r\n"
        "\t{\r\n"
        "\t\tM.MinimumObjectives = Min(M.MinimumObjectives - Fewer, Capacity);\r\n"
        "\t\tif (M.MinimumObjectives < 1 && M.Objectives.Length > 0)\r\n"
        "\t\t\tM.MinimumObjectives = 1;\r\n"
        "\t\tif (M.MinimumObjectives < 0)\r\n"
        "\t\t\tM.MinimumObjectives = 0;\r\n"
        "\t}\r\n"
        "\tif (M.RandomObjectives > M.Objectives.Length)\r\n"
        "\t\tM.RandomObjectives = M.Objectives.Length;\r\n"
        "}\r\n"
        "\r\n"
        "// Switches an objective off the way the game does, with its devices and, for a\r\n"
        "// mission, everything it still lists.\r\n"
        "function Drop(SObjective O, int Depth)\r\n"
        "{\r\n"
        "\tlocal int i;\r\n"
        "\tlocal SMission Sub;\r\n"
        "\r\n"
        "\tSub = SMission(O);\r\n"
        "\tif (Sub != None && Depth < " + depth + ")\r\n"
        "\t\tfor (i = 0; i < Sub.Objectives.Length; i++)\r\n"
        "\t\t\tif (Sub.Objectives[i] != None)\r\n"
        "\t\t\t\tDrop(Sub.Objectives[i], Depth + 1);\r\n"
        "\tfor (i = 0; i < O.Triggers.Length; i++)\r\n"
        "\t\tif (O.Triggers[i] != None)\r\n"
        "\t\t\tO.Triggers[i].Desactivate();\r\n"
        "\tO.Desactivate();\r\n"
        "\tif (Sub == None)\r\n"
        "\t\tLeftOut++;\r\n"
        "}\r\n"
        "\r\n"
        "defaultproperties\r\n"
        "{\r\n"
        "\tbStatic=False\r\n"
        "\tbNoDelete=True\r\n"
        "\tRemoteRole=ROLE_None\r\n"
        "\tVersion=" + std::to_string(Version) + "\r\n"
        "}\r\n";
}
} // namespace ObjectivePlayers
