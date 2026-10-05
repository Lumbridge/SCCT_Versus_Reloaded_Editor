#pragma once
#include "CharacterSkinsModel.h"
#include "EmitterPreviewModel.h"
#include "WorkflowModel.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

// Engine-independent half of the Character Skins 3D preview (CharacterPreview.cpp):
// what each team looks like from the panel's fields, the T3D that stands both of
// them in the private preview level, which animation pose they hold and where the
// camera starts.
namespace CharacterPreview::Model
{
using Workflow::Json;
using Workflow::Vector;

// Half the distance between the two characters, and the yaw that turns a stock
// character model (which faces +X at yaw 0) towards a camera looking along +X.
constexpr double kSpacing = 42;
constexpr int kFacing = 32768;

// One character as the game dresses it (CharacterSkinsModel.h's script): a team
// with a model shows that model in its own materials; otherwise the stock model
// with the map's materials in the slots that have one, the stock material in the
// rest. Either way it moves with the team's own animations.
struct Figure
{
    std::string team;      // "Spy" or "Merc"
    std::string mesh;      // SkeletalMesh path
    std::string animation; // MeshAnimation path
    // Material paths by skin index (CharacterSkins::Slot::section): 0 body, 1 head, and
    // on the merc 2 the rest of the body. Empty keeps the mesh's own.
    std::array<std::string, CharacterSkins::SkinCount> skins;
    double y = 0;          // where it stands across the view
};

inline std::string Lower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// A team's model field naming the team's own stock model is no model: the stock
// model with the slot materials, as Apply stores it.
inline std::string ChosenModel(const CharacterSkins::ModelSlot& model, const Json& models)
{
    auto chosen = models.is_object() ? models.value(model.property, std::string{}) : std::string{};
    return Lower(chosen) == Lower(model.stockMesh) ? std::string{} : chosen;
}
// The slots a model leaves unused: a team with a model wears the model's own materials.
inline std::vector<std::string> UnusedSlots(const Json& slots, const Json& models)
{
    std::vector<std::string> out;
    for (size_t m = 0; m < CharacterSkins::Models.size(); ++m)
        if (!ChosenModel(CharacterSkins::Models[m], models).empty())
            for (size_t k = 0; k < 2; ++k)
            {
                const auto& slot = CharacterSkins::Slots[m * 2 + k];
                if (slots.is_object() && !slots.value(slot.property, std::string{}).empty()) out.push_back(slot.label);
            }
    return out;
}

// slots: {SpyBody: path, ...}, models: {SpyModel: path, ...} as the panel's fields
// hold them (blank = stock). Throws std::runtime_error with the field's label for a
// path that is not one.
inline std::vector<Figure> Figures(const Json& slots, const Json& models)
{
    std::vector<Figure> out;
    for (size_t m = 0; m < CharacterSkins::Models.size(); ++m)
    {
        const auto& model = CharacterSkins::Models[m];
        Figure f;
        f.team = m == 0 ? "Spy" : "Merc";
        f.animation = model.animation;
        const auto chosen = ChosenModel(model, models);
        if (!CharacterSkins::ValidPath(chosen))
            throw std::runtime_error(std::string(model.label) + ": not a model path: " + chosen);
        f.mesh = chosen.empty() ? model.stockMesh : chosen;
        if (chosen.empty())
            for (size_t k = 0; k < 2; ++k)
            {
                const auto& slot = CharacterSkins::Slots[m * 2 + k];
                auto path = slots.is_object() ? slots.value(slot.property, std::string{}) : std::string{};
                if (!CharacterSkins::ValidPath(path))
                    throw std::runtime_error(std::string(slot.label) + ": not a material path: " + path);
                f.skins[slot.section] = path;
                if (slot.alsoSection >= 0) f.skins[slot.alsoSection] = path;
            }
        // The spy on the left of the starting view, the merc on the right.
        f.y = m == 0 ? -kSpacing : kSpacing;
        out.push_back(f);
    }
    return out;
}

// The two characters as one T3D map: SAnimatedMesh, SBase's placeable animated
// mesh actor, drawn as a mesh with the figure's materials, standing at the origin.
// prefix names the actors (prefix + team) so the preview can find them again.
inline std::string T3D(const std::vector<Figure>& figures, const std::string& actorClass, const std::string& prefix)
{
    std::string text = "Begin Map\n";
    for (const auto& f : figures)
    {
        text += "Begin Actor Class=" + actorClass + " Name=" + prefix + f.team + "\n";
        text += " DrawType=DT_Mesh\n";
        text += " Mesh=" + CharacterSkins::PropertyText("SkeletalMesh", f.mesh) + "\n";
        for (size_t k = 0; k < f.skins.size(); ++k)
            if (!f.skins[k].empty())
                text += " Skins(" + std::to_string(k) + ")=" + CharacterSkins::PropertyText("Material", f.skins[k]) + "\n";
        text += " Location=(X=0.000000,Y=" + CharacterSkins::Number(f.y) + ",Z=0.000000)\n";
        text += " Rotation=(Pitch=0,Yaw=" + std::to_string(kFacing) + ",Roll=0)\n";
        // Unlit: the preview level has no lights, and the materials show at their own colours.
        text += " bUnlit=True\n bHidden=False\n bCollideActors=False\n bBlockActors=False\n bBlockPlayers=False\n";
        text += " Name=\"" + prefix + f.team + "\"\n";
        text += "End Actor\n";
    }
    return text + "End Map\n";
}

// The pose a character holds: a standing wait ("WaitSt..."), else any wait, else
// an idle or stand, else the first sequence. -1 when there are none.
inline int Pose(const std::vector<std::string>& sequences)
{
    const auto find = [&](auto matches) -> int {
        for (size_t i = 0; i < sequences.size(); ++i)
            if (matches(Lower(sequences[i]))) return static_cast<int>(i);
        return -1;
    };
    for (auto pick : {find([](const std::string& s) { return s.rfind("waitst", 0) == 0; }),
                      find([](const std::string& s) { return s.rfind("wait", 0) == 0; }),
                      find([](const std::string& s) {
                          return s.find("idle") != std::string::npos || s.find("stand") != std::string::npos ||
                                 s.find("breath") != std::string::npos;
                      })})
        if (pick >= 0) return pick;
    return sequences.empty() ? -1 : 0;
}

// The starting view: a little above eye level and a quarter turn off square, so
// both characters show their front and a side.
constexpr int kPitch = -1100, kYaw = -2400;
constexpr double kFov = 50;
// What the two characters cover around the origin: a stock character model is
// about 180 units tall, centred on its actor.
inline Workflow::EmitterPreviewModel::Box Bounds()
{
    return {{-30, -kSpacing - 34, -92}, {30, kSpacing + 34, 92}, true};
}
} // namespace CharacterPreview::Model
