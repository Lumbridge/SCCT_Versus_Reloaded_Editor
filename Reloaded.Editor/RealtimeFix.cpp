#include "pch.h"
#include "RealtimeFix.h"
#include "Hooks.h"
#include "MemoryWriter.h"
#include "SoftBodyStepModel.h"
#include "logger.h"

#include <climits>
#include <cstring>
#include <string>

INIT_HOOKS;

int  g_ReloadedMaxFPS            = 120;
bool g_ReloadedMuteSounds        = false;
bool g_ReloadedNoDuplicateOffset = false;
bool g_ReloadedMinimizeOnPlay    = false;

static LARGE_INTEGER s_rtFreq      = {};
static LARGE_INTEGER s_rtLastFrame = {};
static bool          s_rtReady     = false;

bool  g_SoftBodyStepRateCapEnabled = true;
float g_SoftBodyMinStepSeconds     = 1.0f / 120.0f;
// The engine clamps its own step here, so banked time past it is discarded either way.
static const float g_sbMaxStep = 0.05f;
static float g_sbPending = 0.0f;
static float g_sbStep    = 0.0f;

static void __cdecl DoRealtimeCap()
{
    if (!s_rtReady)
    {
        QueryPerformanceFrequency(&s_rtFreq);
        QueryPerformanceCounter(&s_rtLastFrame);
        s_rtReady = true;
        return;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    // SoftBody fix
    if (g_SoftBodyStepRateCapEnabled)
    {
        const double seconds =
            static_cast<double>(now.QuadPart - s_rtLastFrame.QuadPart) /
            static_cast<double>(s_rtFreq.QuadPart);
        g_sbStep = SoftBodyStepModel::Bank(g_sbPending, seconds, g_SoftBodyMinStepSeconds, g_sbMaxStep);
    }

    if (g_ReloadedMaxFPS <= 0)
    {
        s_rtLastFrame = now;
        return;
    }

    const LONGLONG ticksPerFrame =
        s_rtFreq.QuadPart / static_cast<LONGLONG>(g_ReloadedMaxFPS);

    const LONGLONG remaining = ticksPerFrame - (now.QuadPart - s_rtLastFrame.QuadPart);
    if (remaining > 0)
    {
        const DWORD sleepMs =
            static_cast<DWORD>(remaining * 1000 / s_rtFreq.QuadPart);
        if (sleepMs > 1)
            Sleep(sleepMs - 1);

        do { QueryPerformanceCounter(&now); }
        while ((now.QuadPart - s_rtLastFrame.QuadPart) < ticksPerFrame);
    }

    s_rtLastFrame = now;
}

CALL_HOOK(0x10fd96ff, RealtimeCapThrottle)
{
    __asm
    {
        call    DoRealtimeCap
        ret
    }
}

// Animated texture fix
static float s_animTickThreshold = 1.0f / 33.333333f;

JMP_HOOK(0x11096de8, TexTickAccumulate)
{
    static int s_epilogue = 0x11096df2;

    __asm
    {
        fld     dword ptr [ebp + 8]
        fadd    dword ptr [esi + 0x8c]
        fcom    dword ptr [s_animTickThreshold]
        fstp    dword ptr [esi + 0x8c]
        fnstsw  ax
        test    ah, 0x05
        jnp     done

        mov     edx, dword ptr [esi]
        mov     ecx, esi
        call    dword ptr [edx + 0xac]

        xor     eax, eax
        mov     dword ptr [esi + 0x8c], eax

    done:
        jmp     dword ptr [s_epilogue]
    }
}

// SoftBody fix
using UESoftBodyUpdateFn = void(__thiscall*)(void* self, float dt);
static const UESoftBodyUpdateFn UESoftBody_Update =
    reinterpret_cast<UESoftBodyUpdateFn>(0x110d4610);

// A new simulation's first step (its build's Update(0)) always runs: it gives the simulation the
// render bounds without which it is never drawn or stepped (SoftBodyStepModel.h).
extern "C" static void __cdecl SoftBodyStepCapped(void* self, float dt)
{
    const float radius = *reinterpret_cast<const float*>(
        static_cast<const char*>(self) + SoftBodyStepModel::kBoundsRadiusOffset);
    const SoftBodyStepModel::Step step = SoftBodyStepModel::Choose(
        g_SoftBodyStepRateCapEnabled, SoftBodyStepModel::HasRenderBounds(radius), dt, g_sbStep);
    if (step.run)
        UESoftBody_Update(self, step.dt);
}

__declspec(naked) static void ESoftBodyUpdateThunk()
{
    __asm
    {
        push    dword ptr [esp + 4]     // dt
        push    ecx                     // self
        call    SoftBodyStepCapped
        add     esp, 8
        ret     4
    }
}

// Maps saved by RE+ before the first step above carry strip doors and patches whose simulation has
// no render bounds, and loading a map builds nothing. Before every map save (File Save, Save As,
// Play Level's runtime copy and MapRecovery all issue MAP SAVE), each such
// simulation gets the call its build ends with: vtable +0xA0 with 0 (10F5F933), which steps it
// through the thunk above and rebuilds its render data. Simulations with bounds are not touched.
namespace
{
    constexpr uintptr_t kGEditor               = 0x1165DFA0;
    constexpr uintptr_t kGNamesData            = 0x1169CFBC;
    constexpr size_t    kEditorLevelOffset     = 0x130;
    constexpr size_t    kLevelActorsDataOffset = 0x2C;
    constexpr size_t    kLevelActorsCountOffset = 0x30;
    constexpr size_t    kObjectNameOffset      = 0x20;
    constexpr size_t    kObjectClassOffset     = 0x24;
    constexpr size_t    kClassSuperOffset      = 0x28;
    constexpr size_t    kNameEntryTextOffset   = 0x0C;
    constexpr size_t    kActorFlagsOffset      = 0x2E8;
    constexpr uint32_t  kActorDeleteMe         = 0x8000;
    constexpr size_t    kActorSimulationOffset = 0x2F8;   // written by each BUILD (10F5F64C)
    constexpr size_t    kSimulationActorOffset = 0x54;    // written by the constructor (110D4542)
    constexpr size_t    kSimulationStepSlot    = 0xA0 / sizeof(void*);
    constexpr uintptr_t kStripDoorStep         = 0x10F602B0;   // calls Update at 10F602B8
    constexpr uintptr_t kPatchStep             = 0x10F64A00;   // calls Update at 10F64A08

    bool IsSoftBodyActorClass(const char* cls)
    {
        const char* const* names = *reinterpret_cast<const char* const* const*>(kGNamesData);
        for (int depth = 0; cls && names && depth < 32; ++depth)
        {
            const char* entry = names[*reinterpret_cast<const int*>(cls + kObjectNameOffset)];
            if (entry && (_stricmp(entry + kNameEntryTextOffset, "ESBStripDoorActor") == 0
                          || _stricmp(entry + kNameEntryTextOffset, "ESBPatchActor") == 0))
                return true;
            cls = *reinterpret_cast<const char* const*>(cls + kClassSuperOffset);
        }
        return false;
    }

    // From actor `next` on, the open level's strip door and patch simulations that have no render
    // bounds, up to `capacity`; `next` ends past the last actor read.
    int FindSimulationsWithoutBounds(int& next, void** found, int capacity)
    {
        int count = 0;
        __try
        {
            const char* editor = *reinterpret_cast<const char* const*>(kGEditor);
            const char* level = editor ? *reinterpret_cast<const char* const*>(editor + kEditorLevelOffset) : nullptr;
            if (!level) return 0;
            const char* const* actors = *reinterpret_cast<const char* const* const*>(level + kLevelActorsDataOffset);
            const int actorCount = *reinterpret_cast<const int*>(level + kLevelActorsCountOffset);
            if (!actors || actorCount <= 0 || actorCount > 2000000) return 0;
            for (; next < actorCount && count < capacity; ++next)
            {
                const char* actor = actors[next];
                if (!actor || (*reinterpret_cast<const uint32_t*>(actor + kActorFlagsOffset) & kActorDeleteMe)
                    || !IsSoftBodyActorClass(*reinterpret_cast<const char* const*>(actor + kObjectClassOffset)))
                    continue;
                char* simulation = *reinterpret_cast<char* const*>(actor + kActorSimulationOffset);
                if (!simulation || *reinterpret_cast<const char* const*>(simulation + kSimulationActorOffset) != actor)
                    continue;
                const uintptr_t step = (*reinterpret_cast<const uintptr_t* const*>(simulation))[kSimulationStepSlot];
                const float radius = *reinterpret_cast<const float*>(simulation + SoftBodyStepModel::kBoundsRadiusOffset);
                if ((step == kStripDoorStep || step == kPatchStep) && !SoftBodyStepModel::HasRenderBounds(radius))
                    found[count++] = simulation;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            next = INT_MAX;
        }
        return count;
    }

    bool IsMapSave(const char* command)
    {
        if (!command) return false;
        while (*command == ' ' || *command == '\t') ++command;
        return _strnicmp(command, "MAP SAVE", 8) == 0
            && (command[8] == '\0' || command[8] == ' ' || command[8] == '\t');
    }
}

void RealtimeFix::BeforeEditorCommand(const char* command)
{
    if (!IsMapSave(command)) return;
    using StepFn = void(__thiscall*)(void* self, float dt);
    int stepped = 0;
    void* found[32];
    for (int next = 0, count; (count = FindSimulationsWithoutBounds(next, found, 32)) > 0;)
        for (int i = 0; i < count; ++i, ++stepped)
            reinterpret_cast<StepFn>((*reinterpret_cast<void***>(found[i]))[kSimulationStepSlot])(found[i], 0.0f);
    if (stepped)
        Logger::log("RealtimeFix: gave " + std::to_string(stepped)
                    + " soft-body simulation(s) without render bounds their build step before the save");
}

// ESoftBodyActor.EnergyFactor is the per-step energy loss; its class default of 0 never settles.
JMP_HOOK(0x110d5002, SoftBodyEnergyFactor)
{
    static int s_resume = 0x110d5008;

    __asm
    {
        mov     edx, dword ptr [ebx + 0x110]
        test    edx, edx
        jnz     have_factor
        mov     dword ptr [ebx + 0x110], 0x3c23d70a     // 0.01f

    have_factor:
        fld     dword ptr [ebx + 0x110]
        jmp     dword ptr [s_resume]
    }
}

// Mute viewport sounds in Realtime Preview by skipping UUNIAudioSubsystem::Update
JMP_HOOK(0x10f6e980, AudioUpdateMuteHook)
{
    static int  s_resume             = 0x10f6e985;
    static bool s_audioListenerReady = false;

    __asm
    {
        cmp  byte ptr [g_ReloadedMuteSounds], 0
        jz   run_update
        cmp  byte ptr [s_audioListenerReady], 0
        jnz  skip_update
        cmp  dword ptr [ecx + 0x2C], 0
        jz   run_update
        mov  byte ptr [s_audioListenerReady], 1

    run_update:
        push ebp
        mov  ebp, esp
        push -1
        jmp  dword ptr [s_resume]

    skip_update:
        ret  4
    }
}

void RealtimeFix::Initialize()
{
    INSTALL_HOOKS;

    // Throttle animated textures
    uint8_t nop5[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };
    MemoryWriter::WriteBytes(0x11096ded, nop5, 5);

    // NOP the Sleep call in on-demand viewport tick
    // Must NOP both PUSH + CALL (RET 4) to avoid unbalancing the stack
    uint8_t nop7[7] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    MemoryWriter::WriteBytes(0x10f0cd92, nop7, 7);

    // NOP the 2 trailing bytes of the original Sleep call,
    // already handled by RealtimeCapThrottle hook
    uint8_t nop2[2] = { 0x90, 0x90 };
    MemoryWriter::WriteBytes(0x10fd9704, nop2, 2);

    // Redirect the two CALL UESoftBody::Update sites to our thunk
    MemoryWriter::WriteCall(0x10f602b8, ESoftBodyUpdateThunk);
    MemoryWriter::WriteCall(0x10f64a08, ESoftBodyUpdateThunk);
}
