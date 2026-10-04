#include "../Reloaded.Editor/EntryThumbnailModel.h"
#include <iostream>
#include <source_location>
#include <string>
using namespace Thumbnail;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
template <class F> void Reject(F f, std::source_location where = std::source_location::current())
{
    bool caught = false;
    try { f(); } catch (const std::exception&) { caught = true; }
    if (!caught) throw std::runtime_error("invalid input accepted at line " + std::to_string(where.line()));
}
bool Near(double a, double b, double tolerance = 0.5) { return std::abs(a - b) <= tolerance; }

void Names()
{
    const std::string id = "0123456789abcdef0123456789ABCDEF";
    Check(FileName(Kind::View, id) == "view-0123456789abcdef0123456789abcdef.png", "view file name from the id, lower case");
    Check(FileName(Kind::Assembly, id) == "assembly-0123456789abcdef0123456789abcdef.png", "assembly file name");
    Check(FileName(Kind::View, id) != FileName(Kind::Assembly, id), "kinds never share a file");
    // Ids that could escape the folder or are not ids at all.
    for (const char* bad : { "", "..", "a/b", "a\\b", "a.b", "C:x", "a b", "name with spaces" }) Reject([&] { FileName(Kind::View, bad); });
    Reject([&] { FileName(Kind::View, std::string(65, 'a')); });

    Parsed parsed;
    Check(Parse("view-abc123.png", parsed) && parsed.kind == Kind::View && parsed.id == "abc123" && !parsed.temporary, "parse a view thumbnail");
    Check(Parse("ASSEMBLY-ABC.PNG", parsed) && parsed.kind == Kind::Assembly && parsed.id == "abc", "parse ignores case");
    Check(Parse("assembly-abc.png.tmp", parsed) && parsed.temporary && parsed.id == "abc", "an interrupted save parses as temporary");
    Check(!Parse("view-.png", parsed), "no id");
    Check(!Parse("view-abc.bmp", parsed), "other extension");
    Check(!Parse("notes.png", parsed), "other file");
    Check(!Parse("view-a.b.png", parsed), "dotted id");
    Check(!Parse("desktop.ini", parsed), "system file");
    // The round trip.
    Check(Parse(FileName(Kind::Assembly, id), parsed) && parsed.kind == Kind::Assembly && parsed.id == Lower(id), "round trip");
}

void Sweep()
{
    const std::vector<std::string> files = { "view-aa.png", "view-bb.png", "assembly-aa.png", "assembly-cc.png", "view-aa.png.tmp", "readme.txt", "view-zz.jpg", "thumbs.db" };
    auto orphans = Orphans(files, { "AA" }, { "cc" });
    const std::vector<std::string> expected = { "view-bb.png", "assembly-aa.png", "view-aa.png.tmp" };
    Check(orphans == expected, "deleted entries and interrupted saves are swept, ids matched without case, foreign files kept");
    Check(Orphans(files, { "aa", "bb" }, { "aa", "cc" }).size() == 1, "only the temporary file when every entry is live");
    Check(Orphans({}, {}, {}).empty(), "empty folder");
    // A view id never protects an assembly picture of the same id.
    Check(Orphans({ "assembly-aa.png" }, { "aa" }, {}).size() == 1, "kinds are separate");
}

void Sizes()
{
    auto size = Fit(1024, 768);
    Check(size.width == 256 && size.height == 192, "4:3 viewport");
    size = Fit(1920, 1080);
    Check(size.width == 256 && size.height == 144, "16:9 viewport");
    size = Fit(300, 900);
    Check(size.height == 256 && size.width == 85, "tall viewport fits by height");
    size = Fit(5000, 3);
    Check(size.width == 256 && size.height == 1, "a sliver keeps one row");
    Reject([] { Fit(0, 10); });
    Reject([] { Fit(10, -1); });

    auto cell = Letterbox(256, 192);
    Check(cell.height == 72 && cell.width == 96 && cell.x == 12 && cell.y == 0, "4:3 picture pillarboxed in the cell");
    cell = Letterbox(256, 144);
    Check(cell.width == 120 && cell.y >= 1 && cell.height <= 72, "16:9 picture letterboxed");
    cell = Letterbox(120, 72);
    Check(cell.x == 0 && cell.y == 0 && cell.width == 120 && cell.height == 72, "exact fit");
}

void Framing()
{
    // A box around the origin, looking along +X (yaw 0).
    auto camera = FrameBox({ -100, -100, 0 }, { 100, 100, 200 }, 0, 90, 4.0 / 3.0);
    Check(camera.rotation[1] == 0 && camera.rotation[2] == 0, "yaw kept, no roll");
    Check(camera.rotation[0] == ((-4551) & 0xffff), "three-quarter pitch looking down");
    Check(camera.location[0] < -100, "behind the box along the view");
    Check(Near(camera.location[1], 0), "centred sideways");
    Check(camera.location[2] > 100, "above the centre, looking down");
    // The centre lies on the view ray.
    const double pi = 3.14159265358979323846, p = -25.0 * pi / 180;
    const double distance = std::sqrt(std::pow(camera.location[0], 2) + std::pow(camera.location[1], 2) + std::pow(camera.location[2] - 100, 2));
    Check(Near(camera.location[0], -std::cos(p) * distance, 1) && Near(camera.location[2] - 100, -std::sin(p) * distance, 1), "aims at the centre");
    // Far enough that the bounding sphere fits the narrower field of view.
    const double radius = kPadding + std::sqrt(200.0 * 200 * 3) / 2;
    const double halfVertical = std::atan(std::tan(pi / 4) / (4.0 / 3.0));
    Check(distance >= radius / std::sin(halfVertical) - 1, "whole box in view");

    // Yaw 90 degrees: the camera moves to -Y.
    camera = FrameBox({ 0, 0, 0 }, { 0, 0, 0 }, 16384, 90, 1);
    Check(camera.location[1] < 0 && Near(camera.location[0], 0), "turned viewport");
    Check(camera.rotation[1] == 16384, "turned yaw kept");
    // A single actor still gets a usable distance.
    const double single = std::sqrt(std::pow(camera.location[1], 2) + std::pow(camera.location[2], 2));
    Check(single >= kMinDistance - 1, "minimum distance for one actor");
    // A bigger box backs off further; a wider FOV comes closer.
    auto close = FrameBox({ 0, 0, 0 }, { 100, 100, 100 }, 0, 90, 1);
    auto distant = FrameBox({ 0, 0, 0 }, { 5000, 5000, 100 }, 0, 90, 1);
    Check(distant.location[0] < close.location[0] - 1000, "bigger box, further away");
    auto wide = FrameBox({ 0, 0, 0 }, { 5000, 5000, 100 }, 0, 120, 1);
    Check(wide.location[0] > distant.location[0], "wider FOV, closer");
    // Nonsense FOV or aspect fall back to the defaults instead of NaN.
    auto fallback = FrameBox({ 0, 0, 0 }, { 100, 100, 100 }, 0, 0, 0);
    for (double v : fallback.location) Check(std::isfinite(v), "finite with a bad FOV");
    Reject([] { FrameBox({ 1, 0, 0 }, { 0, 0, 0 }, 0, 90, 1); });
    Reject([] { FrameBox({ 0, 0, 0 }, { std::nan(""), 0, 0 }, 0, 90, 1); });
    Reject([] { FrameBox({ -2e7, 0, 0 }, { 0, 0, 0 }, 0, 90, 1); });
}

int main()
{
    try
    {
        Names(); Sweep(); Sizes(); Framing();
        std::cout << "Entry thumbnail model tests passed (" << checks << " checks)." << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
}
