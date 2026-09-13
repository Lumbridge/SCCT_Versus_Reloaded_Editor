#pragma once
#include "Include/nlohmann/json.hpp"
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// Engine-independent document, identity and placement logic.
namespace Workflow
{
    using Json = nlohmann::json;
    using Vector = std::array<double, 3>;
    using Rotation = std::array<int, 3>; // Unreal pitch/yaw/roll (65536 units/turn).
    struct Pose { Vector position{}; Rotation rotation{}; };
    struct ActorText { std::string name, type, text; };
    struct Reference { std::string type, path; size_t begin{}, end{}; };
    std::string Fold(std::string value);
    // Exclusions identify exact preview rows; never accept replacement changes.
    Json SelectedTagChanges(const Json& preview);
    std::string Id();
    std::string Timestamp();
    Json ReadDocument(const std::filesystem::path& path, const Json& empty);
    void WriteDocument(const std::filesystem::path& path, const Json& document);
    void UpdateEntry(Json& entries, const std::string& id, Json replacement);
    std::vector<ActorText> ParseActors(const std::string& t3d);
    std::string NormalizeExportNames(const std::string& text);
    std::vector<Reference> References(const std::string& text);
    std::string RewriteReferences(const std::string& text,
                                  const std::map<std::string, std::string>& paths);
    std::string Property(const std::string& text, const std::string& key);
    std::string SetProperty(std::string text, const std::string& key, const std::string& value);
    std::string RemoveProperty(const std::string& text, const std::string& key);
    std::string RenameObjects(const std::string& text, const std::string& prefix);
    Json CanonicalizeAssembly(Json definition, const std::map<std::string,std::string>& memberNames);
    Vector TransformPoint(const Vector& point, const Pose& frame, bool inverse = false);
    Rotation TransformRotation(const Rotation& rotation, const Rotation& frame, bool inverse = false);
    std::string VectorText(const Vector& v);
    std::string RotationText(const Rotation& v);
    // Generates a copy; saved definitions are never mutated by placement.
    Json PreparePlacement(const Json& definition, const Pose& frame,
                          const std::string& prefix, const std::string& levelPath,
                          const std::map<std::string, std::string>& bindings);
}
