#include "pch.h"
#include "BspDiagnostics.h"

#include "CrashDiagnostics.h"
#include "MemoryWriter.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr uintptr_t kExpectedImageBase = 0x10E00000;
    constexpr DWORD kExpectedImageTimestamp = 0x42789253;
    constexpr DWORD kExpectedImageSize = 0x00D93000;

    // The stock CSG remap and FindNearestVertex loops read FVert::pVertex with
    // MOVSX even though the serialized field is a 16-bit index. Large BSP
    // rebuilds that pass 0x7FFF points therefore turn valid indices into
    // negative remap-table and point-array offsets.
    constexpr int kSignedPointIndexMax = 0x7FFF;
    constexpr int kUnsignedPointIndexMax = 0xFFFF;

    constexpr uintptr_t kGEditor = 0x1165DFA0;
    constexpr uintptr_t kGNamesData = 0x1169CFBC;
    constexpr uintptr_t kGNamesCount = 0x1169CFC0;
    constexpr size_t kEditorLevelOffset = 0x130;
    constexpr size_t kLevelActorsCountOffset = 0x30;
    constexpr size_t kLevelModelOffset = 0x13C;
    constexpr size_t kObjectNameOffset = 0x20;
    constexpr size_t kObjectClassOffset = 0x28;
    constexpr size_t kFNameTextOffset = 0x0C;
    constexpr size_t kActorBrushOffset = 0x238;
    constexpr size_t kActorPolyFlagsOffset = 0x344;
    constexpr size_t kActorCsgOperOffset = 0x34C;

    constexpr size_t kBrushPolysOffset = 0x50;
    constexpr size_t kArrayDataOffset = 0x00;
    constexpr size_t kArrayCountOffset = 0x04;
    constexpr size_t kArrayMaxOffset = 0x08;
    constexpr size_t kPolysDataOffset = 0x28;
    constexpr size_t kPolysCountOffset = 0x2C;
    constexpr size_t kFPolyStride = 0x14C;
    constexpr size_t kFPolyBaseOffset = 0x00;
    constexpr size_t kFPolyNormalOffset = 0x0C;
    constexpr size_t kFPolyVerticesOffset = 0x18;
    constexpr size_t kFPolyActorOffset = 0x130;
    constexpr size_t kFPolyBrushPolyOffset = 0x134;
    constexpr size_t kFPolyLinkOffset = 0x138;
    constexpr size_t kFPolyFlagsOffset = 0x140;
    constexpr size_t kFPolyVertexCountOffset = 0x148;
    constexpr int kMaxFPolyVertices = 16;

    // Editor UModel arrays (TArray data/count/max). These are verified from
    // the instructions used by FilterEdPoly and FindNearestVertex in this exe.
    constexpr size_t kModelNodesOffset = 0x54;
    constexpr size_t kModelVertsOffset = 0x64;
    constexpr size_t kModelVectorsOffset = 0x74;
    constexpr size_t kModelPointsOffset = 0x84;
    constexpr size_t kModelSurfsOffset = 0x94;
    constexpr size_t kNodeStride = 0x5C;
    constexpr size_t kVertStride = 0x08;
    constexpr size_t kSurfStride = 0x2C;
    constexpr size_t kNodeVertPoolOffset = 0x28;
    constexpr size_t kNodeSurfOffset = 0x2C;
    constexpr size_t kNodeFrontOffset = 0x30;
    constexpr size_t kNodeBackOffset = 0x34;
    constexpr size_t kNodePlaneOffset = 0x38;
    constexpr size_t kNodeVertexCountOffset = 0x5A;
    constexpr size_t kSurfBaseOffset = 0x18;
    constexpr size_t kSurfNormalOffset = 0x1A;
    constexpr int kMaxDiagnosticArrayCount = 2000000;

    enum class BuildStage : LONG
    {
        None = 0,
        Geometry = 1,
        Bsp = 2,
        Lighting = 3,
        CsgOperation = 4
    };

    enum class EventType : LONG
    {
        StageBegin = 1,
        StageEnd,
        BrushBegin,
        BrushPolygon,
        Filter,
        Split,
        AddPoint,
        NearestVertex,
        BrushEnd,
        InvariantFailure
    };

    enum class InvariantCode : LONG
    {
        None = 0,
        NullModel,
        BadNodeArray,
        BadNodeIndex,
        BadSurfaceArray,
        BadSurfaceIndex,
        BadVertArray,
        BadVertPoolIndex,
        BadPointArray,
        BadPointIndex,
        PointIndexCapacityExceeded,
        BadVectorArray,
        BadNormalIndex,
        BadChildIndex,
        BadFPolyPointer,
        BadFPolyVertexCount,
        NonFiniteFPoly
    };

    struct Vec3
    {
        float x;
        float y;
        float z;
    };

    struct RawArray
    {
        void* data;
        int count;
        int max;
    };

    struct ModelSnapshot
    {
        void* model;
        RawArray nodes;
        RawArray verts;
        RawArray vectors;
        RawArray points;
        RawArray surfs;
        bool readable;
    };

    struct PolySnapshot
    {
        void* address;
        int vertexCount;
        int sourceBrushPoly;
        int link;
        uint32_t flags;
        Vec3 base;
        Vec3 normal;
        Vec3 vertices[kMaxFPolyVertices];
        bool readable;
        bool finite;
    };

    struct BrushAnalysis
    {
        int polygonCount;
        int invalidVertexCounts;
        int nonFinite;
        int zeroArea;
        int duplicateVertices;
        int nonPlanar;
        int concave;
        int badLinks;
        int firstInvalidVertexCount;
        int firstNonFinite;
        int firstZeroArea;
        int firstDuplicate;
        int firstNonPlanar;
        int firstConcave;
        int firstBadLink;
        bool readable;
    };

    struct DiagnosticEvent
    {
        volatile LONG committedSequence;
        LONG sequence;
        EventType type;
        DWORD threadId;
        LONGLONG qpc;
        uintptr_t a;
        uintptr_t b;
        uintptr_t c;
        uintptr_t d;
        LONG x;
        LONG y;
    };

    constexpr LONG kEventCapacity = 128;
    DiagnosticEvent g_events[kEventCapacity] = {};
    volatile LONG g_eventSequence = 0;

    volatile LONG g_initialized = 0;
    volatile LONG g_buildActive = 0;
    volatile LONG g_buildSerial = 0;
    volatile LONG g_stage = static_cast<LONG>(BuildStage::None);
    volatile LONG g_adHocStage = 0;
    volatile LONG g_crashCaptured = 0;
    volatile LONG g_invariantCaptured = 0;
    volatile LONG g_lastInvariant = static_cast<LONG>(InvariantCode::None);
    volatile LONG g_lastInvariantA = 0;
    volatile LONG g_lastInvariantB = 0;
    volatile LONG g_unsignedPointIndexPatchInstalled = 0;
    volatile LONG g_pointRangeLogged = 0;
    DWORD g_buildThreadId = 0;
    LONGLONG g_stageStartedQpc = 0;
    LARGE_INTEGER g_qpcFrequency = {};

    void* g_level = nullptr;
    void* g_levelModel = nullptr;
    int g_actorCount = 0;
    char g_mapName[128] = "<unknown>";
    char g_editorWindowTitle[512] = "<unknown>";

    void* g_currentActor = nullptr;
    void* g_currentBrush = nullptr;
    void* g_currentModel = nullptr;
    LONG g_currentCsgOper = -1;
    uint32_t g_currentPolyFlags = 0;
    LONG g_currentBrushPoly = -1;
    char g_currentActorName[128] = "<none>";
    char g_currentActorClass[128] = "<none>";
    BrushAnalysis g_currentBrushAnalysis = {};

    void* g_currentFilterModel = nullptr;
    LONG g_currentFilterNode = -1;
    void* g_currentFilterPoly = nullptr;
    volatile LONG g_filterEntries = 0;
    volatile LONG g_splitCalls = 0;
    volatile LONG g_addPointCalls = 0;
    volatile LONG g_nearestVertexCalls = 0;
    PolySnapshot g_currentPolySnapshot = {};

    HANDLE g_journal = INVALID_HANDLE_VALUE;
    char g_journalPath[MAX_PATH] = {};

    const char* StageName(BuildStage stage)
    {
        switch (stage)
        {
        case BuildStage::Geometry:      return "geometry";
        case BuildStage::Bsp:           return "BSP";
        case BuildStage::Lighting:      return "lighting";
        case BuildStage::CsgOperation:  return "individual CSG operation";
        default:                        return "none";
        }
    }

    const char* EventName(EventType type)
    {
        switch (type)
        {
        case EventType::StageBegin:        return "stage-begin";
        case EventType::StageEnd:          return "stage-end";
        case EventType::BrushBegin:        return "brush-begin";
        case EventType::BrushPolygon:      return "brush-poly";
        case EventType::Filter:            return "filter";
        case EventType::Split:             return "split";
        case EventType::AddPoint:          return "add-point";
        case EventType::NearestVertex:     return "nearest-vertex";
        case EventType::BrushEnd:          return "brush-end";
        case EventType::InvariantFailure:  return "invariant-failure";
        default:                           return "unknown";
        }
    }

    const char* InvariantName(InvariantCode code)
    {
        switch (code)
        {
        case InvariantCode::NullModel:             return "null model";
        case InvariantCode::BadNodeArray:          return "invalid node array";
        case InvariantCode::BadNodeIndex:          return "node index out of range";
        case InvariantCode::BadSurfaceArray:       return "invalid surface array";
        case InvariantCode::BadSurfaceIndex:       return "surface index out of range";
        case InvariantCode::BadVertArray:          return "invalid vertex-pool array";
        case InvariantCode::BadVertPoolIndex:      return "vertex-pool index out of range";
        case InvariantCode::BadPointArray:         return "invalid point array";
        case InvariantCode::BadPointIndex:         return "point index out of range";
        case InvariantCode::PointIndexCapacityExceeded:
            return "16-bit BSP point-index capacity exceeded";
        case InvariantCode::BadVectorArray:        return "invalid vector array";
        case InvariantCode::BadNormalIndex:        return "normal-vector index out of range";
        case InvariantCode::BadChildIndex:         return "BSP child index out of range";
        case InvariantCode::BadFPolyPointer:       return "unreadable FPoly";
        case InvariantCode::BadFPolyVertexCount:   return "FPoly vertex count out of range";
        case InvariantCode::NonFiniteFPoly:        return "FPoly contains non-finite coordinates";
        default:                                   return "none";
        }
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

    Vec3 Subtract(const Vec3& a, const Vec3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    float Dot(const Vec3& a, const Vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    float LengthSquared(const Vec3& value)
    {
        return Dot(value, value);
    }

    void RecordEvent(EventType type, uintptr_t a = 0, uintptr_t b = 0,
                     uintptr_t c = 0, uintptr_t d = 0,
                     LONG x = 0, LONG y = 0)
    {
        const LONG sequence = InterlockedIncrement(&g_eventSequence);
        DiagnosticEvent& event = g_events[(sequence - 1) % kEventCapacity];
        InterlockedExchange(&event.committedSequence, 0);
        event.sequence = sequence;
        event.type = type;
        event.threadId = GetCurrentThreadId();
        LARGE_INTEGER qpc = {};
        QueryPerformanceCounter(&qpc);
        event.qpc = qpc.QuadPart;
        event.a = a;
        event.b = b;
        event.c = c;
        event.d = d;
        event.x = x;
        event.y = y;
        MemoryBarrier();
        InterlockedExchange(&event.committedSequence, sequence);
    }

    void JournalLine(const char* format, ...)
    {
        if (g_journal == INVALID_HANDLE_VALUE || !format)
            return;

        char message[1792] = {};
        va_list args;
        va_start(args, format);
        _vsnprintf_s(message, std::size(message), _TRUNCATE, format, args);
        va_end(args);

        SYSTEMTIME time = {};
        GetLocalTime(&time);
        char line[2048] = {};
        const int length = _snprintf_s(
            line, std::size(line), _TRUNCATE,
            "[%02u:%02u:%02u.%03u] %s\r\n",
            time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
            message);
        if (length > 0)
        {
            DWORD written = 0;
            WriteFile(g_journal, line, static_cast<DWORD>(length),
                      &written, nullptr);
        }
    }

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
            if (!names || nameCount <= 0 || nameIndex < 0
                || nameIndex >= nameCount || !names[nameIndex])
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

    bool CopyObjectClassName(void* object, char* output, size_t outputSize)
    {
        if (!object)
            return false;
        void* objectClass = nullptr;
        __try
        {
            objectClass = *reinterpret_cast<void**>(
                static_cast<char*>(object) + kObjectClassOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            objectClass = nullptr;
        }
        return CopyObjectName(objectClass, output, outputSize);
    }

    bool ReadRawArray(void* owner, size_t offset, RawArray& result)
    {
        ZeroMemory(&result, sizeof(result));
        if (!owner)
            return false;
        __try
        {
            const char* field = static_cast<const char*>(owner) + offset;
            result.data = *reinterpret_cast<void* const*>(field + kArrayDataOffset);
            result.count = *reinterpret_cast<const int*>(field + kArrayCountOffset);
            result.max = *reinterpret_cast<const int*>(field + kArrayMaxOffset);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ZeroMemory(&result, sizeof(result));
            return false;
        }
    }

    bool IsPlausibleArray(const RawArray& array)
    {
        return array.count >= 0 && array.count <= kMaxDiagnosticArrayCount
            && array.max >= array.count && array.max <= kMaxDiagnosticArrayCount
            && (array.count == 0 || array.data != nullptr);
    }

    ModelSnapshot ReadModelSnapshot(void* model)
    {
        ModelSnapshot snapshot = {};
        snapshot.model = model;
        if (!model)
            return snapshot;
        snapshot.readable = ReadRawArray(model, kModelNodesOffset, snapshot.nodes)
            && ReadRawArray(model, kModelVertsOffset, snapshot.verts)
            && ReadRawArray(model, kModelVectorsOffset, snapshot.vectors)
            && ReadRawArray(model, kModelPointsOffset, snapshot.points)
            && ReadRawArray(model, kModelSurfsOffset, snapshot.surfs);
        return snapshot;
    }

    PolySnapshot ReadPolySnapshot(void* poly)
    {
        PolySnapshot snapshot = {};
        snapshot.address = poly;
        snapshot.vertexCount = -1;
        snapshot.sourceBrushPoly = -1;
        snapshot.link = -1;
        snapshot.finite = true;
        if (!poly)
            return snapshot;

        __try
        {
            const char* bytes = static_cast<const char*>(poly);
            snapshot.vertexCount = *reinterpret_cast<const uint16_t*>(
                bytes + kFPolyVertexCountOffset);
            snapshot.sourceBrushPoly = *reinterpret_cast<const int*>(
                bytes + kFPolyBrushPolyOffset);
            snapshot.link = *reinterpret_cast<const int*>(bytes + kFPolyLinkOffset);
            snapshot.flags = *reinterpret_cast<const uint32_t*>(
                bytes + kFPolyFlagsOffset);
            memcpy(&snapshot.base, bytes + kFPolyBaseOffset, sizeof(Vec3));
            memcpy(&snapshot.normal, bytes + kFPolyNormalOffset, sizeof(Vec3));
            const int copyCount = std::clamp(
                snapshot.vertexCount, 0, kMaxFPolyVertices);
            memcpy(snapshot.vertices, bytes + kFPolyVerticesOffset,
                   static_cast<size_t>(copyCount) * sizeof(Vec3));
            snapshot.readable = true;
            snapshot.finite = IsFinite(snapshot.base) && IsFinite(snapshot.normal);
            for (int index = 0; index < copyCount; ++index)
                snapshot.finite = snapshot.finite
                    && IsFinite(snapshot.vertices[index]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            snapshot.readable = false;
            snapshot.finite = false;
        }
        return snapshot;
    }

    BrushAnalysis AnalyzeBrush(void* actor)
    {
        BrushAnalysis analysis = {};
        analysis.firstInvalidVertexCount = -1;
        analysis.firstNonFinite = -1;
        analysis.firstZeroArea = -1;
        analysis.firstDuplicate = -1;
        analysis.firstNonPlanar = -1;
        analysis.firstConcave = -1;
        analysis.firstBadLink = -1;

        __try
        {
            void* brush = *reinterpret_cast<void**>(
                static_cast<char*>(actor) + kActorBrushOffset);
            void* polys = brush ? *reinterpret_cast<void**>(
                static_cast<char*>(brush) + kBrushPolysOffset) : nullptr;
            void* data = polys ? *reinterpret_cast<void**>(
                static_cast<char*>(polys) + kPolysDataOffset) : nullptr;
            const int count = polys ? *reinterpret_cast<int*>(
                static_cast<char*>(polys) + kPolysCountOffset) : 0;
            if (!brush || !polys || count < 0 || count > 100000
                || (count > 0 && !data))
                return analysis;

            analysis.readable = true;
            analysis.polygonCount = count;
            for (int polyIndex = 0; polyIndex < count; ++polyIndex)
            {
                const char* poly = static_cast<const char*>(data)
                    + static_cast<size_t>(polyIndex) * kFPolyStride;
                const int vertexCount = *reinterpret_cast<const uint16_t*>(
                    poly + kFPolyVertexCountOffset);
                const int link = *reinterpret_cast<const int*>(
                    poly + kFPolyLinkOffset);
                if (link < -1 || link >= count)
                {
                    ++analysis.badLinks;
                    if (analysis.firstBadLink < 0)
                        analysis.firstBadLink = polyIndex;
                }
                if (vertexCount < 3 || vertexCount > kMaxFPolyVertices)
                {
                    ++analysis.invalidVertexCounts;
                    if (analysis.firstInvalidVertexCount < 0)
                        analysis.firstInvalidVertexCount = polyIndex;
                    continue;
                }

                Vec3 vertices[kMaxFPolyVertices] = {};
                memcpy(vertices, poly + kFPolyVerticesOffset,
                       static_cast<size_t>(vertexCount) * sizeof(Vec3));
                bool finite = true;
                float coordinateScale = 1.0f;
                for (int index = 0; index < vertexCount; ++index)
                {
                    finite = finite && IsFinite(vertices[index]);
                    coordinateScale = (std::max)(
                        coordinateScale,
                        (std::max)(std::fabs(vertices[index].x),
                                   (std::max)(std::fabs(vertices[index].y),
                                              std::fabs(vertices[index].z))));
                }
                if (!finite)
                {
                    ++analysis.nonFinite;
                    if (analysis.firstNonFinite < 0)
                        analysis.firstNonFinite = polyIndex;
                    continue;
                }

                bool duplicate = false;
                for (int index = 0; index < vertexCount; ++index)
                {
                    const Vec3 delta = Subtract(
                        vertices[index], vertices[(index + 1) % vertexCount]);
                    if (LengthSquared(delta) < 0.000001f)
                        duplicate = true;
                }
                if (duplicate)
                {
                    ++analysis.duplicateVertices;
                    if (analysis.firstDuplicate < 0)
                        analysis.firstDuplicate = polyIndex;
                }

                Vec3 geometricNormal = {};
                for (int index = 0; index < vertexCount; ++index)
                {
                    const Vec3& current = vertices[index];
                    const Vec3& next = vertices[(index + 1) % vertexCount];
                    geometricNormal.x += (current.y - next.y)
                        * (current.z + next.z);
                    geometricNormal.y += (current.z - next.z)
                        * (current.x + next.x);
                    geometricNormal.z += (current.x - next.x)
                        * (current.y + next.y);
                }
                const float normalLengthSquared = LengthSquared(geometricNormal);
                if (!std::isfinite(normalLengthSquared)
                    || normalLengthSquared < 0.000001f)
                {
                    ++analysis.zeroArea;
                    if (analysis.firstZeroArea < 0)
                        analysis.firstZeroArea = polyIndex;
                    continue;
                }

                const float inverseLength = 1.0f / std::sqrt(normalLengthSquared);
                geometricNormal.x *= inverseLength;
                geometricNormal.y *= inverseLength;
                geometricNormal.z *= inverseLength;
                const float planeTolerance = 0.05f + coordinateScale * 0.00001f;
                bool nonPlanar = false;
                for (int index = 1; index < vertexCount; ++index)
                {
                    const float distance = Dot(
                        Subtract(vertices[index], vertices[0]), geometricNormal);
                    if (std::fabs(distance) > planeTolerance)
                        nonPlanar = true;
                }
                if (nonPlanar)
                {
                    ++analysis.nonPlanar;
                    if (analysis.firstNonPlanar < 0)
                        analysis.firstNonPlanar = polyIndex;
                }

                bool positive = false;
                bool negative = false;
                for (int index = 0; index < vertexCount; ++index)
                {
                    const Vec3 edgeA = Subtract(
                        vertices[(index + 1) % vertexCount], vertices[index]);
                    const Vec3 edgeB = Subtract(
                        vertices[(index + 2) % vertexCount],
                        vertices[(index + 1) % vertexCount]);
                    const float turn = Dot(Cross(edgeA, edgeB), geometricNormal);
                    if (turn > 0.0001f)
                        positive = true;
                    else if (turn < -0.0001f)
                        negative = true;
                }
                if (positive && negative)
                {
                    ++analysis.concave;
                    if (analysis.firstConcave < 0)
                        analysis.firstConcave = polyIndex;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            analysis.readable = false;
        }
        return analysis;
    }

    void WriteModel(HANDLE file, const char* label, void* model)
    {
        const ModelSnapshot snapshot = ReadModelSnapshot(model);
        CrashDiagnostics::WriteLine(
            file,
            "%s model=%p readable=%d nodes=%p/%d/%d verts=%p/%d/%d "
            "vectors=%p/%d/%d points=%p/%d/%d surfs=%p/%d/%d",
            label, model, snapshot.readable ? 1 : 0,
            snapshot.nodes.data, snapshot.nodes.count, snapshot.nodes.max,
            snapshot.verts.data, snapshot.verts.count, snapshot.verts.max,
            snapshot.vectors.data, snapshot.vectors.count, snapshot.vectors.max,
            snapshot.points.data, snapshot.points.count, snapshot.points.max,
            snapshot.surfs.data, snapshot.surfs.count, snapshot.surfs.max);
    }

    void WriteMemoryRegion(HANDLE file, const char* label, const void* address)
    {
        MEMORY_BASIC_INFORMATION region = {};
        const SIZE_T result = address
            ? VirtualQuery(address, &region, sizeof(region)) : 0;
        CrashDiagnostics::WriteLine(
            file, "%s=%p regionBase=%p regionSize=0x%lX state=0x%lX "
            "protect=0x%lX type=0x%lX",
            label, address, result ? region.BaseAddress : nullptr,
            static_cast<unsigned long>(result ? region.RegionSize : 0),
            result ? region.State : 0, result ? region.Protect : 0,
            result ? region.Type : 0);
    }

    void WritePoly(HANDLE file, const PolySnapshot& poly)
    {
        CrashDiagnostics::WriteLine(
            file, "FPoly=%p readable=%d finite=%d vertices=%d sourcePoly=%d "
            "link=%d flags=0x%08lX",
            poly.address, poly.readable ? 1 : 0, poly.finite ? 1 : 0,
            poly.vertexCount, poly.sourceBrushPoly, poly.link,
            static_cast<unsigned long>(poly.flags));
        CrashDiagnostics::WriteLine(
            file, "  Base=(%.9g, %.9g, %.9g) Normal=(%.9g, %.9g, %.9g)",
            poly.base.x, poly.base.y, poly.base.z,
            poly.normal.x, poly.normal.y, poly.normal.z);
        const int count = std::clamp(poly.vertexCount, 0, kMaxFPolyVertices);
        for (int index = 0; index < count; ++index)
        {
            CrashDiagnostics::WriteLine(
                file, "  Vertex[%d]=(%.9g, %.9g, %.9g)", index,
                poly.vertices[index].x, poly.vertices[index].y,
                poly.vertices[index].z);
        }
    }

    void WriteStateReport(const char* prefix,
                          EXCEPTION_POINTERS* exceptionInfo)
    {
        char reportPath[MAX_PATH] = {};
        char dumpPath[MAX_PATH] = {};
        if (!CrashDiagnostics::MakeArtifactPath(
                prefix, "log", reportPath, std::size(reportPath)))
            return;
        CrashDiagnostics::MakeArtifactPath(
            prefix, "dmp", dumpPath, std::size(dumpPath));

        HANDLE file = CreateFileA(
            reportPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        const BuildStage stage = static_cast<BuildStage>(
            InterlockedCompareExchange(&g_stage, 0, 0));
        CrashDiagnostics::WriteLine(file, "SCCT BSP/CSG diagnostic report");
        CrashDiagnostics::WriteLine(file, "Report=%s", reportPath);
        CrashDiagnostics::WriteLine(file, "BuildJournal=%s", g_journalPath);
        CrashDiagnostics::WriteLine(
            file, "BuildSerial=%ld stage=%s active=%ld buildThread=%lu "
            "faultThread=%lu map=%s level=%p actorCount=%d",
            InterlockedCompareExchange(&g_buildSerial, 0, 0), StageName(stage),
            InterlockedCompareExchange(&g_buildActive, 0, 0),
            g_buildThreadId, GetCurrentThreadId(), g_mapName, g_level,
            g_actorCount);
        CrashDiagnostics::WriteLine(
            file, "EditorWindowTitle=%s", g_editorWindowTitle);

        const ModelSnapshot levelSnapshot = ReadModelSnapshot(g_levelModel);
        CrashDiagnostics::WriteLine(
            file, "PointIndexCompatibility patchInstalled=%ld "
            "stockSignedMax=%d patchedUnsignedMax=%d currentPoints=%d",
            InterlockedCompareExchange(
                &g_unsignedPointIndexPatchInstalled, 0, 0),
            kSignedPointIndexMax, kUnsignedPointIndexMax,
            levelSnapshot.points.count);

        if (exceptionInfo && exceptionInfo->ExceptionRecord
            && exceptionInfo->ContextRecord)
        {
            const EXCEPTION_RECORD* record = exceptionInfo->ExceptionRecord;
            const CONTEXT* context = exceptionInfo->ContextRecord;
            CrashDiagnostics::WriteLine(
                file, "ExceptionCode=0x%08lX address=%p flags=0x%08lX",
                record->ExceptionCode, record->ExceptionAddress,
                record->ExceptionFlags);
            HMODULE executable = GetModuleHandleA(nullptr);
            const uintptr_t executableOffset = executable
                ? reinterpret_cast<uintptr_t>(record->ExceptionAddress)
                    - reinterpret_cast<uintptr_t>(executable)
                : 0;
            CrashDiagnostics::WriteLine(
                file, "ExceptionModule=%p executableOffset=0x%08lX",
                executable, static_cast<unsigned long>(executableOffset));
            if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
                 || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
                && record->NumberParameters >= 2)
            {
                CrashDiagnostics::WriteLine(
                    file, "AccessOperation=%llu AccessAddress=%p",
                    static_cast<unsigned long long>(record->ExceptionInformation[0]),
                    reinterpret_cast<void*>(record->ExceptionInformation[1]));
            }
            CrashDiagnostics::WriteLine(
                file, "EIP=%08lX ESP=%08lX EBP=%08lX EFLAGS=%08lX",
                context->Eip, context->Esp, context->Ebp, context->EFlags);
            CrashDiagnostics::WriteLine(
                file, "EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX "
                "ESI=%08lX EDI=%08lX",
                context->Eax, context->Ebx, context->Ecx, context->Edx,
                context->Esi, context->Edi);
        }

        CrashDiagnostics::WriteLine(file, "Current brush:");
        CrashDiagnostics::WriteLine(
            file, "  actor=%p name=%s class=%s brush=%p worldModel=%p "
            "operation=%ld polyFlags=0x%08lX sourcePoly=%ld",
            g_currentActor, g_currentActorName, g_currentActorClass,
            g_currentBrush, g_currentModel, g_currentCsgOper,
            static_cast<unsigned long>(g_currentPolyFlags),
            g_currentBrushPoly);
        const BrushAnalysis& analysis = g_currentBrushAnalysis;
        CrashDiagnostics::WriteLine(
            file, "  preflight readable=%d polygons=%d invalidVertexCounts=%d "
            "nonFinite=%d zeroArea=%d duplicateVertices=%d nonPlanar=%d "
            "concave=%d badLinks=%d",
            analysis.readable ? 1 : 0, analysis.polygonCount,
            analysis.invalidVertexCounts, analysis.nonFinite,
            analysis.zeroArea, analysis.duplicateVertices,
            analysis.nonPlanar, analysis.concave, analysis.badLinks);
        CrashDiagnostics::WriteLine(
            file, "  firstIssues invalidVertices=%d nonFinite=%d zeroArea=%d "
            "duplicate=%d nonPlanar=%d concave=%d badLink=%d",
            analysis.firstInvalidVertexCount, analysis.firstNonFinite,
            analysis.firstZeroArea, analysis.firstDuplicate,
            analysis.firstNonPlanar, analysis.firstConcave,
            analysis.firstBadLink);

        WriteModel(file, "LevelModel", g_levelModel);
        if (g_currentModel != g_levelModel)
            WriteModel(file, "CurrentWorldModel", g_currentModel);
        WriteMemoryRegion(file, "CurrentActor", g_currentActor);
        WriteMemoryRegion(file, "CurrentBrush", g_currentBrush);
        WriteMemoryRegion(file, "CurrentFilterPoly", g_currentFilterPoly);

        CrashDiagnostics::WriteLine(
            file, "Current filter: model=%p node=%ld poly=%p entries=%ld "
            "splitCalls=%ld addPointCalls=%ld nearestVertexCalls=%ld",
            g_currentFilterModel, g_currentFilterNode, g_currentFilterPoly,
            InterlockedCompareExchange(&g_filterEntries, 0, 0),
            InterlockedCompareExchange(&g_splitCalls, 0, 0),
            InterlockedCompareExchange(&g_addPointCalls, 0, 0),
            InterlockedCompareExchange(&g_nearestVertexCalls, 0, 0));
        const InvariantCode invariant = static_cast<InvariantCode>(
            InterlockedCompareExchange(&g_lastInvariant, 0, 0));
        CrashDiagnostics::WriteLine(
            file, "LastInvariant=%ld (%s) detailA=%ld detailB=%ld",
            static_cast<LONG>(invariant), InvariantName(invariant),
            InterlockedCompareExchange(&g_lastInvariantA, 0, 0),
            InterlockedCompareExchange(&g_lastInvariantB, 0, 0));
        WritePoly(file, g_currentPolySnapshot);

        CrashDiagnostics::WriteLine(file, "Flight recorder (oldest to newest):");
        const LONG newest = InterlockedCompareExchange(&g_eventSequence, 0, 0);
        const LONG oldest = (std::max<LONG>)(
            1, newest - kEventCapacity + 1);
        for (LONG sequence = oldest; sequence <= newest; ++sequence)
        {
            const DiagnosticEvent& event =
                g_events[(sequence - 1) % kEventCapacity];
            if (InterlockedCompareExchange(
                    const_cast<volatile LONG*>(&event.committedSequence), 0, 0)
                != sequence)
                continue;
            const double milliseconds = g_qpcFrequency.QuadPart > 0
                ? static_cast<double>(event.qpc - g_stageStartedQpc) * 1000.0
                    / static_cast<double>(g_qpcFrequency.QuadPart)
                : 0.0;
            CrashDiagnostics::WriteLine(
                file, "  #%ld %+10.3fms tid=%lu %-18s "
                "a=%p b=%p c=%p d=%p x=%ld y=%ld",
                event.sequence, milliseconds, event.threadId,
                EventName(event.type), reinterpret_cast<void*>(event.a),
                reinterpret_cast<void*>(event.b),
                reinterpret_cast<void*>(event.c),
                reinterpret_cast<void*>(event.d), event.x, event.y);
        }

        if (exceptionInfo && exceptionInfo->ContextRecord)
        {
            CrashDiagnostics::WriteLine(file, "Raw stack:");
            const DWORD* stack = reinterpret_cast<const DWORD*>(
                exceptionInfo->ContextRecord->Esp);
            for (int index = 0; index < 64; ++index)
            {
                DWORD value = 0;
                bool readable = false;
                __try
                {
                    value = stack[index];
                    readable = true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    readable = false;
                }
                if (!readable)
                    break;
                CrashDiagnostics::WriteLine(
                    file, "  ESP+0x%03X=%08lX", index * 4, value);
            }
        }

        bool dumpWritten = false;
        if (exceptionInfo && exceptionInfo->ExceptionRecord
            && exceptionInfo->ExceptionRecord->ExceptionCode
                != EXCEPTION_STACK_OVERFLOW)
        {
            dumpWritten = CrashDiagnostics::WriteMiniDump(
                exceptionInfo, dumpPath);
        }
        CrashDiagnostics::WriteLine(
            file, "MiniDump=%s status=%s", dumpPath,
            dumpWritten ? "written" : "not written");
        FlushFileBuffers(file);
        CloseHandle(file);
    }

    void ReportInvariant(InvariantCode code, LONG detailA, LONG detailB)
    {
        InterlockedExchange(&g_lastInvariant, static_cast<LONG>(code));
        InterlockedExchange(&g_lastInvariantA, detailA);
        InterlockedExchange(&g_lastInvariantB, detailB);
        RecordEvent(EventType::InvariantFailure, 0, 0, 0, 0,
                    static_cast<LONG>(code), detailA);

        if (InterlockedCompareExchange(&g_invariantCaptured, 1, 0) == 0)
        {
            JournalLine(
                "INVARIANT FAILURE: %s detailA=%ld detailB=%ld actor=%s "
                "sourcePoly=%ld filterNode=%ld",
                InvariantName(code), detailA, detailB, g_currentActorName,
                g_currentBrushPoly, g_currentFilterNode);
            FlushFileBuffers(g_journal);
            WriteStateReport("BspInvariant", nullptr);
        }
    }

    InvariantCode ValidateFilterContext(void* model, int nodeIndex,
                                        LONG& detailA, LONG& detailB)
    {
        detailA = nodeIndex;
        detailB = 0;
        if (!model)
            return InvariantCode::NullModel;

        const ModelSnapshot snapshot = ReadModelSnapshot(model);
        if (!snapshot.readable || !IsPlausibleArray(snapshot.nodes))
            return InvariantCode::BadNodeArray;
        if (nodeIndex < 0 || nodeIndex >= snapshot.nodes.count)
        {
            detailB = snapshot.nodes.count;
            return InvariantCode::BadNodeIndex;
        }
        if (!IsPlausibleArray(snapshot.surfs))
            return InvariantCode::BadSurfaceArray;
        if (!IsPlausibleArray(snapshot.verts))
            return InvariantCode::BadVertArray;
        if (!IsPlausibleArray(snapshot.points))
            return InvariantCode::BadPointArray;
        if (snapshot.points.count > kUnsignedPointIndexMax + 1)
        {
            detailA = snapshot.points.count;
            detailB = kUnsignedPointIndexMax + 1;
            return InvariantCode::PointIndexCapacityExceeded;
        }
        if (!IsPlausibleArray(snapshot.vectors))
            return InvariantCode::BadVectorArray;

        __try
        {
            const char* node = static_cast<const char*>(snapshot.nodes.data)
                + static_cast<size_t>(nodeIndex) * kNodeStride;
            const int surfaceIndex = *reinterpret_cast<const int*>(
                node + kNodeSurfOffset);
            const int vertPoolIndex = *reinterpret_cast<const int*>(
                node + kNodeVertPoolOffset);
            const int front = *reinterpret_cast<const int*>(
                node + kNodeFrontOffset);
            const int back = *reinterpret_cast<const int*>(
                node + kNodeBackOffset);
            const int plane = *reinterpret_cast<const int*>(
                node + kNodePlaneOffset);
            if (surfaceIndex < 0 || surfaceIndex >= snapshot.surfs.count)
            {
                detailA = surfaceIndex;
                detailB = snapshot.surfs.count;
                return InvariantCode::BadSurfaceIndex;
            }
            if (vertPoolIndex < 0 || vertPoolIndex >= snapshot.verts.count)
            {
                detailA = vertPoolIndex;
                detailB = snapshot.verts.count;
                return InvariantCode::BadVertPoolIndex;
            }
            if ((front < -1 || front >= snapshot.nodes.count)
                || (back < -1 || back >= snapshot.nodes.count)
                || (plane < -1 || plane >= snapshot.nodes.count))
            {
                detailA = front;
                detailB = back;
                return InvariantCode::BadChildIndex;
            }

            const char* surface = static_cast<const char*>(snapshot.surfs.data)
                + static_cast<size_t>(surfaceIndex) * kSurfStride;
            const int normalIndex = *reinterpret_cast<const uint16_t*>(
                surface + kSurfNormalOffset);
            if (normalIndex < 0 || normalIndex >= snapshot.vectors.count)
            {
                detailA = normalIndex;
                detailB = snapshot.vectors.count;
                return InvariantCode::BadNormalIndex;
            }

            const char* vert = static_cast<const char*>(snapshot.verts.data)
                + static_cast<size_t>(vertPoolIndex) * kVertStride;
            const int pointIndex = *reinterpret_cast<const uint16_t*>(vert);
            if (pointIndex >= snapshot.points.count)
            {
                detailA = pointIndex;
                detailB = snapshot.points.count;
                return InvariantCode::BadPointIndex;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return InvariantCode::BadNodeArray;
        }
        return InvariantCode::None;
    }

    void CaptureLevelContext()
    {
        g_level = nullptr;
        g_levelModel = nullptr;
        g_actorCount = 0;
        strncpy_s(g_mapName, "<unknown>", _TRUNCATE);
        __try
        {
            void* editor = *reinterpret_cast<void**>(kGEditor);
            g_level = editor ? *reinterpret_cast<void**>(
                static_cast<char*>(editor) + kEditorLevelOffset) : nullptr;
            if (g_level)
            {
                g_levelModel = *reinterpret_cast<void**>(
                    static_cast<char*>(g_level) + kLevelModelOffset);
                g_actorCount = *reinterpret_cast<int*>(
                    static_cast<char*>(g_level) + kLevelActorsCountOffset);
                if (!CopyObjectName(g_level, g_mapName, std::size(g_mapName)))
                    strncpy_s(g_mapName, "<unreadable>", _TRUNCATE);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_level = nullptr;
            g_levelModel = nullptr;
            g_actorCount = 0;
            strncpy_s(g_mapName, "<unreadable>", _TRUNCATE);
        }
    }

    BOOL CALLBACK FindEditorWindow(HWND window, LPARAM parameter)
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId != GetCurrentProcessId() || !IsWindowVisible(window))
            return TRUE;

        char title[512] = {};
        if (GetWindowTextA(window, title, static_cast<int>(std::size(title))) <= 0)
            return TRUE;
        if (!strstr(title, "Chaos Theory Editor") && !strstr(title, ".sdc]"))
            return TRUE;

        char* destination = reinterpret_cast<char*>(parameter);
        strncpy_s(destination, 512, title, _TRUNCATE);
        return FALSE;
    }

    void CaptureEditorWindowTitle()
    {
        strncpy_s(g_editorWindowTitle, "<unavailable>", _TRUNCATE);
        EnumWindows(FindEditorWindow,
                    reinterpret_cast<LPARAM>(g_editorWindowTitle));
    }

    void ResetHotContext()
    {
        g_currentActor = nullptr;
        g_currentBrush = nullptr;
        g_currentModel = nullptr;
        g_currentCsgOper = -1;
        g_currentPolyFlags = 0;
        g_currentBrushPoly = -1;
        strncpy_s(g_currentActorName, "<none>", _TRUNCATE);
        strncpy_s(g_currentActorClass, "<none>", _TRUNCATE);
        ZeroMemory(&g_currentBrushAnalysis, sizeof(g_currentBrushAnalysis));
        g_currentFilterModel = nullptr;
        g_currentFilterNode = -1;
        g_currentFilterPoly = nullptr;
        ZeroMemory(&g_currentPolySnapshot, sizeof(g_currentPolySnapshot));
        g_currentPolySnapshot.vertexCount = -1;
        InterlockedExchange(&g_filterEntries, 0);
        InterlockedExchange(&g_splitCalls, 0);
        InterlockedExchange(&g_addPointCalls, 0);
        InterlockedExchange(&g_nearestVertexCalls, 0);
        InterlockedExchange(&g_lastInvariant,
                            static_cast<LONG>(InvariantCode::None));
        InterlockedExchange(&g_lastInvariantA, 0);
        InterlockedExchange(&g_lastInvariantB, 0);
        InterlockedExchange(&g_invariantCaptured, 0);
        InterlockedExchange(&g_pointRangeLogged, 0);
    }

    void BeginStage(BuildStage stage, bool adHoc)
    {
        CaptureLevelContext();
        CaptureEditorWindowTitle();
        ResetHotContext();
        ZeroMemory(g_events, sizeof(g_events));
        InterlockedExchange(&g_eventSequence, 0);
        g_buildThreadId = GetCurrentThreadId();
        LARGE_INTEGER qpc = {};
        QueryPerformanceCounter(&qpc);
        g_stageStartedQpc = qpc.QuadPart;
        InterlockedIncrement(&g_buildSerial);
        InterlockedExchange(&g_stage, static_cast<LONG>(stage));
        InterlockedExchange(&g_adHocStage, adHoc ? 1 : 0);
        InterlockedExchange(&g_crashCaptured, 0);
        InterlockedExchange(&g_buildActive, 1);
        RecordEvent(EventType::StageBegin, reinterpret_cast<uintptr_t>(g_level),
                    reinterpret_cast<uintptr_t>(g_levelModel), 0, 0,
                    static_cast<LONG>(stage), g_actorCount);
        const ModelSnapshot model = ReadModelSnapshot(g_levelModel);
        JournalLine(
            "BEGIN serial=%ld stage=%s map=%s title=\"%s\" level=%p actors=%d model=%p "
            "nodes=%d verts=%d vectors=%d points=%d surfs=%d",
            InterlockedCompareExchange(&g_buildSerial, 0, 0), StageName(stage),
            g_mapName, g_editorWindowTitle, g_level, g_actorCount, g_levelModel,
            model.nodes.count, model.verts.count, model.vectors.count,
            model.points.count, model.surfs.count);
        FlushFileBuffers(g_journal);
    }

    void EndStage(BuildStage stage)
    {
        if (InterlockedCompareExchange(&g_buildActive, 0, 0) == 0)
            return;
        const BuildStage current = static_cast<BuildStage>(
            InterlockedCompareExchange(&g_stage, 0, 0));
        if (current != stage && stage != BuildStage::CsgOperation)
            return;

        LARGE_INTEGER qpc = {};
        QueryPerformanceCounter(&qpc);
        const double elapsedMs = g_qpcFrequency.QuadPart > 0
            ? static_cast<double>(qpc.QuadPart - g_stageStartedQpc) * 1000.0
                / static_cast<double>(g_qpcFrequency.QuadPart)
            : 0.0;
        const ModelSnapshot model = ReadModelSnapshot(g_levelModel);
        JournalLine(
            "END serial=%ld stage=%s elapsedMs=%.3f nodes=%d verts=%d "
            "vectors=%d points=%d surfs=%d filterEntries=%ld splits=%ld "
            "addPoints=%ld nearest=%ld",
            InterlockedCompareExchange(&g_buildSerial, 0, 0),
            StageName(current), elapsedMs, model.nodes.count,
            model.verts.count, model.vectors.count, model.points.count,
            model.surfs.count,
            InterlockedCompareExchange(&g_filterEntries, 0, 0),
            InterlockedCompareExchange(&g_splitCalls, 0, 0),
            InterlockedCompareExchange(&g_addPointCalls, 0, 0),
            InterlockedCompareExchange(&g_nearestVertexCalls, 0, 0));
        RecordEvent(EventType::StageEnd, reinterpret_cast<uintptr_t>(g_levelModel),
                    0, 0, 0, static_cast<LONG>(current), 0);
        FlushFileBuffers(g_journal);
        InterlockedExchange(&g_buildActive, 0);
        InterlockedExchange(&g_stage, static_cast<LONG>(BuildStage::None));
        InterlockedExchange(&g_adHocStage, 0);
    }

    void __cdecl BeginStageThunk(int stage)
    {
        BeginStage(static_cast<BuildStage>(stage), false);
    }

    void __cdecl EndStageThunk(int stage)
    {
        EndStage(static_cast<BuildStage>(stage));
    }

    void __cdecl RecordBrushStart(void* actor, void* model,
                                  uint32_t polyFlags, int csgOper)
    {
        if (InterlockedCompareExchange(&g_buildActive, 0, 0) == 0)
            BeginStage(BuildStage::CsgOperation, true);

        g_currentActor = actor;
        g_currentModel = model;
        g_currentCsgOper = csgOper;
        g_currentPolyFlags = polyFlags;
        g_currentBrushPoly = -1;
        strncpy_s(g_currentActorName, "<unreadable>", _TRUNCATE);
        strncpy_s(g_currentActorClass, "<unreadable>", _TRUNCATE);
        CopyObjectName(actor, g_currentActorName, std::size(g_currentActorName));
        CopyObjectClassName(actor, g_currentActorClass,
                            std::size(g_currentActorClass));
        __try
        {
            g_currentBrush = actor ? *reinterpret_cast<void**>(
                static_cast<char*>(actor) + kActorBrushOffset) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_currentBrush = nullptr;
        }
        g_currentBrushAnalysis = AnalyzeBrush(actor);

        const ModelSnapshot modelSnapshot = ReadModelSnapshot(model);
        if (modelSnapshot.readable
            && modelSnapshot.points.count > kSignedPointIndexMax
            && InterlockedCompareExchange(&g_pointRangeLogged, 1, 0) == 0)
        {
            JournalLine(
                "POINT INDEX RANGE: points=%d exceeded stock signed limit=%d "
                "unsignedPatch=%ld hardCapacity=%d",
                modelSnapshot.points.count, kSignedPointIndexMax,
                InterlockedCompareExchange(
                    &g_unsignedPointIndexPatchInstalled, 0, 0),
                kUnsignedPointIndexMax + 1);
            FlushFileBuffers(g_journal);
        }
        if (modelSnapshot.readable
            && modelSnapshot.points.count > kUnsignedPointIndexMax + 1)
        {
            ReportInvariant(InvariantCode::PointIndexCapacityExceeded,
                            modelSnapshot.points.count,
                            kUnsignedPointIndexMax + 1);
        }
        RecordEvent(EventType::BrushBegin,
                    reinterpret_cast<uintptr_t>(actor),
                    reinterpret_cast<uintptr_t>(g_currentBrush),
                    reinterpret_cast<uintptr_t>(model), polyFlags,
                    csgOper, g_currentBrushAnalysis.polygonCount);

        const BrushAnalysis& a = g_currentBrushAnalysis;
        JournalLine(
            "BRUSH actor=%s class=%s actorPtr=%p brush=%p model=%p op=%d "
            "flags=0x%08lX polys=%d preflight={invalidVerts:%d "
            "nonFinite:%d zeroArea:%d duplicates:%d nonPlanar:%d concave:%d "
            "badLinks:%d} first={invalidVerts:%d nonFinite:%d zeroArea:%d "
            "duplicates:%d nonPlanar:%d concave:%d badLink:%d}",
            g_currentActorName, g_currentActorClass, actor, g_currentBrush,
            model, csgOper, static_cast<unsigned long>(polyFlags),
            a.polygonCount, a.invalidVertexCounts, a.nonFinite, a.zeroArea,
            a.duplicateVertices, a.nonPlanar, a.concave, a.badLinks,
            a.firstInvalidVertexCount, a.firstNonFinite, a.firstZeroArea,
            a.firstDuplicate, a.firstNonPlanar, a.firstConcave,
            a.firstBadLink);
        if (!a.readable || a.invalidVertexCounts || a.nonFinite || a.zeroArea
            || a.duplicateVertices || a.nonPlanar || a.concave || a.badLinks)
            FlushFileBuffers(g_journal);
    }

    void __cdecl RecordBrushPolygon(void* actor, void* model, int polyIndex)
    {
        g_currentActor = actor;
        g_currentModel = model;
        g_currentBrushPoly = polyIndex;
        void* poly = nullptr;
        __try
        {
            void* brush = actor ? *reinterpret_cast<void**>(
                static_cast<char*>(actor) + kActorBrushOffset) : nullptr;
            void* polys = brush ? *reinterpret_cast<void**>(
                static_cast<char*>(brush) + kBrushPolysOffset) : nullptr;
            void* data = polys ? *reinterpret_cast<void**>(
                static_cast<char*>(polys) + kPolysDataOffset) : nullptr;
            const int count = polys ? *reinterpret_cast<int*>(
                static_cast<char*>(polys) + kPolysCountOffset) : 0;
            if (data && polyIndex >= 0 && polyIndex < count)
                poly = static_cast<char*>(data)
                    + static_cast<size_t>(polyIndex) * kFPolyStride;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            poly = nullptr;
        }
        if (poly)
            g_currentPolySnapshot = ReadPolySnapshot(poly);
        RecordEvent(EventType::BrushPolygon,
                    reinterpret_cast<uintptr_t>(actor),
                    reinterpret_cast<uintptr_t>(model),
                    reinterpret_cast<uintptr_t>(poly), 0, polyIndex, 0);
    }

    void __cdecl RecordFilter(void* model, int nodeIndex, void* poly)
    {
        if (InterlockedCompareExchange(&g_buildActive, 0, 0) == 0)
            return;
        g_currentFilterModel = model;
        g_currentFilterNode = nodeIndex;
        g_currentFilterPoly = poly;
        InterlockedIncrement(&g_filterEntries);
        g_currentPolySnapshot = ReadPolySnapshot(poly);
        RecordEvent(EventType::Filter, reinterpret_cast<uintptr_t>(model),
                    reinterpret_cast<uintptr_t>(poly), 0, 0, nodeIndex,
                    g_currentPolySnapshot.vertexCount);

        if (!g_currentPolySnapshot.readable)
        {
            ReportInvariant(InvariantCode::BadFPolyPointer, nodeIndex, 0);
            return;
        }
        if (g_currentPolySnapshot.vertexCount < 3
            || g_currentPolySnapshot.vertexCount > kMaxFPolyVertices)
        {
            ReportInvariant(InvariantCode::BadFPolyVertexCount,
                            g_currentPolySnapshot.vertexCount, nodeIndex);
            return;
        }
        if (!g_currentPolySnapshot.finite)
        {
            ReportInvariant(InvariantCode::NonFiniteFPoly, nodeIndex,
                            g_currentPolySnapshot.vertexCount);
            return;
        }

        LONG detailA = 0;
        LONG detailB = 0;
        const InvariantCode invariant = ValidateFilterContext(
            model, nodeIndex, detailA, detailB);
        if (invariant != InvariantCode::None)
            ReportInvariant(invariant, detailA, detailB);
    }

    void __cdecl RecordSplit(void* poly, void* planeBase, void* planeNormal)
    {
        if (InterlockedCompareExchange(&g_buildActive, 0, 0) == 0)
            return;
        InterlockedIncrement(&g_splitCalls);
        g_currentFilterPoly = poly;
        g_currentPolySnapshot = ReadPolySnapshot(poly);
        RecordEvent(EventType::Split, reinterpret_cast<uintptr_t>(poly),
                    reinterpret_cast<uintptr_t>(planeBase),
                    reinterpret_cast<uintptr_t>(planeNormal), 0,
                    g_currentPolySnapshot.vertexCount, 0);
        if (!g_currentPolySnapshot.readable)
            ReportInvariant(InvariantCode::BadFPolyPointer, 0, 0);
        else if (g_currentPolySnapshot.vertexCount < 3
                || g_currentPolySnapshot.vertexCount > kMaxFPolyVertices)
            ReportInvariant(InvariantCode::BadFPolyVertexCount,
                            g_currentPolySnapshot.vertexCount, 0);
        else if (!g_currentPolySnapshot.finite)
            ReportInvariant(InvariantCode::NonFiniteFPoly,
                            g_currentPolySnapshot.vertexCount, 0);
    }

    void __cdecl RecordAddPoint(void* model, void* point, int exact)
    {
        if (InterlockedCompareExchange(&g_buildActive, 0, 0) == 0)
            return;
        InterlockedIncrement(&g_addPointCalls);
        RecordEvent(EventType::AddPoint,
                    reinterpret_cast<uintptr_t>(model),
                    reinterpret_cast<uintptr_t>(point), 0, 0, exact, 0);
    }

    void __cdecl RecordNearestVertex(void* model, void* point, float threshold)
    {
        if (InterlockedCompareExchange(&g_buildActive, 0, 0) == 0)
            return;
        InterlockedIncrement(&g_nearestVertexCalls);
        uint32_t thresholdBits = 0;
        memcpy(&thresholdBits, &threshold, sizeof(thresholdBits));
        RecordEvent(EventType::NearestVertex,
                    reinterpret_cast<uintptr_t>(model),
                    reinterpret_cast<uintptr_t>(point), thresholdBits, 0, 0, 0);
    }

    void __cdecl RecordBrushEnd()
    {
        if (InterlockedCompareExchange(&g_buildActive, 0, 0) == 0)
            return;
        RecordEvent(EventType::BrushEnd,
                    reinterpret_cast<uintptr_t>(g_currentActor),
                    reinterpret_cast<uintptr_t>(g_currentModel), 0, 0,
                    g_currentBrushPoly, 0);
        if (InterlockedCompareExchange(&g_adHocStage, 0, 0) != 0)
            EndStage(BuildStage::CsgOperation);
    }

    __declspec(naked) void GeometryBeginHook()
    {
        static int resume = 0x10E10C39;
        __asm
        {
            pushad
            push 1
            call BeginStageThunk
            add  esp, 4
            popad
            mov  ecx, dword ptr ds:[1165DFA0h]
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void GeometryEndHook()
    {
        static int resume = 0x10E10C51;
        __asm
        {
            pushad
            push 1
            call EndStageThunk
            add  esp, 4
            popad
            mov  ecx, dword ptr [ebp-0Ch]
            pop  edi
            pop  esi
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void BspBeginHook()
    {
        static int resume = 0x10E10CF9;
        __asm
        {
            pushad
            push 2
            call BeginStageThunk
            add  esp, 4
            popad
            mov  ecx, dword ptr ds:[1165DFA0h]
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void BspEndHook()
    {
        static int resume = 0x10E10D11;
        __asm
        {
            pushad
            push 2
            call EndStageThunk
            add  esp, 4
            popad
            mov  ecx, dword ptr [ebp-0Ch]
            pop  edi
            pop  esi
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void LightingBeginHook()
    {
        static int resume = 0x10E10DB9;
        __asm
        {
            pushad
            push 3
            call BeginStageThunk
            add  esp, 4
            popad
            mov  ecx, dword ptr ds:[1165DFA0h]
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void LightingEndHook()
    {
        static int resume = 0x10E10DD1;
        __asm
        {
            pushad
            push 3
            call EndStageThunk
            add  esp, 4
            popad
            mov  ecx, dword ptr [ebp-0Ch]
            pop  edi
            pop  esi
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void BrushStartHook()
    {
        static int resume = 0x110860F5;
        __asm
        {
            pushad
            mov  eax, dword ptr [esp+36]
            mov  edx, dword ptr [esp+40]
            mov  ecx, dword ptr [esp+44]
            mov  ebx, dword ptr [esp+48]
            push ebx
            push ecx
            push edx
            push eax
            call RecordBrushStart
            add  esp, 16
            popad
            push ebp
            mov  ebp, esp
            push -1
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void BrushPolygonHook()
    {
        static int resume = 0x11086403;
        __asm
        {
            pushad
            push edi
            push dword ptr [ebp+0Ch]
            push dword ptr [ebp+08h]
            call RecordBrushPolygon
            add  esp, 12
            popad
            mov  esi, edi
            imul esi, esi, 14Ch
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void FilterEntryHook()
    {
        static int resume = 0x11083195;
        __asm
        {
            pushad
            mov  eax, dword ptr [esp+40]
            mov  edx, dword ptr [esp+44]
            mov  ecx, dword ptr [esp+48]
            push ecx
            push edx
            push eax
            call RecordFilter
            add  esp, 12
            popad
            push ebp
            mov  ebp, esp
            push -1
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void SplitEntryHook()
    {
        static int resume = 0x110BFFB5;
        __asm
        {
            pushad
            mov  eax, dword ptr [esp+24]
            mov  edx, dword ptr [esp+36]
            mov  ecx, dword ptr [esp+40]
            push ecx
            push edx
            push eax
            call RecordSplit
            add  esp, 12
            popad
            push ebp
            mov  ebp, esp
            push -1
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void AddPointEntryHook()
    {
        static int resume = 0x110844D5;
        __asm
        {
            pushad
            mov  eax, dword ptr [esp+36]
            mov  edx, dword ptr [esp+40]
            mov  ecx, dword ptr [esp+44]
            push ecx
            push edx
            push eax
            call RecordAddPoint
            add  esp, 12
            popad
            push ebp
            mov  ebp, esp
            push -1
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void NearestVertexEntryHook()
    {
        static int resume = 0x11192FC5;
        __asm
        {
            pushad
            mov  eax, dword ptr [esp+24]
            mov  edx, dword ptr [esp+36]
            mov  ecx, dword ptr [esp+44]
            push ecx
            push edx
            push eax
            call RecordNearestVertex
            add  esp, 12
            popad
            push ebp
            mov  ebp, esp
            push -1
            jmp  dword ptr [resume]
        }
    }

    __declspec(naked) void BrushEndHook()
    {
        static int resume = 0x110867B1;
        __asm
        {
            pushad
            call RecordBrushEnd
            popad
            mov  eax, dword ptr ds:[117A8238h]
            jmp  dword ptr [resume]
        }
    }

    bool VerifyEditorBuild()
    {
        HMODULE executable = GetModuleHandleA(nullptr);
        if (reinterpret_cast<uintptr_t>(executable) != kExpectedImageBase)
            return false;
        __try
        {
            const IMAGE_DOS_HEADER* dos =
                reinterpret_cast<const IMAGE_DOS_HEADER*>(executable);
            const IMAGE_NT_HEADERS32* nt =
                reinterpret_cast<const IMAGE_NT_HEADERS32*>(
                    reinterpret_cast<const char*>(executable) + dos->e_lfanew);
            return dos->e_magic == IMAGE_DOS_SIGNATURE
                && nt->Signature == IMAGE_NT_SIGNATURE
                && nt->FileHeader.TimeDateStamp == kExpectedImageTimestamp
                && nt->OptionalHeader.SizeOfImage == kExpectedImageSize;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SafeBytesMatch(uintptr_t address, const unsigned char* expected,
                        size_t length)
    {
        __try
        {
            return memcmp(reinterpret_cast<const void*>(address),
                          expected, length) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool InstallCheckedJump(uintptr_t address, const unsigned char* expected,
                            size_t length, void (*hook)(), const char* name)
    {
        if (!expected || !hook || length < 5 || length > 16)
            return false;

        if (!SafeBytesMatch(address, expected, length))
        {
            JournalLine("HOOK SKIPPED name=%s address=%p reason=byte mismatch",
                        name, reinterpret_cast<void*>(address));
            Logger::log(std::string("BspDiagnostics: hook byte mismatch: ") + name);
            return false;
        }

        unsigned char patch[16] = {};
        memset(patch, 0x90, length);
        patch[0] = 0xE9;
        const uintptr_t relative = reinterpret_cast<uintptr_t>(hook)
            - address - 5;
        *reinterpret_cast<uint32_t*>(patch + 1) =
            static_cast<uint32_t>(relative);
        if (!MemoryWriter::WriteBytes(address, patch, length))
        {
            JournalLine("HOOK FAILED name=%s address=%p reason=write failed",
                        name, reinterpret_cast<void*>(address));
            return false;
        }
        JournalLine("HOOK INSTALLED name=%s address=%p length=%u", name,
                    reinterpret_cast<void*>(address),
                    static_cast<unsigned>(length));
        return true;
    }

    bool InstallUnsignedPointIndexPatchSite(
        uintptr_t address, unsigned char operand, const char* name)
    {
        const unsigned char expected[] = {
            0x0F, 0xBF, operand // movsx r32, word ptr [...]
        };
        const unsigned char replacement[] = {
            0x0F, 0xB7, operand // movzx r32, word ptr [...]
        };

        if (SafeBytesMatch(address, replacement, sizeof(replacement)))
        {
            JournalLine(
                "COMPAT PATCH ALREADY INSTALLED name=%s address=%p",
                name, reinterpret_cast<void*>(address));
            return true;
        }
        if (!SafeBytesMatch(address, expected, sizeof(expected)))
        {
            JournalLine(
                "COMPAT PATCH SKIPPED name=%s address=%p reason=byte mismatch",
                name, reinterpret_cast<void*>(address));
            Logger::log(std::string("BspDiagnostics: point-index byte mismatch: ")
                        + name);
            return false;
        }
        if (!MemoryWriter::WriteBytes(address, replacement,
                                      sizeof(replacement)))
        {
            JournalLine(
                "COMPAT PATCH FAILED name=%s address=%p reason=write failed",
                name, reinterpret_cast<void*>(address));
            return false;
        }

        JournalLine(
            "COMPAT PATCH INSTALLED name=%s address=%p",
            name, reinterpret_cast<void*>(address));
        return true;
    }

    bool InstallPointIndexSentinelPatch()
    {
        // This compact/remap loop originally skipped every value with its sign
        // bit set. Once pVertex is interpreted as unsigned, the following
        // count check already rejects the real 0xFFFF sentinel and any other
        // value outside the point array, while allowing 0x8000..0xFFFE.
        constexpr uintptr_t address = 0x11083FD6;
        const unsigned char expected[] = {
            0x66, 0x85, 0xC0, // test ax, ax
            0x7C, 0x14        // jl skip
        };
        const unsigned char replacement[] = {
            0x90, 0x90, 0x90, 0x90, 0x90
        };
        const char* name = "bspRemapPointIndices.useCountForSentinel";

        if (SafeBytesMatch(address, replacement, sizeof(replacement)))
        {
            JournalLine(
                "COMPAT PATCH ALREADY INSTALLED name=%s address=%p",
                name, reinterpret_cast<void*>(address));
            return true;
        }
        if (!SafeBytesMatch(address, expected, sizeof(expected)))
        {
            JournalLine(
                "COMPAT PATCH SKIPPED name=%s address=%p reason=byte mismatch",
                name, reinterpret_cast<void*>(address));
            Logger::log(std::string("BspDiagnostics: point-index sentinel byte mismatch: ")
                        + name);
            return false;
        }
        if (!MemoryWriter::WriteBytes(address, replacement,
                                      sizeof(replacement)))
        {
            JournalLine(
                "COMPAT PATCH FAILED name=%s address=%p reason=write failed",
                name, reinterpret_cast<void*>(address));
            return false;
        }

        JournalLine(
            "COMPAT PATCH INSTALLED name=%s address=%p",
            name, reinterpret_cast<void*>(address));
        return true;
    }

    bool InstallUnsignedPointIndexPatches()
    {
        struct PatchSite
        {
            uintptr_t address;
            unsigned char operand;
            const char* name;
        };

        // Every entry below was verified in the supported executable by
        // disassembling the surrounding UModel access: the instruction reads
        // the first 16-bit field of an 8-byte FVert and uses it either to read
        // UModel::Points or to index a point-sized remap/adjacency table.
        const PatchSite sites[] = {
            { 0x10ED0A13, 0x04, "ModelPointLookup.10ED0A13" },
            { 0x10ED0A3A, 0x04, "ModelPointLookup.10ED0A3A" },
            { 0x10ED0CDE, 0x04, "ModelPointLookup.10ED0CDE" },
            { 0x10ED0D05, 0x04, "ModelPointLookup.10ED0D05" },
            { 0x1106693B, 0x04, "ModelPointLookup.1106693B" },
            { 0x110697AB, 0x01, "ModelPointLookup.110697AB" },
            { 0x1106980D, 0x0C, "ModelPointLookup.1106980D" },
            { 0x11083245, 0x04, "FilterEdPoly.splitPlanePoint" },
            { 0x1108378B, 0x04, "FilterEdPoly.copyNodePoint" },
            { 0x11083C84, 0x1C, "bspAddNode.pointAdjacencyBuild" },
            { 0x11083E0F, 0x04, "bspAddNode.pointAdjacencyLookup" },
            { 0x11083FE1, 0xC0, "bspRemapPointIndices.pointIndex" },
            { 0x110846A6, 0x14, "bspAddPoint.comparePointIndex" },
            { 0x110848D8, 0x3C, "bspAddNode.findPointIndex" },
            { 0x1108491C, 0x0C, "bspAddNode.previousPoint" },
            { 0x1108492F, 0x0C, "bspAddNode.currentPoint" },
            { 0x11084C75, 0x14, "bspAddNode.edgePointA" },
            { 0x11084C8F, 0x04, "bspAddNode.edgePointB" },
            { 0x11084CCE, 0x0C, "bspAddNode.insertPointA" },
            { 0x11084CEA, 0x14, "bspAddNode.insertPointB" },
            { 0x11084DD5, 0x08, "bspAddNode.linkPointA" },
            { 0x11084DFB, 0x0C, "bspAddNode.linkPointB" },
            { 0x11086C35, 0x0F, "bspBrushCSG.clearPointMap" },
            { 0x11086E55, 0x3A, "bspBrushCSG.applyPointMap" },
            { 0x1108781D, 0x54, "bspNodeVertex.previousPoint" },
            { 0x11088BFB, 0x3E, "bspOptimization.markPoint" },
            { 0x11088C66, 0x31, "bspOptimization.testPoint" },
            { 0x11088E2E, 0x04, "bspOptimization.planePoint" },
            { 0x11088E9C, 0x04, "bspOptimization.nodePoint" },
            { 0x110C15BC, 0x04, "ModelPointLookup.110C15BC" },
            { 0x110CFC1E, 0x44, "ModelPointLookup.110CFC1E" },
            { 0x110D173E, 0x04, "ModelPointLookup.110D173E" },
            { 0x11192F00, 0x13, "FindNearestVertex.pointIndex" },
            { 0x11194BB7, 0x04, "ModelPointLookup.11194BB7" },
            { 0x111A04E8, 0x04, "ModelPointLookup.111A04E8" },
            { 0x111BA8AA, 0x14, "ModelPointLookup.111BA8AA" }
        };

        int installed = 0;
        for (const PatchSite& site : sites)
        {
            installed += InstallUnsignedPointIndexPatchSite(
                site.address, site.operand, site.name);
        }
        installed += InstallPointIndexSentinelPatch();

        const int requested = static_cast<int>(std::size(sites)) + 1;
        const bool complete = installed == requested;
        InterlockedExchange(&g_unsignedPointIndexPatchInstalled,
                            complete ? 1 : 0);
        JournalLine(
            "COMPAT PATCH SUMMARY unsignedPointIndexSites=%d/%d "
            "stockMax=%d patchedMax=%d",
            installed, requested, kSignedPointIndexMax,
            kUnsignedPointIndexMax);
        return complete;
    }

    void InstallHooks()
    {
        const unsigned char geometryBegin[] =
            { 0x8B, 0x0D, 0xA0, 0xDF, 0x65, 0x11 };
        const unsigned char stageEnd[] =
            { 0x8B, 0x4D, 0xF4, 0x5F, 0x5E };
        const unsigned char bspBegin[] =
            { 0x8B, 0x0D, 0xA0, 0xDF, 0x65, 0x11 };
        const unsigned char lightingBegin[] =
            { 0x8B, 0x0D, 0xA0, 0xDF, 0x65, 0x11 };
        const unsigned char functionPrologue[] =
            { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
        const unsigned char brushPoly[] =
            { 0x8B, 0xF7, 0x69, 0xF6, 0x4C, 0x01, 0x00, 0x00 };
        const unsigned char brushEnd[] =
            { 0xA1, 0x38, 0x82, 0x7A, 0x11 };

        int installed = 0;
        installed += InstallCheckedJump(
            0x10E10C33, geometryBegin, sizeof(geometryBegin),
            GeometryBeginHook, "BuildGeometry.begin");
        installed += InstallCheckedJump(
            0x10E10C4C, stageEnd, sizeof(stageEnd),
            GeometryEndHook, "BuildGeometry.end");
        installed += InstallCheckedJump(
            0x10E10CF3, bspBegin, sizeof(bspBegin),
            BspBeginHook, "BuildBsp.begin");
        installed += InstallCheckedJump(
            0x10E10D0C, stageEnd, sizeof(stageEnd),
            BspEndHook, "BuildBsp.end");
        installed += InstallCheckedJump(
            0x10E10DB3, lightingBegin, sizeof(lightingBegin),
            LightingBeginHook, "BuildLighting.begin");
        installed += InstallCheckedJump(
            0x10E10DCC, stageEnd, sizeof(stageEnd),
            LightingEndHook, "BuildLighting.end");
        installed += InstallCheckedJump(
            0x110860F0, functionPrologue, sizeof(functionPrologue),
            BrushStartHook, "bspBrushCSG.begin");
        installed += InstallCheckedJump(
            0x110863FB, brushPoly, sizeof(brushPoly),
            BrushPolygonHook, "bspBrushCSG.filter-poly");
        installed += InstallCheckedJump(
            0x11083190, functionPrologue, sizeof(functionPrologue),
            FilterEntryHook, "FilterEdPoly.begin");
        installed += InstallCheckedJump(
            0x110BFFB0, functionPrologue, sizeof(functionPrologue),
            SplitEntryHook, "FPoly.SplitWithPlane.begin");
        installed += InstallCheckedJump(
            0x110844D0, functionPrologue, sizeof(functionPrologue),
            AddPointEntryHook, "bspAddPoint.begin");
        installed += InstallCheckedJump(
            0x11192FC0, functionPrologue, sizeof(functionPrologue),
            NearestVertexEntryHook, "FindNearestVertex.begin");
        installed += InstallCheckedJump(
            0x110867AC, brushEnd, sizeof(brushEnd),
            BrushEndHook, "bspBrushCSG.end");
        JournalLine("HOOK SUMMARY installed=%d requested=13", installed);
        FlushFileBuffers(g_journal);
    }
}

void BspDiagnostics::Initialize(const std::wstring& dllPath)
{
    if (InterlockedCompareExchange(&g_initialized, 1, 0) != 0)
        return;

    QueryPerformanceFrequency(&g_qpcFrequency);
    if (CrashDiagnostics::MakeArtifactPath(
            "BspBuildSession", "log", g_journalPath,
            std::size(g_journalPath)))
    {
        g_journal = CreateFileA(
            g_journalPath, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    JournalLine("SCCT BSP/CSG diagnostic session started");
    JournalLine("DLL=%ls", dllPath.c_str());
    JournalLine("ExpectedEditor imageBase=%p timestamp=0x%08lX imageSize=0x%08lX",
                reinterpret_cast<void*>(kExpectedImageBase),
                kExpectedImageTimestamp, kExpectedImageSize);
    if (!VerifyEditorBuild())
    {
        JournalLine("HOOKS DISABLED: ChaosTheory_Editor.exe fingerprint mismatch");
        FlushFileBuffers(g_journal);
        Logger::log("BspDiagnostics: editor fingerprint mismatch; hooks disabled");
        return;
    }

    InstallUnsignedPointIndexPatches();
    InstallHooks();
    Logger::log(std::string("BspDiagnostics: build journal: ") + g_journalPath);
}

bool BspDiagnostics::LogException(EXCEPTION_POINTERS* exceptionInfo)
{
    if (!exceptionInfo || !exceptionInfo->ExceptionRecord
        || InterlockedCompareExchange(&g_buildActive, 0, 0) == 0)
        return false;

    const DWORD code = exceptionInfo->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION
        && code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED
        && code != EXCEPTION_DATATYPE_MISALIGNMENT
        && code != EXCEPTION_ILLEGAL_INSTRUCTION
        && code != EXCEPTION_IN_PAGE_ERROR
        && code != EXCEPTION_INT_DIVIDE_BY_ZERO
        && code != EXCEPTION_FLT_INVALID_OPERATION
        && code != EXCEPTION_STACK_OVERFLOW)
        return false;

    if (InterlockedCompareExchange(&g_crashCaptured, 1, 0) != 0)
        return false;

    WriteStateReport("BspCrash", exceptionInfo);
    return true;
}
