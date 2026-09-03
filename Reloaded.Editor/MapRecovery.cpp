#include "pch.h"
#include "MapRecovery.h"

#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
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
    constexpr size_t kNodeVertPoolOffset = 0x28;
    constexpr size_t kNodeSurfOffset = 0x2C;
    constexpr size_t kNodeVertexCountOffset = 0x5A;
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
    constexpr float kRecoveredBrushThickness = 1.0f;
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
    bool FormatExternalObjectPath(void* object, char* output, size_t outputSize)
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
            if (_stricmp(topPackage, "MyLevel") == 0
                || _stricmp(topPackage, "Transient") == 0
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
                               size_t& skipped, std::string& error)
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
            if (vertexCount < 3 || vertPool < 0
                || vertexCount > static_cast<unsigned int>(verts.count)
                || vertPool > verts.count - static_cast<int>(vertexCount))
            {
                ++skipped;
                continue;
            }

            RecoveredFace face;
            face.vertices.reserve(vertexCount);
            bool valid = true;
            for (unsigned int vertexIndex = 0;
                 vertexIndex < vertexCount; ++vertexIndex)
            {
                const unsigned char* vert = verts.data
                    + static_cast<size_t>(vertPool + vertexIndex) * kVertStride;
                const int pointIndex = ReadRaw<short>(vert, 0);
                if (pointIndex < 0 || pointIndex >= points.count)
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
                const int baseIndex = ReadRaw<unsigned short>(surf, kSurfBaseOffset);
                const int normalIndex = ReadRaw<unsigned short>(surf, kSurfNormalOffset);
                const int textureUIndex = ReadRaw<unsigned short>(surf, kSurfTextureUOffset);
                const int textureVIndex = ReadRaw<unsigned short>(surf, kSurfTextureVOffset);

                void* material = ReadRaw<void*>(surf, kSurfMaterialOffset);
                char materialPath[512] = {};
                if (FormatExternalObjectPath(
                        material, materialPath, sizeof(materialPath)))
                    face.materialPath = materialPath;

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
        output << std::showpos << std::fixed << std::setprecision(6)
               << value.x << ',' << value.y << ',' << value.z
               << std::noshowpos;
    }

    void WritePolygon(std::ostream& output, const std::vector<Vec3>& vertices,
                      const Vec3& normal, const Vec3& origin,
                      const Vec3& textureU, const Vec3& textureV,
                      const std::string& materialPath)
    {
        output << "          Begin Polygon Texture=" << materialPath << "\n";
        output << "             Origin   ";
        WriteVector(output, origin);
        output << "\n             Normal   ";
        WriteVector(output, normal);
        output << "\n             TextureU ";
        WriteVector(output, textureU);
        output << "\n             TextureV ";
        WriteVector(output, textureV);
        output << '\n';
        for (const Vec3& vertex : vertices)
        {
            output << "             Vertex   ";
            WriteVector(output, vertex);
            output << '\n';
        }
        output << "          End Polygon\n";
    }

    bool WriteRecoveredBrushFile(const std::filesystem::path& path,
                                 const std::vector<RecoveredFace>& faces,
                                 std::string& error)
    {
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output)
        {
            error = "Could not create the T3D output file.";
            return false;
        }

        output << "Begin Map\n";
        for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
        {
            const RecoveredFace& face = faces[faceIndex];
            std::ostringstream suffix;
            suffix << std::setw(6) << std::setfill('0') << (faceIndex + 1);
            const std::string actorName = "RecoveredBrush" + suffix.str();
            const std::string modelName = "RecoveredModel" + suffix.str();

            output << "Begin Actor Class=Brush Name=" << actorName << "\n"
                   << "    CsgOper=CSG_Add\n"
                   << "    Begin Brush Name=" << modelName << "\n"
                   << "       Begin PolyList\n";

            WritePolygon(output, face.vertices, face.normal, face.origin,
                         face.textureU, face.textureV, face.materialPath);

            std::vector<Vec3> back;
            back.reserve(face.vertices.size());
            const Vec3 extrusion = Scale(face.normal, -kRecoveredBrushThickness);
            for (auto vertex = face.vertices.rbegin();
                 vertex != face.vertices.rend(); ++vertex)
                back.push_back(Add(*vertex, extrusion));
            Vec3 backTextureU = face.textureU;
            Vec3 backTextureV = Scale(face.textureV, -1.0f);
            WritePolygon(output, back, Scale(face.normal, -1.0f), back.front(),
                         backTextureU, backTextureV, face.materialPath);

            for (size_t edge = 0; edge < face.vertices.size(); ++edge)
            {
                const Vec3& a = face.vertices[edge];
                const Vec3& b = face.vertices[(edge + 1) % face.vertices.size()];
                const Vec3 aBack = Add(a, extrusion);
                const Vec3 bBack = Add(b, extrusion);
                std::vector<Vec3> side = { a, aBack, bBack, b };
                Vec3 sideNormal = Cross(Subtract(aBack, a), Subtract(bBack, a));
                if (!Normalize(sideNormal))
                    continue;
                Vec3 sideU;
                Vec3 sideV;
                MakeTextureAxes(sideNormal, sideU, sideV);
                WritePolygon(output, side, sideNormal, a, sideU, sideV,
                             face.materialPath);
            }

            output << "       End PolyList\n"
                   << "    End Brush\n"
                   << "    Brush=Model'MyLevel." << modelName << "'\n"
                   << "End Actor\n";
        }
        output << "End Map\n";
        output.close();
        if (!output)
        {
            error = "Writing the T3D output file failed.";
            return false;
        }
        return true;
    }

    bool ChooseBrushExportPath(HWND owner, std::filesystem::path& path)
    {
        char selectedPath[MAX_PATH] = {};
        strncpy_s(selectedPath, path.filename().string().c_str(), _TRUNCATE);
        const std::string initialDirectory = path.parent_path().string();

        OPENFILENAMEA dialog = {};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner;
        dialog.lpstrFilter = "Unreal text maps (*.t3d)\0*.t3d\0All files (*.*)\0*.*\0\0";
        dialog.lpstrFile = selectedPath;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrInitialDir = initialDirectory.c_str();
        dialog.lpstrTitle = "Export recovered BSP as editable brushes";
        dialog.lpstrDefExt = "t3d";
        dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT
                     | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameA(&dialog))
            return false;

        path = selectedPath;
        if (_stricmp(path.extension().string().c_str(), ".t3d") != 0)
            path.replace_extension(".t3d");
        return true;
    }

    bool ValidateFullMapExport(const std::filesystem::path& path,
                               int& actorCount, std::string& error)
    {
        actorCount = 0;
        std::ifstream input(path);
        if (!input)
        {
            error = "The editor did not create the actor snapshot T3D.";
            return false;
        }

        bool beginMap = false;
        bool endMap = false;
        bool firstIsLevelInfo = false;
        std::string line;
        while (std::getline(input, line))
        {
            const size_t first = line.find_first_not_of(" \t\r");
            if (first == std::string::npos)
                continue;
            const std::string_view text(line.data() + first, line.size() - first);
            if (text.starts_with("Begin Map"))
                beginMap = true;
            else if (text.starts_with("End Map"))
                endMap = true;
            else if (text.starts_with("Begin Actor Class="))
            {
                if (actorCount == 0)
                    firstIsLevelInfo =
                        text.starts_with("Begin Actor Class=LevelInfo");
                ++actorCount;
            }
        }

        if (!beginMap || !endMap || !firstIsLevelInfo || actorCount < 1)
        {
            error = "The actor snapshot is not a valid map T3D "
                    "(LevelInfo or map boundary missing).";
            return false;
        }
        return true;
    }

    bool IsRuntimeActorProperty(std::string_view text)
    {
        static constexpr std::string_view prefixes[] = {
            "Level=", "Region=", "StaticMeshInstance=",
            "nextNavigationPoint=", "NavigationPointList=",
            "bMustInitNetChannels=", "PhysicsVolume=", "Touching(",
            "Owner=", "Base=", "Instigator=", "StateFrame=",
            "RenderData=", "LightRenderData=", "LastRenderTime=",
            "LastRenderTimeOnScreen=", "OctreeNodes(", "Leaves(",
            "NetTag=", "NetUpdateTime=", "LatentAction="
        };
        for (const std::string_view prefix : prefixes)
        {
            if (text.starts_with(prefix))
                return true;
        }
        return false;
    }

    bool PrepareActorImportAdd(const std::filesystem::path& source,
                               const std::filesystem::path& destination,
                               std::string& error)
    {
        std::ifstream input(source);
        if (!input)
        {
            error = "Could not reopen the recovered actor snapshot.";
            return false;
        }

        std::ofstream output(destination, std::ios::out | std::ios::trunc);
        if (!output)
        {
            error = "Could not create the repaired actor-import T3D.";
            return false;
        }

        output << "Begin Map\n";
        int actorIndex = -1;
        bool insideSkippedLevelInfo = false;
        int importedActorCount = 0;
        std::string line;
        while (std::getline(input, line))
        {
            const size_t first = line.find_first_not_of(" \t\r");
            const std::string_view text = first == std::string::npos
                ? std::string_view{}
                : std::string_view(line.data() + first, line.size() - first);

            if (text.starts_with("Begin Actor Class="))
            {
                ++actorIndex;
                insideSkippedLevelInfo = actorIndex == 0;
                if (!insideSkippedLevelInfo)
                {
                    output << line << '\n';
                    ++importedActorCount;
                }
                continue;
            }

            if (insideSkippedLevelInfo)
            {
                if (text == "End Actor")
                    insideSkippedLevelInfo = false;
                continue;
            }

            if (text == "Begin Map" || text == "End Map")
                continue;
            if (actorIndex >= 1 && IsRuntimeActorProperty(text))
                continue;
            if (actorIndex >= 1)
                output << line << '\n';
        }
        output << "End Map\n";
        output.close();
        if (!output)
        {
            error = "Writing the repaired actor-import T3D failed.";
            return false;
        }
        if (importedActorCount == 0)
        {
            error = "The recovered actor snapshot did not contain importable actors.";
            return false;
        }
        return true;
    }

    bool g_recoveryLoadActive = false;
    bool g_recoveredMapActive = false;
    bool g_oneClickEditableRecovery = false;
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
            std::error_code existsError;
            if (!std::filesystem::exists(candidate, existsError) && !existsError)
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
        if (!platform)
        {
            UnrootFallbackBuilderBrushObjects();
            return false;
        }

        const int previousPlatform = *platform;
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

        g_recoveryLoadActive = false;
        *platform = previousPlatform;
        UnrootFallbackBuilderBrushObjects();

        return accepted && g_recoveredMapActive;
    }

    void RefreshEditorAfterRecovery(HWND owner,
                                    const std::filesystem::path& destination)
    {
        void* editor = *reinterpret_cast<void**>(kGEditor);
        void* output = *reinterpret_cast<void**>(kGWarn);
        if (!editor)
            return;

        // LOADMAPPROP is the stock post-MAP-LOAD command. It restores the
        // map-specific editor properties that still exist in the package.
        DispatchEditorCommand(
            editor, output,
            "LOADMAPPROP MAP=\"" + destination.string() + "\"");

        void* postLoadObject = *reinterpret_cast<void**>(
            static_cast<char*>(editor) + kEditorPostLoadObjectOffset);
        if (postLoadObject)
        {
            void** vtable = *reinterpret_cast<void***>(postLoadObject);
            void* refresh = vtable
                ? vtable[kPostLoadRefreshVtableOffset / sizeof(void*)]
                : nullptr;
            if (refresh)
            {
                using RefreshFn = void(__thiscall*)(void*);
                reinterpret_cast<RefreshFn>(refresh)(postLoadObject);
            }
        }

        void* level = *reinterpret_cast<void**>(
            static_cast<char*>(editor) + kEditorLevelOffset);
        void** editorVtable = *reinterpret_cast<void***>(editor);
        void* redraw = editorVtable
            ? editorVtable[kEditorRedrawLevelVtableOffset / sizeof(void*)]
            : nullptr;
        if (level && redraw)
        {
            using RedrawFn = void(__thiscall*)(void*, void*);
            reinterpret_cast<RedrawFn>(redraw)(editor, level);
        }

        if (owner)
        {
            const std::string title =
                "Reloaded Chaos Theory Editor - [v1.2] - [RECOVERED: "
                + destination.stem().string() + "]";
            SetWindowTextA(owner, title.c_str());
            InvalidateRect(owner, nullptr, FALSE);
            UpdateWindow(owner);
        }
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
             + "\n\nOpen this package again with Recover Compiled Map; the normal Open command "
               "uses the source-map serializer and cannot read compiled-layout packages.").c_str(),
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

void MapRecovery::Run(HWND owner)
{
    const std::filesystem::path systemDirectory = ExecutableDirectory();
    if (systemDirectory.empty())
    {
        ShowError(owner, "Could not locate the editor's System directory.");
        return;
    }

    const std::filesystem::path packagesDirectory = systemDirectory.parent_path() / "Packages";
    const std::filesystem::path compiledMapsDirectory = packagesDirectory / "Maps";
    const std::filesystem::path mapsEdDirectory = packagesDirectory / "MapsEd";

    std::error_code directoryError;
    std::filesystem::create_directories(mapsEdDirectory, directoryError);
    if (directoryError)
    {
        ShowError(owner, "Could not create or access the Packages\\MapsEd directory.\n\n"
                         + directoryError.message());
        return;
    }

    char selectedPath[MAX_PATH] = {};
    const std::string initialDirectory = compiledMapsDirectory.string();
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = "Compiled map packages (*.sdc)\0*.sdc\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = selectedPath;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.lpstrTitle = "Choose a compiled map to recover";
    dialog.lpstrDefExt = "sdc";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST
                 | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;

    if (!GetOpenFileNameA(&dialog))
        return;

    const std::filesystem::path source(selectedPath);
    if (!HasSdcExtension(source))
    {
        ShowError(owner, "The selected file is not an .sdc map package.");
        return;
    }

    const std::filesystem::path destination =
        UniqueRecoveryPath(mapsEdDirectory, source);
    if (destination.empty())
    {
        ShowError(owner, "Could not choose a unique recovery filename in Packages\\MapsEd.");
        return;
    }

    std::ostringstream warning;
    warning
        << (g_oneClickEditableRecovery
                ? "This one-step editable recovery will copy:\n\n"
                : "This experimental recovery will copy:\n\n")
        << source.string() << "\n\n"
        << "to:\n\n" << destination.string() << "\n\n"
        << (g_oneClickEditableRecovery
                ? "then snapshot its actors and asset references, reconstruct its cooked BSP, "
                  "and replace the current view with a normal editable source map. Save any "
                  "current changes first.\n\n"
                : "and then open the copy. The current map will be replaced, so save "
                  "any changes first.\n\n")
        << "Compiled maps no longer contain the original construction-brush history or every "
           "editor-only object. Runtime actors, external asset references, materials and final "
           "geometry will be recovered where possible.\n\n"
        << "Continue?";

    if (MessageBoxA(owner, warning.str().c_str(), "Recover Compiled Map (Experimental)",
                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        return;

    std::error_code copyError;
    const bool copied = std::filesystem::copy_file(
        source, destination, std::filesystem::copy_options::none, copyError);
    if (!copied || copyError)
    {
        ShowError(owner, "Could not create the recovery copy.\n\n"
                         + copyError.message());
        return;
    }

    const std::string command = "MAP LOAD FILE=\"" + destination.string() + "\"";
    if (!ExecuteRecoveryLoad(command))
    {
        ShowError(owner,
            "The recovery copy was created, but the editor did not accept the MAP LOAD command.\n\n"
            "The copy has been kept at:\n" + destination.string());
        return;
    }

    g_recoveryOwner = owner;
    g_recoveredPath = destination;

    RefreshEditorAfterRecovery(owner, destination);

    if (g_oneClickEditableRecovery)
    {
        ExportRecoveredBrushes(owner);
        return;
    }

    MessageBoxA(owner,
        ("Recovered map opened from:\n" + destination.string()
         + "\n\nInspect the result, then use Save or Save As. Recovered maps are "
           "saved with the compiled package layout and must be reopened with this recovery command. "
           "Missing construction brushes and stripped editor-only data cannot be reconstructed.").c_str(),
        "Recover Compiled Map", MB_OK | MB_ICONINFORMATION);
    ArmViewportExceptionDiagnostic();
}

void MapRecovery::RunEditable(HWND owner)
{
    g_oneClickEditableRecovery = true;
    Run(owner);
    g_oneClickEditableRecovery = false;
}

void MapRecovery::OpenRecovered(HWND owner)
{
    const std::filesystem::path systemDirectory = ExecutableDirectory();
    if (systemDirectory.empty())
    {
        ShowError(owner, "Could not locate the editor's System directory.");
        return;
    }

    const std::filesystem::path mapsEdDirectory =
        systemDirectory.parent_path() / "Packages" / "MapsEd";

    std::error_code directoryError;
    std::filesystem::create_directories(mapsEdDirectory, directoryError);
    if (directoryError)
    {
        ShowError(owner,
            "Could not create or access the Packages\\MapsEd directory.\n\n"
            + directoryError.message());
        return;
    }

    char selectedPath[MAX_PATH] = {};
    const std::string initialDirectory = mapsEdDirectory.string();
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter =
        "Recovered map packages (*.sdc)\0*.sdc\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = selectedPath;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.lpstrTitle = "Open a recovered map";
    dialog.lpstrDefExt = "sdc";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST
                 | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;

    if (!GetOpenFileNameA(&dialog))
        return;

    const std::filesystem::path source(selectedPath);
    if (!HasSdcExtension(source))
    {
        ShowError(owner, "The selected file is not an .sdc map package.");
        return;
    }

    const std::string warning =
        "Open this recovered map in the experimental cooked-map mode?\n\n"
        + source.string()
        + "\n\nThe current map will be replaced, so save any changes first.";
    if (MessageBoxA(owner, warning.c_str(), "Open Recovered Map",
                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        return;

    const std::string command =
        "MAP LOAD FILE=\"" + source.string() + "\"";
    if (!ExecuteRecoveryLoad(command))
    {
        ShowError(owner,
            "The editor did not accept the recovered-map load command.\n\n"
            "The selected file was not modified.");
        return;
    }

    g_recoveryOwner = owner;
    g_recoveredPath = source;
    RefreshEditorAfterRecovery(owner, source);

    MessageBoxA(owner,
        ("Recovered map opened from:\n" + source.string()
         + "\n\nThis package was opened in place; no additional copy was created. "
           "Missing construction brushes and stripped editor-only data cannot "
           "be reconstructed.").c_str(),
        "Open Recovered Map", MB_OK | MB_ICONINFORMATION);
    ArmViewportExceptionDiagnostic();
}

void MapRecovery::ExportRecoveredBrushes(HWND owner)
{
    if (!IsRecoveredMapActive())
    {
        ShowError(owner,
            "Open a compiled or recovered map with the recovery command first.\n\n"
            "This exporter deliberately does not interpret source-map BSP arrays.");
        return;
    }

    std::vector<RecoveredFace> faces;
    size_t skipped = 0;
    std::string error;
    if (!ExtractRecoveredFaces(g_recoveredLevel, faces, skipped, error))
    {
        ShowError(owner, error);
        return;
    }

    const std::filesystem::path systemDirectory = ExecutableDirectory();
    if (systemDirectory.empty())
    {
        ShowError(owner, "Could not locate the editor's System directory.");
        return;
    }

    const std::filesystem::path mapsEdDirectory =
        systemDirectory.parent_path() / "Packages" / "MapsEd";
    std::error_code directoryError;
    std::filesystem::create_directories(mapsEdDirectory, directoryError);
    if (directoryError)
    {
        ShowError(owner,
            "Could not create or access the Packages\\MapsEd directory.\n\n"
            + directoryError.message());
        return;
    }

    std::string sourceStem = g_recoveredPath.stem().string();
    if (sourceStem.empty())
        sourceStem = "RecoveredMap";
    std::filesystem::path destination =
        mapsEdDirectory / (sourceStem + "_EditableBSP.t3d");
    if (g_oneClickEditableRecovery)
    {
        for (unsigned int index = 0; index < 10000; ++index)
        {
            std::ostringstream name;
            name << sourceStem << "_EditableBSP";
            if (index != 0)
                name << '_' << (index + 1);
            name << ".t3d";
            const std::filesystem::path candidate = mapsEdDirectory / name.str();
            std::error_code existsError;
            if (!std::filesystem::exists(candidate, existsError) && !existsError)
            {
                destination = candidate;
                break;
            }
        }
    }
    else if (!ChooseBrushExportPath(owner, destination))
    {
        return;
    }

    std::ostringstream warning;
    warning
        << "This will reconstruct " << faces.size()
        << " final cooked BSP polygon(s) as separate 1-unit-thick additive brushes.\n\n"
        << "The original construction-brush grouping, additive/subtractive history, "
           "names, pivots and exact CSG intent were stripped during cooking and cannot be "
           "recovered. Surface material references and UV axes are retained where available; "
           "the result is editable approximation geometry.\n\n"
        << "The active recovered map will not be modified. Import the resulting T3D "
           "into a new normal source map, where it can be selected, rebuilt and saved safely.\n\n"
        << "Output:\n" << destination.string();
    if (skipped != 0)
        warning << "\n\n" << skipped
                << " malformed or degenerate cooked node(s) will be skipped.";
    warning << "\n\nContinue?";

    if (!g_oneClickEditableRecovery
        && MessageBoxA(owner, warning.str().c_str(),
                       "Export Recovered BSP as Brushes (Experimental)",
                       MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        return;

    if (!WriteRecoveredBrushFile(destination, faces, error))
    {
        ShowError(owner, error + "\n\n" + destination.string());
        return;
    }

    std::ostringstream nextStep;
    nextStep
        << "Exported " << faces.size() << " editable brush actor(s) to:\n\n"
        << destination.string()
        << "\n\nCreate a NEW normal editable map and add these brushes now?\n\n"
           "Yes: replace the current recovered view with a fresh source map, then import "
           "the brushes with MAP IMPORTADD. The recovered .sdc and exported T3D stay safe.\n\n"
           "No: keep the recovered map open and only keep the exported T3D.\n\n"
           "Do not use File > Import for this actor-fragment T3D; that command expects a "
           "complete map containing LevelInfo and a builder brush.";
    const int createEditable = g_oneClickEditableRecovery
        ? IDYES
        : MessageBoxA(owner, nextStep.str().c_str(),
                      "Recovered BSP Brush Export Complete",
                      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    if (createEditable != IDYES)
        return;

    void* editor = *reinterpret_cast<void**>(kGEditor);
    void* output = *reinterpret_cast<void**>(kGWarn);
    if (!editor || !output)
    {
        ShowError(owner,
            "The brush T3D was exported, but the editor is not ready to create a new map.\n\n"
            + destination.string());
        return;
    }

    std::filesystem::path actorSnapshot = destination;
    actorSnapshot.replace_filename(
        destination.stem().string() + "_Actors.t3d");
    std::filesystem::path actorImportSnapshot = destination;
    actorImportSnapshot.replace_filename(
        destination.stem().string() + "_Actors_ImportAdd.t3d");
    std::error_code removeError;
    std::filesystem::remove(actorSnapshot, removeError);
    removeError.clear();
    std::filesystem::remove(actorImportSnapshot, removeError);

    int exportedActorCount = 0;
    bool transferActors = false;
    const std::string actorExportCommand =
        "MAP EXPORT FILE=\"" + actorSnapshot.string() + "\"";
    if (DispatchEditorCommand(editor, output, actorExportCommand))
    {
        transferActors = ValidateFullMapExport(
            actorSnapshot, exportedActorCount, error);
        if (transferActors)
            transferActors = PrepareActorImportAdd(
                actorSnapshot, actorImportSnapshot, error);
    }
    else
        error = "The editor did not accept MAP EXPORT for the recovered actors.";

    if (!transferActors)
    {
        const std::string actorWarning =
            error
            + "\n\nThe reconstructed BSP T3D is safe, but actors and static-mesh references "
              "cannot be transferred automatically on this attempt.\n\n"
              "Continue with geometry only?";
        if (g_oneClickEditableRecovery)
        {
            ShowError(owner, actorWarning.substr(
                0, actorWarning.size() - strlen("\n\nContinue with geometry only?")));
            return;
        }
        if (MessageBoxA(owner, actorWarning.c_str(),
                        "Recovered Actor Snapshot Failed",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
    }

    void* recoveredLevel = g_recoveredLevel;
    void* recoveredModel = recoveredLevel
        ? *reinterpret_cast<void**>(
            static_cast<char*>(recoveredLevel) + kLevelModelOffset)
        : nullptr;
    const RawArray recoveredNodes = recoveredModel
        ? ReadRawArray(recoveredModel, kModelNodesOffset)
        : RawArray{};
    if (!DispatchEditorCommand(editor, output, "MAP NEW"))
    {
        ShowError(owner,
            "The brush T3D was exported, but the editor did not accept MAP NEW.\n\n"
            "The recovered map was not intentionally modified.\n\n"
            + destination.string());
        return;
    }

    void* editableLevel = *reinterpret_cast<void**>(
        static_cast<char*>(editor) + kEditorLevelOffset);
    void* editableModel = editableLevel
        ? *reinterpret_cast<void**>(
            static_cast<char*>(editableLevel) + kLevelModelOffset)
        : nullptr;
    const RawArray editableNodes = editableModel
        ? ReadRawArray(editableModel, kModelNodesOffset)
        : RawArray{};
    const bool modelWasReset = editableModel
        && (editableModel != recoveredModel
            || editableNodes.data != recoveredNodes.data
            || editableNodes.count != recoveredNodes.count);
    if (!editableLevel || !modelWasReset)
    {
        ShowError(owner,
            "MAP NEW did not reset the cooked BSP model. Recovery mode remains active and the "
            "exported T3D was kept at:\n\n"
            + destination.string());
        return;
    }

    // MAP NEW reuses ULevel in this editor build, but resets its model arrays
    // in place. Pointer inequality is therefore not a valid transition test.
    // End cooked-layout mode explicitly before importing any editor UModels.
    g_recoveredMapActive = false;
    g_recoveredLevel = nullptr;
    g_recoveredPath.clear();
    g_recoveryOwner = nullptr;
    g_fallbackBuilderBrushActor = nullptr;
    g_fallbackBuilderBrushModel = nullptr;

    if (transferActors)
    {
        const int actorCountBeforeTransfer = *reinterpret_cast<int*>(
            static_cast<char*>(editableLevel) + kLevelActorsCountOffset);
        const std::string actorImportCommand =
            "MAP IMPORTADD FILE=\"" + actorImportSnapshot.string() + "\"";
        if (!DispatchEditorCommand(editor, output, actorImportCommand))
        {
            ShowError(owner,
                "A new normal map was created, but MAP IMPORTADD could not restore the recovered "
                "actor snapshot. The editor remains on a normal blank map.\n\n"
                + actorImportSnapshot.string());
            return;
        }

        editableLevel = *reinterpret_cast<void**>(
            static_cast<char*>(editor) + kEditorLevelOffset);
        if (!editableLevel)
        {
            ShowError(owner,
                "The actor snapshot import completed without an active level.\n\n"
                + actorImportSnapshot.string());
            return;
        }
        const int actorCountAfterTransfer = *reinterpret_cast<int*>(
            static_cast<char*>(editableLevel) + kLevelActorsCountOffset);
        if (actorCountAfterTransfer <= actorCountBeforeTransfer)
        {
            ShowError(owner,
                "MAP IMPORTADD returned without adding the recovered actor set. The editor "
                "remains on a normal blank map.\n\n"
                + actorImportSnapshot.string());
            return;
        }
    }

    const int actorCountBefore = *reinterpret_cast<int*>(
        static_cast<char*>(editableLevel) + kLevelActorsCountOffset);
    const std::string importCommand =
        "MAP IMPORTADD FILE=\"" + destination.string() + "\"";
    if (!DispatchEditorCommand(editor, output, importCommand))
    {
        ShowError(owner,
            "A new normal map was created, but MAP IMPORTADD did not accept the exported brushes.\n\n"
            "The editor is now on a blank normal map and the T3D is still available at:\n\n"
            + destination.string());
        return;
    }

    const int actorCountAfter = *reinterpret_cast<int*>(
        static_cast<char*>(editableLevel) + kLevelActorsCountOffset);
    if (actorCountAfter <= actorCountBefore)
    {
        ShowError(owner,
            "MAP IMPORTADD returned without adding brush actors. The editor is now on a blank "
            "normal map and the exported T3D was kept at:\n\n"
            + destination.string());
        return;
    }

    // Arm only after both imports have completed.  The next viewport update
    // will identify any reconstructed actor whose native Tick still depends
    // on cooked-only state that a text export cannot preserve.
    ArmActorTickDiagnostic();

    void** editorVtable = *reinterpret_cast<void***>(editor);
    void* redraw = editorVtable
        ? editorVtable[kEditorRedrawLevelVtableOffset / sizeof(void*)]
        : nullptr;
    if (redraw)
    {
        using RedrawFn = void(__thiscall*)(void*, void*);
        reinterpret_cast<RedrawFn>(redraw)(editor, editableLevel);
    }

    if (owner)
    {
        SetWindowTextA(owner,
            "Reloaded Chaos Theory Editor - [v1.2] - [NEW EDITABLE BSP RECOVERY]");
        InvalidateRect(owner, nullptr, FALSE);
    }

    std::ostringstream complete;
    complete
        << "Created a normal editor map and added "
        << (actorCountAfter - actorCountBefore) << " reconstructed brush actor(s).";
    if (transferActors)
        complete << "\nRestored " << (exportedActorCount - 1)
                 << " exported actors (LevelInfo was replaced by the fresh normal map's "
                    "instance) and their asset references.";
    complete << "\n\n"
        << "Save As under a new map name before rebuilding. The original recovered .sdc and "
           "this interchange file were not modified:\n\n"
        << destination.string();
    MessageBoxA(owner, complete.str().c_str(),
                "Editable BSP Recovery Created",
                MB_OK | MB_ICONINFORMATION);
}
