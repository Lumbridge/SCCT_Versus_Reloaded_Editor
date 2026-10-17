#include "../Reloaded.Editor/WindowPlacementModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace WindowPlacement;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
bool Same(const std::optional<Placement>& p, int x, int y, int w, int h, bool maximised)
{
    return p && p->x == x && p->y == y && p->width == w && p->height == h && p->maximised == maximised;
}

void Text()
{
    Check(Format({120, 80, 900, 560, false}) == "120,80,900,560,0", "a window as written");
    Check(Format({-1800, -20, 640, 480, true}) == "-1800,-20,640,480,1", "a maximised window left of the main monitor");
    Check(Same(Parse("120,80,900,560,0"), 120, 80, 900, 560, false), "read back");
    Check(Same(Parse(Format({-1800, -20, 640, 480, true})), -1800, -20, 640, 480, true), "round trip, negative and maximised");
    Check(Same(Parse(" 120 , 80,900 ,560, 1 "), 120, 80, 900, 560, true), "blanks around the numbers");

    Check(!Parse(""), "nothing written yet");
    Check(!Parse("120,80,900,560"), "four numbers");
    Check(!Parse("120,80,900,560,0,7"), "six numbers");
    Check(!Parse("120,80,900,560,2"), "maximised is 0 or 1");
    Check(!Parse("120,80,0,560,0") && !Parse("120,80,900,-5,0"), "no size");
    Check(!Parse("120,80,900,560,0x") && !Parse("12a,80,900,560,0") && !Parse("120;80;900;560;0"), "not numbers");
    Check(!Parse("120,,900,560,0") && !Parse(",80,900,560,0") && !Parse("120,80,900,560,"), "a missing number");
    Check(!Parse("99999999999,80,900,560,0"), "a number too large for the screen");
    Check(!Parse("1.5,80,900,560,0"), "whole pixels only");
}

void Sizes()
{
    const auto kept = Clamp({10, 20, 900, 560, false});
    Check(kept.x == 10 && kept.y == 20 && kept.width == 900 && kept.height == 560, "a sensible size is kept");
    const auto tiny = Clamp({10, 20, 12, 3, false});
    Check(tiny.width == kMinWidth && tiny.height == kMinHeight, "too small to use");
    const auto huge = Clamp({10, 20, 99999, 70000, true});
    Check(huge.width == kMaxSize && huge.height == kMaxSize && huge.maximised, "larger than any desktop");
    const auto own = Clamp({10, 20, 300, 200, false}, 520, 360);
    Check(own.width == 520 && own.height == 360, "no smaller than the window's own minimum");
    const auto silly = Clamp({0, 0, 300, 200, false}, 0, -4);
    Check(silly.width == 300 && silly.height == 200, "a minimum of nothing keeps the size");
}

void Monitors()
{
    // A 1920x1040 work area (taskbar below), and a second monitor to the left.
    const std::vector<Rect> one{{0, 0, 1920, 1040}};
    const std::vector<Rect> two{{0, 0, 1920, 1040}, {-1280, 0, 0, 984}};

    Check(Usable({100, 100, 900, 560}, one), "well inside");
    Check(Usable({-8, -8, 1936, 1056, true}, one), "maximised, edges just off the monitor");
    Check(Usable({1700, 100, 900, 560}, one), "mostly off to the right, the left of the title bar still there");
    Check(Usable({-800, 100, 900, 560}, one), "mostly off to the left, the right of the title bar still there");
    Check(!Usable({1870, 100, 900, 560}, one), "only a sliver of title bar on the monitor");
    Check(!Usable({100, 1030, 900, 560}, one), "the title bar under the taskbar");
    Check(!Usable({100, -200, 900, 560}, one), "the title bar above the monitor");
    Check(Usable({100, -6, 900, 560}, one), "a few pixels above the top, as a restored window can be");

    Check(Usable({-1000, 100, 640, 480}, two), "on the second monitor");
    Check(!Usable({-1000, 100, 640, 480}, one), "that monitor unplugged");
    Check(!Usable({100, 100, 900, 560}, {}), "no monitors known");
    Check(Usable({-40, 100, 120, 80}, two), "across the join of two monitors, enough on one");
    Check(Usable({10, 10, 40, 40}, one), "a window narrower than the grip, all of it on the monitor");

    Check(!Usable({100, 100, 0, 560}, one) && !Usable({100, 100, 900, -1}, one), "no size");
    Check(!Usable({2147483000, 100, 900, 560}, one), "far beyond any monitor, without overflowing");
}

int main()
{
    try
    {
        Text();
        Sizes();
        Monitors();
        std::cout << "WindowPlacementModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "WindowPlacementModelTests failed: " << error.what() << '\n';
        return 1;
    }
}
