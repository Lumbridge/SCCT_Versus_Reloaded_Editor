#include "../Reloaded.Editor/AssetNameGlossModel.h"
#include "../Reloaded.Editor/AssetNameDictionary.gen.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace AssetNames;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
void Same(const std::string& actual, const std::string& expected, const char* message,
          std::source_location where = std::source_location::current())
{
    ++checks;
    if (actual != expected)
        throw std::runtime_error(std::string(message) + ": got \"" + actual + "\", expected \"" + expected +
                                 "\" (line " + std::to_string(where.line()) + ")");
}
std::string Words(const std::vector<Token>& tokens)
{
    std::string out;
    for (const auto& t : tokens) out += (out.empty() ? "" : "|") + t.text + (t.number ? "#" : "");
    return out;
}

int main()
{
    try
    {
        // Tokens: separators, digits, CamelCase and acronyms; accents folded.
        Same(Words(Tokenize("Mur_Beton_Sale02")), "Mur|Beton|Sale|02#", "underscores and digits");
        Same(Words(Tokenize("IND03_BuroChaise")), "IND|03#|Buro|Chaise", "camel case");
        Same(Words(Tokenize("HTMLPanel-01b")), "HTML|Panel|01#|b", "acronym then word");
        Same(Words(Tokenize("G\xE9n\xE9rateur.D\xC9P\xD4T")), "Generateur|DEPOT", "Windows-1252 accents");
        Same(Words(Tokenize("__")), "", "nothing but separators");
        Same(Fold("\x8C" "uvre \xC6"), "oeuvre ae", "ligatures");

        Dictionary d;
        d.Load(R"(
# comment
[nouns]
mur = wall
porte = door
beton = concrete
bois = wood
planche = plank
tuyau = pipe
caisse = crate      # trailing comment
etagere = shelf
hangar = hangar
[adjectives]
rouge = red
sale = dirty
[context]
fin = end
carton = cardboard
[context adjective]
grand = big
[english]
wall
metal
)");
        Check(d.Size() == 16, "entries loaded");
        Check(d.Translations() == 13, "translations exclude English words and identical ones");
        Check(d.Find("caisse") && d.Find("caisse")->english == "crate", "trailing comment stripped");
        Check(d.Find("fin") && d.Find("fin")->context, "context section");
        Check(d.Find("grand") && d.Find("grand")->kind == Kind::Adjective && d.Find("grand")->context,
              "context adjective section");
        Check(d.Find("metal") && d.Find("metal")->kind == Kind::English, "English section");

        // Token by token, unknown words and numbers kept.
        Same(Gloss(d, "Porte_Metal01"), "door metal 01", "spec example");
        Same(Gloss(d, "Mur_Beton_Sale02"), "wall concrete dirty 02", "adjective kept in place");
        Same(Gloss(d, "IND03_Buro_Porte_Old"), "IND 03 Buro door Old", "unknown words kept as written");
        Same(Gloss(d, "PORTE"), "door", "case-insensitive");
        Same(Gloss(d, "G\xE9n\xE9ral_Porte"), "General door", "accented unknown word folded");
        // English names: no gloss.
        Same(Gloss(d, "Metal_Plate_D"), "", "English name");
        Same(Gloss(d, "Hangar_Door"), "", "identical translation is not a translation");
        Same(Gloss(d, "Hangar_Porte"), "hangar door", "identical word kept beside a real one");
        Same(Gloss(d, "Box01"), "", "no words known");
        Same(Gloss(d, ""), "", "empty name");
        // Context-only words need a French neighbour.
        Same(Gloss(d, "Fin_Swim"), "", "context word alone");
        Same(Gloss(d, "Carton_Box"), "", "context word alone, again");
        Same(Gloss(d, "Carton_Caisse"), "cardboard crate", "context word beside French");
        Same(Gloss(d, "Grand_Mur"), "big wall", "context adjective beside French");
        // Plurals.
        Same(Gloss(d, "Caisses_Bois"), "crates wood", "noun plural");
        Same(Gloss(d, "Tuyaux"), "pipes", "x plural");
        Same(Gloss(d, "Etageres"), "shelfs", "regular English plural only");
        Same(Gloss(d, "Murs_Rouges"), "walls red", "adjective plural keeps the English");
        // Glued words: every part known, at least one French.
        Same(Gloss(d, "aqua_boisplanche2"), "aqua wood plank 2", "two French words glued");
        Same(Gloss(d, "BetonWall"), "concrete wall", "camel case then known words");
        Same(Gloss(d, "betonwall"), "concrete wall", "French glued to English");
        Same(Gloss(d, "metalporte"), "metal door", "English glued to French");
        Same(Gloss(d, "metalwall"), "", "only English parts");
        Same(Gloss(d, "murxyz"), "", "unknown part");
        Same(Gloss(d, "boisplanches"), "wood planks", "plural inside a glued word");

        // Plural English.
        Same(Pluralize("box"), "boxes", "x");
        Same(Pluralize("battery"), "batteries", "consonant y");
        Same(Pluralize("key"), "keys", "vowel y");
        Same(Pluralize("stairs"), "stairs", "already plural");
        Same(Pluralize("gas bottle"), "gas bottles", "last word");

        // Labels.
        Same(Label("Porte_Metal01", "door metal 01"), "Porte_Metal01 (door metal 01)", "label");
        Same(Label("Metal_Plate", ""), "Metal_Plate", "no gloss, no brackets");

        // Search: original or English.
        Check(Matches(d, "Porte_01", "door"), "English finds French");
        Check(Matches(d, "Porte_01", "porte"), "French still works");
        Check(Matches(d, "Porte_01", "PORTE_0"), "whole query against the name, as stock");
        Check(Matches(d, "Porte_Metal01", "metal door"), "every word, any order");
        Check(Matches(d, "Porte_Metal01", "  Door  "), "trimmed and case-insensitive");
        Check(!Matches(d, "Porte_Metal01", "wood door"), "every word must match");
        Check(!Matches(d, "Metal_Plate", "door"), "English name, English query");
        Check(Matches(d, "Metal_Plate", ""), "empty query matches all");
        Check(Matches(d, "D\xE9p\xF4t", "depot"), "accents ignored");
        Check(MatchesGloss("Mur_02", "wall 02", "wall"), "precomputed gloss");

        // A later text wins; "word =" removes.
        Dictionary user = d;
        user.Load("[nouns]\nporte = gate\nmur =\n[english]\nbox\n");
        Same(Gloss(user, "Porte_Mur"), "gate Mur", "user entry wins and removal");
        Check(!user.Find("mur"), "removed");
        Check(user.Find("box") && user.Find("box")->kind == Kind::English, "user English word");
        user.Load("junk line\n= nothing\n[nouns]\nMUR = Wall\nmur2 = x\n");
        Same(Gloss(user, "Mur"), "Wall", "French folded to lower case, English kept as written");
        Check(!user.Find("mur2"), "non-letter words ignored");

        // The built-in dictionary loads and covers the stock vocabulary.
        Dictionary builtIn;
        builtIn.Load(BuiltInDictionaryText());
        Check(builtIn.Translations() > 700, "built-in size");
        Same(Gloss(builtIn, "Porte_Metal01"), "door metal 01", "built-in: door");
        Same(Gloss(builtIn, "IND03_Buro_Chaise_Old"), "IND 03 office chair old", "built-in: abbreviation");
        Same(Gloss(builtIn, "Bunk_Bunk_TuyoPlfdGrimpe"), "Bunk Bunk pipe ceiling climb", "built-in: inferred");
        Same(Gloss(builtIn, "PorteEtanche_Closed"), "door watertight Closed", "built-in: adjective");
        Same(Gloss(builtIn, "aqua_boisplanche2"), "aqua wood plank 2", "built-in: glued");
        Same(Gloss(builtIn, "Concrete_Wall_Grime_D"), "", "built-in: English texture");
        Same(Gloss(builtIn, "Metal_Plate_D"), "", "built-in: metal alone");
        Same(Gloss(builtIn, "wet_lit_ATT"), "", "built-in: lit is not bed");
        Same(Gloss(builtIn, "Sale1"), "", "built-in: sale alone");
        Same(Gloss(builtIn, "GAR_grille_grd_sale"), "GAR grate grd dirty", "built-in: sale beside French");
        Check(Matches(builtIn, "Ind03_caisseOutilFerme01", "closed crate"), "built-in search");
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "AssetNameGlossModelTests: " << checks << " checks passed\n";
    return 0;
}
