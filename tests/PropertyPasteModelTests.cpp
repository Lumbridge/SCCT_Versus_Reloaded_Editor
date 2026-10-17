#include "../Reloaded.Editor/PropertyPasteModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace Workflow;
namespace Paste = Workflow::PropertyPaste;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
// Schemas as MagicSchema writes them.
Json Leaf(const char* kind) { return {{"kind", kind}}; }
Json Vector3() { return {{"kind", "StructProperty"}, {"fields", {{"X", Leaf("FloatProperty")}, {"Y", Leaf("FloatProperty")}, {"Z", Leaf("FloatProperty")}}}}; }
Json ObjectRef(const char* type) { return {{"kind", "ObjectProperty"}, {"type", type}}; }
Json Entry(const char* name, const char* category, Json schema, Json value, Json lineage = Json::object())
{
    return {{"name", name}, {"category", category}, {"schema", std::move(schema)}, {"value", std::move(value)}, {"lineage", std::move(lineage)}};
}
const Json kVector = {{"X", "1.000000"}, {"Y", "2.000000"}, {"Z", "3.000000"}};

int main()
{
    try
    {
        // What starts ticked.
        Check(!Paste::CheckedByDefault(Entry("Location", "Movement", Vector3(), kVector)), "Location starts unticked");
        Check(!Paste::CheckedByDefault(Entry("Rotation", "Movement", {{"kind", "StructProperty"}, {"fields", {{"Pitch", Leaf("IntProperty")}, {"Yaw", Leaf("IntProperty")}, {"Roll", Leaf("IntProperty")}}}},
                                             {{"Pitch", "0"}, {"Yaw", "16384"}, {"Roll", "0"}})), "Rotation starts unticked");
        for (const auto* name : {"Tag", "Event", "Group", "AttachTag"})
            Check(!Paste::CheckedByDefault(Entry(name, "Events", Leaf("NameProperty"), "Door_1")), "names that wire or group an actor start unticked");
        for (const auto* name : {"bLockLocation", "bHiddenEd", "bHiddenEdGroup"})
            Check(!Paste::CheckedByDefault(Entry(name, "Advanced", {{"kind", "BoolProperty"}, {"choices", {"False", "True"}}}, "True")), "editor flags start unticked");
        for (const auto* name : {"PrePivot", "MainScale", "PostScale"})
            Check(!Paste::CheckedByDefault(Entry(name, "Brush", Vector3(), kVector)), "the brush transform starts unticked");
        Check(!Paste::CheckedByDefault(Entry("bhiddened", "Advanced", Leaf("BoolProperty"), "True")), "the names are matched without case");
        Check(!Paste::CheckedByDefault(Entry("DrawType", "Display", {{"kind", "ByteProperty"}, {"choices", {"DT_None", "DT_Sprite", "DT_StaticMesh"}}}, "DT_Sprite")),
              "what an actor is drawn as starts unticked");
        const Json action = {{"kind", "StructProperty"}, {"fields", {{"Event", Leaf("NameProperty")}, {"Delay", Leaf("FloatProperty")}}}};
        const Json group = {{"kind", "StructProperty"}, {"fields", {{"EventGroup", {{"kind", "ArrayProperty"}, {"inner", action}}}}}};
        const auto groups = Entry("Groups", "MagicEvent", {{"kind", "ArrayProperty"}, {"inner", group}},
                                  Json::array({Json{{"EventGroup", Json::array({Json{{"Event", "Door_1"}, {"Delay", "0.000000"}}})}}}));
        Check(Paste::Offered(groups) && !Paste::CheckedByDefault(groups), "an SMagicEvent's actions are offered unticked");
        Check(Paste::CheckedByDefault(Entry("DrawScale", "Display", Leaf("FloatProperty"), "1.000000")), "DrawScale starts ticked");
        Check(Paste::CheckedByDefault(Entry("DrawScale3D", "Display", Vector3(), kVector)), "DrawScale3D starts ticked");
        Check(Paste::CheckedByDefault(Entry("LightBrightness", "LightColor", Leaf("FloatProperty"), "64.000000")), "a light's brightness starts ticked");

        // Actor references are offered, unticked.
        const auto target = Entry("Target", "Camera", ObjectRef("Engine.Actor"), "SCamera'MyLevel.SCamera3'", {{"Engine.Actor", {"Actor", "Object"}}});
        Check(Paste::Offered(target) && Paste::ActorReference(target) && !Paste::CheckedByDefault(target), "a reference to an actor is offered unticked");
        const auto pawn = Entry("Watched", "Camera", ObjectRef("Engine.Pawn"), "None", {{"Engine.Pawn", {"Pawn", "Actor", "Object"}}});
        Check(Paste::ActorReference(pawn) && !Paste::CheckedByDefault(pawn), "a reference to an actor subclass is an actor reference");
        const auto nested = Entry("Links", "Events", {{"kind", "ArrayProperty"}, {"inner", {{"kind", "StructProperty"}, {"fields", {{"Who", ObjectRef("Engine.Actor")}, {"Delay", Leaf("FloatProperty")}}}}}},
                                  Json::array(), {{"Engine.Actor", {"Actor", "Object"}}});
        Check(Paste::Offered(nested) && !Paste::CheckedByDefault(nested), "an actor reference inside an array of structs leaves it unticked");

        // Assets are offered and ticked.
        const auto skins = Entry("Skins", "Display", {{"kind", "ArrayProperty"}, {"inner", ObjectRef("Engine.Material")}}, Json::array({"Texture'Pkg.Wall'"}),
                                 {{"Engine.Material", {"Material", "Object"}}});
        Check(Paste::Offered(skins) && Paste::CheckedByDefault(skins), "Skins (materials) are offered ticked");
        const auto mesh = Entry("StaticMesh", "Display", ObjectRef("Engine.StaticMesh"), "StaticMesh'Pkg.Crate'", {{"Engine.StaticMesh", {"StaticMesh", "Primitive", "Object"}}});
        Check(Paste::Offered(mesh) && !Paste::CheckedByDefault(mesh), "a prop's static mesh is offered unticked");
        const auto texture = Entry("Texture", "Display", ObjectRef("Engine.Texture"), "None",
                                   {{"Engine.Texture", {"Texture", "BitmapMaterial", "RenderedMaterial", "Material", "Object"}}});
        Check(Paste::Offered(texture), "a material subclass is a material");
        Check(Paste::Offered(Entry("Mesh", "Display", ObjectRef("Engine.Mesh"), "None", {{"Engine.Mesh", {"Mesh", "Primitive", "Object"}}})), "a mesh is offered");
        Check(Paste::Offered(Entry("AmbientSound", "Sound", ObjectRef("Engine.Sound"), "None", {{"Engine.Sound", {"Sound", "Object"}}})), "a sound is offered");
        Check(Paste::Offered(Entry("Projected", "Projector", {{"kind", "FixedArray"}, {"dimension", 2}, {"inner", ObjectRef("Engine.Material")}}, Json::array({"None", "None"}),
                                   {{"Engine.Material", {"Material", "Object"}}})), "a fixed array of materials is offered");
        const auto spawn = Entry("PawnClass", "Spawn", {{"kind", "ClassProperty"}, {"type", "Engine.Pawn"}}, "Class'Engine.Pawn'");
        Check(Paste::Offered(spawn) && !Paste::ActorReference(spawn) && Paste::CheckedByDefault(spawn), "a class reference is offered ticked, and is not an actor reference");

        // Owned sub-objects are never offered.
        Check(!Paste::Offered(Entry("Brush", "Brush", ObjectRef("Engine.Model"), "Model'MyLevel.Brush3Model'", {{"Engine.Model", {"Model", "Primitive", "Object"}}})),
              "a brush's Model is never offered");
        Check(!Paste::Offered(Entry("Emitters", "Emitter", {{"kind", "ArrayProperty"}, {"inner", ObjectRef("Engine.ParticleEmitter")}}, Json::array({"SpriteEmitter'MyLevel.Emitter0.SpriteEmitter0'"}),
                                    {{"Engine.ParticleEmitter", {"ParticleEmitter", "Object"}}})), "an emitter's particle emitters are never offered");
        Check(!Paste::Offered(Entry("Emitters", "Emitter", {{"kind", "ArrayProperty"}, {"inner", ObjectRef("Engine.ParticleEmitter")}}, Json::array(),
                                    {{"Engine.ParticleEmitter", {"ParticleEmitter", "Object"}}})), "an empty list of owned objects is not offered either");
        Check(!Paste::Offered(Entry("KParams", "Karma", ObjectRef("Engine.KarmaParamsCollision"), "None",
                                    {{"Engine.KarmaParamsCollision", {"KarmaParamsCollision", "Object"}}})), "Karma parameters are never offered");
        Check(!Paste::Offered(Entry("Thing", "Object", ObjectRef("Core.Object"), "None", {{"Core.Object", {"Object"}}})), "a reference of any class is not offered");
        Check(!Paste::Offered(Entry("Unknown", "Object", ObjectRef("Engine.Material"), "None")), "a reference whose class is unknown is not offered");
        Check(!Paste::Offered(Entry("Mixed", "Object", {{"kind", "StructProperty"}, {"fields", {{"Look", ObjectRef("Engine.Material")}, {"Shape", ObjectRef("Engine.Model")}}}},
                                    {{"Look", "None"}, {"Shape", "None"}}, {{"Engine.Material", {"Material", "Object"}}, {"Engine.Model", {"Model", "Object"}}})),
              "one owned reference in a struct is enough to leave it out");
        Check(Paste::Offered(Entry("DrawScale", "Display", Leaf("FloatProperty"), "1.000000")), "plain values are offered");

        // Offer: the offered ones, by category and then name.
        const Json copied = Json::array({Entry("Tag", "Events", Leaf("NameProperty"), "Light3"), skins,
                                         Entry("Brush", "Brush", ObjectRef("Engine.Model"), "None", {{"Engine.Model", {"Model", "Object"}}}),
                                         Entry("drawScale", "Display", Leaf("FloatProperty"), "1.000000"), Entry("Event", "Events", Leaf("NameProperty"), "None")});
        const auto offered = Paste::Offer(copied);
        std::string order;
        for (const auto& entry : offered) order += (order.empty() ? "" : ",") + entry.at("name").get<std::string>();
        Check(order == "drawScale,Skins,Event,Tag", "offered in category and name order, the owned Model left out");
        Check(Paste::Offer(Json::array()).is_array() && Paste::Offer(Json::array()).empty(), "nothing offered is an empty list");

        // What would name something else once another map is open.
        Check(Paste::NamesMapObject(target), "a reference to an actor names something in the map");
        Check(!Paste::NamesMapObject(pawn), "a reference to no actor does not");
        Check(Paste::NamesMapObject(Entry("Links", "Events", nested.at("schema"), Json::array({Json{{"Who", "PathNode'MyLevel.PathNode3'"}, {"Delay", "0.000000"}}}),
                                          {{"Engine.Actor", {"Actor", "Object"}}})), "an actor inside an array of structs does");
        Check(!Paste::NamesMapObject(nested), "an empty list does not");
        Check(!Paste::NamesMapObject(skins) && !Paste::NamesMapObject(mesh), "assets in a package do not");
        Check(Paste::NamesMapObject(Entry("StaticMesh", "Display", ObjectRef("Engine.StaticMesh"), "StaticMesh'mylevel.Crate'", mesh.at("lineage"))),
              "a mesh stored in the map does");
        Check(Paste::NamesMapObject(Entry("Skins", "Display", skins.at("schema"), Json::array({"Texture'Pkg.Wall'", "Texture'MyLevel.Poster'"}), skins.at("lineage"))),
              "one material stored in the map is enough");
        Check(Paste::NamesMapObject(Entry("PawnClass", "Spawn", {{"kind", "ClassProperty"}, {"type", "Engine.Pawn"}}, "Class'MyLevel.MyGuard'")), "a class compiled in the map does");
        Check(!Paste::NamesMapObject(spawn), "a class in a package does not");
        Check(!Paste::NamesMapObject(groups) && !Paste::NamesMapObject(Entry("DrawScale", "Display", Leaf("FloatProperty"), "1.000000")), "names and plain values do not");

        // Applicable: the same schema, in any class.
        const auto brightness = Entry("LightBrightness", "LightColor", Leaf("FloatProperty"), "64.000000");
        Check(Paste::Applicable(brightness, Leaf("FloatProperty")), "a spotlight takes a light's brightness: the same schema in another class");
        Json withCategory = Leaf("FloatProperty"); withCategory["category"] = "LightColor";
        Check(Paste::Applicable(brightness, withCategory), "the category is not part of the schema");
        Check(!Paste::Applicable(brightness, Leaf("IntProperty")), "another kind is skipped");
        const auto type = Entry("LightType", "LightColor", {{"kind", "ByteProperty"}, {"choices", {"LT_None", "LT_Steady", "LT_Pulse"}}}, "LT_Steady");
        Check(Paste::Applicable(type, {{"kind", "ByteProperty"}, {"choices", {"LT_None", "LT_Steady", "LT_Pulse"}}}), "the same enum");
        Check(!Paste::Applicable(type, {{"kind", "ByteProperty"}, {"choices", {"PHYS_None", "PHYS_Walking"}}}), "a byte of another enum is skipped");
        Check(!Paste::Applicable(Entry("DrawScale3D", "Display", Vector3(), kVector),
                                 {{"kind", "StructProperty"}, {"fields", {{"X", Leaf("FloatProperty")}, {"Y", Leaf("FloatProperty")}}}}), "a struct with other fields is skipped");
        Check(!Paste::Applicable(mesh, ObjectRef("Engine.Mesh")), "a reference to another class is skipped");
        Check(Paste::Applicable(skins, {{"kind", "ArrayProperty"}, {"inner", ObjectRef("Engine.Material")}}), "the same array of materials");

        // Text.
        Check(Paste::ValueText(kVector) == "(X=1.000000,Y=2.000000,Z=3.000000)", "a struct shows as the editor writes it");
        Check(Paste::ValueText("64.000000") == "64.000000", "a leaf shows as it is");
        const auto longText = Paste::ValueText(std::string(300, 'a'), 20);
        Check(longText == std::string(20, 'a') + "...", "a long value is cut short");
        Check(Paste::ChoiceSummary(4, 37, 6) == "4 of 37 properties ticked, for 6 selected actors. Each actor takes the ones it has with the same name and type, all in one Undo step.",
              "the dialog's line");
        Check(Paste::ChoiceSummary(1, 1, 1).rfind("1 of 1 property ticked, for 1 selected actor.", 0) == 0, "the dialog's line in the singular");
        Check(Paste::Summary(3, 12, 0) == "Pasted 12 values onto 3 actors.", "a paste that took everything");
        Check(Paste::Summary(2, 7, 1) == "Pasted 7 values onto 2 actors. 1 value was left out where an actor has no property of the same name and type, "
                                         "or is a brush or mover, which keeps its own mesh, draw type and scale.", "skipped values are counted");
        Check(Paste::Summary(1, 1, 4).rfind("Pasted 1 value onto 1 actor. 4 values were left out where", 0) == 0, "singular and plural");
        Check(Paste::Summary(0, 0, 5) == "None of the selected actors can take these properties, so nothing was pasted.", "nothing pasted");
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PropertyPasteModelTests: " << checks << " checks passed\n";
    return 0;
}
