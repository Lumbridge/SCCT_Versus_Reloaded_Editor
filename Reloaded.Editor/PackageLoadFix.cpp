#include "pch.h"
#include "PackageLoadFix.h"
#include "PackageLoadFixModel.h"
#include "MemoryWriter.h"
#include "logger.h"

#include <sstream>

// Stock UObject::LoadPackage, from its call to GetPackageLinker:
//   10FB1F12  call GetPackageLinker
//   10FB1F17  mov  esi, eax
//   10FB1F19  add  esp, 1Ch          <- kSite: these seven bytes become jmp LinkerCheck
//   10FB1F1C  test esi, esi
//   10FB1F1E  je   10FB1EE0          (return NULL, skipping EndLoad)
//   10FB1F20  test bl, 10h           <- kResume: a linker was found
//   ...
//   10FB1F2F  call EndLoad
// The stub keeps LoadPackage's frame, registers and try state; on the NULL path it
// makes the EndLoad the found path makes, then takes the stock NULL return.
static uintptr_t g_resume = PackageLoadFix::kResume;
static uintptr_t g_nullReturn = PackageLoadFix::kNullReturn;
static uintptr_t g_endLoad = PackageLoadFix::kEndLoad;

static __declspec(naked) void LinkerCheck()
{
    __asm
    {
        add  esp, 0x1C
        test esi, esi
        jz   no_linker
        jmp  dword ptr [g_resume]
    no_linker:
        call dword ptr [g_endLoad]
        jmp  dword ptr [g_nullReturn]
    }
}

void PackageLoadFix::Initialize()
{
    const auto* code = reinterpret_cast<const uint8_t*>(kCallLinker);
    const State state = Classify(code);
    if (state == State::Patched)
    {
        std::ostringstream message;
        message << "PackageLoadFix: LoadPackage already jumps to 0x" << std::hex << JumpTarget(code);
        Logger::log(message.str());
        return;
    }
    if (state == State::Unknown)
    {
        Logger::log("PackageLoadFix: LoadPackage is not the stock code - not installed; a map whose own package "
                    "holds a sound will still crash the editor when opened again");
        return;
    }
    if (!CallsTo(reinterpret_cast<const uint8_t*>(kFoundEndLoadCall), kFoundEndLoadCall, kEndLoad))
    {
        Logger::log("PackageLoadFix: LoadPackage does not call EndLoad where expected - not installed");
        return;
    }
    const auto jump = Jump(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LinkerCheck)));
    if (MemoryWriter::WriteBytes(kSite, jump.data(), jump.size()))
        Logger::log("PackageLoadFix: LoadPackage ends its load when a package has no linker (MyLevel)");
    else
        Logger::log("PackageLoadFix: could not write LoadPackage's jump");
}
