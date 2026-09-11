#include "pch.h"
#include "MapRecovery.h"
#include "RecoveredBspGeometry.h"
#include "RecoveredSurfacePartition.h"
#include "RecoveredPolygonImport.h"
#include "RecoveredActorImport.h"
#include "RecoveredAssetPackage.h"
#include "LightmapFix.h"
#include "BspLeafLightFix.h"
#include "logger.h"

#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "comdlg32.lib")

namespace
{
    constexpr uintptr_t kGEditor = 0x1165DFA0;
    constexpr uintptr_t kGWarn = 0x115BEFB0;
    constexpr uintptr_t kGNamesData = 0x1169CFBC;
    constexpr uintptr_t kGNamesCount = 0x1169CFC0;
    constexpr uintptr_t kGSerializationContext = 0x11691D7C;
    constexpr uintptr_t kUObjectAddToRoot = 0x10FAA0A0;
    constexpr uintptr_t kUObjectRemoveFromRoot = 0x10FA6750;
    constexpr uintptr_t kFArrayAdd = 0x10E022DF;
    constexpr uintptr_t kABrushClass = 0x1181B048;
    constexpr size_t kSerializationPlatformOffset = 0x80;
    constexpr int kPcRuntimePlatform = 1;
    constexpr size_t kEditorLevelOffset = 0x130;
    constexpr size_t kLevelModelOffset = 0x13C;
    constexpr size_t kLevelActorsDataOffset = 0x2C;
    constexpr size_t kLevelActorsCountOffset = 0x30;
    constexpr size_t kLevelCollisionHashOffset = 0x3A4C;
    constexpr size_t kActorBrushOffset = 0x238;
    constexpr size_t kActorLevelOffset = 0x1A4;
    constexpr size_t kActorZoneNumberOffset = 0x1B4;
    constexpr size_t kEditorRedrawLevelVtableOffset = 0xE8;
    constexpr size_t kEditorPostLoadObjectOffset = 0x148;
    constexpr size_t kPostLoadRefreshVtableOffset = 0x84;
    constexpr UINT kFileSaveCommandId = 40007;
    constexpr UINT kFileSaveAsCommandId = 40008;

    // PC-runtime UModel layout used by compiled .sdc maps. These offsets and
    // strides are the ones consumed by this editor build's
    // UModel::BuildRenderData (0x110D13D0).
    constexpr size_t kModelNodesOffset = 0x54;
    constexpr size_t kModelVertsOffset = 0x64;
    constexpr size_t kModelVectorsOffset = 0x74;
    constexpr size_t kModelPointsOffset = 0x84;
    constexpr size_t kModelSurfsOffset = 0x94;
    constexpr size_t kNodeStride = 0x5C;
    constexpr size_t kVertStride = 0x08;
    constexpr size_t kSurfStride = 0x2C;
    constexpr size_t kSurfMaterialOffset = 0x10;
    constexpr size_t kSurfFlagsOffset = 0x14;
    constexpr uint32_t kSurfEditorPolyFlags = 0x0C000000u;
    // Native Add Special's Portal checkbox (0x421); Anti-Portal is 0x08000000.
    constexpr uint32_t kSurfZonePortal = 0x04000000u;
    constexpr size_t kNodeVertPoolOffset = 0x28;
    constexpr size_t kNodeSurfOffset = 0x2C;
    constexpr size_t kNodeVertexCountOffset = 0x5A;
    // Verified against UModel::PointRegion in the supported editor executable.
    constexpr size_t kNodeFrontOffset = 0x34;
    constexpr size_t kNodeBackOffset = 0x30;
    constexpr size_t kNodeFlagsOffset = 0x5B;
    constexpr size_t kModelRootOutsideOffset = 0x104;
    constexpr size_t kSurfBaseOffset = 0x18;
    constexpr size_t kSurfNormalOffset = 0x1A;
    constexpr size_t kSurfTextureUOffset = 0x1C;
    constexpr size_t kSurfTextureVOffset = 0x1E;
    constexpr size_t kObjectOuterOffset = 0x18;
    constexpr size_t kObjectClassOffset = 0x24;
    constexpr size_t kClassSuperOffset = 0x28;
    constexpr size_t kObjectNameOffset = 0x20;
    constexpr size_t kFNameTextOffset = 0x0C;
    constexpr uintptr_t kALevelInfoClass = 0x11819838;
    constexpr size_t kModelZonesOffset = 0x118;
    constexpr size_t kModelZoneStride = 0x10;
    constexpr int kMaxCookedArrayElements = 1000000;
    constexpr size_t kMaxRecoveredFaces = 250000;

    struct RawArray
    {
        const unsigned char* data = nullptr;
        int count = 0;
    };

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct RecoveredFace
    {
        std::vector<Vec3> vertices;
        Vec3 normal;
        Vec3 origin;
        Vec3 textureU;
        Vec3 textureV;
        std::string materialPath;
        int surfaceIndex = -1;
        uint32_t flags = 0;
        bool structural = true;
        void* embeddedMaterialClass = nullptr;
    };

    template <typename T>
    T ReadRaw(const unsigned char* data, size_t offset)
    {
        T value = {};
        memcpy(&value, data + offset, sizeof(value));
        return value;
    }

    RawArray ReadRawArray(const void* owner, size_t offset)
    {
        RawArray result;
        const unsigned char* field =
            static_cast<const unsigned char*>(owner) + offset;
        result.data = ReadRaw<const unsigned char*>(field, 0);
        result.count = ReadRaw<int>(field, sizeof(void*));
        return result;
    }

    bool IsValidArray(const RawArray& array)
    {
        return array.data && array.count >= 0
            && array.count <= kMaxCookedArrayElements;
    }

    bool IsFinite(const Vec3& value)
    {
        constexpr float kCoordinateLimit = 10000000.0f;
        return std::isfinite(value.x) && std::isfinite(value.y)
            && std::isfinite(value.z)
            && std::fabs(value.x) <= kCoordinateLimit
            && std::fabs(value.y) <= kCoordinateLimit
            && std::fabs(value.z) <= kCoordinateLimit;
    }

    Vec3 Add(const Vec3& a, const Vec3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    Vec3 Subtract(const Vec3& a, const Vec3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    Vec3 Scale(const Vec3& value, float scale)
    {
        return { value.x * scale, value.y * scale, value.z * scale };
    }

    float Dot(const Vec3& a, const Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    bool Normalize(Vec3& value)
    {
        const float lengthSquared = Dot(value, value);
        if (!std::isfinite(lengthSquared) || lengthSquared < 0.00000001f)
            return false;

        value = Scale(value, 1.0f / std::sqrt(lengthSquared));
        return true;
    }

    bool NearlySamePoint(const Vec3& a, const Vec3& b)
    {
        const Vec3 delta = Subtract(a, b);
        return Dot(delta, delta) < 0.000001f;
    }

    Vec3 ReadVector(const RawArray& array, int index)
    {
        if (index < 0 || index >= array.count)
            return {};
        return ReadRaw<Vec3>(array.data, static_cast<size_t>(index) * sizeof(Vec3));
    }

    Vec3 PolygonNormal(const std::vector<Vec3>& vertices)
    {
        Vec3 normal;
        for (size_t index = 0; index < vertices.size(); ++index)
        {
            const Vec3& current = vertices[index];
            const Vec3& next = vertices[(index + 1) % vertices.size()];
            normal.x += (current.y - next.y) * (current.z + next.z);
            normal.y += (current.z - next.z) * (current.x + next.x);
            normal.z += (current.x - next.x) * (current.y + next.y);
        }
        return normal;
    }

    // Kept POD-only so SEH can contain invalid or partially stripped UObject
    // references without interacting with C++ stack unwinding.
    bool FormatExternalObjectPath(void* object, char* output, size_t outputSize,
                                  const char* embeddedPackage = nullptr)
    {
        if (!output || outputSize == 0)
            return false;
        output[0] = '\0';
        if (!object)
            return false;

        __try
        {
            void** names = *reinterpret_cast<void***>(kGNamesData);
            const int nameCount = *reinterpret_cast<int*>(kGNamesCount);
            if (!names || nameCount <= 0)
                return false;

            const char* parts[16] = {};
            int depth = 0;
            void* current = object;
            while (current && depth < 16)
            {
                const int nameIndex = *reinterpret_cast<int*>(
                    static_cast<char*>(current) + kObjectNameOffset);
                if (nameIndex < 0 || nameIndex >= nameCount)
                    return false;
                void* entry = names[nameIndex];
                if (!entry)
                    return false;
                const char* name = static_cast<const char*>(entry)
                    + kFNameTextOffset;
                if (!*name)
                    return false;
                parts[depth++] = name;
                current = *reinterpret_cast<void**>(
                    static_cast<char*>(current) + kObjectOuterOffset);
            }
            if (current || depth < 2)
                return false;

            const char* topPackage = parts[depth - 1];
            if (_stricmp(topPackage, "MyLevel") == 0)
            {
                if (!embeddedPackage || !*embeddedPackage) return false;
                parts[depth - 1] = embeddedPackage;
            }
            if (_stricmp(topPackage, "Transient") == 0
                || _stricmp(topPackage, "None") == 0)
                return false;

            for (int index = depth - 1; index >= 0; --index)
            {
                if (output[0] != '\0')
                    strncat_s(output, outputSize, ".", _TRUNCATE);
                strncat_s(output, outputSize, parts[index], _TRUNCATE);
            }
            return output[0] != '\0';
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            output[0] = '\0';
            return false;
        }
    }

    void MakeTextureAxes(const Vec3& normal, Vec3& textureU, Vec3& textureV)
    {
        Vec3 reference = std::fabs(normal.z) < 0.9f
            ? Vec3{ 0.0f, 0.0f, 1.0f }
            : Vec3{ 0.0f, 1.0f, 0.0f };
        textureU = Cross(reference, normal);
        if (!Normalize(textureU))
            textureU = { 1.0f, 0.0f, 0.0f };
        textureV = Cross(normal, textureU);
        if (!Normalize(textureV))
            textureV = { 0.0f, 1.0f, 0.0f };
    }

    bool ExtractRecoveredFaces(void* level, std::vector<RecoveredFace>& faces,
                               size_t& skipped, std::string& error,
                               const char* embeddedPackage = nullptr)
    {
        skipped = 0;
        if (!level)
        {
            error = "The active recovered level is missing.";
            return false;
        }

        void* model = *reinterpret_cast<void**>(
            static_cast<unsigned char*>(level) + kLevelModelOffset);
        if (!model)
        {
            error = "The recovered level has no cooked BSP model.";
            return false;
        }

        const RawArray nodes = ReadRawArray(model, kModelNodesOffset);
        const RawArray verts = ReadRawArray(model, kModelVertsOffset);
        const RawArray vectors = ReadRawArray(model, kModelVectorsOffset);
        const RawArray points = ReadRawArray(model, kModelPointsOffset);
        const RawArray surfs = ReadRawArray(model, kModelSurfsOffset);
        if (!IsValidArray(nodes) || !IsValidArray(verts)
            || !IsValidArray(vectors) || !IsValidArray(points)
            || !IsValidArray(surfs))
        {
            error = "The cooked BSP arrays are missing or have unsafe sizes.";
            return false;
        }
        if (nodes.count == 0 || points.count == 0 || verts.count == 0)
        {
            error = "The recovered map does not contain any cooked BSP polygons.";
            return false;
        }

        faces.clear();
        faces.reserve(static_cast<size_t>(nodes.count));
        for (int nodeIndex = 0; nodeIndex < nodes.count; ++nodeIndex)
        {
            const unsigned char* node =
                nodes.data + static_cast<size_t>(nodeIndex) * kNodeStride;
            const int vertPool = ReadRaw<int>(node, kNodeVertPoolOffset);
            const int surfIndex = ReadRaw<int>(node, kNodeSurfOffset);
            const unsigned int vertexCount =
                ReadRaw<unsigned char>(node, kNodeVertexCountOffset);
            // Empty splitter records still partition the BSP tree; they have
            // no visible polygon to export and are not malformed surfaces.
            if (vertexCount == 0) continue;
            if (surfIndex < 0 || surfIndex >= surfs.count)
            {
                error = "A cooked BSP polygon references a missing surface.";
                return false;
            }
            if (vertexCount < 3 || vertPool < 0
                || vertexCount > static_cast<unsigned int>(verts.count)
                || vertPool > verts.count - static_cast<int>(vertexCount))
            {
                ++skipped;
                continue;
            }

            RecoveredFace face;
            face.surfaceIndex = surfIndex;
            face.structural = (ReadRaw<unsigned char>(node, kNodeFlagsOffset) & 0x21) == 0;
            face.vertices.reserve(vertexCount);
            bool valid = true;
            for (unsigned int vertexIndex = 0;
                 vertexIndex < vertexCount; ++vertexIndex)
            {
                const unsigned char* vert = verts.data
                    + static_cast<size_t>(vertPool + vertexIndex) * kVertStride;
                const unsigned int pointIndex = ReadRaw<unsigned short>(vert, 0);
                if (pointIndex == 0xFFFFu || pointIndex >= static_cast<unsigned int>(points.count))
                {
                    valid = false;
                    break;
                }

                const Vec3 point = ReadVector(points, pointIndex);
                if (!IsFinite(point))
                {
                    valid = false;
                    break;
                }
                if (face.vertices.empty()
                    || !NearlySamePoint(face.vertices.back(), point))
                    face.vertices.push_back(point);
            }
            if (!face.vertices.empty()
                && NearlySamePoint(face.vertices.front(), face.vertices.back()))
                face.vertices.pop_back();
            if (!valid || face.vertices.size() < 3)
            {
                ++skipped;
                continue;
            }

            Vec3 calculatedNormal = PolygonNormal(face.vertices);
            if (!Normalize(calculatedNormal))
            {
                ++skipped;
                continue;
            }

            face.normal = calculatedNormal;
            face.origin = face.vertices.front();
            MakeTextureAxes(face.normal, face.textureU, face.textureV);

            if (surfIndex >= 0 && surfIndex < surfs.count)
            {
                const unsigned char* surf = surfs.data
                    + static_cast<size_t>(surfIndex) * kSurfStride;
                face.flags = ReadRaw<uint32_t>(surf, kSurfFlagsOffset);
                const int baseIndex = ReadRaw<unsigned short>(surf, kSurfBaseOffset);
                const int normalIndex = ReadRaw<unsigned short>(surf, kSurfNormalOffset);
                const int textureUIndex = ReadRaw<unsigned short>(surf, kSurfTextureUOffset);
                const int textureVIndex = ReadRaw<unsigned short>(surf, kSurfTextureVOffset);

                void* material = ReadRaw<void*>(surf, kSurfMaterialOffset);
                char materialPath[512] = {};
                if (FormatExternalObjectPath(
                        material, materialPath, sizeof(materialPath)))
                    face.materialPath = materialPath;
                else if (material && FormatExternalObjectPath(
                    material, materialPath, sizeof(materialPath), embeddedPackage))
                {
                    face.materialPath = materialPath;
                    face.embeddedMaterialClass = *reinterpret_cast<void**>(
                        static_cast<char*>(material) + kObjectClassOffset);
                }
                else if (material)
                {
                    error = "A BSP material is embedded in the map or has an invalid asset reference. "
                            "Move that material to an external package before source recovery.";
                    return false;
                }

                Vec3 cookedNormal = ReadVector(vectors, normalIndex);
                if (IsFinite(cookedNormal) && Normalize(cookedNormal))
                {
                    if (Dot(calculatedNormal, cookedNormal) < 0.0f)
                        std::reverse(face.vertices.begin(), face.vertices.end());
                    face.normal = cookedNormal;
                }

                if (baseIndex >= 0 && baseIndex < points.count)
                {
                    const Vec3 base = ReadVector(points, baseIndex);
                    if (IsFinite(base))
                        face.origin = base;
                }

                Vec3 textureU = ReadVector(vectors, textureUIndex);
                Vec3 textureV = ReadVector(vectors, textureVIndex);
                if (IsFinite(textureU) && IsFinite(textureV)
                    && Dot(textureU, textureU) > 0.00000001f
                    && Dot(textureV, textureV) > 0.00000001f)
                {
                    face.textureU = textureU;
                    face.textureV = textureV;
                }
            }

            faces.push_back(std::move(face));
            if (faces.size() > kMaxRecoveredFaces)
            {
                error = "The map contains too many cooked BSP faces to export safely.";
                faces.clear();
                return false;
            }
        }

        if (faces.empty())
        {
            error = "No valid cooked BSP polygons could be reconstructed.";
            return false;
        }

        std::string fallbackMaterial;
        for (const RecoveredFace& face : faces)
        {
            if (!face.materialPath.empty())
            {
                fallbackMaterial = face.materialPath;
                break;
            }
        }
        if (fallbackMaterial.empty())
        {
            // TXT_INI is a core editor package. This is only reached for a
            // map whose cooked surfaces all contain null/embedded materials.
            fallbackMaterial = "TXT_INI.BSP.BETON_Lit_Spec";
        }
        for (RecoveredFace& face : faces)
        {
            if (face.materialPath.empty())
                face.materialPath = fallbackMaterial;
        }
        return true;
    }

    void WriteVector(std::ostream& output, const Vec3& value)
    {
        // Round-trip the float vertices validated for import, including small
        // coordinates near zero. Fixed six-decimal text can collapse edges.
        output << std::showpos << std::scientific
               << std::setprecision(std::numeric_limits<float>::max_digits10 - 1)
               << value.x << ',' << value.y << ',' << value.z
               << std::noshowpos;
    }

    Vec3 ToEditorVector(const RecoveredBspGeometry::Vec3& value)
    {
        return { static_cast<float>(value.x), static_cast<float>(value.y),
                 static_cast<float>(value.z) };
    }

    bool ReconstructSourceBrushes(void* level, RecoveredBspGeometry::Result& result,
                                  RecoveredPolygonImport::VertexPool& sourcePoints,
                                  std::string& error)
    {
        using namespace RecoveredBspGeometry;
        void* model = *reinterpret_cast<void**>(
            static_cast<char*>(level) + kLevelModelOffset);
        const RawArray rawNodes = ReadRawArray(model, kModelNodesOffset);
        const RawArray rawPoints = ReadRawArray(model, kModelPointsOffset);
        if (!IsValidArray(rawNodes) || !IsValidArray(rawPoints) || !rawPoints.count)
        {
            error = "The compiled map has no valid BSP tree or bounds.";
            return false;
        }
        std::vector<Node> nodes;
        nodes.reserve(rawNodes.count);
        for (int index = 0; index < rawNodes.count; ++index)
        {
            const auto* data = rawNodes.data + static_cast<size_t>(index) * kNodeStride;
            const auto normal = ReadRaw<::Vec3>(data, 0);
            Node node;
            node.normal = { normal.x, normal.y, normal.z };
            node.distance = ReadRaw<float>(data, 0x0C);
            node.front = ReadRaw<int>(data, kNodeFrontOffset);
            node.back = ReadRaw<int>(data, kNodeBackOffset);
            node.surfaceIndex = ReadRaw<int>(data, kNodeSurfOffset);
            node.isCsg = ReadRaw<unsigned char>(data, kNodeVertexCountOffset) != 0
                && (ReadRaw<unsigned char>(data, kNodeFlagsOffset) & 0x21) == 0;
            nodes.push_back(node);
        }
        const auto first = ReadVector(rawPoints, 0);
        Bounds bounds{ {first.x, first.y, first.z}, {first.x, first.y, first.z} };
        for (int index = 0; index < rawPoints.count; ++index)
        {
            const auto point = ReadVector(rawPoints, index);
            if (!IsFinite(point))
            {
                error = "The BSP contains a non-finite point.";
                return false;
            }
            bounds.minimum.x = (std::min)(bounds.minimum.x, double(point.x));
            bounds.minimum.y = (std::min)(bounds.minimum.y, double(point.y));
            bounds.minimum.z = (std::min)(bounds.minimum.z, double(point.z));
            bounds.maximum.x = (std::max)(bounds.maximum.x, double(point.x));
            bounds.maximum.y = (std::max)(bounds.maximum.y, double(point.y));
            bounds.maximum.z = (std::max)(bounds.maximum.z, double(point.z));
        }
        // These planes only bound the extraction algorithm. The reconstructor
        // rejects output touching them; they never become invented map walls.
        constexpr double margin = 64.0;
        bounds.minimum.x -= margin; bounds.minimum.y -= margin; bounds.minimum.z -= margin;
        bounds.maximum.x += margin; bounds.maximum.y += margin; bounds.maximum.z += margin;
        const bool rootOutside = *reinterpret_cast<int*>(
            static_cast<char*>(model) + kModelRootOutsideOffset) != 0;
        if (!Reconstruct(nodes, rootOutside, bounds, result, error)) return false;
        const size_t cells = result.brushes.size();
        Logger::log("MapRecovery: merging compatible structural cells");
        if (!MergeAdjacentConvexBrushes(result, error)) return false;
        Logger::log("MapRecovery: merged " + std::to_string(cells) + " structural cells into "
            + std::to_string(result.brushes.size()) + " closed brushes");
        // Establish shared corners in reconstruction order, before material
        // subdivision and the independent CSG build-order optimization.
        for (const auto& brush : result.brushes)
            for (const auto& face : brush.faces)
                for (const auto& vertex : face.vertices)
                {
                    RecoveredBspGeometry::Vec3 canonical;
                    if (!sourcePoints.Resolve(vertex,canonical,error)) return false;
                }
        for (auto& brush : result.brushes)
            if (!RecoveredSurfacePartition::CanonicalizeBrushForEditor(brush, error)) return false;
        if (!OrderForRebuild(result, error)) return false;
        return true;
    }

    void DumpSurfaceCase(const std::filesystem::path& path,
                         const RecoveredBspGeometry::Face& face,
                         const std::vector<RecoveredSurfacePartition::Surface>& candidates)
    {
        std::ofstream output(path);
        output << std::setprecision(17);
        auto vector = [&](const RecoveredBspGeometry::Vec3& value) {
            output << '[' << value.x << ',' << value.y << ',' << value.z << ']';
        };
        auto polygon = [&](const RecoveredBspGeometry::Vec3& normal,
                           const std::vector<RecoveredBspGeometry::Vec3>& vertices) {
            output << "\"normal\":"; vector(normal); output << ",\"vertices\":[";
            for (size_t index = 0; index < vertices.size(); ++index)
            {
                if (index) output << ',';
                vector(vertices[index]);
            }
            output << ']';
        };
        output << "{\"face\":{";
        polygon(face.normal, face.vertices);
        output << "},\"candidates\":[";
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            if (index) output << ',';
            output << '{'; polygon(candidates[index].normal, candidates[index].vertices);
            output << ",\"materialIndex\":" << candidates[index].materialIndex << '}';
        }
        output << "]}\n";
    }

    void DumpSurfaceBatch(const std::filesystem::path& path,
                          const RecoveredBspGeometry::Result& geometry,
                          const std::vector<RecoveredFace>& surfaces)
    {
        // Only produced after a partition failure. This allows all remaining
        // cases to be replayed without repeatedly launching the native editor.
        std::ofstream output(path);
        output << std::setprecision(17);
        auto vector = [&](const RecoveredBspGeometry::Vec3& value) {
            output << '[' << value.x << ',' << value.y << ',' << value.z << ']';
        };
        auto polygon = [&](const RecoveredBspGeometry::Vec3& normal,
                           const std::vector<RecoveredBspGeometry::Vec3>& vertices) {
            output << "\"normal\":"; vector(normal); output << ",\"vertices\":[";
            for (size_t index=0; index<vertices.size(); ++index)
            {
                if (index) output << ',';
                vector(vertices[index]);
            }
            output << ']';
        };
        output << "{\"brushes\":[";
        for (size_t index=0; index<geometry.brushes.size(); ++index)
        {
            if (index) output << ',';
            const auto& brush=geometry.brushes[index];
            output << "{\"subtractive\":" << (brush.subtractive ? "true" : "false") << ",\"faces\":[";
            for (size_t faceIndex=0; faceIndex<brush.faces.size(); ++faceIndex)
            {
                if (faceIndex) output << ',';
                const auto& face=brush.faces[faceIndex];
                output << '{'; polygon(face.normal,face.vertices);
                output << ",\"surfaceIndex\":" << face.surfaceIndex << '}';
            }
            output << "]}";
        }
        output << "],\"surfaces\":[";
        for (size_t index=0; index<surfaces.size(); ++index)
        {
            if (index) output << ',';
            const auto& source=surfaces[index];
            std::vector<RecoveredBspGeometry::Vec3> vertices;
            vertices.reserve(source.vertices.size());
            for (const auto& vertex:source.vertices) vertices.push_back({vertex.x,vertex.y,vertex.z});
            output << '{'; polygon({source.normal.x,source.normal.y,source.normal.z},vertices);
            output << ",\"materialIndex\":" << index << ",\"surfaceIndex\":" << source.surfaceIndex
                   << ",\"flags\":" << source.flags << ",\"structural\":"
                   << (source.structural ? "true" : "false") << '}';
        }
        output << "]}\n";
    }

    bool WriteSourceBrushes(const RecoveredBspGeometry::Result& geometry,
                            const std::vector<RecoveredFace>& surfaces,
                            RecoveredPolygonImport::VertexPool& sourcePoints,
                            std::string& text, std::string& error,
                            const std::filesystem::path& failurePath)
    {
        namespace Surface = RecoveredSurfacePartition;
        std::unordered_map<int, int> bySurface;
        std::unordered_map<int64_t, std::vector<size_t>> byPlaneDistance;
        std::vector<Surface::Surface> candidates;
        for (size_t index = 0; index < surfaces.size(); ++index)
        {
            const auto& surface = surfaces[index];
            if (!surface.structural && !(surface.flags & 8u))
            {
                error = "A non-structural BSP surface has unsupported collision flags ("
                    + std::to_string(surface.flags) + "). No finished source map was saved.";
                return false;
            }
            bySurface.emplace(surface.surfaceIndex, static_cast<int>(index));
            Surface::Surface candidate;
            candidate.normal = {surface.normal.x, surface.normal.y, surface.normal.z};
            // Cooked BSP nodes can be fragments of the same original surface;
            // that surface owns the texture, UV basis and polygon flags.
            candidate.materialIndex = bySurface.at(surface.surfaceIndex);
            for (const auto& point : surface.vertices)
                candidate.vertices.push_back({point.x, point.y, point.z});
            int64_t bucket = 0;
            if (!Surface::PlaneBucket(candidate.normal, candidate.vertices.front(), bucket))
            {
                error = "A recovered surface plane is outside the supported coordinate range.";
                return false;
            }
            if (surface.structural) byPlaneDistance[bucket].push_back(index);
            candidates.push_back(std::move(candidate));
        }
        std::ostringstream output;
        output << "Begin Map\n";
        size_t polygonCount = 0;
        auto emit = [&](const std::vector<Surface::Vec3>& polygon,
                        const Surface::Vec3& normal, const RecoveredFace& material,
                        bool prepared = false, bool shareCorners = false) {
            std::vector<std::vector<Surface::Vec3>> parts;
            if (prepared) parts.push_back(polygon);
            else if (!Surface::SplitForVertexLimit(polygon, normal, 16, parts, error)) return false;
            for (auto& vertices : parts)
            {
                if (shareCorners)
                    for (auto& vertex : vertices)
                    {
                        Surface::Vec3 canonical;
                        if (!sourcePoints.Resolve(vertex,canonical,error)) return false;
                        vertex=canonical;
                    }
                // Native T3D import ignores Normal and cleans every polygon.
                // A valid float polygon can still disappear here. Check the
                // actual importer rules before sending it an incomplete face.
                const auto native = RecoveredPolygonImport::Prepare(vertices);
                if (!native.accepted())
                {
                    error = "The editor would discard a reconstructed surface during import. "
                        "Recovery stopped before creating an incomplete brush.";
                    return false;
                }
                if (!RecoveredPolygonImport::PreservesOutline(vertices, native))
                {
                    error = "The editor's polygon cleanup would change a reconstructed surface boundary. "
                        "Recovery stopped before creating an incomplete brush.";
                    return false;
                }
                if (++polygonCount > 200000)
                {
                    error = "Surface reconstruction exceeded the editor's safe polygon budget.";
                    return false;
                }
                if (material.materialPath.find_first_of("\"\r\n") != std::string::npos)
                {
                    error = "A BSP material path cannot be represented in the editor's text format: "
                        + material.materialPath;
                    return false;
                }
                output << "          Begin Polygon Texture=\"" << material.materialPath
                       << "\" Flags=" << material.flags << "\n";
                output << "             Origin   "; WriteVector(output, material.origin);
                output << "\n             Normal   "; WriteVector(output, ToEditorVector(normal));
                output << "\n             TextureU "; WriteVector(output, material.textureU);
                output << "\n             TextureV "; WriteVector(output, material.textureV);
                output << '\n';
                for (const auto& vertex : vertices)
                {
                    output << "             Vertex   "; WriteVector(output, ToEditorVector(vertex)); output << '\n';
                }
                output << "          End Polygon\n";
            }
            return true;
        };
        size_t brushIndex = 0;
        auto beginBrush = [&](bool subtractive, bool nonSolid, bool hiddenDivider = false) {
            ++brushIndex;
            output << "Begin Actor Class=Brush Name=RecoveredVolume" << brushIndex
                   << "\n    CsgOper=" << (subtractive ? "CSG_Subtract" : "CSG_Add")
                   << "\n    PolyFlags=" << (nonSolid ? 8 : 0);
            // Native Build All clears bHiddenEd while building, then restores
            // it. Keep functional, ordinary editable zone boundaries without
            // their wireframes cluttering the initial editing view.
            if (hiddenDivider) output << "\n    bHiddenEd=True";
            output << "\n    Begin Brush Name=RecoveredModel" << brushIndex
                   << "\n       Begin PolyList\n";
        };
        auto endBrush = [&]() {
            output << "       End PolyList\n    End Brush\n    Brush=Model'MyLevel.RecoveredModel"
                   << brushIndex << "'\nEnd Actor\n";
        };
        for (const auto& brush : geometry.brushes)
        {
            std::vector<bool> collapsedFaces;
            if (!Surface::ValidateEditorBrush(brush, collapsedFaces, error))
            {
                DumpSurfaceBatch(failurePath.parent_path()/"SurfaceBatch.json",geometry,surfaces);
                return false;
            }
            beginBrush(brush.subtractive, false);
            for (size_t faceIndex = 0; faceIndex < brush.faces.size(); ++faceIndex)
            {
                if (collapsedFaces[faceIndex]) continue;
                const auto& face = brush.faces[faceIndex];
                const auto found = bySurface.find(face.surfaceIndex);
                const int fallback = found == bySurface.end() ? 0 : found->second;
                int64_t bucket = 0;
                if (face.vertices.empty() || !Surface::PlaneBucket(face.normal, face.vertices.front(), bucket))
                {
                    error = "A reconstructed brush face has an invalid plane.";
                    return false;
                }
                std::vector<Surface::Surface> coplanar;
                for (int offset = -1; offset <= 1; ++offset)
                {
                    const auto group = byPlaneDistance.find(bucket + offset);
                    if (group != byPlaneDistance.end())
                        for (size_t index : group->second) coplanar.push_back(candidates[index]);
                }
                std::vector<Surface::Piece> pieces;
                Surface::Limits partitionLimits;
                partitionLimits.matchEditorPrecision = true;
                if (!Surface::Partition(face, coplanar, fallback, pieces, error, partitionLimits))
                {
                    DumpSurfaceCase(failurePath, face, coplanar);
                    DumpSurfaceBatch(failurePath.parent_path()/"SurfaceBatch.json",geometry,surfaces);
                    error += " (material partition; details: " + failurePath.string() + ")";
                    return false;
                }
                std::vector<Surface::Piece> prepared;
                if (!Surface::PrepareForEditor(face, pieces, 16, prepared, error))
                {
                    DumpSurfaceCase(failurePath, face, coplanar);
                    DumpSurfaceBatch(failurePath.parent_path()/"SurfaceBatch.json",geometry,surfaces);
                    error += " (editor surface preparation; details: " + failurePath.string() + ")";
                    return false;
                }
                for (const auto& piece : prepared)
                    if (!emit(piece.vertices, face.normal, surfaces[piece.materialIndex], true, true))
                    {
                        auto failedFace = face;
                        failedFace.vertices = piece.vertices;
                        DumpSurfaceCase(failurePath, failedFace, {});
                        DumpSurfaceBatch(failurePath.parent_path()/"SurfaceBatch.json",geometry,surfaces);
                        error += " (source polygon; details: " + failurePath.string() + ")";
                        return false;
                    }
            }
            endBrush();
        }
        // Cooked BSP cuts an authored sheet into many fragments. Keep each
        // original surface in one ordinary non-solid brush, retaining every
        // fragment and its exact triangulation instead of creating an actor
        // and a separate CSG operation for every fragment.
        std::vector<std::vector<size_t>> sheetGroups;
        std::unordered_map<int, size_t> sheetGroupBySurface;
        for (size_t index = 0; index < surfaces.size(); ++index)
        {
            if (surfaces[index].structural) continue;
            const auto group = sheetGroupBySurface.emplace(surfaces[index].surfaceIndex, sheetGroups.size());
            if (group.second) sheetGroups.emplace_back();
            sheetGroups[group.first->second].push_back(index);
        }
        size_t hiddenZoneDividers = 0;
        for (const auto& group : sheetGroups)
        {
            const bool zoneDivider = (surfaces[group.front()].flags & kSurfZonePortal) != 0;
            if (zoneDivider) ++hiddenZoneDividers;
            beginBrush(false, true, zoneDivider);
            struct SheetPlane
            {
                Surface::Vec3 normal;
                Surface::Vec3 origin;
                std::vector<Surface::Piece> pieces;
            };
            std::vector<SheetPlane> sheetPlanes;
            const Surface::Limits sheetLimits;
            for (size_t index : group)
            {
                std::vector<Surface::Surface> sheetParts;
                if (!Surface::PrepareSheetForEditor(candidates[index], sheetParts, error))
                {
                    RecoveredBspGeometry::Face failedFace{candidates[index].vertices,
                        candidates[index].normal, surfaces[index].surfaceIndex};
                    DumpSurfaceCase(failurePath, failedFace, {});
                    DumpSurfaceBatch(failurePath.parent_path()/"SurfaceBatch.json",geometry,surfaces);
                    error += " (non-solid sheet " + std::to_string(index) + ", surface "
                        + std::to_string(surfaces[index].surfaceIndex) + "; details: "
                        + failurePath.string() + ")";
                    return false;
                }
                for (auto& part : sheetParts)
                {
                    auto plane = std::find_if(sheetPlanes.begin(), sheetPlanes.end(),
                        [&](const SheetPlane& existing) {
                            const double alignment = existing.normal.x * part.normal.x
                                + existing.normal.y * part.normal.y + existing.normal.z * part.normal.z;
                            if (alignment < 1.0 - sheetLimits.normalTolerance) return false;
                            return std::all_of(part.vertices.begin(), part.vertices.end(),
                                [&](const Surface::Vec3& point) {
                                    const double distance = existing.normal.x * (point.x - existing.origin.x)
                                        + existing.normal.y * (point.y - existing.origin.y)
                                        + existing.normal.z * (point.z - existing.origin.z);
                                    return std::fabs(distance) <= sheetLimits.epsilon;
                                });
                        });
                    if (plane == sheetPlanes.end())
                    {
                        sheetPlanes.push_back({part.normal, part.vertices.front(), {}});
                        plane = std::prev(sheetPlanes.end());
                    }
                    plane->pieces.push_back({std::move(part.vertices), part.materialIndex});
                }
            }
            for (const auto& plane : sheetPlanes)
            {
                // Only remove internal edges between exactly adjacent convex
                // pieces. Disjoint regions, holes and nonplanar folds remain.
                std::vector<Surface::Piece> coalesced;
                if (!Surface::CoalesceCoplanarPieces(plane.pieces, plane.normal, coalesced, error))
                {
                    DumpSurfaceBatch(failurePath.parent_path()/"SurfaceBatch.json",geometry,surfaces);
                    error += " (non-solid surface " + std::to_string(surfaces[group.front()].surfaceIndex)
                        + " coalescing)";
                    return false;
                }
                for (const auto& piece : coalesced)
                {
                    const Surface::Surface sheet{piece.vertices, plane.normal, piece.materialIndex};
                    std::vector<Surface::Surface> finalParts;
                    if (!Surface::PrepareSheetForEditor(sheet, finalParts, error)) return false;
                    for (const auto& part : finalParts)
                        if (!emit(part.vertices, part.normal, surfaces[piece.materialIndex], true))
                        {
                            RecoveredBspGeometry::Face failedFace{part.vertices, part.normal,
                                surfaces[piece.materialIndex].surfaceIndex};
                            DumpSurfaceCase(failurePath, failedFace, {});
                            DumpSurfaceBatch(failurePath.parent_path()/"SurfaceBatch.json",geometry,surfaces);
                            error += " (non-solid sheet emission; details: " + failurePath.string() + ")";
                            return false;
                        }
                }
            }
            endBrush();
        }
        output << "End Map\n";
        Logger::log("MapRecovery: emitted " + std::to_string(geometry.brushes.size())
            + " structural brushes, " + std::to_string(sheetGroups.size())
            + " non-solid sheet brushes and " + std::to_string(polygonCount) + " polygons");
        Logger::log("MapRecovery: retained " + std::to_string(hiddenZoneDividers)
            + " zone-divider brushes, hidden by default; retained ZoneInfo settings");
        text = output.str();
        return true;
    }

    bool g_recoveryLoadActive = false;
    bool g_recoveredMapActive = false;
    void* g_recoveredLevel = nullptr;
    void* g_fallbackBuilderBrushActor = nullptr;
    void* g_fallbackBuilderBrushModel = nullptr;
    void* g_runtimeBuilderSlotActor = nullptr;
    int g_runtimeBuilderSlotRelocatedIndex = -1;
    HWND g_recoveryOwner = nullptr;
    std::filesystem::path g_recoveredPath;
    constexpr size_t kMaxSavePackageDepth = 64;
    uintptr_t g_savePackageReturnAddresses[kMaxSavePackageDepth] = {};
    size_t g_savePackageDepth = 0;
    int g_savePreviousPlatform = 0;
    bool g_savePlatformAdjusted = false;
    void* g_saveDetachedBuilderBrushActor = nullptr;
    void* g_saveDetachedBuilderBrushModel = nullptr;
    bool g_saveRestoredRuntimeBuilderSlot = false;
    std::vector<uint32_t> g_saveSurfaceEditorPolyBits;
    volatile LONG g_actorTickDiagnosticArmed = 0;
    volatile LONG g_actorTickLoopActive = 0;
    void* volatile g_currentTickLevel = nullptr;
    void* volatile g_currentTickActor = nullptr;
    volatile LONG g_currentTickActorIndex = -1;
    char g_actorTickDiagnosticPath[MAX_PATH] = {};
    volatile LONG g_viewportDiagnosticArmed = 0;
    volatile DWORD g_viewportDiagnosticThreadId = 0;
    char g_viewportDiagnosticPath[MAX_PATH] = {};
    bool g_fallbackBuilderObjectsRooted = false;

    bool CopyObjectName(void* object, char* output, size_t outputSize)
    {
        if (!output || outputSize == 0)
            return false;
        output[0] = '\0';
        if (!object)
            return false;

        __try
        {
            void** names = *reinterpret_cast<void***>(kGNamesData);
            const int nameCount = *reinterpret_cast<int*>(kGNamesCount);
            const int nameIndex = *reinterpret_cast<int*>(
                static_cast<char*>(object) + kObjectNameOffset);
            if (!names || nameIndex < 0 || nameIndex >= nameCount
                || !names[nameIndex])
                return false;

            const char* name = static_cast<const char*>(names[nameIndex])
                + kFNameTextOffset;
            if (!*name)
                return false;
            strncpy_s(output, outputSize, name, _TRUNCATE);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            output[0] = '\0';
            return false;
        }
    }

    void* BuilderBrushActor(void* level)
    {
        if (!level)
            return nullptr;

        const int actorCount = *reinterpret_cast<int*>(
            static_cast<char*>(level) + kLevelActorsCountOffset);
        void** actors = *reinterpret_cast<void***>(
            static_cast<char*>(level) + kLevelActorsDataOffset);
        if (!actors || actorCount < 2)
            return nullptr;

        return actors[1];
    }

    void CaptureFallbackBuilderBrushModel(void* editor)
    {
        g_fallbackBuilderBrushActor = nullptr;
        g_fallbackBuilderBrushModel = nullptr;
        if (!editor)
            return;

        void* level = *reinterpret_cast<void**>(
            static_cast<char*>(editor) + kEditorLevelOffset);
        void* actor = BuilderBrushActor(level);
        if (actor)
        {
            g_fallbackBuilderBrushActor = actor;
            g_fallbackBuilderBrushModel = *reinterpret_cast<void**>(
                static_cast<char*>(actor) + kActorBrushOffset);
        }
    }

    void RootFallbackBuilderBrushObjects()
    {
        if (g_fallbackBuilderObjectsRooted
            || !g_fallbackBuilderBrushActor || !g_fallbackBuilderBrushModel)
            return;

        using RootFn = void(__thiscall*)(void*);
        reinterpret_cast<RootFn>(kUObjectAddToRoot)(g_fallbackBuilderBrushActor);
        reinterpret_cast<RootFn>(kUObjectAddToRoot)(g_fallbackBuilderBrushModel);
        g_fallbackBuilderObjectsRooted = true;
    }

    void UnrootFallbackBuilderBrushObjects()
    {
        if (!g_fallbackBuilderObjectsRooted)
            return;

        using UnrootFn = void(__thiscall*)(void*);
        reinterpret_cast<UnrootFn>(kUObjectRemoveFromRoot)(g_fallbackBuilderBrushModel);
        reinterpret_cast<UnrootFn>(kUObjectRemoveFromRoot)(g_fallbackBuilderBrushActor);
        g_fallbackBuilderObjectsRooted = false;
    }

    bool IsExactBrushActor(void* actor)
    {
        return actor && *reinterpret_cast<void**>(
            static_cast<char*>(actor) + kObjectClassOffset)
            == reinterpret_cast<void*>(kABrushClass);
    }

    bool InstallFallbackBuilderBrush(void* level)
    {
        if (!level || !g_fallbackBuilderBrushActor
            || !g_fallbackBuilderBrushModel
            || !IsExactBrushActor(g_fallbackBuilderBrushActor))
            return false;

        void*** actorsDataField = reinterpret_cast<void***>(
            static_cast<char*>(level) + kLevelActorsDataOffset);
        int* actorCountField = reinterpret_cast<int*>(
            static_cast<char*>(level) + kLevelActorsCountOffset);
        void** actors = *actorsDataField;
        const int actorCount = *actorCountField;
        if (!actors || actorCount < 2)
            return false;

        void* displacedActor = actors[1];
        if (displacedActor && displacedActor != g_fallbackBuilderBrushActor
            && !IsExactBrushActor(displacedActor))
        {
            // GetBrush temporarily borrows the editor model while the cooked
            // actor slot is being loaded. It can be a PlayerStart, not a Brush;
            // do not export that temporary attachment as an authored reference.
            void** borrowedModel = reinterpret_cast<void**>(
                static_cast<char*>(displacedActor) + kActorBrushOffset);
            if (*borrowedModel == g_fallbackBuilderBrushModel) *borrowedModel = nullptr;
        }
        g_runtimeBuilderSlotActor = nullptr;
        g_runtimeBuilderSlotRelocatedIndex = -1;
        if (displacedActor != g_fallbackBuilderBrushActor)
        {
            g_runtimeBuilderSlotActor = displacedActor;
            bool displacedAlreadyPresent = false;
            for (int actorIndex = 2; actorIndex < actorCount; ++actorIndex)
            {
                if (actors[actorIndex] == displacedActor)
                {
                    displacedAlreadyPresent = true;
                    break;
                }
            }

            if (displacedActor && !displacedAlreadyPresent)
            {
                int destinationIndex = -1;
                for (int actorIndex = 2; actorIndex < actorCount; ++actorIndex)
                {
                    if (!actors[actorIndex])
                    {
                        destinationIndex = actorIndex;
                        break;
                    }
                }

                if (destinationIndex < 0)
                {
                    using AddFn = int(__thiscall*)(void*, int, int);
                    destinationIndex = reinterpret_cast<AddFn>(kFArrayAdd)(
                        static_cast<char*>(level) + kLevelActorsDataOffset,
                        1, sizeof(void*));
                    actors = *actorsDataField;
                    if (!actors || destinationIndex < 0
                        || destinationIndex >= *actorCountField)
                        return false;
                }
                actors[destinationIndex] = displacedActor;
                g_runtimeBuilderSlotRelocatedIndex = destinationIndex;
            }

            actors = *actorsDataField;
            actors[1] = g_fallbackBuilderBrushActor;
        }

        // The rooted objects survived the old level's garbage collection but
        // still name that level as their Outer. Rehome the editor-only pair to
        // the recovered level before releasing their temporary root entries.
        *reinterpret_cast<void**>(
            static_cast<char*>(g_fallbackBuilderBrushActor)
                + kObjectOuterOffset) = level;
        *reinterpret_cast<void**>(
            static_cast<char*>(g_fallbackBuilderBrushModel)
                + kObjectOuterOffset) = level;
        *reinterpret_cast<void**>(
            static_cast<char*>(g_fallbackBuilderBrushActor)
                + kActorBrushOffset) = g_fallbackBuilderBrushModel;
        return true;
    }

    std::filesystem::path ExecutableDirectory()
    {
        char path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};

        return std::filesystem::path(path).parent_path();
    }

    bool HasSdcExtension(const std::filesystem::path& path)
    {
        return _stricmp(path.extension().string().c_str(), ".sdc") == 0;
    }

    std::string RecoveryAssetName(const std::filesystem::path& destination)
    {
        std::string name = destination.stem().string() + "_Assets";
        for (char& character : name)
            if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_') character = '_';
        if (std::isdigit(static_cast<unsigned char>(name.front()))) name.insert(0, "Recovery_");
        return name;
    }

    void SetCurrentMapFilename(const char* filename)
    {
        // The stock Save As setter also establishes the next regular Save
        // target; raw MAP NEW/MAP LOAD do not update the level window.
        if (void* levelWindow = *reinterpret_cast<void**>(0x1165E80C))
        {
            using SetFilename = void(__thiscall*)(void*, const char*);
            reinterpret_cast<SetFilename>(0x10E05E1C)(levelWindow, filename);
        }
    }

    std::filesystem::path UniqueRecoveryPath(
        const std::filesystem::path& mapsEdDirectory,
        const std::filesystem::path& source)
    {
        const std::string stem = source.stem().string();

        for (unsigned int index = 0; index < 10000; ++index)
        {
            std::ostringstream name;
            name << stem << "_Recovered";
            if (index != 0)
                name << '_' << (index + 1);
            name << ".sdc";

            const std::filesystem::path candidate = mapsEdDirectory / name.str();
            const auto runtimeCandidate = mapsEdDirectory.parent_path() / "Maps" / name.str();
            const auto scratchCandidate = mapsEdDirectory / "Recovery" / candidate.stem();
            const auto assetCandidate = mapsEdDirectory.parent_path() / "StaticMeshes"
                / (RecoveryAssetName(candidate) + ".usx");
            std::error_code existsError;
            if (!std::filesystem::exists(candidate, existsError) && !existsError
                && !std::filesystem::exists(runtimeCandidate, existsError) && !existsError
                && !std::filesystem::exists(assetCandidate, existsError) && !existsError
                && !std::filesystem::exists(scratchCandidate, existsError) && !existsError)
                return candidate;
        }

        return {};
    }

    int* SerializationPlatform()
    {
        void* serializationContext =
            *reinterpret_cast<void**>(kGSerializationContext);
        if (!serializationContext)
            return nullptr;

        return reinterpret_cast<int*>(
            static_cast<char*>(serializationContext)
            + kSerializationPlatformOffset);
    }

    bool DispatchEditorCommand(void* editor, void* output,
                               const std::string& command)
    {
        if (!editor || !output)
            return false;

        // UUnrealEdEngine exposes FExec at +0x28. Its first virtual method is
        // the command dispatcher used by the editor's own menu actions.
        void* execInterface = static_cast<char*>(editor) + 0x28;
        void** vtable = *reinterpret_cast<void***>(execInterface);
        if (!vtable || !vtable[0])
            return false;

        using ExecFn = int(__thiscall*)(void*, const char*, void*);
        return reinterpret_cast<ExecFn>(vtable[0])(
            execInterface, command.c_str(), output) != 0;
    }

    bool ExecuteRecoveryLoad(const std::string& command)
    {
        void* editor = *reinterpret_cast<void**>(kGEditor);
        void* output = *reinterpret_cast<void**>(kGWarn);
        if (!editor || !output)
            return false;

        CaptureFallbackBuilderBrushModel(editor);
        if (!g_fallbackBuilderBrushModel)
            return false;
        RootFallbackBuilderBrushObjects();

        // Compiled maps were written with the PC runtime serializers. The
        // editor mode expects source FPoly/portal fields that the compiler
        // strips, so it quickly loses alignment in UModel::Serialize. Select
        // the runtime layout for this synchronous load, then restore editor
        // mode before returning to the UI.
        int* platform = SerializationPlatform();
        struct RestoreLoadState
        {
            int* platform;
            int previousPlatform;
            ~RestoreLoadState()
            {
                if (platform) *platform = previousPlatform;
                UnrootFallbackBuilderBrushObjects();
            }
        } restoreLoadState{platform, platform ? *platform : 0};
        if (!platform) return false;
        *platform = kPcRuntimePlatform;
        g_recoveryLoadActive = true;
        const bool accepted = DispatchEditorCommand(editor, output, command);

        if (accepted)
        {
            // The cooked package keeps the builder-brush actor but clears its
            // editor-only UModel after MAP LOAD has finished.  Install the
            // blank map's model again after finalisation so later editor
            // operations (visibility testing, rebuild and save) do not trip
            // ULevel::GetBrush()'s Brush != nullptr invariant.
            g_recoveredLevel = *reinterpret_cast<void**>(
                static_cast<char*>(editor) + kEditorLevelOffset);
            g_recoveredMapActive = g_recoveredLevel != nullptr
                && InstallFallbackBuilderBrush(g_recoveredLevel);
        }

        // On rejection keep the extraction guards armed until the caller's
        // failure cleanup disposes of any partially loaded cooked model.
        if (accepted && g_recoveredMapActive) g_recoveryLoadActive = false;

        return accepted && g_recoveredMapActive;
    }

    void ShowError(HWND owner, const std::string& message)
    {
        MessageBoxA(owner, message.c_str(), "Recover Compiled Map",
                    MB_OK | MB_ICONERROR);
    }

    bool ChooseRecoveredSavePath(HWND owner, std::filesystem::path& path)
    {
        char selectedPath[MAX_PATH] = {};
        const std::string suggestedName = path.filename().string();
        strncpy_s(selectedPath, suggestedName.c_str(), _TRUNCATE);

        const std::string initialDirectory = path.parent_path().string();
        OPENFILENAMEA dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = "Map packages (*.sdc)\0*.sdc\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile = selectedPath;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrInitialDir = initialDirectory.c_str();
        dialog.lpstrTitle = "Save recovered map as";
        dialog.lpstrDefExt = "sdc";
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT
                     | OFN_NOCHANGEDIR;

        if (!GetSaveFileNameA(&dialog))
            return false;

        path = selectedPath;
        if (!HasSdcExtension(path))
            path.replace_extension(".sdc");
        return true;
    }

    void SaveRecoveredMap(bool saveAs)
    {
        HWND owner = IsWindow(g_recoveryOwner) ? g_recoveryOwner : GetActiveWindow();
        std::filesystem::path target = g_recoveredPath;
        if (target.empty())
        {
            ShowError(owner, "The recovered map does not have a valid save path.");
            return;
        }

        if (saveAs && !ChooseRecoveredSavePath(owner, target))
            return;

        void* editor = *reinterpret_cast<void**>(kGEditor);
        void* output = *reinterpret_cast<void**>(kGWarn);
        int* platform = SerializationPlatform();
        if (!editor || !output || !platform)
        {
            ShowError(owner, "The editor is not ready to save the recovered map.");
            return;
        }

        void* currentLevel = *reinterpret_cast<void**>(
            static_cast<char*>(editor) + kEditorLevelOffset);
        if (currentLevel != g_recoveredLevel)
        {
            g_recoveredMapActive = false;
            g_recoveredLevel = nullptr;
            g_recoveredPath.clear();
            ShowError(owner, "The recovered map is no longer the active level.");
            return;
        }

        MapRecovery::ResolveBuilderBrushActor(currentLevel);

        // WEditorFrame's stock Save path always runs editor visibility and
        // portal reconstruction first. Cooked maps no longer contain the
        // source polygons that pass requires. Dispatch MAP SAVE directly and
        // retain the runtime serializer layout for the complete operation.
        const int previousPlatform = *platform;
        *platform = kPcRuntimePlatform;
        const bool accepted = DispatchEditorCommand(
            editor, output, "MAP SAVE FILE=\"" + target.string() + "\"");
        *platform = previousPlatform;

        if (!accepted)
        {
            ShowError(owner, "The editor did not accept the recovered-map save command.\n\n"
                             "No source map was overwritten.");
            return;
        }

        g_recoveredPath = target;
        SetWindowTextA(owner,
            ("Reloaded Chaos Theory Editor - [v1.2] - [RECOVERED: "
             + target.stem().string() + "]").c_str());
        MessageBoxA(owner,
            ("Recovered map saved to:\n" + target.string()
             + "\n\nThis working copy still uses the compiled layout. Use Convert Legacy Recovered Map "
               "to create a normal editable source map before opening it with regular Open.").c_str(),
            "Recover Compiled Map", MB_OK | MB_ICONINFORMATION);
    }
}

bool MapRecovery::HandleSaveCommand(UINT commandId)
{
    if (!g_recoveredMapActive
        || (commandId != kFileSaveCommandId
            && commandId != kFileSaveAsCommandId))
        return false;

    // A cancelled Save As is still handled: falling through to the stock
    // command would immediately reopen its incompatible save path.
    SaveRecoveredMap(commandId == kFileSaveAsCommandId);
    return true;
}

void* MapRecovery::ResolveBuilderBrushActor(void* level)
{
    const bool isRecoveredLevel =
        g_recoveredMapActive && level == g_recoveredLevel;
    if (g_recoveryLoadActive || isRecoveredLevel)
    {
        // MAP LOAD destroys the blank level's builder-brush actor, so its
        // pointer is only a transient fallback while loading. The compiled
        // level retains its own actor at Actors[1]; attach only the preserved
        // blank-map UModel to that live actor once it is available.
        void* actor = BuilderBrushActor(level);
        if (!actor && g_recoveryLoadActive)
            actor = g_fallbackBuilderBrushActor;
        if (!actor)
            return nullptr;

        void** fallbackBrush = reinterpret_cast<void**>(
            static_cast<char*>(actor) + kActorBrushOffset);
        if (!*fallbackBrush && g_fallbackBuilderBrushModel)
            *fallbackBrush = g_fallbackBuilderBrushModel;
        return actor;
    }

    void* actor = BuilderBrushActor(level);
    if (!actor)
        return nullptr;
    return actor;
}

bool MapRecovery::IsRecoveredMapActive()
{
    if (!g_recoveredMapActive || !g_recoveredLevel)
        return false;

    void* editor = *reinterpret_cast<void**>(kGEditor);
    void* currentLevel = editor
        ? *reinterpret_cast<void**>(
            static_cast<char*>(editor) + kEditorLevelOffset)
        : nullptr;

    if (currentLevel == g_recoveredLevel)
        return true;

    // A normal New/Open replaced the recovered level. Do not let the guard
    // affect editable source maps after that transition.
    g_recoveredMapActive = false;
    g_recoveredLevel = nullptr;
    g_recoveredPath.clear();
    return false;
}

bool MapRecovery::UsesRecoveredBspLayout()
{
    // The cooked layout is already required inside MAP LOAD, before the new
    // level can be recorded as the active recovered level. In particular,
    // cooked surfaces may retain editor-poly flag bits without containing the
    // FPoly payload those bits select in the editor serializer.
    if (g_recoveryLoadActive)
        return true;

    return IsRecoveredMapActive();
}

bool MapRecovery::ShouldSkipRecoveredEditorPoly()
{
    // Compiled packages can legitimately contain an FPoly payload on flagged
    // surfaces. Let MAP LOAD consume it using the stock runtime branch. The
    // skip is only for later editor operations which serialize temporary
    // cooked-surface copies and cannot safely follow their FPoly pointer.
    if (g_recoveryLoadActive)
        return false;

    return IsRecoveredMapActive();
}

bool MapRecovery::IsRecoveredBspModel(void* model)
{
    if (!model || !IsRecoveredMapActive())
        return false;

    void* recoveredModel = *reinterpret_cast<void**>(
        static_cast<char*>(g_recoveredLevel) + kLevelModelOffset);
    return model == recoveredModel;
}

void MapRecovery::RepairZoneInfoAssignment(void* actor)
{
    if (!g_recoveredMapActive || !g_recoveredLevel || !actor)
        return;

    __try
    {
        void* actorLevel = *reinterpret_cast<void**>(
            static_cast<char*>(actor) + kActorLevelOffset);
        if (actorLevel != g_recoveredLevel)
            return;

        // ALevelInfo derives from AZoneInfo but represents the persistent
        // level rather than an entry in UModel::Zones.
        void* actorClass = *reinterpret_cast<void**>(
            static_cast<char*>(actor) + kObjectClassOffset);
        for (void* currentClass = actorClass; currentClass;
             currentClass = *reinterpret_cast<void**>(
                 static_cast<char*>(currentClass) + kClassSuperOffset))
        {
            if (currentClass == reinterpret_cast<void*>(kALevelInfoClass))
                return;
        }

        const unsigned char zoneNumber = *reinterpret_cast<unsigned char*>(
            static_cast<char*>(actor) + kActorZoneNumberOffset);
        if (zoneNumber == 0)
            return;

        void* model = *reinterpret_cast<void**>(
            static_cast<char*>(g_recoveredLevel) + kLevelModelOffset);
        if (!model)
            return;

        void** zoneActor = reinterpret_cast<void**>(
            static_cast<char*>(model) + kModelZonesOffset
                + static_cast<size_t>(zoneNumber) * kModelZoneStride);
        if (!*zoneActor)
            *zoneActor = actor;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Leave non-recovery corruption to the engine's normal validation.
    }
}

void MapRecovery::ArmActorTickDiagnostic()
{
    char executablePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(
        nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
    if (length != 0 && length < std::size(executablePath))
    {
        char* slash = strrchr(executablePath, '\\');
        if (slash)
            slash[1] = '\0';
        else
            executablePath[0] = '\0';
    }
    else
    {
        executablePath[0] = '\0';
    }

    _snprintf_s(g_actorTickDiagnosticPath,
                std::size(g_actorTickDiagnosticPath), _TRUNCATE,
                "%sRecoveredActorTickCrash.log", executablePath);
    DeleteFileA(g_actorTickDiagnosticPath);
    InterlockedExchangePointer(&g_currentTickActor, nullptr);
    InterlockedExchangePointer(&g_currentTickLevel, nullptr);
    InterlockedExchange(&g_currentTickActorIndex, -1);
    InterlockedExchange(&g_actorTickLoopActive, 0);
    InterlockedExchange(&g_actorTickDiagnosticArmed, 1);
}

void MapRecovery::BeginActorTickLoop(void* level)
{
    InterlockedExchangePointer(&g_currentTickLevel, level);
    InterlockedExchangePointer(&g_currentTickActor, nullptr);
    InterlockedExchange(&g_currentTickActorIndex, -1);
    InterlockedExchange(&g_actorTickLoopActive, 1);
}

void MapRecovery::EndActorTickLoop()
{
    InterlockedExchange(&g_actorTickLoopActive, 0);
    InterlockedExchangePointer(&g_currentTickActor, nullptr);
    InterlockedExchangePointer(&g_currentTickLevel, nullptr);
    InterlockedExchange(&g_currentTickActorIndex, -1);
}

void MapRecovery::RecordActorTick(void* actor, int actorIndex)
{
    if (InterlockedCompareExchange(&g_actorTickDiagnosticArmed, 0, 0) == 0)
        return;

    InterlockedExchange(&g_currentTickActorIndex, actorIndex);
    InterlockedExchangePointer(&g_currentTickActor, actor);
}

void MapRecovery::ClearActorTick()
{
    if (InterlockedCompareExchange(&g_actorTickDiagnosticArmed, 0, 0) == 0)
        return;

    // Retain the array index until the next actor starts. If the surrounding
    // loop's profiling/flag code faults after Tick returns, that index still
    // tells us which slot was being processed.
    InterlockedExchangePointer(&g_currentTickActor, nullptr);
}

bool MapRecovery::LogActorTickException(EXCEPTION_POINTERS* exceptionInfo)
{
    if (!exceptionInfo
        || InterlockedCompareExchange(&g_actorTickDiagnosticArmed, 0, 0) == 0
        || InterlockedCompareExchange(&g_actorTickLoopActive, 0, 0) == 0)
        return false;

    void* actor = InterlockedCompareExchangePointer(
        &g_currentTickActor, nullptr, nullptr);
    void* level = InterlockedCompareExchangePointer(
        &g_currentTickLevel, nullptr, nullptr);

    // Disarm before reading UObject fields: malformed object metadata must not
    // recursively re-enter this first-chance diagnostic.
    InterlockedExchange(&g_actorTickDiagnosticArmed, 0);

    char actorName[128] = "<unreadable>";
    char className[128] = "<unreadable>";
    if (actor)
        CopyObjectName(actor, actorName, std::size(actorName));
    else
        strncpy_s(actorName, "<not reached or already returned>", _TRUNCATE);

    __try
    {
        // This executable's UObject::Class field is at +0x28 (also probed by
        // AnimationBrowser before it walks GObjects).
        if (actor)
        {
            void* actorClass = *reinterpret_cast<void**>(
                static_cast<char*>(actor) + 0x28);
            CopyObjectName(actorClass, className, std::size(className));
        }
        else
        {
            strncpy_s(className, "<not reached or already returned>", _TRUNCATE);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        strncpy_s(className, "<unreadable>", _TRUNCATE);
    }

    char report[1024] = {};
    _snprintf_s(
        report, std::size(report), _TRUNCATE,
        "Recovered actor tick exception\r\n"
        "ExceptionCode=0x%08lX\r\n"
        "ExceptionAddress=%p\r\n"
        "LevelPointer=%p\r\n"
        "ActorIndex=%ld\r\n"
        "ActorPointer=%p\r\n"
        "ActorClass=%s\r\n"
        "ActorName=%s\r\n"
        "EIP=0x%08lX EAX=0x%08lX EBX=0x%08lX ECX=0x%08lX "
        "EDX=0x%08lX ESI=0x%08lX EDI=0x%08lX\r\n",
        exceptionInfo->ExceptionRecord->ExceptionCode,
        exceptionInfo->ExceptionRecord->ExceptionAddress,
        level,
        InterlockedCompareExchange(&g_currentTickActorIndex, -1, -1),
        actor, className, actorName,
        exceptionInfo->ContextRecord->Eip,
        exceptionInfo->ContextRecord->Eax,
        exceptionInfo->ContextRecord->Ebx,
        exceptionInfo->ContextRecord->Ecx,
        exceptionInfo->ContextRecord->Edx,
        exceptionInfo->ContextRecord->Esi,
        exceptionInfo->ContextRecord->Edi);

    HANDLE file = CreateFileA(
        g_actorTickDiagnosticPath, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(file, report, static_cast<DWORD>(strlen(report)),
                  &written, nullptr);
        FlushFileBuffers(file);
        CloseHandle(file);
    }
    return true;
}

void MapRecovery::ArmViewportExceptionDiagnostic()
{
    char executablePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(
        nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
    if (length != 0 && length < std::size(executablePath))
    {
        char* slash = strrchr(executablePath, '\\');
        if (slash)
            slash[1] = '\0';
        else
            executablePath[0] = '\0';
    }
    else
    {
        executablePath[0] = '\0';
    }

    _snprintf_s(g_viewportDiagnosticPath,
                std::size(g_viewportDiagnosticPath), _TRUNCATE,
                "%sRecoveredViewportCrash.log", executablePath);
    DeleteFileA(g_viewportDiagnosticPath);
    InterlockedExchange(
        reinterpret_cast<volatile LONG*>(&g_viewportDiagnosticThreadId),
        static_cast<LONG>(GetCurrentThreadId()));
    InterlockedExchange(&g_viewportDiagnosticArmed, 1);
}

bool MapRecovery::LogViewportException(EXCEPTION_POINTERS* exceptionInfo)
{
    if (!exceptionInfo
        || InterlockedCompareExchange(&g_viewportDiagnosticArmed, 0, 0) == 0
        || GetCurrentThreadId() != InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(&g_viewportDiagnosticThreadId), 0, 0))
        return false;

    const DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION
        && code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED
        && code != EXCEPTION_BREAKPOINT
        && code != EXCEPTION_ILLEGAL_INSTRUCTION
        && code != EXCEPTION_IN_PAGE_ERROR
        && code != EXCEPTION_STACK_OVERFLOW)
        return false;

    // Claim the diagnostic before touching the failing stack so a secondary
    // exception in this best-effort logger cannot recurse.
    if (InterlockedCompareExchange(&g_viewportDiagnosticArmed, 0, 1) != 1)
        return false;

    const CONTEXT* context = exceptionInfo->ContextRecord;
    HMODULE faultModule = nullptr;
    char faultModulePath[MAX_PATH] = "<unmapped>";
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(
                exceptionInfo->ExceptionRecord->ExceptionAddress),
            &faultModule))
    {
        if (!GetModuleFileNameA(faultModule, faultModulePath,
                                static_cast<DWORD>(std::size(faultModulePath))))
            strncpy_s(faultModulePath, "<module path unavailable>", _TRUNCATE);
    }

    void* currentBuilderActor = nullptr;
    void* currentBuilderModel = nullptr;
    __try
    {
        currentBuilderActor = BuilderBrushActor(g_recoveredLevel);
        if (currentBuilderActor)
        {
            currentBuilderModel = *reinterpret_cast<void**>(
                static_cast<char*>(currentBuilderActor) + kActorBrushOffset);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        currentBuilderActor = reinterpret_cast<void*>(static_cast<uintptr_t>(-1));
        currentBuilderModel = reinterpret_cast<void*>(static_cast<uintptr_t>(-1));
    }

    char report[16384] = {};
    size_t used = static_cast<size_t>(_snprintf_s(
        report, std::size(report), _TRUNCATE,
        "Recovered viewport exception\r\n"
        "ExceptionCode=0x%08lX\r\n"
        "ExceptionAddress=%p\r\n"
        "ExceptionModule=%p %s\r\n"
        "ReloadedEditorModule=%p\r\n"
        "RecoveredLevel=%p\r\n"
        "FallbackBuilderActor=%p\r\n"
        "FallbackBuilderModel=%p\r\n"
        "CurrentBuilderActor=%p\r\n"
        "CurrentBuilderModel=%p\r\n"
        "EIP=0x%08lX ESP=0x%08lX EBP=0x%08lX EFLAGS=0x%08lX\r\n"
        "EAX=0x%08lX EBX=0x%08lX ECX=0x%08lX EDX=0x%08lX "
        "ESI=0x%08lX EDI=0x%08lX\r\n"
        "Stack:\r\n",
        code, exceptionInfo->ExceptionRecord->ExceptionAddress,
        faultModule, faultModulePath,
        GetModuleHandleA("Reloaded.Editor.dll"),
        g_recoveredLevel,
        g_fallbackBuilderBrushActor, g_fallbackBuilderBrushModel,
        currentBuilderActor, currentBuilderModel,
        context->Eip, context->Esp, context->Ebp, context->EFlags,
        context->Eax, context->Ebx, context->Ecx, context->Edx,
        context->Esi, context->Edi));

    if (used >= std::size(report))
        used = std::size(report) - 1;

    const DWORD probeAddresses[] = {
        context->Eax, context->Ebx, context->Ecx,
        context->Edx, context->Esi, context->Edi
    };
    const char* probeNames[] = { "EAX", "EBX", "ECX", "EDX", "ESI", "EDI" };
    for (size_t probeIndex = 0; probeIndex < std::size(probeAddresses)
         && used < std::size(report) - 256; ++probeIndex)
    {
        int appended = _snprintf_s(
            report + used, std::size(report) - used, _TRUNCATE,
            "%s memory @ 0x%08lX:", probeNames[probeIndex],
            probeAddresses[probeIndex]);
        if (appended <= 0)
            break;
        used += static_cast<size_t>(appended);

        __try
        {
            const DWORD* values = reinterpret_cast<const DWORD*>(
                static_cast<uintptr_t>(probeAddresses[probeIndex]));
            for (size_t valueIndex = 0; valueIndex < 12; ++valueIndex)
            {
                appended = _snprintf_s(
                    report + used, std::size(report) - used, _TRUNCATE,
                    " %08lX", values[valueIndex]);
                if (appended <= 0)
                    break;
                used += static_cast<size_t>(appended);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            appended = _snprintf_s(
                report + used, std::size(report) - used, _TRUNCATE,
                " <unreadable>");
            if (appended > 0)
                used += static_cast<size_t>(appended);
        }

        if (used < std::size(report) - 3)
        {
            report[used++] = '\r';
            report[used++] = '\n';
            report[used] = '\0';
        }
    }

    __try
    {
        const DWORD* stack = reinterpret_cast<const DWORD*>(context->Esp);
        for (size_t index = 0; index < 128 && used < std::size(report) - 64;
             ++index)
        {
            const int appended = _snprintf_s(
                report + used, std::size(report) - used, _TRUNCATE,
                "[%03zu] 0x%08lX\r\n", index, stack[index]);
            if (appended <= 0)
                break;
            used += static_cast<size_t>(appended);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        const char unreadable[] = "<stack became unreadable>\r\n";
        const size_t available = std::size(report) - used;
        if (available > sizeof(unreadable))
        {
            memcpy(report + used, unreadable, sizeof(unreadable) - 1);
            used += sizeof(unreadable) - 1;
        }
    }

    HANDLE file = CreateFileA(
        g_viewportDiagnosticPath, GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(file, report, static_cast<DWORD>(used), &written, nullptr);
        FlushFileBuffers(file);
        CloseHandle(file);
    }
    return true;
}

void MapRecovery::BeginSavePackage(uintptr_t returnAddress)
{
    // SavePackage is synchronous and is not expected to approach this depth.
    // Keep storage fixed because this hook runs before SavePackage installs
    // its own exception frame.
    if (g_savePackageDepth >= kMaxSavePackageDepth)
        __debugbreak();

    g_savePackageReturnAddresses[g_savePackageDepth++] = returnAddress;

    if (!g_recoveredMapActive || g_savePackageDepth != 1)
        return;

    void* editor = *reinterpret_cast<void**>(kGEditor);
    void* currentLevel = editor
        ? *reinterpret_cast<void**>(
            static_cast<char*>(editor) + kEditorLevelOffset)
        : nullptr;
    if (!currentLevel || currentLevel != g_recoveredLevel)
    {
        // A normal New/Open replaced the recovered level.  Do not let its
        // later saves inherit the cooked serializer layout.
        g_recoveredMapActive = false;
        g_recoveredLevel = nullptr;
        return;
    }

    // Actors[1] is an editor invariant, so recovery temporarily puts the
    // preserved blank-map ABrush there and relocates the cooked actor which
    // originally occupied that slot. The runtime package must contain the
    // original ordering instead. Restore it only for synchronous SavePackage,
    // then put the editor brush back after serialization completes.
    void** actors = *reinterpret_cast<void***>(
        static_cast<char*>(currentLevel) + kLevelActorsDataOffset);
    const int actorCount = *reinterpret_cast<int*>(
        static_cast<char*>(currentLevel) + kLevelActorsCountOffset);
    if (actors && actorCount >= 2
        && actors[1] == g_fallbackBuilderBrushActor)
    {
        actors[1] = g_runtimeBuilderSlotActor;
        if (g_runtimeBuilderSlotRelocatedIndex >= 2
            && g_runtimeBuilderSlotRelocatedIndex < actorCount
            && actors[g_runtimeBuilderSlotRelocatedIndex]
                == g_runtimeBuilderSlotActor)
        {
            actors[g_runtimeBuilderSlotRelocatedIndex] = nullptr;
        }
        g_saveRestoredRuntimeBuilderSlot = true;
    }

    // The temporary builder-brush model owns editor-layout BSP arrays and
    // must not be pulled into a cooked-layout package. It is no longer in the
    // level actor array above, but detach the reference as an additional
    // export-tagging guard.
    void* builderBrush = g_fallbackBuilderBrushActor;
    if (builderBrush)
    {
        void** brush = reinterpret_cast<void**>(
            static_cast<char*>(builderBrush) + kActorBrushOffset);
        if (*brush == g_fallbackBuilderBrushModel)
        {
            g_saveDetachedBuilderBrushActor = builderBrush;
            g_saveDetachedBuilderBrushModel = *brush;
            *brush = nullptr;
        }
    }

    // The cooked surface stride excludes FPoly, but some recovered surfaces
    // retain the two compatibility bits which make the runtime loader expect
    // one. The editor-side serializer hook already skips that missing object;
    // clear the matching flags in the package so the game takes the same
    // cooked branch. Restore them in memory after the synchronous save.
    g_saveSurfaceEditorPolyBits.clear();
    void* recoveredModel = *reinterpret_cast<void**>(
        static_cast<char*>(currentLevel) + kLevelModelOffset);
    const RawArray surfaces = recoveredModel
        ? ReadRawArray(recoveredModel, kModelSurfsOffset)
        : RawArray{};
    if (IsValidArray(surfaces))
    {
        g_saveSurfaceEditorPolyBits.resize(
            static_cast<size_t>(surfaces.count));
        for (int surfaceIndex = 0; surfaceIndex < surfaces.count;
             ++surfaceIndex)
        {
            uint32_t* flags = reinterpret_cast<uint32_t*>(
                const_cast<unsigned char*>(surfaces.data)
                + static_cast<size_t>(surfaceIndex) * kSurfStride
                + kSurfFlagsOffset);
            const uint32_t editorPolyBits = *flags & kSurfEditorPolyFlags;
            g_saveSurfaceEditorPolyBits[static_cast<size_t>(surfaceIndex)]
                = editorPolyBits;
            *flags &= ~kSurfEditorPolyFlags;
        }
    }

    int* platform = SerializationPlatform();
    if (!platform)
        return;

    g_savePreviousPlatform = *platform;
    *platform = kPcRuntimePlatform;
    g_savePlatformAdjusted = true;
}

uintptr_t MapRecovery::EndSavePackage()
{
    if (g_savePackageDepth == 0)
        return 0;

    const uintptr_t returnAddress =
        g_savePackageReturnAddresses[--g_savePackageDepth];

    if (g_savePlatformAdjusted && g_savePackageDepth == 0)
    {
        int* platform = SerializationPlatform();
        if (platform)
            *platform = g_savePreviousPlatform;
        g_savePlatformAdjusted = false;
    }

    if (g_savePackageDepth == 0 && g_saveDetachedBuilderBrushActor)
    {
        void** brush = reinterpret_cast<void**>(
            static_cast<char*>(g_saveDetachedBuilderBrushActor)
            + kActorBrushOffset);
        if (!*brush)
            *brush = g_saveDetachedBuilderBrushModel;
        g_saveDetachedBuilderBrushActor = nullptr;
        g_saveDetachedBuilderBrushModel = nullptr;
    }

    if (g_savePackageDepth == 0 && g_saveRestoredRuntimeBuilderSlot)
    {
        void** actors = *reinterpret_cast<void***>(
            static_cast<char*>(g_recoveredLevel) + kLevelActorsDataOffset);
        const int actorCount = *reinterpret_cast<int*>(
            static_cast<char*>(g_recoveredLevel) + kLevelActorsCountOffset);
        if (actors && actorCount >= 2)
        {
            if (g_runtimeBuilderSlotRelocatedIndex >= 2
                && g_runtimeBuilderSlotRelocatedIndex < actorCount
                && !actors[g_runtimeBuilderSlotRelocatedIndex])
            {
                actors[g_runtimeBuilderSlotRelocatedIndex]
                    = g_runtimeBuilderSlotActor;
            }
            actors[1] = g_fallbackBuilderBrushActor;
        }
        g_saveRestoredRuntimeBuilderSlot = false;
    }

    if (g_savePackageDepth == 0 && !g_saveSurfaceEditorPolyBits.empty()
        && g_recoveredLevel)
    {
        void* recoveredModel = *reinterpret_cast<void**>(
            static_cast<char*>(g_recoveredLevel) + kLevelModelOffset);
        const RawArray surfaces = recoveredModel
            ? ReadRawArray(recoveredModel, kModelSurfsOffset)
            : RawArray{};
        if (IsValidArray(surfaces)
            && surfaces.count
                == static_cast<int>(g_saveSurfaceEditorPolyBits.size()))
        {
            for (int surfaceIndex = 0; surfaceIndex < surfaces.count;
                 ++surfaceIndex)
            {
                uint32_t* flags = reinterpret_cast<uint32_t*>(
                    const_cast<unsigned char*>(surfaces.data)
                    + static_cast<size_t>(surfaceIndex) * kSurfStride
                    + kSurfFlagsOffset);
                *flags |= g_saveSurfaceEditorPolyBits[
                    static_cast<size_t>(surfaceIndex)];
            }
        }
        g_saveSurfaceEditorPolyBits.clear();
    }

    return returnAddress;
}

namespace
{
    void EndRecoveryMode()
    {
        g_recoveryLoadActive = false;
        g_recoveredMapActive = false;
        g_recoveredLevel = nullptr;
        g_recoveredPath.clear();
        g_recoveryOwner = nullptr;
        g_fallbackBuilderBrushActor = nullptr;
        g_fallbackBuilderBrushModel = nullptr;
        g_runtimeBuilderSlotActor = nullptr;
        g_runtimeBuilderSlotRelocatedIndex = -1;
    }

    void* CurrentModel()
    {
        void* editor = *reinterpret_cast<void**>(kGEditor);
        void* level = editor ? *reinterpret_cast<void**>(
            static_cast<char*>(editor) + kEditorLevelOffset) : nullptr;
        return level ? *reinterpret_cast<void**>(static_cast<char*>(level) + kLevelModelOffset) : nullptr;
    }

    bool HasLiveSourceLevelInfo()
    {
        __try
        {
            void* editor = *reinterpret_cast<void**>(kGEditor);
            void* level = editor ? *reinterpret_cast<void**>(
                static_cast<char*>(editor) + kEditorLevelOffset) : nullptr;
            void** actors = level ? *reinterpret_cast<void***>(
                static_cast<char*>(level) + kLevelActorsDataOffset) : nullptr;
            if (!actors || !actors[0]) return false;
            void** vtable = *reinterpret_cast<void***>(actors[0]);
            if (!vtable || !vtable[0]) return false;
            void* type = *reinterpret_cast<void**>(static_cast<char*>(actors[0]) + kObjectClassOffset);
            for (int depth = 0; type && depth < 64; ++depth)
            {
                if (type == reinterpret_cast<void*>(kALevelInfoClass)) return true;
                type = *reinterpret_cast<void**>(static_cast<char*>(type) + kClassSuperOffset);
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool SynchronizeImportedActorPlatforms(void* level, std::string& error)
    {
        if (!level) return false;
        auto* data = static_cast<char*>(level);
        void** actors = *reinterpret_cast<void***>(data + kLevelActorsDataOffset);
        const int count = *reinterpret_cast<int*>(data + kLevelActorsCountOffset);
        if (!actors || count < 2 || count > kMaxCookedArrayElements)
        {
            error = "The source import did not create a valid actor list.";
            return false;
        }
        const std::vector<void*> activeActors(actors, actors + count);
        // MAP IMPORT replaces the active Actors array but leaves the PC/Xbox
        // caches on the previous map. Normal Save switches platforms through
        // these arrays. Match native 0x10EE1290's m_Platform partition rules,
        // using the engine's allocator/AddUnique and retaining array ownership.
        // 0 = common, 1 = PC, 2 = Xbox. Validate before replacing either cache.
        for (int index = 0; index < count; ++index)
            if (activeActors[index])
            {
                const int platform = *reinterpret_cast<unsigned char*>(static_cast<char*>(activeActors[index]) + 0x2D0);
                if (platform < 0 || platform > 2)
                {
                    error = "An imported actor has an unsupported platform value: " + std::to_string(platform);
                    return false;
                }
            }
        *reinterpret_cast<int*>(data + 0x40) = 0;
        *reinterpret_cast<int*>(data + 0x50) = 0;
        using AddUnique = int(__thiscall*)(void*, void**);
        for (int index = 0; index < count; ++index)
        {
            void* actor = activeActors[index];
            if (!actor) continue;
            const int platform = *reinterpret_cast<unsigned char*>(static_cast<char*>(actor) + 0x2D0);
            if (platform == 0 || platform == 1)
                reinterpret_cast<AddUnique>(0x10E025CD)(data + 0x3C, &actor);
            if (platform == 0 || platform == 2)
                reinterpret_cast<AddUnique>(0x10E025CD)(data + 0x4C, &actor);
        }
        return true;
    }

    struct ExternalAssetLoad
    {
        std::string path;
        void* type = nullptr;
    };

    bool CollectDeletedActorPaths(void* level, std::vector<std::string>& paths, std::string& error)
    {
        paths.clear();
        if (!level) return false;
        std::unordered_set<void*> listedActors;
        void* levelInfo = nullptr;
        for (size_t offset : {size_t(0x2C), size_t(0x3C), size_t(0x4C)})
        {
            const RawArray actors = ReadRawArray(level, offset);
            if (actors.count < 0 || actors.count > kMaxCookedArrayElements
                || (actors.count && !actors.data))
            {
                error = "The map has an invalid platform actor list.";
                return false;
            }
            void* const* data = reinterpret_cast<void* const*>(actors.data);
            if (offset == 0x2C && actors.count) levelInfo = data[0];
            for (int index = 0; index < actors.count; ++index)
                if (data[index]) listedActors.insert(data[index]);
        }
        void** objects = *reinterpret_cast<void***>(0x11697B70);
        const int count = *reinterpret_cast<int*>(0x11697B74);
        if (!objects || count < 0 || count > 10000000 || !levelInfo)
        {
            error = "The editor cannot validate deleted actor references.";
            return false;
        }
        std::unordered_map<void*, bool> actorClasses;
        for (int index = 0; index < count; ++index)
        {
            void* object = objects[index];
            if (!object || listedActors.count(object)) continue;
            void* type = *reinterpret_cast<void**>(static_cast<char*>(object) + kObjectClassOffset);
            auto found = actorClasses.find(type);
            if (found == actorClasses.end())
            {
                bool actor = false;
                void* parent = type;
                for (int depth = 0; parent && depth < 64; ++depth)
                {
                    char name[128]{};
                    if (CopyObjectName(parent, name, sizeof(name)) && _stricmp(name, "Actor") == 0)
                    {
                        actor = true;
                        break;
                    }
                    parent = *reinterpret_cast<void**>(static_cast<char*>(parent) + kClassSuperOffset);
                }
                found = actorClasses.emplace(type, actor).first;
            }
            if (!found->second) continue;
            const auto* actor = static_cast<const char*>(object);
            // Reflected AActor::bDeleteMe is bit 0x8000 at +0x2E8 in this
            // editor. Require both deletion and absence from every platform
            // list; a merely missing export must never be assumed deleted.
            if (!(*reinterpret_cast<const uint32_t*>(actor + 0x2E8) & 0x8000u)
                || *reinterpret_cast<void* const*>(actor + 0x1A0) != levelInfo) continue;
            char path[512]{};
            if (FormatExternalObjectPath(object, path, sizeof(path), "MyLevel")
                && _strnicmp(path, "MyLevel.", 8) == 0)
                paths.emplace_back(path);
        }
        return true;
    }

    void* FindEmbeddedAssetClass(const std::string& externalPath, const std::string& alias)
    {
        void** objects = *reinterpret_cast<void***>(0x11697B70);
        const int count = *reinterpret_cast<int*>(0x11697B74);
        if (!objects || count < 0 || count > 10000000) return nullptr;
        for (int index = 0; index < count; ++index)
        {
            char path[512]{};
            if (objects[index] && FormatExternalObjectPath(objects[index], path, sizeof(path), alias.c_str())
                && _stricmp(path, externalPath.c_str()) == 0)
                return *reinterpret_cast<void**>(static_cast<char*>(objects[index]) + kObjectClassOffset);
        }
        return nullptr;
    }

    struct StripDoorStructure
    {
        std::vector<unsigned char> points, springs, settings;
    };

    // ESBStripDoor is a procedural, level-owned simulation. Its native
    // serializer (110D7800) stores points at 70 (stride 48) and springs at 7C
    // (stride C). Compare topology, rest lengths, pins and physical settings;
    // current/previous positions of free points, forces and wind timers are
    // simulation state, not the authored shape. Never approve an arbitrary
    // custom soft body simply because the native importer returned an object.
    bool CaptureStripDoorStructure(void* level, const std::string& actorName,
                                   StripDoorStructure& structure, std::string& error)
    {
        structure = {};
        const auto actors = ReadRawArray(level, kLevelActorsDataOffset);
        const unsigned char* actor = nullptr;
        if (!IsValidArray(actors)) return false;
        for (int i = 0; i < actors.count; ++i)
        {
            auto candidate = ReadRaw<unsigned char*>(actors.data, static_cast<size_t>(i) * sizeof(void*));
            char path[512]{};
            if (candidate && FormatExternalObjectPath(candidate, path, sizeof(path), "MyLevel")
                && _stricmp(path, ("MyLevel." + actorName).c_str()) == 0) { actor = candidate; break; }
        }
        auto fail = [&](const std::string& detail) {
            error = "The strip-door simulation on " + actorName
                + " cannot be recovered: " + detail + ". Recovery cannot discard that data.";
            return false;
        };
        if (!actor) return fail("actor missing");
        char typePath[512]{};
        if (!FormatExternalObjectPath(ReadRaw<void*>(actor, kObjectClassOffset), typePath, sizeof(typePath))
            || _stricmp(typePath, "SoftBody.ESBStripDoorActor") != 0) return fail("unexpected actor class " + std::string(typePath));
        const auto* body = ReadRaw<unsigned char*>(actor, 0x2F8);
        if (!body) return fail("simulation missing");
        if (ReadRaw<void*>(body, kObjectOuterOffset) != level || ReadRaw<void*>(body, 0x54) != actor)
            return fail("simulation ownership differs from its actor");
        if (!FormatExternalObjectPath(ReadRaw<void*>(body, kObjectClassOffset), typePath, sizeof(typePath))
            || _stricmp(typePath, "SoftBody.ESBStripDoor") != 0) return fail("unexpected simulation class " + std::string(typePath));
        const auto points = ReadRawArray(body, 0x70), springs = ReadRawArray(body, 0x7C);
        if (!IsValidArray(points) || !IsValidArray(springs) || points.count < 1
            || points.count > 100000 || springs.count < 1 || springs.count > 400000) return fail("invalid point/spring counts");
        // Additional native constraints/attachments need their own lossless
        // recovery support. An unrecognized nonempty array must stop recovery.
        for (const size_t offset : {0x88u, 0x94u, 0xA0u, 0xACu})
            if (ReadRawArray(body, offset).count != 0) return fail("additional native constraints or attachments");
        for (int i = 0; i < points.count; ++i)
        {
            const auto* point = points.data + static_cast<size_t>(i) * 0x48;
            const auto fixed = ReadRaw<uint32_t>(point, 0);
            if (fixed > 1 || !IsFinite(ReadRaw<Vec3>(point, 4))) return fail("invalid simulation point");
            structure.points.insert(structure.points.end(), point, point + 4);
            if (fixed) structure.points.insert(structure.points.end(), point + 4, point + 16);
            structure.points.insert(structure.points.end(), point + 0x30, point + 0x40);
            structure.points.insert(structure.points.end(), point + 0x44, point + 0x48);
        }
        for (int i = 0; i < springs.count; ++i)
        {
            const auto* spring = springs.data + static_cast<size_t>(i) * 12;
            const int first = ReadRaw<int>(spring, 0), second = ReadRaw<int>(spring, 4);
            const auto length = ReadRaw<float>(spring, 8);
            if (first < 0 || first >= points.count || second < 0 || second >= points.count
                || !std::isfinite(length) || length < 0) return fail("invalid spring");
            structure.springs.insert(structure.springs.end(), spring, spring + 12);
        }
        structure.settings.insert(structure.settings.end(), body + 0xB8, body + 0x114);
        structure.settings.insert(structure.settings.end(), body + 0x11C, body + 0x15C);
        return true;
    }

    bool ReadTextFile(const std::filesystem::path& path, std::string& text, std::string& error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            error = "The editor did not create its map export: " + path.string();
            return false;
        }
        text.assign(std::istreambuf_iterator<char>(input), {});
        if (input.bad() || text.empty())
        {
            error = "The map export is empty or unreadable: " + path.string();
            return false;
        }
        return true;
    }

    bool ExportMapText(void* editor, void* output, const std::filesystem::path& path,
                       std::string& text, std::string& error)
    {
        // Native UStrProperty::ExportText formats into 1024 bytes. Some maps
        // repeat every actor's group in LevelInfo.VisibleGroups, overflowing
        // that buffer. Its comma-separated membership set needs each group
        // only once. Export that equivalent set and restore the live FString.
        struct NativeString { char* data; int count; int capacity; };
        struct RestoreString
        {
            NativeString* field = nullptr;
            NativeString original{};
            std::vector<char> replacement;
            ~RestoreString() { if (field) *field = original; }
        } restore;
        void* level = *reinterpret_cast<void**>(static_cast<char*>(editor) + kEditorLevelOffset);
        void** actorData = level ? *reinterpret_cast<void***>(
            static_cast<char*>(level) + kLevelActorsDataOffset) : nullptr;
        if (actorData && actorData[0] && HasLiveSourceLevelInfo())
        {
            auto* groups = reinterpret_cast<NativeString*>(static_cast<char*>(actorData[0]) + 0x41C);
            if (groups->count > 0)
            {
                if (!groups->data || groups->count > 1000000 || groups->capacity < groups->count
                    || groups->data[groups->count - 1] != '\0')
                {
                    error = "The map has an invalid visible-group list.";
                    return false;
                }
                const std::string original(groups->data, groups->count - 1);
                std::unordered_set<std::string> seen;
                std::string unique;
                bool appended = false;
                size_t start = 0;
                do
                {
                    const size_t end = original.find(',', start);
                    const std::string group = original.substr(start, end - start);
                    if (seen.insert(group).second)
                    {
                        if (appended) unique += ',';
                        unique += group;
                        appended = true;
                    }
                    if (end == std::string::npos) break;
                    start = end + 1;
                } while (start <= original.size());
                if (unique.size() + 3 > 1024)
                {
                    error = "The map has too many distinct visible groups for the native text exporter.";
                    return false;
                }
                if (unique != original)
                {
                    restore.replacement.assign(unique.begin(), unique.end());
                    restore.replacement.push_back('\0');
                    restore.field = groups;
                    restore.original = *groups;
                    *groups = {restore.replacement.data(), static_cast<int>(restore.replacement.size()),
                               static_cast<int>(restore.replacement.size())};
                }
            }
        }
        const std::string command = "MAP EXPORT FILE=\"" + path.string() + "\"";
        Logger::log("MapRecovery: " + command);
        if (!DispatchEditorCommand(editor, output, command))
        {
            error = "The editor rejected: " + command;
            return false;
        }
        return ReadTextFile(path, text, error);
    }

    bool WriteTextFile(const std::filesystem::path& path, const std::string& text, std::string& error)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        if (!output)
        {
            error = "Could not write recovery data: " + path.string();
            return false;
        }
        return true;
    }

    bool IsOutside(void* model, const Vec3& point, bool& outside)
    {
        if (!model) return false;
        const RawArray nodes = ReadRawArray(model, kModelNodesOffset);
        outside = *reinterpret_cast<int*>(static_cast<char*>(model) + kModelRootOutsideOffset) != 0;
        if (!nodes.count) return true;
        int index = 0;
        for (int visited = 0; visited <= nodes.count; ++visited)
        {
            if (index == -1) return true;
            if (index < 0 || index >= nodes.count || !nodes.data) return false;
            const auto* node = nodes.data + static_cast<size_t>(index) * kNodeStride;
            const bool front = Dot(ReadRaw<Vec3>(node, 0), point) >= ReadRaw<float>(node, 12);
            const bool csg = ReadRaw<unsigned char>(node, kNodeVertexCountOffset) != 0
                && (ReadRaw<unsigned char>(node, kNodeFlagsOffset) & 0x21) == 0;
            outside = front ? outside || csg : outside && !csg;
            index = ReadRaw<int>(node, front ? kNodeFrontOffset : kNodeBackOffset);
        }
        return false;
    }

    struct SpaceProbe { Vec3 point; bool outside; };

    bool ValidateCollisionBounds(void* model, std::string& error)
    {
        if (!model) { error = "The rebuilt map has no collision model."; return false; }
        const RawArray nodes = ReadRawArray(model, kModelNodesOffset);
        const RawArray hulls = ReadRawArray(model, 0xB0);
        if (!IsValidArray(nodes) || !IsValidArray(hulls))
        {
            error = "The rebuilt map has an invalid collision-bound table.";
            return false;
        }
        for (int i=0; i<nodes.count; ++i)
        {
            const auto start=ReadRaw<uint16_t>(nodes.data+static_cast<size_t>(i)*kNodeStride,0x54);
            if (start==0xFFFF) continue;
            // Both the stock editor (11193198) and PC game (10A41194)
            // sign-extend this field. Extending only the editor would produce
            // a map which still crashes in the game. Stop before path builds.
            if (start>=0x8000 || start>=hulls.count)
            {
                error = "The rebuilt map exceeds the PC engine's collision-bound index limit. "
                        "Recovery stopped before navigation building or saving; the reconstructed T3D is retained. "
                        "This map needs further collision-data support.";
                return false;
            }
        }
        return true;
    }

    bool CaptureSpaceProbes(const std::vector<RecoveredFace>& faces,
                             std::vector<SpaceProbe>& probes, std::string& error)
    {
        void* model = CurrentModel();
        for (const auto& face : faces)
        {
            if (!face.structural) continue;
            Vec3 centre{};
            for (const auto& point : face.vertices) centre = Add(centre, point);
            centre = Scale(centre, 1.0f / static_cast<float>(face.vertices.size()));
            for (float side : {-0.5f, 0.5f})
            {
                SpaceProbe probe{Add(centre, Scale(face.normal, side)), false};
                if (!IsOutside(model, probe.point, probe.outside))
                {
                    error = "The original BSP tree failed a solid/empty space probe.";
                    return false;
                }
                probes.push_back(probe);
            }
        }
        return true;
    }

    bool VerifySpaceProbes(const std::vector<SpaceProbe>& probes, std::string& error)
    {
        void* model = CurrentModel();
        size_t different = 0;
        for (const auto& probe : probes)
        {
            bool outside = false;
            const bool valid = IsOutside(model, probe.point, outside);
            if (!valid || outside != probe.outside)
            {
                if (different < 16)
                {
                    std::ostringstream detail;
                    detail << "MapRecovery: space verification mismatch at ";
                    WriteVector(detail, probe.point);
                    detail << "; expected " << (probe.outside ? "empty" : "solid")
                           << ", actual " << (valid ? (outside ? "empty" : "solid") : "invalid BSP");
                    Logger::log(detail.str());
                }
                ++different;
            }
        }
        if (different)
        {
            error = "The normal BSP rebuild changed " + std::to_string(different) + " of "
                + std::to_string(probes.size()) + " solid/empty space samples. "
                  "The reconstructed source T3D has been kept for inspection.";
            return false;
        }
        return true;
    }

    void ChooseSourceRecovery(HWND owner, bool legacy)
    {
        const auto packages = ExecutableDirectory().parent_path() / "Packages";
        const auto initial = packages / (legacy ? "MapsEd" : "Maps");
        char selected[MAX_PATH]{};
        const auto initialString = initial.string();
        OPENFILENAMEA dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = "Map packages (*.sdc)\0*.sdc\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile = selected;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrInitialDir = initialString.c_str();
        dialog.lpstrTitle = legacy ? "Convert a legacy recovered map to an editable source map"
                                  : "Recover a compiled map as an editable source map";
        dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameA(&dialog)) return;
        const std::filesystem::path source(selected);
        const auto destination = UniqueRecoveryPath(packages / "MapsEd", source);
        if (destination.empty())
        {
            ShowError(owner, "Could not choose a new recovery filename.");
            return;
        }
        const std::string message =
            "Create a normal editable source map from:\n\n" + source.string()
            + "\n\nOutput:\n" + destination.string()
            + "\n\nRecovery reconstructs new brushes, restores common and PC-only objects and level settings, "
              "and rebuilds geometry and lighting. It replaces the current map; save your work first. "
              "Large maps can take several minutes.\n\nContinue?";
        if (MessageBoxA(owner, message.c_str(), "Recover Compiled Map", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
            return;
        const HCURSOR previous = SetCursor(LoadCursor(nullptr, IDC_WAIT));
        std::string error;
        const bool success = MapRecovery::RecoverToSource(source, destination, error);
        SetCursor(previous);
        if (!success)
        {
            ShowError(owner, error);
            MapRecovery::ArmViewportExceptionDiagnostic();
            return;
        }
        SetWindowTextA(owner, ("Reloaded Chaos Theory Editor - [v1.2] - [" + destination.stem().string() + "]").c_str());
        std::string completion = "Created and verified a normal editable map:\n\n" + destination.string()
            + "\n\nUse regular File > Open for this map. Its reconstructed brushes can be edited and rebuilt. "
              "Test gameplay after making your changes.";
        const auto assetPath = packages / "StaticMeshes" / (RecoveryAssetName(destination) + ".usx");
        std::error_code assetError;
        if (std::filesystem::exists(assetPath, assetError))
            completion += "\n\nInclude this asset package when sharing the playable map:\n" + assetPath.string();
        completion += "\n\nRecovery details:\n"
            + (destination.parent_path() / "Recovery" / destination.stem() / "Recovery.txt").string();
        MessageBoxA(owner, completion.c_str(), "Map Recovery Complete", MB_OK | MB_ICONINFORMATION);
    }
}

bool MapRecovery::RecoverToSource(const std::filesystem::path& source,
                                  const std::filesystem::path& destination,
                                  std::string& error)
{
    error.clear();
    try
    {
        if (!HasSdcExtension(source) || !HasSdcExtension(destination)
            || !std::filesystem::is_regular_file(source))
        {
            error = "Select an existing compiled .sdc map and a new .sdc output filename.";
            return false;
        }
        // The native normal save also writes the runtime Maps copy.
        const auto runtime = destination.parent_path().parent_path() / "Maps" / destination.filename();
        if (std::filesystem::exists(destination) || std::filesystem::exists(runtime))
        {
            error = "The destination source or playable map already exists. Choose a new map name.";
            return false;
        }
        const auto packages = ExecutableDirectory().parent_path() / "Packages";
        std::filesystem::create_directories(packages / "MapsEd");
        if (!std::filesystem::equivalent(destination.parent_path(), packages / "MapsEd"))
        {
            error = "Save the recovered source in this editor's Packages/MapsEd directory.";
            return false;
        }
        const std::string assetPackageName = RecoveryAssetName(destination);
        const auto assetPackagePath = packages / "StaticMeshes" / (assetPackageName + ".usx");
        if (std::filesystem::exists(assetPackagePath))
        {
            error = "The recovery asset package already exists. Choose a new map name.";
            return false;
        }
        void* editor = *reinterpret_cast<void**>(kGEditor);
        void* output = *reinterpret_cast<void**>(kGWarn);
        if (!editor || !output)
        {
            error = "The editor is not ready to recover a map.";
            return false;
        }
        struct ResetCookedWorkingMap
        {
            void* editor;
            void* output;
            std::string& error;
            ~ResetCookedWorkingMap()
            {
                if (!g_recoveredMapActive && !g_recoveryLoadActive) return;
                // Failed extraction must not leave cooked guards attached to
                // the next ordinary New/Open: the native editor reuses ULevel.
                // Dispose of the cooked model before clearing its guards.
                try
                {
                    if (DispatchEditorCommand(editor, output, "MAP NEW"))
                    {
                        EndRecoveryMode();
                        SetCurrentMapFilename("");
                    }
                    else error += " The editor could not reset the compiled working map; restart it before opening another map.";
                }
                catch (...) {}
            }
        } resetCookedWorkingMap{editor, output, error};
        const auto scratch = destination.parent_path() / "Recovery" / destination.stem();
        std::filesystem::create_directories(scratch);
        const auto staged = scratch / "CompiledInput.sdc";
        const auto rawAssets = scratch / (assetPackageName + ".usx");
        if (std::filesystem::exists(staged))
        {
            error = "A previous recovery attempt already uses " + scratch.string() + ". Choose a new output name.";
            return false;
        }
        std::filesystem::copy_file(source, staged);
        const auto blankPath = scratch / "Blank.t3d";
        const auto actorsPath = scratch / "Actors.t3d";
        const auto geometryPath = scratch / "Geometry.t3d";
        const auto sourcePath = scratch / "Source.t3d";
        auto exec = [&](const std::string& command) {
            Logger::log("MapRecovery: " + command);
            if (!DispatchEditorCommand(editor, output, command))
            {
                error = "The editor rejected: " + command;
                return false;
            }
            return true;
        };
        auto exportMap = [&](const std::filesystem::path& path, std::string& text) {
            return ExportMapText(editor, output, path, text, error);
        };

        // Keep a known-good source LevelInfo and builder before loading cooked
        // objects. All exports are completed before the final source import.
        if (!exec("MAP NEW")) return false;
        // Any later failure leaves an unsaved working map. Do not let Ctrl+S
        // overwrite the map that was open before conversion started.
        SetCurrentMapFilename("");
        EndRecoveryMode();
        std::string blankText;
        if (!exportMap(blankPath, blankText)) return false;
        if (!ExecuteRecoveryLoad("MAP LOAD FILE=\"" + staged.string() + "\""))
        {
            error = "The compiled map could not be loaded. The input copy is at " + staged.string();
            return false;
        }
        g_recoveredPath = staged;
        g_recoveryOwner = GetActiveWindow();
        std::vector<RecoveredFace> faces;
        size_t skipped = 0;
        Logger::log("MapRecovery: extracting cooked surfaces");
        if (!ExtractRecoveredFaces(g_recoveredLevel, faces, skipped, error, assetPackageName.c_str())) return false;
        if (skipped)
        {
            error = "The compiled BSP contains " + std::to_string(skipped)
                + " invalid polygons. Source recovery cannot safely discard them.";
            return false;
        }
        // Validate transferable objects before the more expensive brush and
        // material reconstruction. Keep this export even if geometry fails.
        std::string actorText;
        if (!exportMap(actorsPath, actorText)) return false;
        std::vector<std::string> deletedActorPaths;
        if (!CollectDeletedActorPaths(g_recoveredLevel, deletedActorPaths, error)) return false;
        // ExecuteRecoveryLoad used the PC runtime serializer. Its active
        // actor list is stronger evidence than legacy editor platform tags.
        std::vector<std::string> pcActorPaths;
        const RawArray pcActors = ReadRawArray(g_recoveredLevel, kLevelActorsDataOffset);
        if (!IsValidArray(pcActors))
        {
            error = "The compiled PC map has an invalid actor list.";
            return false;
        }
        for (int index = 0; index < pcActors.count; ++index)
        {
            auto* actor = reinterpret_cast<unsigned char* const*>(pcActors.data)[index];
            if (!actor || (ReadRaw<uint32_t>(actor, 0x2E8) & 0x8000u)) continue;
            char name[256]{};
            if (!CopyObjectName(actor, name, sizeof(name)))
            {
                error = "An active PC actor has an invalid name.";
                return false;
            }
            pcActorPaths.push_back(std::string("MyLevel.") + name);
        }
        RecoveredActorImport::PreparedMap actors;
        Logger::log("MapRecovery: preparing source actors and level settings");
        if (!RecoveredActorImport::Prepare(actorText, actors, error, assetPackageName, deletedActorPaths, pcActorPaths)) return false;
        std::vector<StripDoorStructure> stripDoors;
        for (const auto& name : actors.regeneratedStripDoors)
        {
            stripDoors.emplace_back();
            if (!CaptureStripDoorStructure(g_recoveredLevel, name, stripDoors.back(), error)) return false;
        }
        const bool rootOutside = *reinterpret_cast<int*>(static_cast<char*>(CurrentModel()) + kModelRootOutsideOffset) != 0;
        RecoveredBspGeometry::Result geometry;
        RecoveredPolygonImport::VertexPool sourcePoints;
        Logger::log("MapRecovery: reconstructing structural volumes");
        if (!ReconstructSourceBrushes(g_recoveredLevel, geometry, sourcePoints, error)) return false;
        Logger::log("MapRecovery: reconstructed " + std::to_string(geometry.brushes.size()) + " closed brushes");
        std::string geometryText;
        Logger::log("MapRecovery: restoring surface materials and texture coordinates");
        if (!WriteSourceBrushes(geometry, faces, sourcePoints, geometryText, error, scratch / "SurfaceFailure.json")
            || !WriteTextFile(geometryPath, geometryText, error)) return false;
        std::vector<SpaceProbe> probes;
        if (!CaptureSpaceProbes(faces, probes, error)) return false;
        std::vector<ExternalAssetLoad> assets;
        std::unordered_set<std::string> assetPaths;
        auto addAsset = [&](const std::string& path, void* type) {
            std::string key = path;
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (assetPaths.insert(key).second) assets.push_back({path, type});
        };
        for (const auto& asset : actors.externalizedAssets)
        {
            void* type = FindEmbeddedAssetClass(asset.externalPath, assetPackageName);
            if (!type)
            {
                error = "An embedded asset could not be resolved in the original map: " + asset.originalPath;
                return false;
            }
            addAsset(asset.externalPath, type);
        }
        for (const auto& face : faces)
            if (face.embeddedMaterialClass) addAsset(face.materialPath, face.embeddedMaterialClass);
        if (!assets.empty())
        {
            Logger::log("MapRecovery: preserving embedded assets in " + assetPackagePath.string());
            std::filesystem::create_directories(assetPackagePath.parent_path());
            if (!RecoveredAssetPackage::Write(staged, rawAssets, error)) return false;
        }
        std::string completeText;
        if (!RecoveredActorImport::ComposeSourceMap(actors, blankText, geometryText, completeText, error)
            || !WriteTextFile(sourcePath, completeText, error)) return false;
        // The native editor is 32-bit. Release interchange copies before its
        // importer and BSP builder allocate their own complete map models.
        std::string{}.swap(blankText);
        std::string{}.swap(actorText);
        std::string{}.swap(geometryText);
        std::string{}.swap(completeText);
        const size_t structuralBrushCount = geometry.brushes.size();
        std::vector<RecoveredBspGeometry::Brush>{}.swap(geometry.brushes);
        std::vector<RecoveredFace>{}.swap(faces);

        auto verifyActors = [&](const char* stage) {
            std::string actual;
            const auto exported = scratch / (std::string(stage) + ".t3d");
            if (!exportMap(exported, actual)
                || !RecoveredActorImport::VerifySourceMap(actors, actual, error))
            {
                error = std::string(stage) + " actor verification failed: " + error;
                return false;
            }
            void* level = *reinterpret_cast<void**>(static_cast<char*>(editor) + kEditorLevelOffset);
            for (size_t i = 0; i < stripDoors.size(); ++i)
            {
                StripDoorStructure actualStructure;
                if (!CaptureStripDoorStructure(level, actors.regeneratedStripDoors[i], actualStructure, error)) return false;
                const auto& expected = stripDoors[i];
                if (expected.points != actualStructure.points || expected.springs != actualStructure.springs
                    || expected.settings != actualStructure.settings)
                {
                    error = std::string(stage) + " changed the strip-door topology, anchors or physical settings on "
                        + actors.regeneratedStripDoors[i] + ". Recovery stopped to preserve the original data.";
                    return false;
                }
            }
            return true;
        };

        if (!exec("MAP NEW")) return false;
        EndRecoveryMode();
        // Load only referenced asset exports from the preserved package. OBJ
        // LOAD loads every export, including the old compiled level itself.
        using LoadObject = void*(__cdecl*)(void*, void*, const char*, const char*, unsigned int, void*);
        for (const auto& asset : assets)
        {
            Logger::log("MapRecovery: loading external asset " + asset.path);
            const auto rawAssetFilename = rawAssets.string();
            void* object = reinterpret_cast<LoadObject>(0x10FB19C0)(
                asset.type, nullptr, asset.path.c_str(), rawAssetFilename.c_str(), 2, nullptr);
            if (!object)
            {
                error = "The preserved asset cannot load in normal editor mode: " + asset.path;
                return false;
            }
            // Selected assets become ordinary cross-package dependencies.
            // RF_Standalone roots them for OBJ SAVEPACKAGE; RF_Public allows
            // the source map to reference exports that were originally private
            // inside MyLevel (notably ConvexVolume antiportal data).
            *reinterpret_cast<uint32_t*>(static_cast<char*>(object) + 0x1C) |= 0x00080004u;
        }
        if (!assets.empty())
        {
            // Save the selectively loaded assets into a clean package. The
            // scratch input also contains the old cooked level; whole-package
            // browser loads must never encounter that level in the dependency.
            const auto relativeAssetPath = std::filesystem::relative(assetPackagePath, ExecutableDirectory()).string();
            if (relativeAssetPath.size() >= 79)
            {
                error = "The recovery asset filename is too long for the editor. Choose a shorter map name.";
                return false;
            }
            if (!exec("OBJ SAVEPACKAGE PACKAGE=\"" + assetPackageName + "\" FILE=\"" + relativeAssetPath + "\"")
                || !std::filesystem::is_regular_file(assetPackagePath))
            {
                error = "The editor could not save the recovered asset package.";
                return false;
            }
            std::ifstream savedAssets(assetPackagePath, std::ios::binary);
            uint32_t packageMagic = 0;
            savedAssets.read(reinterpret_cast<char*>(&packageMagic), sizeof(packageMagic));
            if (!savedAssets || packageMagic != 0x9E2A83C1u
                || std::filesystem::file_size(assetPackagePath) < 64)
            {
                error = "The editor did not write a valid recovered asset package: " + assetPackagePath.string();
                return false;
            }
        }
        if (!exec("MAP IMPORT FILE=\"" + sourcePath.string() + "\"")) return false;
        // Match File > Import's native level finalizer. The separate platform
        // synchronization below is also required before normal Save.
        void* importedLevel = *reinterpret_cast<void**>(
            static_cast<char*>(editor) + kEditorLevelOffset);
        void** editorVtable = *reinterpret_cast<void***>(editor);
        using FinalizeImport = void(__thiscall*)(void*, void*);
        reinterpret_cast<FinalizeImport>(editorVtable[0xE0 / sizeof(void*)])(editor, importedLevel);
        if (!SynchronizeImportedActorPlatforms(importedLevel, error)) return false;
        if (!HasLiveSourceLevelInfo())
        {
            error = "The normal map importer did not retain a live LevelInfo object.";
            return false;
        }
        if (!CurrentModel())
        {
            error = "The normal source import did not create a level model.";
            return false;
        }
        *reinterpret_cast<int*>(static_cast<char*>(CurrentModel()) + kModelRootOutsideOffset) = rootOutside ? 1 : 0;
        if (!verifyActors("Imported")) return false;
        // MAP IMPORT creates a level without the collision hash initialized
        // by ordinary level loading. LIGHT APPLY temporarily inserts actors
        // into that hash and assumes it exists. Use the same native setup as
        // normal loading (11133F12), including registration of existing actors.
        auto* collisionHash=reinterpret_cast<void**>(
            static_cast<char*>(importedLevel)+kLevelCollisionHashOffset);
        if (!*collisionHash)
        {
            Logger::log("MapRecovery: initializing normal actor collision state");
            using SetActorCollision=void(__thiscall*)(void*,int,int);
            reinterpret_cast<SetActorCollision>(0x1111EB30)(importedLevel,1,0);
            if (!*collisionHash)
            {
                error="The editor could not initialize collision for the imported source map.";
                return false;
            }
        }
        ArmActorTickDiagnostic();

        // Same actor-state bracket as the editor's Build All path. Explicit
        // commands ensure the user's last Build Options cannot skip a stage.
        struct BuildArray { void* data{}; int count{}; int capacity{}; };
        static BuildArray stateA, stateB;
        using Bracket = void(__thiscall*)(void*, void*, void*);
        reinterpret_cast<Bracket>(0x10E06A1A)(editor, &stateA, &stateB);
        struct RestoreBuildState
        {
            void* editor;
            BuildArray& stateA;
            BuildArray& stateB;
            bool active = true;
            void Finish()
            {
                if (!active) return;
                active = false;
                reinterpret_cast<Bracket>(0x10E02EEC)(editor, &stateA, &stateB);
            }
            ~RestoreBuildState() { try { Finish(); } catch (...) {} }
        } restoreBuildState{editor, stateA, stateB};
        bool built = HasLiveSourceLevelInfo() && exec("MAP REBUILD")
            && BspLeafLightFix::Validate(CurrentModel(), error)
            && ValidateCollisionBounds(CurrentModel(), error)
            && HasLiveSourceLevelInfo() && exec("BSP REBUILD") && HasLiveSourceLevelInfo()
            && BspLeafLightFix::Validate(CurrentModel(), error)
            && ValidateCollisionBounds(CurrentModel(), error);
        if (built) built = VerifySpaceProbes(probes, error);
        if (built) built = exec("LIGHT APPLY") && BspLeafLightFix::Validate(CurrentModel(), error)
            && ValidateCollisionBounds(CurrentModel(), error);
        if (built)
        {
            Logger::log("MapRecovery: rebuilding navigation and gameplay paths");
            using BuildPaths = void(__thiscall*)(void*);
            reinterpret_cast<BuildPaths>(0x10E06399)(editor);
        }
        restoreBuildState.Finish();
        if (!HasLiveSourceLevelInfo())
        {
            error = "The source map lost its LevelInfo object during the normal build.";
            return false;
        }
        if (!built) return false;
        if (!verifyActors("Built")) return false;
        void* builtLevel = *reinterpret_cast<void**>(static_cast<char*>(editor) + kEditorLevelOffset);
        if (!SynchronizeImportedActorPlatforms(builtLevel, error)) return false;

        // LIGHT APPLY updates the active StaticMeshInstance pointer. The
        // normal Build UI then commits it to the current platform's cache
        // (10EE6616); Save switches platforms and reads that cache. Without
        // this native finalizer, fresh imported maps lose their baked mesh
        // lighting during the first ordinary Save.
        void* context = *reinterpret_cast<void**>(kGSerializationContext);
        if (!context)
        {
            error = "The editor's lighting platform context is unavailable.";
            return false;
        }
        const int lightingPlatform = *reinterpret_cast<int*>(static_cast<char*>(context) + 0x78);
        using CacheMeshLighting = void(__thiscall*)(void*, int);
        reinterpret_cast<CacheMeshLighting>(0x10E06605)(editor, lightingPlatform);
        std::unordered_set<std::string> bakedMeshActors;
        const RawArray builtActors = ReadRawArray(builtLevel, kLevelActorsDataOffset);
        for (int index = 0; index < builtActors.count; ++index)
        {
            auto* actor = reinterpret_cast<unsigned char* const*>(builtActors.data)[index];
            if (!actor || !ReadRaw<void*>(actor, 0x100) || !ReadRaw<void*>(actor, 0x22C)) continue;
            char name[256]{};
            if (!CopyObjectName(actor, name, sizeof(name)))
            {
                error = "A baked mesh actor has an invalid name.";
                return false;
            }
            bakedMeshActors.insert(name);
        }

        using SaveFn = int(__thiscall*)(void*, const char*);
        Logger::log("MapRecovery: saving normal source " + destination.string());
        const std::string outputName = destination.string();
        if (!reinterpret_cast<SaveFn>(0x10E0416B)(editor, outputName.c_str()))
        {
            error = "The normal editor save failed. The complete source export is at " + sourcePath.string();
            return false;
        }
        LightmapFix::RepairSavedMap(outputName.c_str());
        if (!std::filesystem::is_regular_file(destination) || std::filesystem::file_size(destination) == 0)
        {
            error = "The normal save did not produce a source map. The complete T3D is at " + sourcePath.string();
            return false;
        }
        if (!std::filesystem::is_regular_file(runtime) || std::filesystem::file_size(runtime) == 0)
        {
            error = "The source was saved, but the editor did not create its playable Maps copy.";
            return false;
        }
        // This is deliberately the ordinary loader, with every recovery guard
        // off. A conversion is not successful unless its normal save reopens.
        if (!exec("MAP NEW") || !exec("MAP LOAD FILE=\"" + outputName + "\"")) return false;
        const auto reopenedLevel = *reinterpret_cast<void**>(static_cast<char*>(editor) + kEditorLevelOffset);
        const RawArray reopenedActors = ReadRawArray(reopenedLevel, kLevelActorsDataOffset);
        if (!IsValidArray(reopenedActors))
        {
            error = "The saved map has an invalid actor list.";
            return false;
        }
        for (int index = 0; index < reopenedActors.count; ++index)
        {
            auto* actor = reinterpret_cast<unsigned char* const*>(reopenedActors.data)[index];
            if (!actor || !ReadRaw<void*>(actor, 0x22C)) continue;
            char name[256]{};
            if (CopyObjectName(actor, name, sizeof(name))) bakedMeshActors.erase(name);
        }
        if (!bakedMeshActors.empty())
        {
            error = "The ordinary save lost rebuilt mesh lighting on "
                + std::to_string(bakedMeshActors.size()) + " actors (including "
                + *bakedMeshActors.begin() + "). The saved outputs should not be used.";
            return false;
        }
        Logger::log("MapRecovery: rebuilt mesh lighting survived ordinary save and reopening");
        if (!VerifySpaceProbes(probes, error) || !BspLeafLightFix::Validate(CurrentModel(), error)
            || !ValidateCollisionBounds(CurrentModel(), error))
        {
            error = "The saved outputs failed normal reopening verification and should not be used: "
                + destination.string() + " and " + runtime.string() + ". " + error;
            return false;
        }
        if (!verifyActors("Reopened"))
        {
            error = "The saved outputs failed actor verification and should not be used: "
                + destination.string() + " and " + runtime.string() + ". " + error;
            return false;
        }
        exec("LOADMAPPROP MAP=\"" + outputName + "\"");
        SetCurrentMapFilename(outputName.c_str());
        Logger::log("MapRecovery: normal source conversion, rebuild and reload verified; "
            + std::to_string(structuralBrushCount) + " structural brushes, "
            + std::to_string(actors.actorCount) + " actors, " + std::to_string(probes.size()) + " space probes.");
        std::ostringstream report;
        report << "Normal source map: " << destination.string()
               << "\nPlayable map: " << runtime.string()
               << "\nTarget platform: PC (common and PC-only actors)"
               << "\nReconstructed structural brushes: " << structuralBrushCount
               << "\nRetained actors: " << actors.actorCount
               << "\nExcluded Xbox-only actors: " << actors.skippedXboxActorCount
               << "\nCorrected stale platform labels on active PC actors: " << actors.correctedPcActorPlatformCount
               << "\nCleared references to excluded Xbox actors: " << actors.clearedXboxActorReferenceCount
               << "\nCleared references to confirmed deleted actors: " << actors.clearedDeletedActorReferenceCount
               << "\nRegenerated and verified procedural strip doors: " << actors.regeneratedStripDoors.size()
               << "\nVerified solid/empty samples: " << probes.size()
               << "\nNormal import, geometry/BSP/lighting/path builds, save and reopening verified.\n";
        if (!assets.empty()) report << "Include this asset dependency when distributing the map: "
                                    << assetPackagePath.string() << '\n';
        report << "Original construction-brush grouping/history is not retained. "
                  "Inspect the reconstructed map and test gameplay before distribution.\n";
        std::string reportError;
        if (!WriteTextFile(scratch / "Recovery.txt", report.str(), reportError)) Logger::log(reportError);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = std::string("Map recovery failed: ") + exception.what();
        return false;
    }
}

extern "C" __declspec(dllexport) int __cdecl ReloadedRecoverMapToSource(
    const char* source, const char* destination, char* errorBuffer, unsigned int errorSize)
{
    std::string error;
    const bool success = source && destination
        && MapRecovery::RecoverToSource(source, destination, error);
    if (errorBuffer && errorSize) strncpy_s(errorBuffer, errorSize, error.c_str(), _TRUNCATE);
    return success ? 1 : 0;
}

void MapRecovery::Run(HWND owner) { ChooseSourceRecovery(owner, false); }
void MapRecovery::RunEditable(HWND owner) { Run(owner); }
void MapRecovery::OpenRecovered(HWND owner) { ChooseSourceRecovery(owner, true); }
void MapRecovery::ExportRecoveredBrushes(HWND owner) { OpenRecovered(owner); }
