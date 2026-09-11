// Execute only Reloaded's patched scoreboard helper against synthetic objects.
// DONT_RESOLVE_DLL_REFERENCES skips dependencies and DLL initialization: this
// test never starts the game, invokes DllMain, or creates a graphics device.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static_assert(sizeof(void*) == 4, "Build this test with the x86 compiler.");

namespace
{
    constexpr std::size_t HelperRva = 0xE6F0;
    constexpr std::size_t ProfileRva = 0x322FB4;
    constexpr std::size_t RenderObjectRva = 0x324644;
    constexpr std::size_t FirstInitializationRva = 0x323192;
    constexpr std::size_t SavedScoreboardRva = 0x323684;
    constexpr std::size_t LevelContextRva = 0x322FB0;
    constexpr std::size_t OverlayCallRva = 0x580F1;
    constexpr std::size_t OverlayHelperRva = 0x4BDD0;
    unsigned int overlayCalls = 0;

    struct alignas(4) FakeObject
    {
        std::array<unsigned char, 0xE00> bytes = {};
    };

    template <typename T>
    void Write(unsigned char* address, const T& value)
    {
        // memcpy avoids pointer-aliasing and object-lifetime assumptions.
        std::memcpy(address, &value, sizeof(value));
    }

    template <typename T>
    T Read(const unsigned char* address)
    {
        T result = {};
        std::memcpy(&result, address, sizeof(result));
        return result;
    }

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(EXIT_FAILURE);
        }
    }

    using ScoreboardHelper = bool(__cdecl*)();

    void CheckNoGraphicsModules()
    {
        // Core imports d3d9.dll. Its absence also verifies that mapping Core
        // has not loaded even that dependency, much less initialized a device.
        const char* modules[] = { "d3d8.dll", "d3d9.dll", "ddraw.dll", "dxgi.dll" };
        for (const char* name : modules)
        {
            if (GetModuleHandleA(name) != nullptr)
            {
                std::fprintf(stderr, "FAIL: unexpected graphics module %s\n", name);
                std::exit(EXIT_FAILURE);
            }
        }
    }

    DWORD Invoke(ScoreboardHelper helper, bool* result)
    {
        // Report regressions as test failures instead of opening a crash dialog.
        __try
        {
            *result = helper();
            return ERROR_SUCCESS;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return GetExceptionCode();
        }
    }

    DWORD InvokeOverlay(void(__cdecl* helper)())
    {
        __try
        {
            helper();
            return ERROR_SUCCESS;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return GetExceptionCode();
        }
    }

    void CheckOverlay(unsigned char* module, std::size_t callRva, bool hasLevelContext, bool hasProfile,
        bool hasOwner, unsigned int expectedCalls)
    {
        FakeObject levelContext;
        FakeObject profile;
        FakeObject owner;
        Write(module + LevelContextRva, hasLevelContext
            ? reinterpret_cast<std::uintptr_t>(levelContext.bytes.data()) : 0);
        Write(module + ProfileRva, hasProfile
            ? reinterpret_cast<std::uintptr_t>(profile.bytes.data()) : 0);
        Write(profile.bytes.data() + 0x28, hasOwner
            ? reinterpret_cast<std::uintptr_t>(owner.bytes.data()) : 0);
        // Disable the pre-query feature in the original overlay, so running
        // this against revision 1 reproduces the recorded profile dereference.
        Write(module + 0x323685, static_cast<unsigned char>(0));
        Require(module[callRva] == 0xE8, "expected frame callback direct call");
        const auto displacement = Read<std::int32_t>(module + callRva + 1);
        auto* target = module + callRva + 5 + displacement;
        overlayCalls = 0;
        const DWORD exception = InvokeOverlay(reinterpret_cast<void(__cdecl*)()>(target));
        if (exception != ERROR_SUCCESS)
        {
            std::fprintf(stderr, "FAIL: callback readiness (%d,%d,%d) raised 0x%08lX\n",
                hasLevelContext, hasProfile, hasOwner, exception);
            std::exit(EXIT_FAILURE);
        }
        Require(overlayCalls == expectedCalls, "overlay dispatch count");
        std::printf("PASS: callback readiness level=%d profile=%d owner=%d dispatches=%u\n",
            hasLevelContext, hasProfile, hasOwner, overlayCalls);
    }

    void CheckOverlayGate(unsigned char* module, std::size_t callRva,
        std::size_t helperRva)
    {
        // These execute the installed call target with its real callee intact.
        // A missing gate faults before any graphics call and is caught above.
        CheckOverlay(module, callRva, true, false, false, 0);
        CheckOverlay(module, callRva, true, true, false, 0);
        CheckOverlay(module, callRva, false, true, true, 0);
        CheckOverlay(module, callRva, false, false, false, 0);

        // Substitute only the original overlay body in this process's mapping.
        // This verifies ready dispatch and later teardown without executing UI
        // code or resolving any imported functions. The DLL file is untouched.
        unsigned char probe[] = { 0xFF, 0x05, 0, 0, 0, 0, 0xC3 };
        Write(probe + 2, reinterpret_cast<std::uintptr_t>(&overlayCalls));
        std::array<unsigned char, sizeof(probe)> original = {};
        auto* body = module + helperRva;
        std::memcpy(original.data(), body, original.size());
        DWORD protection = 0;
        Require(VirtualProtect(body, sizeof(probe), PAGE_EXECUTE_READWRITE,
            &protection) != FALSE, "make test overlay body writable");
        std::memcpy(body, probe, sizeof(probe));
        Require(FlushInstructionCache(GetCurrentProcess(), body, sizeof(probe)) != FALSE,
            "flush overlay probe instructions");
        CheckOverlay(module, callRva, true, true, true, 1);
        CheckOverlay(module, callRva, true, false, false, 0);
        CheckOverlay(module, callRva, true, true, false, 0);
        CheckOverlay(module, callRva, true, true, true, 1);
        std::memcpy(body, original.data(), original.size());
        Require(FlushInstructionCache(GetCurrentProcess(), body, sizeof(probe)) != FALSE,
            "flush restored overlay instructions");
        DWORD ignored = 0;
        Require(VirtualProtect(body, sizeof(probe), protection, &ignored) != FALSE,
            "restore test overlay protection");
    }

    void Check(unsigned char* module, const char* name, bool hasProfile,
        bool hasOwner, unsigned char eligibility, std::uint32_t blocked,
        bool scoreboard, bool expected)
    {
        FakeObject profile;
        FakeObject owner;
        FakeObject renderObject;
        const std::uintptr_t profilePointer = hasProfile
            ? reinterpret_cast<std::uintptr_t>(profile.bytes.data()) : 0;
        const std::uintptr_t ownerPointer = hasOwner
            ? reinterpret_cast<std::uintptr_t>(owner.bytes.data()) : 0;
        const std::uintptr_t renderPointer =
            reinterpret_cast<std::uintptr_t>(renderObject.bytes.data());
        const std::uint32_t renderFlags = 0xA5A50000U
            | (scoreboard ? 0x400U : 0U);

        Write(profile.bytes.data() + 0x28, ownerPointer);
        Write(owner.bytes.data() + 0xC44, eligibility);
        Write(owner.bytes.data() + 0xC80, blocked);
        Write(renderObject.bytes.data() + 0x894, renderFlags);
        Write(module + ProfileRva, profilePointer);
        Write(module + RenderObjectRva, renderPointer);
        Write(module + FirstInitializationRva, static_cast<unsigned char>(0));
        // Matching the cached state excludes the unrelated persistence call.
        Write(module + SavedScoreboardRva, static_cast<unsigned char>(scoreboard));

        const FakeObject originalProfile = profile;
        const FakeObject originalOwner = owner;
        const FakeObject originalRenderObject = renderObject;
        const auto helper = reinterpret_cast<ScoreboardHelper>(module + HelperRva);
        bool result = !expected;
        const DWORD exception = Invoke(helper, &result);
        if (exception != ERROR_SUCCESS)
        {
            std::fprintf(stderr, "FAIL: %s raised exception 0x%08lX\n",
                name, exception);
            std::exit(EXIT_FAILURE);
        }

        Require(result == expected, name);
        Require(profile.bytes == originalProfile.bytes, "profile object was modified");
        Require(owner.bytes == originalOwner.bytes, "owner object was modified");
        Require(renderObject.bytes == originalRenderObject.bytes,
            "render object was modified");
        Require(Read<std::uintptr_t>(module + ProfileRva) == profilePointer,
            "profile global was modified");
        Require(Read<std::uintptr_t>(module + RenderObjectRva) == renderPointer,
            "render-object global was modified");
        Require(Read<unsigned char>(module + FirstInitializationRva) == 0,
            "initialization global was modified");
        Require(Read<unsigned char>(module + SavedScoreboardRva)
            == static_cast<unsigned char>(scoreboard),
            "saved scoreboard global was modified");
        std::printf("PASS: %s\n", name);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: ReloadedCorePlayLevelGuardTests.exe <patched-core.dll>\n");
        return EXIT_FAILURE;
    }

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX
        | SEM_NOOPENFILEERRORBOX);
    CheckNoGraphicsModules();
    const HMODULE mapped = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (mapped == nullptr)
    {
        std::fprintf(stderr, "Could not map test DLL; Win32 error %lu\n", GetLastError());
        return EXIT_FAILURE;
    }
    CheckNoGraphicsModules();

    auto* module = reinterpret_cast<unsigned char*>(mapped);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    Require(dos->e_magic == IMAGE_DOS_SIGNATURE, "expected PE DOS header");
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(module + dos->e_lfanew);
    Require(pe->Signature == IMAGE_NT_SIGNATURE
        && pe->FileHeader.Machine == IMAGE_FILE_MACHINE_I386
        && pe->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC,
        "expected x86 PE image");
    Require(pe->OptionalHeader.SizeOfImage >= RenderObjectRva + sizeof(std::uintptr_t),
        "DLL does not contain the expected helper globals");

    Check(module, "null profile returns false", false, false, 2, 0, true, false);
    Check(module, "null owner returns false", true, false, 2, 0, true, false);
    Check(module, "ineligible owner returns false", true, true, 0, 0, true, false);
    Check(module, "unrelated eligibility bits return false", true, true, 0xFD, 0,
        true, false);
    Check(module, "blocked owner returns false", true, true, 2, 1, true, false);
    Check(module, "initialized hidden scoreboard stays false", true, true, 2, 0,
        false, false);
    Check(module, "initialized visible scoreboard stays true", true, true, 2, 0,
        true, true);
    Check(module, "eligible owner with other flag bits stays true", true, true,
        0xFF, 0, true, true);

    CheckOverlayGate(module, OverlayCallRva, OverlayHelperRva);
    std::puts("Controller frame readiness checks:");
    CheckOverlayGate(module, 0x580B0, 0x67DC0);
    CheckNoGraphicsModules();
    Require(FreeLibrary(mapped) != FALSE, "could not unmap test DLL");
    std::puts("Reloaded Core play-level guard tests passed (CPU only; no DLL initialization).");
    return EXIT_SUCCESS;
}
