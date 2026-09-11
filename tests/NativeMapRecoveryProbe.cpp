// Injected only by run_native_map_recovery_test.ps1 into its isolated editor.
// Run engine commands on the editor's UI thread, never on the injection thread.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <share.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <fstream>

namespace {
HMODULE self;
HHOOK hook;
HWND frameWindow;
std::filesystem::path directory;
FILE* report;
constexpr UINT kRun = WM_APP + 0x319;
constexpr uintptr_t kEditor = 0x1165DFA0;
constexpr uintptr_t kWarn = 0x115BEFB0;
using ExecFn = int(__thiscall*)(void*, const char*, void*);
ExecFn originalExec;
using CsgFn = int(__thiscall*)(void*,void*,void*,unsigned int,int,int,int);
CsgFn originalCsg;
int maximumPointSlots;
int compactionCount;
bool startupFailed;

void Record(const char* stage, const char* detail = "") {
    fprintf(report, "%s %s\n", stage, detail);
    fflush(report);
}

const char* NameText(int index) {
    auto names = *reinterpret_cast<unsigned char***>(0x1169CFBC);
    int count = *reinterpret_cast<int*>(0x1169CFC0);
    return names && index >= 0 && index < count && names[index]
        ? reinterpret_cast<const char*>(names[index]+12) : "<invalid>";
}

const char* ObjectName(void* object) {
    return object ? NameText(*reinterpret_cast<int*>(static_cast<unsigned char*>(object)+0x20)) : "<null>";
}

unsigned char* FindProperty(unsigned char* actor, const char* name) {
    auto type = *reinterpret_cast<unsigned char**>(actor+0x24);
    for (auto property = *reinterpret_cast<unsigned char**>(type+0x58); property;
         property = *reinterpret_cast<unsigned char**>(property+0x40))
        if (!strcmp(ObjectName(property),name)) return property;
    return nullptr;
}

void DumpNativeBsp(const char* filename) {
    auto editor = *reinterpret_cast<unsigned char**>(kEditor);
    auto level = editor ? *reinterpret_cast<unsigned char**>(editor+0x130) : nullptr;
    auto model = level ? *reinterpret_cast<unsigned char**>(level+0x13C) : nullptr;
    if (!model) return;
    auto nodes = *reinterpret_cast<unsigned char**>(model+0x54);
    int count = *reinterpret_cast<int*>(model+0x58);
    std::ofstream output(directory/filename);
    output << std::setprecision(9) << "{\"rootOutside\":"
           << *reinterpret_cast<int*>(model+0x104) << ",\"nodes\":[";
    for (int i=0; i<count; ++i) {
        auto node=nodes+i*0x5C;
        auto plane=reinterpret_cast<float*>(node);
        if(i) output << ',';
        output << "{\"plane\":[" << plane[0] << ',' << plane[1] << ',' << plane[2] << ',' << plane[3]
               << "],\"back\":" << *reinterpret_cast<int*>(node+0x30)
               << ",\"front\":" << *reinterpret_cast<int*>(node+0x34)
               << ",\"coplanar\":" << *reinterpret_cast<int*>(node+0x38)
               << ",\"surface\":" << *reinterpret_cast<int*>(node+0x2C)
               << ",\"vertexCount\":" << static_cast<unsigned>(node[0x5A])
               << ",\"flags\":" << static_cast<unsigned>(node[0x5B]) << '}';
    }
    output << "]}";
}

int ReferencedPointCount(unsigned char* model) {
    int pointCount = *reinterpret_cast<int*>(model+0x88);
    int nodeCount = *reinterpret_cast<int*>(model+0x58);
    int poolCount = *reinterpret_cast<int*>(model+0x68);
    int surfaceCount = *reinterpret_cast<int*>(model+0x98);
    if (pointCount<0 || pointCount>=65535 || nodeCount<0 || poolCount<0 || surfaceCount<0) return -1;
    std::vector<unsigned char> used(pointCount);
    auto nodes = *reinterpret_cast<unsigned char**>(model+0x54);
    auto pool = *reinterpret_cast<unsigned char**>(model+0x64);
    auto surfaces = *reinterpret_cast<unsigned char**>(model+0x94);
    int result=0;
    for (int i=0; i<nodeCount; ++i) {
        auto node = nodes+i*0x5C;
        int start = *reinterpret_cast<int*>(node+0x28), count=node[0x5A];
        if (count && (start<0 || start>poolCount-count)) return -1;
        for (int j=0; j<count; ++j) {
            unsigned index = *reinterpret_cast<unsigned short*>(pool+(start+j)*8);
            if (index>=static_cast<unsigned>(pointCount)) return -1;
            if (!used[index]) {used[index]=1;++result;}
        }
    }
    for (int i=0; i<surfaceCount; ++i) {
        unsigned index = *reinterpret_cast<unsigned short*>(surfaces+i*0x2C+0x18);
        if (index>=static_cast<unsigned>(pointCount)) return -1;
        if (!used[index]) {used[index]=1;++result;}
    }
    return result;
}

int __fastcall CompactBeforeCsg(void* editor, void*, void* actor, void* rawModel,
                               unsigned int flags,int operation,int rebuildBounds,int mergePolys) {
    auto model = static_cast<unsigned char*>(rawModel);
    int before = *reinterpret_cast<int*>(model+0x88);
    if (before>maximumPointSlots) maximumPointSlots=before;
    if (before>=32768) {
        int referenced = ReferencedPointCount(model);
        if (referenced<0) {
            Record("FAIL","Test compaction rejected out-of-range point references before a CSG operation.");
            TerminateProcess(GetCurrentProcess(),41);
            return 0;
        }
        // Test-only wrapper runs before the existing CSG entry/diagnostics
        // hooks, after the previous CSG call has returned completely.
        using Refresh = void(__thiscall*)(void*,void*,int);
        reinterpret_cast<Refresh>(0x11086810)(editor,model,1);
        int after = *reinterpret_cast<int*>(model+0x88);
        ++compactionCount;
        fprintf(report,"point_compaction count=%d brush=%s before=%d referenced=%d after=%d max=%d nodes=%d\n",
            compactionCount,ObjectName(actor),before,referenced,after,maximumPointSlots,
            *reinterpret_cast<int*>(model+0x58));
        fflush(report);
    }
    return originalCsg(editor,actor,model,flags,operation,rebuildBounds,mergePolys);
}

void ObserveCsgCompaction() {
    auto editor = *reinterpret_cast<void**>(kEditor);
    auto slot = *reinterpret_cast<void***>(editor)+0x1DC/4;
    DWORD protection;
    if (!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protection)) {
        Record("FAIL","Could not install the isolated CSG compaction experiment."); return;
    }
    originalCsg = reinterpret_cast<CsgFn>(*slot);
    *slot = reinterpret_cast<void*>(&CompactBeforeCsg);
    DWORD ignored;
    VirtualProtect(slot,sizeof(void*),protection,&ignored);
    Record("test_compaction","Native bspRefresh enabled before CSG when point slots reach32768.");
}

unsigned char* FindStaticMeshActor(unsigned char* level, const char* name=nullptr) {
    auto data = *reinterpret_cast<unsigned char***>(level+0x2C);
    int count = *reinterpret_cast<int*>(level+0x30);
    for (int i=2; data && i<count; ++i)
        if (data[i] && !strcmp(ObjectName(*reinterpret_cast<void**>(data[i]+0x24)),"StaticMeshActor")
            && (!name || !strcmp(ObjectName(data[i]),name))) return data[i];
    return nullptr;
}

bool HasEditedTag(unsigned char* actor) {
    auto property = actor ? FindProperty(actor,"Tag") : nullptr;
    if (!property || strcmp(ObjectName(*reinterpret_cast<void**>(property+0x24)),"NameProperty")) return false;
    auto offset = *reinterpret_cast<unsigned int*>(property+0x3C);
    return !strcmp(NameText(*reinterpret_cast<int*>(actor+offset)),"RecoveredSourceEditProbe");
}

bool EditActorTag(unsigned char* level, std::string& name) {
    auto actor = FindStaticMeshActor(level);
    auto property = actor ? FindProperty(actor,"Tag") : nullptr;
    if (!property || strcmp(ObjectName(*reinterpret_cast<void**>(property+0x24)),"NameProperty")) return false;
    name = ObjectName(actor);
    auto offset = *reinterpret_cast<unsigned int*>(property+0x3C);
    // Same reflected ImportText virtual used by native property editing.
    using Import = const char*(__thiscall*)(void*,const char*,void*,unsigned int);
    auto importer = (*reinterpret_cast<void***>(property))[0x94/4];
    auto result = reinterpret_cast<Import>(importer)(property,"\"RecoveredSourceEditProbe\"",actor+offset,0);
    Record("actor_tag_edit",name.c_str());
    return result && HasEditedTag(actor);
}

bool ContainsActor(unsigned char* level, size_t offset, void* actor) {
    auto data = *reinterpret_cast<void***>(level+offset);
    int count = *reinterpret_cast<int*>(level+offset+4);
    for (int i=0; data && i<count; ++i) if (data[i] == actor) return true;
    return false;
}

void DescribeActorFields(unsigned char* actor) {
    auto type = *reinterpret_cast<unsigned char**>(actor+0x24);
    auto property = *reinterpret_cast<unsigned char**>(type+0x58);
    for (int depth=0; property && depth<4096; ++depth) {
        const char* name = ObjectName(property);
        if (!strcmp(name, "bDeleteMe") || !strcmp(name, "bHidden") || !strcmp(name, "Platform")
            || !strcmp(name, "m_Platform") || !strcmp(name, "Level") || !strcmp(name, "Owner")) {
            unsigned offset = *reinterpret_cast<unsigned int*>(property+0x3C);
            const char* propertyClass = ObjectName(*reinterpret_cast<void**>(property+0x24));
            if (!strcmp(propertyClass,"BoolProperty") || !strcmp(propertyClass,"ByteProperty")) {
                char value[128] = {};
                using ExportItem = void(__thiscall*)(void*, char*, const void*, const void*, unsigned int);
                auto exportItem = (*reinterpret_cast<void***>(property))[0x90/4];
                reinterpret_cast<ExportItem>(exportItem)(property,value,actor+offset,nullptr,0);
                fprintf(report,"actor_property name=%s class=%s offset=%x native_value=%s raw=%08x extra64=%08x\n",
                    name,propertyClass,offset,value,*reinterpret_cast<unsigned int*>(actor+offset),
                    *reinterpret_cast<unsigned int*>(property+0x64));
            } else {
                auto referenced = *reinterpret_cast<unsigned char**>(actor+offset);
                fprintf(report,"actor_property name=%s class=%s offset=%x pointer=%p referenced_name=%s referenced_class=%s\n",
                    name,propertyClass,offset,referenced,ObjectName(referenced),
                    referenced ? ObjectName(*reinterpret_cast<void**>(referenced+0x24)) : "<null>");
            }
            fflush(report);
        }
        property = *reinterpret_cast<unsigned char**>(property+0x40);
    }
}

int __fastcall ObserveExec(void* exec, void*, const char* command, void* output) {
    if (command && strstr(command,"MAP EXPORT")
        && (strstr(command,"Actors.t3d") || strstr(command,"Imported.t3d"))) {
        auto objects = *reinterpret_cast<unsigned char***>(0x11697B70);
        int count = *reinterpret_cast<int*>(0x11697B74);
        for (int i=0; objects && i<count; ++i) {
            auto object = objects[i];
            if (!object || (strcmp(ObjectName(object),"SpriteEmitter300")
                && strcmp(ObjectName(object),"SpriteEmitter617"))) continue;
            auto property = FindProperty(object,"SizeScaleRepeats");
            if (!property || strcmp(ObjectName(*reinterpret_cast<void**>(property+0x24)),"FloatProperty")) continue;
            auto offset = *reinterpret_cast<unsigned int*>(property+0x3C);
            fprintf(report,"particle_scalar stage=%s object=%s offset=%x value=%.12g bits=%08x outer=%s\n",
                strstr(command,"Actors.t3d") ? "cooked" : "imported",ObjectName(object),offset,
                *reinterpret_cast<float*>(object+offset),*reinterpret_cast<unsigned int*>(object+offset),
                ObjectName(*reinterpret_cast<void**>(object+0x18)));
            fflush(report);
        }
    }
    if (command && strstr(command, "MAP EXPORT") && strstr(command, "Actors.t3d")) {
        DumpNativeBsp("NativeCookedBsp.json");
        auto editor = *reinterpret_cast<unsigned char**>(kEditor);
        auto level = *reinterpret_cast<unsigned char**>(editor+0x130);
        auto objects = *reinterpret_cast<unsigned char***>(0x11697B70);
        int count = *reinterpret_cast<int*>(0x11697B74);
        auto active = *reinterpret_cast<unsigned char***>(level+0x2C);
        int activeCount = *reinterpret_cast<int*>(level+0x30);
        for (int i=0; active && i<activeCount; ++i) if (active[i] && active[i][0x2D0] == 2) {
            fprintf(report,"platform_sample name=%s byte2d0=%u\n",ObjectName(active[i]),active[i][0x2D0]);
            fflush(report);
            DescribeActorFields(active[i]);
        }
        for (int i=0; objects && i<count; ++i) {
            auto object = objects[i];
            if (!object) continue;
            const char* name = ObjectName(object);
            if (!strcmp(name, "SPresenceTrigger2071")) {
                auto outer = *reinterpret_cast<unsigned char**>(object+0x18);
                auto package = *reinterpret_cast<void**>(level+0x18);
                fprintf(report, "actor_membership name=%s address=%p class=%s outer=%s outer_ptr=%p outer_is_level=%d outer_is_package=%d platform=%u flags=%08x active=%d pc=%d xbox=%d\n",
                    name, object, ObjectName(*reinterpret_cast<void**>(object+0x24)),
                    ObjectName(outer),outer,outer==level,outer==package,object[0x2D0],
                    *reinterpret_cast<unsigned int*>(object+0x1C), ContainsActor(level,0x2C,object),
                    ContainsActor(level,0x3C,object), ContainsActor(level,0x4C,object));
                fflush(report);
                DescribeActorFields(object);
            }
        }
    }
    return originalExec(exec, command, output);
}

void ObserveEditorExec() {
    auto editor = *reinterpret_cast<unsigned char**>(kEditor);
    auto vtable = *reinterpret_cast<void***>(editor+0x28);
    DWORD protection;
    if (VirtualProtect(vtable, sizeof(void*), PAGE_READWRITE, &protection)) {
        originalExec = reinterpret_cast<ExecFn>(vtable[0]);
        vtable[0] = reinterpret_cast<void*>(&ObserveExec);
        DWORD ignored;
        VirtualProtect(vtable, sizeof(void*), protection, &ignored);
    }
}

void Snapshot(const char* stage) {
    auto editor = *reinterpret_cast<unsigned char**>(kEditor);
    auto level = editor ? *reinterpret_cast<unsigned char**>(editor + 0x130) : nullptr;
    auto model = level ? *reinterpret_cast<unsigned char**>(level + 0x13C) : nullptr;
    int brushes = 0;
    int polys = 0;
    int actors = level ? *reinterpret_cast<int*>(level + 0x30) : 0;
    auto data = level ? *reinterpret_cast<unsigned char***>(level + 0x2C) : nullptr;
    for (int i = 1; data && i < actors; ++i) {
        auto actor = data[i];
        if (!actor || *reinterpret_cast<void**>(actor + 0x24)
                      != reinterpret_cast<void*>(0x1181B048)) continue;
        auto brush = *reinterpret_cast<unsigned char**>(actor + 0x238);
        auto brushPolys = brush ? *reinterpret_cast<unsigned char**>(brush + 0x50) : nullptr;
        if (brushPolys) {
            ++brushes;
            polys += *reinterpret_cast<int*>(brushPolys + 0x2C);
        }
    }
    fprintf(report, "%s level=%p model=%p actors=%d brushes=%d brush_polys=%d nodes=%d surfaces=%d root_outside=%d\n",
        stage, level, model, actors, brushes, polys,
        model ? *reinterpret_cast<int*>(model + 0x58) : 0,
        model ? *reinterpret_cast<int*>(model + 0x98) : 0,
        model ? *reinterpret_cast<int*>(model + 0x104) : -1);
    if (data && data[0]) fprintf(report, "actor0=%p vtable=%p class=%p flags=%08x deleted_list=%p\n",
        data[0], *reinterpret_cast<void**>(data[0]), *reinterpret_cast<void**>(data[0]+0x24),
        *reinterpret_cast<unsigned int*>(data[0]+0x1C), *reinterpret_cast<void**>(level+0x3A50));
    fflush(report);
}

int Exec(const std::string& command) {
    Record("exec", command.substr(0, command.find_first_of("\r\n")).c_str());
    auto editor = *reinterpret_cast<unsigned char**>(kEditor);
    void* output = *reinterpret_cast<void**>(kWarn);
    if (!editor || !output) return 0;
    void* exec = editor + 0x28;
    using Fn = int(__thiscall*)(void*, const char*, void*);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(exec))[0])(
        exec, command.c_str(), output);
}

bool HasGeometry() {
    auto editor = *reinterpret_cast<unsigned char**>(kEditor);
    auto level = editor ? *reinterpret_cast<unsigned char**>(editor + 0x130) : nullptr;
    auto model = level ? *reinterpret_cast<unsigned char**>(level + 0x13C) : nullptr;
    return model && *reinterpret_cast<int*>(model + 0x58) > 0
        && *reinterpret_cast<int*>(model + 0x98) > 0;
}

bool Rebuild() {
    struct Array { void* data{}; int count{}; int capacity{}; } first, second;
    void* editor = *reinterpret_cast<void**>(kEditor);
    using Bracket = void(__thiscall*)(void*, void*, void*);
    reinterpret_cast<Bracket>(0x10E06A1A)(editor, &first, &second);
    bool ok = Exec("MAP REBUILD") && Exec("BSP REBUILD") && Exec("LIGHT APPLY");
    using Paths = void(__thiscall*)(void*);
    if (ok) { Record("rebuild_paths"); reinterpret_cast<Paths>(0x10E06399)(editor); }
    reinterpret_cast<Bracket>(0x10E02EEC)(editor, &first, &second);
    return ok;
}

bool SetCubeBrush(int halfExtent, float x=0, float y=0, float z=0) {
    // Six outward-facing quads form one ordinary subtractive room brush.
    const int vertices[6][4][3] = {
        {{512,-512,-512},{512,512,-512},{512,512,512},{512,-512,512}},
        {{-512,-512,-512},{-512,-512,512},{-512,512,512},{-512,512,-512}},
        {{-512,512,-512},{-512,512,512},{512,512,512},{512,512,-512}},
        {{-512,-512,-512},{512,-512,-512},{512,-512,512},{-512,-512,512}},
        {{-512,-512,512},{512,-512,512},{512,512,512},{-512,512,512}},
        {{-512,-512,-512},{-512,512,-512},{512,512,-512},{512,-512,-512}}
    };
    std::ostringstream command;
    command << std::setprecision(9);
    command << "BRUSH SET\nBegin PolyList\n";
    for (const auto& face : vertices) {
        command << "Begin Polygon Texture=TXT_INI.BSP.BETON_Lit_Spec Flags=0\n";
        for (const auto& point : face)
            command << "Vertex " << x+point[0]*halfExtent/512 << ',' << y+point[1]*halfExtent/512
                << ',' << z+point[2]*halfExtent/512 << "\n";
        command << "End Polygon\n";
    }
    command << "End PolyList\n";
    return Exec(command.str()) != 0;
}

struct ProbePoint { float x{}, y{}, z{}; };

bool EmptyAt(unsigned char* model, ProbePoint point) {
    auto nodes = *reinterpret_cast<unsigned char**>(model+0x54);
    int count = *reinterpret_cast<int*>(model+0x58);
    bool outside = *reinterpret_cast<int*>(model+0x104) != 0;
    for (int index=0, step=0; index>=0 && index<count && step<=count; ++step) {
        auto node = nodes + index*0x5C;
        auto plane = reinterpret_cast<float*>(node);
        bool front = plane[0]*point.x + plane[1]*point.y + plane[2]*point.z - plane[3] >= 0;
        bool csg = node[0x5A] != 0 && !(node[0x5B]&0x21);
        outside = front ? outside || csg : outside && !csg;
        index = *reinterpret_cast<int*>(node+(front ? 0x34 : 0x30));
    }
    return outside;
}

bool FindEmptyEditSpace(unsigned char* model, ProbePoint& point) {
    auto nodes = *reinterpret_cast<unsigned char**>(model+0x54);
    int count = *reinterpret_cast<int*>(model+0x58);
    auto verts = *reinterpret_cast<unsigned char**>(model+0x64);
    auto points = *reinterpret_cast<ProbePoint**>(model+0x84);
    for (int index=0; index<count; ++index) {
        auto node = nodes + index*0x5C;
        int vertexCount = node[0x5A];
        if (vertexCount<3 || (node[0x5B]&0x21)) continue;
        int pool = *reinterpret_cast<int*>(node+0x28);
        ProbePoint center;
        for (int i=0; i<vertexCount; ++i) {
            auto vertex = points[*reinterpret_cast<unsigned short*>(verts+(pool+i)*8)];
            center.x += vertex.x/vertexCount;
            center.y += vertex.y/vertexCount;
            center.z += vertex.z/vertexCount;
        }
        auto normal = reinterpret_cast<float*>(node);
        for (float offset : {-32.0f,32.0f}) {
            point = {center.x+normal[0]*offset,center.y+normal[1]*offset,center.z+normal[2]*offset};
            bool fits = EmptyAt(model,point);
            for (int x : {-8,8}) for (int y : {-8,8}) for (int z : {-8,8})
                fits = fits && EmptyAt(model,{point.x+x,point.y+y,point.z+z});
            if (fits) return true;
        }
    }
    return false;
}

bool MakeFixture(const char* runtimePath, bool rootOutside) {
    if (!Exec("MAP NEW")) return false;
    auto editor = *reinterpret_cast<unsigned char**>(kEditor);
    auto level = *reinterpret_cast<unsigned char**>(editor + 0x130);
    auto model = *reinterpret_cast<unsigned char**>(level + 0x13C);
    *reinterpret_cast<int*>(model + 0x104) = rootOutside ? 1 : 0;
    if (!SetCubeBrush(512) || !Exec(rootOutside ? "BRUSH ADD" : "BRUSH SUBTRACT") || !Rebuild()) return false;
    Snapshot("authored_fixture");
    if (!HasGeometry()) return false;
    auto sourcePath = std::filesystem::path(runtimePath).parent_path().parent_path()
        / "MapsEd" / std::filesystem::path(runtimePath).filename();
    using Save = int(__thiscall*)(void*, const char*);
    Record("saving_authored_fixture", sourcePath.string().c_str());
    if (!reinterpret_cast<Save>(0x10E0416B)(*reinterpret_cast<void**>(kEditor), sourcePath.string().c_str())) return false;
    if (!std::filesystem::exists(runtimePath)) {
        Record("FAIL", "Normal fixture save did not emit the expected runtime sibling.");
        return false;
    }
    return true;
}

void RunTest() {
    auto configuration = (directory / "native_recovery_test.ini").string();
    char source[MAX_PATH] = {}, destination[MAX_PATH] = {}, dll[MAX_PATH] = {};
    GetPrivateProfileStringA("test", "source", "", source, MAX_PATH, configuration.c_str());
    GetPrivateProfileStringA("test", "destination", "", destination, MAX_PATH, configuration.c_str());
    GetPrivateProfileStringA("test", "editor_dll", "", dll, MAX_PATH, configuration.c_str());
    if (report) fclose(report);
    report = _fsopen((directory / "native_recovery_report.txt").string().c_str(), "w", _SH_DENYNO);
    if (!report) return;
    Record("started");
    ObserveEditorExec();
    if (GetPrivateProfileIntA("test","compact_points",0,configuration.c_str())) ObserveCsgCompaction();
    Snapshot("initial");
    bool rootOutsideFixture = GetPrivateProfileIntA("test", "root_outside", 0, configuration.c_str()) != 0;
    if (GetPrivateProfileIntA("test", "import_text_only", 0, configuration.c_str())) {
        if (!Exec("MAP NEW") || !Exec(std::string("MAP IMPORT FILE=\"") + source + "\"")) {
            Record("FAIL", "Native text import probe failed."); return;
        }
        auto editor = *reinterpret_cast<unsigned char**>(kEditor);
        auto level = *reinterpret_cast<void**>(editor+0x130);
        using Finalize = void(__thiscall*)(void*, void*);
        reinterpret_cast<Finalize>((*reinterpret_cast<void***>(editor))[0xE0/4])(editor,level);
        Snapshot("text_imported");
        if (!Exec("MAP EXPORT FILE=\"" + (directory / "import_only.t3d").string() + "\"")) {
            Record("FAIL", "Native text import probe could not export."); return;
        }
        Record("PASS", "Native text import/export probe completed; compare import_only.t3d for semantic verification.");
        return;
    }
    if (GetPrivateProfileIntA("test", "reopen_only", 0, configuration.c_str())) {
        if (!Exec(std::string("MAP LOAD FILE=\"") + source + "\"") || !HasGeometry()) {
            Record("FAIL", "Fresh-process ordinary source load failed."); return;
        }
        Snapshot("fresh_normal_reopen");
        if (!Rebuild() || !HasGeometry()) {
            Record("FAIL", "Fresh-process source rebuild failed."); return;
        }
        Snapshot("fresh_normal_rebuild");
        using Save = int(__thiscall*)(void*, const char*);
        if (!reinterpret_cast<Save>(0x10E0416B)(*reinterpret_cast<void**>(kEditor), destination)) {
            Record("FAIL", "Fresh-process source save failed."); return;
        }
        if (!Exec("MAP EXPORT FILE=\"" + (directory / "fresh_source.t3d").string() + "\"")) {
            Record("FAIL", "Fresh-process source export failed."); return;
        }
        Record("PASS", "Fresh editor ordinary source load/rebuild/save/export completed without invoking recovery.");
        return;
    }
    if (GetPrivateProfileIntA("test", "generate_fixture", 0, configuration.c_str())
        && !MakeFixture(source, rootOutsideFixture)) {
        Record("FAIL", "Could not build and cook the isolated source-room fixture."); return;
    }
    if (GetPrivateProfileIntA("test", "import_baseline", 0, configuration.c_str())) {
        auto t3d = (directory / "authored_baseline.t3d").string();
        if (!Exec("MAP EXPORT FILE=\"" + t3d + "\"") || !Exec("MAP NEW")
            || !Exec("MAP IMPORT FILE=\"" + t3d + "\"")) {
            Record("FAIL", "Baseline source export/new/import failed."); return;
        }
        Snapshot("baseline_imported");
        auto editor = *reinterpret_cast<unsigned char**>(kEditor);
        auto level = *reinterpret_cast<void**>(editor + 0x130);
        auto finalize = (*reinterpret_cast<void***>(editor))[0xE0 / 4];
        fprintf(report, "native_import_finalize=%p\n", finalize); fflush(report);
        using FinalizeImport = void(__thiscall*)(void*, void*);
        reinterpret_cast<FinalizeImport>(finalize)(editor, level);
        Snapshot("baseline_finalized");
        auto sourceLevel = reinterpret_cast<unsigned char*>(level);
        auto activeActors = *reinterpret_cast<unsigned char***>(sourceLevel + 0x2C);
        int activeCount = *reinterpret_cast<int*>(sourceLevel + 0x30);
        *reinterpret_cast<int*>(sourceLevel + 0x40) = 0;
        *reinterpret_cast<int*>(sourceLevel + 0x50) = 0;
        using AddUnique = int(__thiscall*)(void*, void*);
        for (int i=0;i<activeCount;++i) if (activeActors[i]) {
            auto actor = activeActors[i];
            if (actor[0x2D0] != 2) reinterpret_cast<AddUnique>(0x10E025CD)(sourceLevel+0x3C, &actor);
            if (actor[0x2D0] != 1) reinterpret_cast<AddUnique>(0x10E025CD)(sourceLevel+0x4C, &actor);
        }
        Snapshot("baseline_platforms_synchronized");
        if (!Rebuild()) { Record("FAIL", "Baseline rebuild failed."); return; }
        Snapshot("baseline_built");
        using Save = int(__thiscall*)(void*, const char*);
        Record("saving_baseline", destination);
        if (!reinterpret_cast<Save>(0x10E0416B)(*reinterpret_cast<void**>(kEditor), destination)) {
            Record("FAIL", "Baseline source save rejected."); return;
        }
        Snapshot("baseline_saved");
        Record("PASS", "Normal T3D export/new/import/build/save baseline completed without recovery.");
        return;
    }
    HMODULE editorDll = GetModuleHandleA(dll);
    if (!editorDll) editorDll = GetModuleHandleA(std::filesystem::path(dll).filename().string().c_str());
    using RecoverFn = int(__cdecl*)(const char*, const char*, char*, unsigned int);
    auto recover = reinterpret_cast<RecoverFn>(GetProcAddress(editorDll, "ReloadedRecoverMapToSource"));
    if (!recover) recover = reinterpret_cast<RecoverFn>(GetProcAddress(editorDll, "_ReloadedRecoverMapToSource"));
    if (!recover) { Record("FAIL", "Recovery export was not found."); return; }
    char error[8192] = {};
    bool expectFailure = GetPrivateProfileIntA("test", "expect_recovery_failure", 0, configuration.c_str()) != 0;
    if (expectFailure) {
        auto previousFilename = std::filesystem::path(source);
        if (GetPrivateProfileIntA("test", "generate_fixture", 0, configuration.c_str()))
            previousFilename = previousFilename.parent_path().parent_path() / "MapsEd" / previousFilename.filename();
        using SetFilename = void(__thiscall*)(void*, const char*);
        reinterpret_cast<SetFilename>(0x10E05E1C)(*reinterpret_cast<void**>(0x1165E80C), previousFilename.string().c_str());
        Record("previous_map_filename", previousFilename.string().c_str());
    }
    Record("recovering", source);
    if (!recover(source, destination, error, sizeof(error))) {
        if (expectFailure) {
            auto window = *reinterpret_cast<unsigned char**>(0x1165E80C);
            auto filename = window ? reinterpret_cast<const char*>(window + 0x58) : "";
            Record("expected_recovery_error", error);
            Record("failure_map_filename", filename);
            if (!window || filename[0]) {
                Record("FAIL", "Failed recovery retained the previous normal File Save target."); return;
            }
            auto previousSource = std::filesystem::path(source).parent_path().parent_path()
                / "MapsEd" / std::filesystem::path(source).filename();
            if (!Exec("MAP NEW") || !Exec("MAP LOAD FILE=\"" + previousSource.string() + "\"")
                || !SetCubeBrush(128) || !Exec(rootOutsideFixture ? "BRUSH SUBTRACT" : "BRUSH ADD")
                || !Rebuild() || !HasGeometry()) {
                Record("FAIL", "Normal source work failed after rejected recovery."); return;
            }
            Snapshot("failure_followup_normal_edit");
            auto editor = *reinterpret_cast<unsigned char**>(kEditor);
            auto level = *reinterpret_cast<unsigned char**>(editor+0x130);
            auto model = *reinterpret_cast<unsigned char**>(level+0x13C);
            if (*reinterpret_cast<int*>(model+0x98) <= 6) {
                Record("FAIL", "Normal geometry rebuild stayed blocked after rejected recovery."); return;
            }
            Record("PASS", "Expected geometry-write failure cleared the prior File Save target and allowed ordinary source load/edit/rebuild.");
            return;
        }
        DumpNativeBsp("NativeFailedBsp.json");
        Snapshot("failed_recovery");
        Record("FAIL", error);
        return;
    }
    if (expectFailure) { Record("FAIL", "Recovery unexpectedly accepted the blocked geometry write."); return; }
    Snapshot("recovered_source");
    auto levelWindow = *reinterpret_cast<unsigned char**>(0x1165E80C);
    const char* currentFilename = levelWindow ? reinterpret_cast<const char*>(levelWindow + 0x58) : "";
    Record("current_map_filename", currentFilename);
    if (_stricmp(currentFilename, destination) != 0) {
        Record("FAIL", "The normal File Save target was not updated to the recovered source."); return;
    }
    if (!HasGeometry()) { Record("FAIL", "Recovered source contains no BSP."); return; }
    if (!Exec("MAP NEW")) { Record("FAIL", "MAP NEW was not accepted."); return; }
    Snapshot("blank");
    if (!Exec(std::string("MAP LOAD FILE=\"") + destination + "\"")) {
        Record("FAIL", "Normal MAP LOAD was not accepted."); return;
    }
    Snapshot("normal_reopen");
    if (!HasGeometry()) { Record("FAIL", "Normal reopen contains no BSP."); return; }
    if (!Rebuild()) {
        Record("FAIL", "A normal build command was not accepted."); return;
    }
    Snapshot("normal_rebuild");
    if (!HasGeometry()) { Record("FAIL", "Normal rebuild removed the BSP."); return; }
    bool generatedFixture = GetPrivateProfileIntA("test", "generate_fixture", 0, configuration.c_str()) != 0;
    if (generatedFixture || GetPrivateProfileIntA("test", "edit_geometry", 0, configuration.c_str())) {
        // Add a solid brush in verified empty space, or carve a cavity from
        // the additive fixture. Exercise a real edit and ordinary save/reopen.
        auto editor = *reinterpret_cast<unsigned char**>(kEditor);
        auto level = *reinterpret_cast<unsigned char**>(editor + 0x130);
        auto model = *reinterpret_cast<unsigned char**>(level + 0x13C);
        int originalRootOutside = *reinterpret_cast<int*>(model+0x104);
        ProbePoint editPoint;
        if (!generatedFixture && !FindEmptyEditSpace(model,editPoint)) {
            Record("FAIL", "No safe empty BSP space was found for the isolated geometry edit."); return;
        }
        fprintf(report,"geometry_edit_point %.9g,%.9g,%.9g initial_empty=%d\n",editPoint.x,editPoint.y,editPoint.z,EmptyAt(model,editPoint));
        fflush(report);
        bool subtract = generatedFixture && rootOutsideFixture;
        std::string editedActor;
        if (!generatedFixture && !EditActorTag(level,editedActor)) {
            Record("FAIL", "A retained actor could not accept an ordinary Tag property edit."); return;
        }
        if (!SetCubeBrush(generatedFixture ? 128 : 4,editPoint.x,editPoint.y,editPoint.z)
            || !Exec(subtract ? "BRUSH SUBTRACT" : "BRUSH ADD") || !Rebuild()) {
            Record("FAIL", "Recovered source could not rebuild the geometry edit."); return;
        }
        Snapshot("edited_geometry");
        model = *reinterpret_cast<unsigned char**>(level + 0x13C);
        int editedSurfaces = *reinterpret_cast<int*>(model + 0x98);
        if (EmptyAt(model,editPoint) != subtract) { Record("FAIL", "Geometry edit did not change BSP space as expected."); return; }
        using Save = int(__thiscall*)(void*, const char*);
        if (!reinterpret_cast<Save>(0x10E0416B)(editor, destination)
            || !Exec("MAP NEW") || !Exec(std::string("MAP LOAD FILE=\"") + destination + "\"")) {
            Record("FAIL", "Edited source could not save and reopen normally."); return;
        }
        Snapshot("edited_normal_reopen");
        level = *reinterpret_cast<unsigned char**>(editor + 0x130);
        model = *reinterpret_cast<unsigned char**>(level + 0x13C);
        if (*reinterpret_cast<int*>(model + 0x98) != editedSurfaces) {
            Record("FAIL", "Edited source lost BSP surfaces after save/reopen."); return;
        }
        if (*reinterpret_cast<int*>(model + 0x104) != originalRootOutside || EmptyAt(model,editPoint) != subtract) {
            Record("FAIL", "Edited source changed its root outside/solid state."); return;
        }
        if (!generatedFixture && !HasEditedTag(FindStaticMeshActor(level,editedActor.c_str()))) {
            Record("FAIL", "The actor Tag property edit did not survive ordinary save/reopen."); return;
        }
    }
    auto exported = std::filesystem::path(destination).replace_extension(".roundtrip.t3d");
    if (!Exec("MAP EXPORT FILE=\"" + exported.string() + "\"")) {
        Record("FAIL", "Normal MAP EXPORT was not accepted."); return;
    }
    Record("PASS", "Recovery, ordinary reopen, geometry/BSP/lighting/path rebuild, requested geometry edit roundtrip, and T3D export completed.");
}

LRESULT CALLBACK OnMessage(int code, WPARAM removed, LPARAM parameter) {
    if (code >= 0) {
        auto message = reinterpret_cast<CWPSTRUCT*>(parameter);
        if (message->message == kRun) {
            UnhookWindowsHookEx(hook);
            hook = nullptr;
            RunTest();
        }
    }
    return CallNextHookEx(hook, code, removed, parameter);
}

BOOL CALLBACK RecordStartupErrorControl(HWND window, LPARAM) {
    char text[4096] = {};
    GetWindowTextA(window,text,sizeof(text));
    if (report && text[0]) Record("startup_error_text",text);
    return TRUE;
}

BOOL CALLBACK FindEditorWindow(HWND window, LPARAM threadAddress) {
    DWORD process = 0;
    DWORD thread = GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId()) return TRUE;
    ShowWindow(window, SW_HIDE);
    char title[256] = {};
    GetWindowTextA(window, title, sizeof(title));
    char klass[128] = {};
    GetClassNameA(window, klass, sizeof(klass));
    if (report) {
        fprintf(report, "window=%p thread=%lu class=%s title=%s\n", window, thread, klass, title);
        fflush(report);
    }
    if (!strcmp(klass,"#32770") && !strcmp(title,"Critical Error")) {
        EnumChildWindows(window,RecordStartupErrorControl,0);
        FILE* failure = _fsopen((directory/"native_recovery_report.txt").string().c_str(),"w",_SH_DENYNO);
        if (failure) {
            fputs("FAIL Native editor startup reached a Critical Error; see native_recovery_boot.txt.\n",failure);
            fclose(failure);
        }
        startupFailed=true;
        return FALSE;
    }
    if (strstr(klass, "WEditorFrame")) {
        *reinterpret_cast<DWORD*>(threadAddress) = thread;
        frameWindow = window;
    }
    return TRUE;
}

DWORD WINAPI WaitUntilReady(void*) {
    report = _fsopen((directory / "native_recovery_boot.txt").string().c_str(), "w", _SH_DENYNO);
    if (report) Record("waiting_for_editor");
    for (int attempt = 0; attempt < 240; ++attempt) {
        DWORD uiThread = 0;
        EnumWindows(FindEditorWindow, reinterpret_cast<LPARAM>(&uiThread));
        if (startupFailed) return 1;
        auto editor = *reinterpret_cast<unsigned char**>(kEditor);
        if (report) {
            fprintf(report, "attempt=%d editor=%p uiThread=%lu\n", attempt, editor, uiThread);
            fflush(report);
        }
        if (uiThread && editor && *reinterpret_cast<void**>(editor + 0x130)) {
            // Initial windows exist before startup completes. Wait briefly,
            // then execute through the frame's actual UI-thread message hook.
            Sleep(5000);
            hook = SetWindowsHookExA(WH_CALLWNDPROC, OnMessage, self, uiThread);
            if (report) { fprintf(report, "hook=%p error=%lu\n", hook, GetLastError()); fflush(report); }
            DWORD_PTR result = 0;
            if (hook && SendMessageTimeoutA(frameWindow, kRun, 0, 0,
                SMTO_ABORTIFHUNG, 300000, &result)) return 0;
            return 1;
        }
        Sleep(250);
    }
    return 1;
}
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        DisableThreadLibraryCalls(module);
        char path[MAX_PATH] = {};
        GetModuleFileNameA(module, path, MAX_PATH);
        directory = std::filesystem::path(path).parent_path();
        if (HANDLE worker = CreateThread(nullptr, 0, WaitUntilReady, nullptr, 0, nullptr)) CloseHandle(worker);
    }
    return TRUE;
}
