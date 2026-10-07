// Large address aware: finding and setting the PE flag in the editor's bytes, and the
// backup name. Swapping the files in a real editor is checked natively.
#include "../Reloaded.Editor/LargeAddressModel.h"
#include <iostream>
#include <set>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace LargeAddress;
int checks = 0;
void Check(bool value, const char* message, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error(std::string(message) + " (line " + std::to_string(where.line()) + ")");
}
template <class F> bool Throws(F f)
{
    try { f(); }
    catch (const std::exception&) { return true; }
    return false;
}

// A minimal PE image: MZ, e_lfanew = 0x80, "PE\0\0", machine, characteristics.
std::vector<unsigned char> Image(uint16_t machine, uint16_t characteristics)
{
    std::vector<unsigned char> b(0x200, 0);
    b[0] = 'M'; b[1] = 'Z';
    b[0x3c] = 0x80;
    b[0x80] = 'P'; b[0x81] = 'E';
    b[0x84] = static_cast<unsigned char>(machine & 0xff); b[0x85] = static_cast<unsigned char>(machine >> 8);
    b[0x96] = static_cast<unsigned char>(characteristics & 0xff); b[0x97] = static_cast<unsigned char>(characteristics >> 8);
    return b;
}

int main()
{
    try
    {
        // The real editor's header: characteristics 0x010F.
        auto editor = Image(kI386, 0x010f);
        Check(CharacteristicsOffset(editor) == 0x96, "characteristics after the PE signature and machine");
        Check(!IsLargeAddressAware(editor), "the stock editor is not large address aware");
        const auto before = editor;
        Check(MakeLargeAddressAware(editor), "the flag is set");
        Check(IsLargeAddressAware(editor) && Read16(editor, 0x96) == 0x012f, "0x010F becomes 0x012F");
        size_t changed = 0;
        for (size_t i = 0; i < editor.size(); ++i) changed += editor[i] != before[i];
        Check(changed == 1, "one byte changes");
        Check(!MakeLargeAddressAware(editor) && Read16(editor, 0x96) == 0x012f, "a flagged editor is left as it is");

        Check(Throws([] { auto b = Image(kI386, 0); b[0] = 'X'; CharacteristicsOffset(b); }), "not MZ");
        Check(Throws([] { auto b = Image(kI386, 0); b[0x80] = 'N'; CharacteristicsOffset(b); }), "no PE signature");
        Check(Throws([] { auto b = Image(kI386, 0); b[0x3c] = 0xff; b[0x3d] = 0xff; CharacteristicsOffset(b); }), "PE header past the end");
        Check(Throws([] { auto b = Image(0x8664, 0); CharacteristicsOffset(b); }), "64-bit images are refused");
        Check(Throws([] { std::vector<unsigned char> b(10, 0); CharacteristicsOffset(b); }), "too short");

        std::set<std::string> taken;
        auto exists = [&](const std::string& name) { return taken.count(name) != 0; };
        Check(BackupName(exists) == "ChaosTheory_Editor.original.exe", "original first");
        taken.insert("ChaosTheory_Editor.original.exe");
        Check(BackupName(exists) == "ChaosTheory_Editor.before-large-address.exe", "an earlier original is kept");
        taken.insert("ChaosTheory_Editor.before-large-address.exe");
        Check(BackupName(exists) == "ChaosTheory_Editor.before-large-address-2.exe", "then numbered");
        std::cout << "LargeAddressModelTests: " << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
