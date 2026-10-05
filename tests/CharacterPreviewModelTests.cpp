// The Character Skins 3D preview's engine-independent half: how the panel's fields
// dress the two characters, the T3D that stands them in the preview level, the pose
// they hold and the starting frame. The engine side is verified natively.
#include "../Reloaded.Editor/CharacterPreviewModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace CharacterPreview::Model;
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
    catch (const std::runtime_error&) { return true; }
    return false;
}

int main()
{
    try
    {
        // Blank fields: both teams in their stock models and materials, moving with their
        // own animations.
        auto stock = Figures(Json::object(), Json::object());
        Check(stock.size() == 2 && stock[0].team == "Spy" && stock[1].team == "Merc", "a spy and a merc");
        Check(stock[0].mesh == "SPerso.ATT_01" && stock[1].mesh == "SPerso.DEF_01", "stock models");
        Check(stock[0].animation == "SPerso.PRO" && stock[1].animation == "SPerso.Def", "each team's own animations");
        Check(stock[0].skins[0].empty() && stock[0].skins[1].empty() && stock[1].skins[0].empty() && stock[1].skins[1].empty(),
              "blank slots keep the model's own materials");
        Check(stock[0].y < 0 && stock[1].y > 0, "spy on the left, merc on the right");

        // Slots dress the stock model; a team with a model shows the model in its own
        // materials, as the map script does, and keeps its team's animations.
        const Json slots = {{"SpyBody", "ShipD.CharacterSkins.SpyBody"}, {"SpyHead", ""},
                            {"MercBody", "SnowPack.Camo.MercBody"}, {"MercHead", "SnowPack.Camo.MercHead"}};
        auto dressed = Figures(slots, Json::object());
        Check(dressed[0].skins[0] == "ShipD.CharacterSkins.SpyBody" && dressed[0].skins[1].empty(), "spy body slot, stock head");
        Check(dressed[1].skins[0] == "SnowPack.Camo.MercBody" && dressed[1].skins[1] == "SnowPack.Camo.MercHead", "merc slots");
        // The merc's DEF_01 wears its body material on skins 0 and 2; skin 2 is most of the body.
        Check(dressed[1].skins[2] == "SnowPack.Camo.MercBody" && dressed[0].skins[2].empty(), "merc body on both body sections");
        auto swapped = Figures(slots, {{"SpyModel", "SPerso.DEF_01"}, {"MercModel", ""}});
        Check(swapped[0].mesh == "SPerso.DEF_01" && swapped[0].animation == "SPerso.PRO", "a spy in the merc model moves as a spy");
        Check(swapped[0].skins[0].empty() && swapped[0].skins[1].empty(), "a model shows its own materials");
        Check(swapped[1].mesh == "SPerso.DEF_01" && swapped[1].skins[0] == "SnowPack.Camo.MercBody", "the other team is unaffected");

        // The team's own stock model is no model: the slots stay worn. Another model leaves
        // its team's slots unused, and the preview says so.
        auto stockModels = Figures(slots, {{"SpyModel", "sperso.att_01"}, {"MercModel", "SPerso.DEF_01"}});
        Check(stockModels[0].mesh == "SPerso.ATT_01" && stockModels[0].skins[0] == "ShipD.CharacterSkins.SpyBody", "spy's own model keeps the spy slots");
        Check(stockModels[1].skins[1] == "SnowPack.Camo.MercHead", "merc's own model keeps the merc slots");
        Check(UnusedSlots(slots, {{"SpyModel", "SPerso.ATT_01"}}).empty(), "no slot is unused under the team's own model");
        const auto unused = UnusedSlots(slots, {{"SpyModel", "SPerso.DEF_01"}});
        Check(unused.size() == 1 && unused[0] == "Spy body", "the spy body slot is unused under another model");

        // A field that is not an object path names itself.
        try
        {
            Figures({{"MercHead", "Texture'Bad'"}}, Json::object());
            Check(false, "a bad slot path is refused");
        }
        catch (const std::runtime_error& e)
        {
            Check(Contains(e.what(), "Merc head"), "the refusal names the slot");
        }
        Check(Throws([] { Figures(Json::object(), {{"SpyModel", "a b"}}); }), "a bad model path is refused");

        // The T3D: one SAnimatedMesh per team, named for the preview, drawn as a mesh,
        // with only the slots that are set, facing the starting camera.
        const auto t3d = T3D(dressed, "SAnimatedMesh", "CP3_");
        Check(t3d.rfind("Begin Map\n", 0) == 0 && t3d.size() > 8 && t3d.substr(t3d.size() - 8) == "End Map\n", "one map");
        Check(Contains(t3d, "Begin Actor Class=SAnimatedMesh Name=CP3_Spy\n") && Contains(t3d, "Begin Actor Class=SAnimatedMesh Name=CP3_Merc\n"),
              "both actors, named for the preview");
        Check(Contains(t3d, " DrawType=DT_Mesh\n Mesh=SkeletalMesh'SPerso.ATT_01'\n"), "the spy's model");
        Check(Contains(t3d, " Skins(0)=Material'ShipD.CharacterSkins.SpyBody'\n"), "the spy's body material");
        Check(!Contains(t3d, "Skins(1)=Material'ShipD") && Contains(t3d, " Skins(1)=Material'SnowPack.Camo.MercHead'\n"),
              "only the slots that are set");
        Check(Contains(t3d, " Skins(2)=Material'SnowPack.Camo.MercBody'\n") && !Contains(t3d, "Skins(2)=Material'ShipD"),
              "the merc body on skin 2 as well");
        Check(Contains(t3d, " Location=(X=0.000000,Y=-42,Z=0.000000)\n") && Contains(t3d, " Location=(X=0.000000,Y=42,Z=0.000000)\n"),
              "side by side");
        Check(Contains(t3d, " Rotation=(Pitch=0,Yaw=32768,Roll=0)\n"), "facing the camera");
        Check(Contains(t3d, " bUnlit=True\n"), "drawn at the materials' own colours in the unlit preview level");
        Check(Throws([&] { T3D({{"Spy", "Bad'Mesh", "SPerso.PRO", {}, 0}}, "SAnimatedMesh", "CP1_"); }), "a bad mesh path never reaches the T3D");

        // The pose: a standing wait first, then any wait, an idle or stand, then the first.
        Check(Pose({"RunFw", "waitCrSpFd2", "WaitStSpFd2", "Idle"}) == 2, "standing wait");
        Check(Pose({"RunFw", "waitCrSpFd2", "Idle"}) == 1, "any wait");
        Check(Pose({"RunFw", "StandBreath"}) == 1, "idle or stand");
        Check(Pose({"RunFw", "Jump"}) == 0, "first sequence");
        Check(Pose({}) == -1, "no sequence");

        // The starting frame covers both characters and their full height.
        const auto box = Bounds();
        Check(box.valid && box.min[1] < -kSpacing && box.max[1] > kSpacing && box.max[2] - box.min[2] >= 180, "frame covers both");
        const double distance = Workflow::EmitterPreviewModel::FitDistance(box, {0, 0, 0}, kPitch, kYaw, kFov, 0.75, 0.9);
        Check(distance > 150 && distance < 1000, "starting distance");
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "CharacterPreviewModelTests: " << checks << " checks passed\n";
    return 0;
}
