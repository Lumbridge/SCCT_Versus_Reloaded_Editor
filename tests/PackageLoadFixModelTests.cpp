// LoadPackage's missing EndLoad: recognising the stock code (the bytes as read from
// ChaosTheory_Editor.exe), building the jump and reading it back, and the package the
// Sound Browser's refresh skips. That the editor then reopens a map with its own
// sounds is checked in the editor.
#include "../Reloaded.Editor/PackageLoadFixModel.h"
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace PackageLoadFix;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}

// ChaosTheory_Editor.exe (and its .original.exe) at 0x10FB1F12 and 0x10FB1F2F.
const Code kEditor = {0xE8, 0xB9, 0xF1, 0xFF, 0xFF, 0x8B, 0xF0, 0x83, 0xC4, 0x1C, 0x85, 0xF6, 0x74, 0xC0, 0xF6, 0xC3, 0x10};
const uint8_t kEditorEndLoadCall[5] = {0xE8, 0x0C, 0x7D, 0xFF, 0xFF};
// At 0x10E7F510, in WBrowserSound::RefreshPackages.
const uint8_t kEditorSoundBrowserLoad[5] = {0xE8, 0x9B, 0x29, 0x13, 0x00};

Code Patched(uint32_t stub)
{
    Code code = kEditor;
    const auto jump = Jump(stub);
    for (size_t i = 0; i < kSiteLength; ++i) code[kSite - kCallLinker + i] = jump[i];
    return code;
}

int main()
{
    try
    {
        Check(Stock() == kEditor, "the stock code is the editor's bytes");
        Check(CallsTo(kEditor.data(), kCallLinker, kGetPackageLinker), "the call is to GetPackageLinker");
        Check(CallsTo(kEditorEndLoadCall, kFoundEndLoadCall, kEndLoad), "the found path calls EndLoad");
        Check(!CallsTo(kEditorEndLoadCall, kFoundEndLoadCall + 1, kEndLoad), "a call read from elsewhere is not");
        const int32_t je = static_cast<int8_t>(kEditor[13]);
        Check(kSite + 7 + static_cast<uint32_t>(je) == kNullReturn, "je goes to the NULL return");
        Check(kSite + kSiteLength == kResume, "the stub resumes after the seven bytes");
        Check(Classify(kEditor.data()) == State::Stock, "the editor's code is stock");

        // A stub in the DLL, above the editor, and one below it (negative displacement).
        for (uint32_t stub : {0x5C26F000u, 0x0F000010u, kSite + 5})
        {
            const auto jump = Jump(stub);
            Check(jump[0] == 0xE9 && jump[5] == 0x90 && jump[6] == 0x90, "jmp rel32 and two nops");
            const Code code = Patched(stub);
            Check(Classify(code.data()) == State::Patched, "the patched code is recognised");
            Check(JumpTarget(code.data()) == stub, "the jump reads back to the stub");
            Check(CallsTo(code.data(), kCallLinker, kGetPackageLinker), "the call to GetPackageLinker is kept");
            Check(code[kCodeLength - 3] == 0xF6 && code[kCodeLength - 1] == 0x10, "test bl,10h is kept");
        }

        // Anything else is left alone.
        Code other = kEditor;
        other[1] = 0xBA;                       // a call to somewhere else
        Check(Classify(other.data()) == State::Unknown, "another call target");
        other = kEditor;
        other[12] = 0xEB;                      // jmp instead of je: someone else's patch
        Check(Classify(other.data()) == State::Unknown, "another patch of the site");
        other = Patched(0x5C26F000u);
        other[kCodeLength - 1] = 0x20;         // test bl,20h after the site
        Check(Classify(other.data()) == State::Unknown, "different code after the site");
        other = Patched(0x5C26F000u);
        other[kSite - kCallLinker + 5] = 0xCC; // a jump without the nops
        Check(Classify(other.data()) == State::Unknown, "a jump not followed by two nops");
        other.fill(0);
        Check(Classify(other.data()) == State::Unknown, "zeroes");

        // The Sound Browser's refresh: its call to LoadPackage, and the one package it skips.
        Check(CallsTo(kEditorSoundBrowserLoad, kSoundBrowserRefreshLoad, kLoadPackage), "the browser calls LoadPackage");
        Check(IsMapPackage("MyLevel") && IsMapPackage("mylevel") && IsMapPackage("MYLEVEL"), "MyLevel, any case");
        Check(!IsMapPackage("MyLevel2") && !IsMapPackage("MyLeve") && !IsMapPackage("MyMapMusic"), "other packages");
        Check(!IsMapPackage("") && !IsMapPackage(nullptr), "no name");

        std::cout << "PackageLoadFixModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "PackageLoadFixModelTests FAILED: " << error.what() << "\n";
        return 1;
    }
}
