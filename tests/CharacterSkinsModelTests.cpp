// The Character Skins script the editor compiles into a map, and the slot paths
// the panel accepts. The compile and the in-game result are verified natively.
#include "../Reloaded.Editor/CharacterSkinsModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace CharacterSkins;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

int main()
{
    try
    {
        const auto script = Script();
        // The stock compiler wants the class declaration first and CRLF text.
        Check(Contains(script, "class ReloadedCharacterSkins extends Info\r\n\tplaceable;"), "class header");
        Check(script.find("\n") == script.find("\r\n") + 1, "CRLF line ends");
        for (const auto& slot : Slots)
        {
            Check(Contains(script, std::string("var() Material ") + slot.property + ";"), "slot variable");
            Check(Contains(script, std::string("HeatTextureModifier'") + slot.stock + "'"), "stock heat layer");
        }
        Check(Contains(script, "Dressed[3] = Wrap(MercHead"), "merc head wraps last");
        Check(Contains(script, "Dress(P, i, 1, Dressed[1])") && Contains(script, "Dress(P, i, 1, Dressed[3])"), "head slots");
        // Local per machine, never replicated; game effect skins are left alone.
        Check(Contains(script, "RemoteRole=ROLE_None") && Contains(script, "bNoDelete=True"), "local actor");
        Check(Contains(script, "Now == None || Now == Stock[Index * 2 + Slot]"), "effect skins left alone");
        Check(Contains(script, "Known[i].bDeleteMe"), "slots of gone pawns reused");
        Check(Contains(script, "Version=" + std::to_string(Version)), "version default");
        Check(!Contains(script, "replication"), "no replication block");

        Check(ValidPath(""), "empty keeps stock");
        Check(ValidPath("MyLevel.Snow.SpyCamo") && ValidPath("Arctic_TXT.SpyCamo"), "package paths");
        Check(!ValidPath("SpyCamo"), "needs a package");
        Check(!ValidPath("MyLevel..SpyCamo") && !ValidPath(".MyLevel.X") && !ValidPath("MyLevel.X."), "empty parts");
        Check(!ValidPath("MyLevel.X'") && !ValidPath("MyLevel.X Y") && !ValidPath("MyLevel.X\""), "no quotes or spaces");

        Check(PropertyText("Texture", "") == "None", "empty slot");
        Check(PropertyText("Texture", "MyLevel.Snow.SpyCamo") == "Texture'MyLevel.Snow.SpyCamo'", "texture reference");
        Check(PropertyText("Shader", "Arctic_TXT.Camo") == "Shader'Arctic_TXT.Camo'", "shader reference");
        bool threw = false;
        try { PropertyText("Texture", "Bad'Path"); } catch (const std::exception&) { threw = true; }
        Check(threw, "invalid path rejected");
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "CharacterSkinsModelTests: " << checks << " checks passed\n";
    return 0;
}
