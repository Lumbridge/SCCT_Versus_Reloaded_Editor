// Objective player counts: the rules, the missions a lobby size plays (the editor's
// copy of the script's pruning) and the script compiled into the map. The compile
// and the in-game result are checked natively.
#include "../Reloaded.Editor/ObjectivePlayersModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace ObjectivePlayers;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
template <class F> bool Throws(F f)
{
    try { f(); }
    catch (const std::exception&) { return true; }
    return false;
}

// ShipD's shape: a chained top mission needing 4 over two zones of 2 each, zone 1
// with seven objectives and zone 2 with four.
Missions ShipD(int top = 4)
{
    Missions m;
    m["mylevel.top"] = {{"MyLevel.Zone1", "MyLevel.Zone2"}, top};
    m["mylevel.zone1"] = {{"MyLevel.A1", "MyLevel.A2", "MyLevel.A3", "MyLevel.A4", "MyLevel.A5", "MyLevel.A6", "MyLevel.A7"}, 2};
    m["mylevel.zone2"] = {{"MyLevel.B1", "MyLevel.B2", "MyLevel.B3", "MyLevel.B4"}, 2};
    return m;
}

int main()
{
    try
    {
        // Rules.
        Check(Keeps({}, 1) && Keeps({6, 0}, 6) && !Keeps({6, 0}, 5) && Keeps({0, 4}, 4) && !Keeps({0, 4}, 5), "minimum and maximum");
        Check(Keeps({6, 6}, 6) && !Keeps({6, 6}, 4) && !Keeps({6, 6}, 8), "exactly a lobby size");
        Check(Short({}) == "" && Short({6, 0}) == "6+ players" && Short({0, 4}) == "up to 4 players" && Short({6, 6}) == "6 players" &&
                  Short({4, 8}) == "4-8 players",
              "short labels");
        Check(Describe({6, 0}) == "only in matches with 6 or more players" && Describe({}) == "in every match", "descriptions");
        Check(Throws([] { Check(Rule{8, 4}); }) && Throws([] { Check(Rule{17, 0}); }) && Throws([] { Check(Rule{-1, 0}); }), "bad ranges refused");
        Check(!Throws([] { Check(Rule{4, 4}); }) && !Throws([] { Check(Rule{0, 16}); }), "good ranges accepted");
        Check(Choices().size() == 17 && Choices().front() == "Any" && Choices().back() == "16", "sheet choices");
        Check(ChoiceCount("Any") == 0 && ChoiceCount("6") == 6 && Throws([] { ChoiceCount("6x"); }) && Throws([] { ChoiceCount("0"); }), "choice parsing");
        Check(ChoiceText(0) == "Any" && ChoiceText(6) == "6", "choice text");
        Check(ReferencePath("SObjective'MyLevel.SObjective12'") == "MyLevel.SObjective12" && ReferencePath("None") == "" &&
                  ReferencePath("MyLevel.X") == "MyLevel.X",
              "object references");

        // No count (a solo Play Level) changes nothing.
        Rules rules{{"mylevel.a3", {6, 0}}};
        Check(Apply(ShipD(), rules, 0).missions.at("mylevel.zone1").objectives.size() == 7, "no count keeps everything");
        Check(Roots(ShipD()) == std::vector<std::string>{"mylevel.top"}, "the top mission is the root");

        // Five of zone 1's objectives for 3v3 only: a 2v2 plays two, still needing 2.
        rules = {{"mylevel.a3", {6, 0}}, {"mylevel.a4", {6, 0}}, {"mylevel.a5", {6, 0}}, {"mylevel.a6", {6, 0}}, {"mylevel.a7", {6, 0}}};
        auto out = Apply(ShipD(), rules, 4);
        Check(out.missions.at("mylevel.zone1").objectives == std::vector<std::string>{"MyLevel.A1", "MyLevel.A2"}, "zone 1 keeps its first two");
        Check(out.missions.at("mylevel.zone1").minimum == 2 && out.missions.at("mylevel.top").minimum == 4, "thresholds that still fit stay");
        Check(out.left.size() == 5, "five left out");
        out = Apply(ShipD(), rules, 6);
        Check(out.missions.at("mylevel.zone1").objectives.size() == 7 && out.left.empty(), "a 3v3 plays them all");

        // Only one left in zone 1: the zone needs 1, the match one fewer.
        rules["mylevel.a2"] = {6, 0};
        out = Apply(ShipD(), rules, 4);
        Check(out.missions.at("mylevel.zone1").minimum == 1 && out.missions.at("mylevel.top").minimum == 3, "thresholds follow");
        // A match the spies win before the last zone keeps that shape.
        out = Apply(ShipD(3), rules, 4);
        Check(out.missions.at("mylevel.top").minimum == 2, "an early win stays early");

        // A whole zone for big games: the match skips it.
        rules = {{"mylevel.zone2", {6, 0}}};
        out = Apply(ShipD(), rules, 4);
        Check(out.missions.at("mylevel.top").objectives == std::vector<std::string>{"MyLevel.Zone1"} && out.missions.at("mylevel.top").minimum == 2,
              "a zone left out takes its completions with it");
        Check(out.left.size() == 5, "the zone and its four objectives are left out");

        // Every objective of zone 1 for big games: the empty zone goes too.
        rules.clear();
        for (const char* a : {"a1", "a2", "a3", "a4", "a5", "a6", "a7"}) rules[std::string("mylevel.") + a] = {6, 0};
        out = Apply(ShipD(), rules, 2);
        Check(out.missions.at("mylevel.top").objectives == std::vector<std::string>{"MyLevel.Zone2"} && out.missions.at("mylevel.top").minimum == 2,
              "an emptied zone goes");

        // A flat mission: 3 of 5, two of them for small games only.
        Missions flat{{"mylevel.m", {{"MyLevel.O1", "MyLevel.O2", "MyLevel.O3", "MyLevel.O4", "MyLevel.O5"}, 3}}};
        rules = {{"mylevel.o4", {0, 4}}, {"mylevel.o5", {0, 4}}};
        Check(Apply(flat, rules, 4).missions.at("mylevel.m").minimum == 3, "small games keep five");
        out = Apply(flat, rules, 8);
        Check(out.missions.at("mylevel.m").objectives.size() == 3 && out.missions.at("mylevel.m").minimum == 3, "big games play three, needing three");
        rules["mylevel.o3"] = {0, 4};
        Check(Apply(flat, rules, 8).missions.at("mylevel.m").minimum == 2, "never more than the objectives left");

        // Problems: lobby sizes with nothing to do, as runs.
        rules = {{"mylevel.o1", {6, 0}}, {"mylevel.o2", {6, 0}}, {"mylevel.o3", {6, 0}}, {"mylevel.o4", {6, 0}}, {"mylevel.o5", {0, 3}}};
        auto problems = Problems(flat, rules, {{"mylevel.m", "mission Main"}});
        Check(problems.size() == 1 && problems[0] == "With 4 to 5 players, mission Main has no objectives left: the player rules leave nothing to play.",
              "only the lobby sizes no rule covers");
        Check(Problems(ShipD(), {{"mylevel.a1", {6, 0}}}).empty(), "no problems");
        rules = {{"mylevel.o1", {6, 0}}, {"mylevel.o2", {6, 0}}, {"mylevel.o3", {6, 0}}, {"mylevel.o4", {6, 0}}, {"mylevel.o5", {6, 0}}};
        problems = Problems(flat, rules);
        Check(problems.size() == 1 && Contains(problems[0], "With 1 to 5 players, mylevel.m"), "one run, named by path without a name");

        Check(Summary(ShipD(), {{"mylevel.zone2", {6, 0}}}, 4) == "4 players: 7 objectives, the spies need 2", "summary");
        Check(Summary(ShipD(), {}, 1) == "1 player: 11 objectives, the spies need 4", "summary of the whole map");

        // The script.
        const auto script = Script();
        Check(Contains(script, "class SObjectivePlayers extends Info\r\n\tplaceable;"), "class header");
        Check(script.find("\n") == script.find("\r\n") + 1, "CRLF line ends");
        Check(Contains(script, "var() SObjective Objective[64];") && Contains(script, "var() byte MinPlayers[64];") &&
                  Contains(script, "var() byte MaxPlayers[64];") && Contains(script, "var() byte PlayLevelPlayers;"),
              "the table");
        Check(Contains(script, "Level.Game.ParseOption(URL, \"NBPlayers\")") && Contains(script, "SGameInfo(Level.Game).PlayersToWait"),
              "the lobby count, with the game's own copy as the fallback");
        Check(Contains(script, "\"?EDITEUR=TRUE\") >= 0)\r\n\t\treturn PlayLevelPlayers;"), "Play Level's own count");
        // Decided once on the server, before the missions prepare: no Tick, nothing simulated.
        Check(Contains(script, "function PostBeginPlay()\r\n{\r\n\tlocal SMission M;\r\n\r\n\tif (Level.Game == None)\r\n\t\treturn;"), "server only");
        Check(!Contains(script, "simulated") && !Contains(script, "Tick") && Contains(script, "RemoteRole=ROLE_None"), "nothing runs on clients");
        Check(Contains(script, "O.Triggers[i].Desactivate();") && Contains(script, "O.Desactivate();"), "switched off the game's way");
        Check(Contains(script, "M.Objectives.Remove(i, 1);"), "out of the mission's list");
        Check(Contains(script, "M.MinimumObjectives = Min(M.MinimumObjectives - Fewer, Capacity);"), "thresholds as the model has them");
        Check(Contains(script, "Version=1\r\n") && Version == 1, "versioned");
        std::cout << "ObjectivePlayersModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
