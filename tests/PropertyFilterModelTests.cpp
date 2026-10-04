#include "../Reloaded.Editor/PropertyFilterModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace PropertyFilter;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
std::string Kept(const std::vector<Row>& rows, const std::string& filter)
{
    const auto keep = Visible(rows, filter);
    std::string out;
    for (size_t i = 0; i < rows.size(); ++i)
        if (keep[i]) out += (out.empty() ? "" : ",") + rows[i].name;
    return out;
}

int main()
{
    try
    {
        // Words.
        Check(Words("").empty() && Words("   ").empty(), "blank filter has no words");
        auto w = Words("  Light\tCOL ");
        Check(w.size() == 2 && w[0] == "light" && w[1] == "col", "words split on blanks and fold case");
        Check(Matches("LightColor", Words("light col")), "every word inside one name");
        Check(!Matches("LightColor", Words("light tag")), "every word has to match");
        Check(Matches("bHidden", Words("HIDDEN")), "case does not matter");

        // The Actor Properties tree of a light and a static mesh actor, with
        // Location expanded.
        const std::vector<Row> rows = {
            {1, "Advanced"}, {2, "bHidden"}, {2, "bStatic"},
            {1, "Display"}, {2, "DrawScale"}, {2, "DrawScale3D"}, {3, "X"}, {3, "Y"}, {3, "Z"}, {2, "DrawType"},
            {1, "Events"}, {2, "Event"}, {2, "Tag"},
            {1, "LightColor"}, {2, "LightBrightness"}, {2, "LightHue"},
            {1, "Movement"}, {2, "Location"}, {3, "X"}, {3, "Y"}, {3, "Z"}, {2, "Rotation"},
        };
        for (const auto* blank : {"", "  "})
        {
            const auto all = Visible(rows, blank);
            Check(all.size() == rows.size() && std::find(all.begin(), all.end(), false) == all.end(), "an empty filter keeps every row");
        }
        Check(Kept(rows, "tag") == "Events,Tag", "a property keeps its category");
        Check(Kept(rows, "drawscale") == "Display,DrawScale,DrawScale3D,X,Y,Z", "a matched property keeps its expanded members");
        Check(Kept(rows, "events") == "Events,Event,Tag", "a category name keeps all its properties");
        Check(Kept(rows, "Event") == "Events,Event,Tag", "the category Events contains the word event");
        Check(Kept(rows, "events tag") == "Events,Tag", "category and property words together");
        Check(Kept(rows, "location x") == "Movement,Location,X", "a member under a property");
        Check(Kept(rows, "light") == "LightColor,LightBrightness,LightHue", "category match");
        Check(Kept(rows, "zzz").empty(), "nothing matches");
        Check(Kept(rows, "b") == "Advanced,bHidden,bStatic,LightColor,LightBrightness", "substring anywhere in the path");

        // Rows with no name of their own stay with a matching parent only.
        const std::vector<Row> buttons = {{1, "Object"}, {2, "Emitters"}, {3, ""}, {3, ""}, {2, "Name"}};
        Check(Kept(buttons, "emit") == "Object,Emitters,,", "unnamed rows follow their parent");
        Check(Kept(buttons, "name") == "Object,Name", "unnamed rows of another property drop");

        // Depth can jump back several levels.
        const std::vector<Row> deep = {{1, "A"}, {2, "Outer"}, {3, "Inner"}, {4, "Leaf"}, {1, "B"}, {2, "Leaf"}};
        Check(Kept(deep, "leaf") == "A,Outer,Inner,Leaf,B,Leaf", "paths at several depths");
        Check(Kept(deep, "b leaf") == "B,Leaf", "the path resets at the next category");

        // The list under the filter bar.
        auto p = BelowBar(0, 200, 24, 600);
        Check(p.y == 24 && p.height == 200, "a short list moves down whole");
        p = BelowBar(0, 573, 24, 573);
        Check(p.y == 24 && p.height == 549, "a full-height list loses the bar's height");
        p = BelowBar(30, 100, 24, 600);
        Check(p.y == 30 && p.height == 100, "a list already below the bar stays");
        p = BelowBar(0, 10, 24, 20);
        Check(p.y == 24 && p.height == 0, "no negative height in a tiny window");

        // (multiple values)
        Check(ShowsMultipleValues(2, false), "two objects that disagree");
        Check(!ShowsMultipleValues(2, true), "two objects that agree");
        Check(!ShowsMultipleValues(1, false), "one object never shows it");
        Check(std::string(kMultipleValues) == "(multiple values)", "label");
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PropertyFilterModelTests: " << checks << " checks passed\n";
    return 0;
}
