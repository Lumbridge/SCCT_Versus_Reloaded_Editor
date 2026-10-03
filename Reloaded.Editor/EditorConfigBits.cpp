#include "pch.h"
#include "EditorConfigBits.h"
#include "MemoryWriter.h"
#include "logger.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ChaosTheory_Editor.exe, image base 0x10E00000.
namespace
{
    constexpr uintptr_t kEditor = 0x1165DFA0;   // GEditor (UUnrealEdEngine*)
    constexpr uintptr_t kNames  = 0x1169CFBC;   // FName table {data,count}; entry text at +12

    // UObject +0x20 Name, +0x24 Class; UStruct +0x58 PropertyLink;
    // UProperty +0x3C Offset, +0x40 PropertyLinkNext; UBoolProperty +0x64 BitMask.
    constexpr uintptr_t kObjectName = 0x20, kObjectClass = 0x24, kPropertyLink = 0x58;
    constexpr uintptr_t kPropertyOffset = 0x3C, kPropertyNext = 0x40, kBitMask = 0x64;

    bool Copy(void* to, const void* from, size_t size)
    {
        __try { memcpy(to, from, size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    template <class T> bool Read(uintptr_t address, T& value)
    {
        return address && Copy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    }

    std::string NameAt(uintptr_t nameField)
    {
        int index = 0, count = 0;
        uintptr_t table = 0, entry = 0;
        if (!Read(nameField, index) || !Read(kNames, table) || !Read(kNames + 4, count)
            || index < 0 || index >= count || !Read(table + index * 4, entry) || !entry)
            return {};
        char text[64] = {};
        if (!Copy(text, reinterpret_cast<const void*>(entry + 12), sizeof(text) - 1)) return {};
        return text;
    }
    std::string NameOf(uintptr_t object) { return NameAt(object + kObjectName); }

    bool LocateWord(const char* name, uint32_t& offset, uint32_t& mask)
    {
        uintptr_t editor = 0, editorClass = 0, property = 0;
        if (!Read(kEditor, editor) || !Read(editor + kObjectClass, editorClass) || !Read(editorClass + kPropertyLink, property))
            return false;
        for (int guard = 0; property && guard < 4096; ++guard)
        {
            uintptr_t propertyClass = 0;
            if (NameOf(property) == name && Read(property + kObjectClass, propertyClass) && NameOf(propertyClass) == "BoolProperty")
                return Read(property + kPropertyOffset, offset) && Read(property + kBitMask, mask) && mask;
            if (!Read(property + kPropertyNext, property)) return false;
        }
        return false;
    }

    // One native instruction that addresses the flag: [reg+disp32] at at+2,
    // and the immediate mask at maskAt (0 when another site carries it).
    struct Site
    {
        uintptr_t at;
        uint8_t   opcode[2];
        uint32_t  dispAdjust;    // The autosave Exec handler addresses GEditor through its FExec at +0x28.
        uintptr_t maskAt;
        uint8_t   maskOpcode[2]; // Bytes before the immediate, checked before writing.
        size_t    maskOpcodeLength;
        bool      dword;         // Read-modify-write of the whole flag word.
    };

    // The USESIZINGBOX= toggle shifts the parsed 0/1 into place with
    // lea edx,[eax+eax]; mov edx,eax stands in when the flag is bit 0.
    constexpr uintptr_t kToggleShift = 0x10EF487D;
    constexpr uint8_t kShiftByOne[3] = { 0x8D, 0x14, 0x00 };
    constexpr uint8_t kShiftByNone[3] = { 0x8B, 0xD0, 0x90 };

    struct Flag
    {
        const char*       name;
        uint32_t          nativeOffset;
        uint32_t          nativeMask;
        std::vector<Site> sites;
    };

    const std::vector<Flag>& Flags()
    {
        static const std::vector<Flag> flags =
        {
            { "AutoSave", 0x21C, 0x1, {
                { 0x10E2EBF0, { 0xF6, 0x80 }, 0,    0x10E2EBF6, {}, 0, false },  // the one-minute timer (SetTimer 900 at 0x10E889A7)
                { 0x10EFD9C9, { 0xF6, 0x83 }, 0x28, 0x10EFD9CF, {}, 0, false },  // MAYBEAUTOSAVE
            } },
            { "UseSizingBox", 0x21C, 0x2, {
                { 0x10EC71BF, { 0x8A, 0x88 }, 0, 0x10EC71CA, { 0xF6, 0xC1 }, 2, false },
                { 0x10ECCEF7, { 0x8A, 0x82 }, 0, 0x10ECCF00, { 0xA8 },       1, false },  // UUnrealEdEngine::Draw overlay
                { 0x10ED5DC2, { 0xF6, 0x81 }, 0, 0x10ED5DC8, {}, 0, false },
                { 0x10EF485E, { 0x8B, 0x87 }, 0, 0x10EF486C, { 0x83, 0xE1 }, 2, true },   // USESIZINGBOX toggle
                { 0x10EF486F, { 0x89, 0x8F }, 0, 0, {}, 0, true },
                { 0x10EF4877, { 0x8B, 0x9F }, 0, 0x10EF4884, { 0x83, 0xE2 }, 2, true },  // USESIZINGBOX=n
                { 0x10EF4889, { 0x89, 0x87 }, 0, 0, {}, 0, true },
            } },
            { "UseAxisIndicator", 0x21C, 0x4, {
                { 0x10EC0979, { 0xF6, 0x81 }, 0, 0x10EC097F, {}, 0, false },
                { 0x10ECC51A, { 0xF6, 0x81 }, 0, 0x10ECC520, {}, 0, false },
                { 0x10ECCD86, { 0xF6, 0x80 }, 0, 0x10ECCD8C, {}, 0, false },
            } },
        };
        return flags;
    }

    int LowestByte(uint32_t mask)
    {
        int byte = 0;
        while (!(mask & 0xFF)) { mask >>= 8; ++byte; }
        return byte;
    }

    bool Matches(uintptr_t at, const uint8_t* bytes, size_t length)
    {
        uint8_t live[4] = {};
        return length <= sizeof(live) && Copy(live, reinterpret_cast<const void*>(at), length) && std::memcmp(live, bytes, length) == 0;
    }

    // Repoints every site of one flag, or none if any site is not as expected.
    bool Repoint(const Flag& flag, uint32_t offset, uint32_t mask)
    {
        const int byte = LowestByte(mask);
        const uint32_t byteMask = mask >> (8 * byte);
        bool hasToggle = false;
        for (const Site& site : flag.sites)
        {
            if (!Matches(site.at, site.opcode, sizeof(site.opcode))
                || (site.maskAt && !Matches(site.maskAt - site.maskOpcodeLength, site.maskOpcode, site.maskOpcodeLength)))
            {
                Logger::log(std::string("EditorConfigBits: unexpected code for ") + flag.name + ", left alone");
                return false;
            }
            hasToggle |= site.dword;
        }
        if (byteMask > 0xFF || (hasToggle && mask != 1 && mask != 2))
        {
            Logger::log(std::string("EditorConfigBits: ") + flag.name + " mask " + std::to_string(mask) + " cannot be patched in place");
            return false;
        }
        if (hasToggle && !Matches(kToggleShift, kShiftByOne, 3) && !Matches(kToggleShift, kShiftByNone, 3))
        {
            Logger::log(std::string("EditorConfigBits: unexpected toggle code for ") + flag.name + ", left alone");
            return false;
        }

        bool ok = true;
        for (const Site& site : flag.sites)
        {
            const uint32_t disp = (site.dword ? offset : offset + byte) - site.dispAdjust;
            const uint8_t imm = static_cast<uint8_t>(site.dword ? mask : byteMask);
            ok &= MemoryWriter::WriteBytes(site.at + 2, &disp, sizeof(disp));
            if (site.maskAt) ok &= MemoryWriter::WriteBytes(site.maskAt, &imm, 1);
        }
        if (hasToggle) ok &= MemoryWriter::WriteBytes(kToggleShift, mask == 1 ? kShiftByNone : kShiftByOne, 3);
        return ok;
    }
}

namespace
{
    // UField +0x18 Outer (the declaring class), +0x28 SuperField; UStruct +0x34
    // PropertiesSize; UProperty +0x30 ArrayDim, +0x32 ElementSize, +0x34 Flags,
    // +0x38 Category; UClass +0xB0 Defaults {data,num}.
    constexpr uintptr_t kOuter = 0x18, kSuper = 0x28, kPropertiesSize = 0x34;
    constexpr uintptr_t kArrayDim = 0x30, kElementSize = 0x32, kPropertyFlags = 0x34, kCategory = 0x38;
    constexpr uintptr_t kDefaults = 0xB0;
    constexpr uint32_t kEdit = 0x1, kConfig = 0x4000;

    uintptr_t DefaultsOf(uintptr_t cls)
    {
        uintptr_t data = 0;
        int count = 0, size = 0;
        return Read(cls + kDefaults, data) && Read(cls + kDefaults + 4, count) && Read(cls + kPropertiesSize, size)
            && data && count == size ? data : 0;
    }

    std::string Describe(const std::string& type, const uint8_t* value, uint32_t mask)
    {
        if (type == "BoolProperty") return (*reinterpret_cast<const uint32_t*>(value) & mask) ? "True" : "False";
        if (type == "ByteProperty") return std::to_string(*value);
        if (type == "IntProperty") return std::to_string(*reinterpret_cast<const int32_t*>(value));
        return std::to_string(*reinterpret_cast<const float*>(value));
    }
}

// View > Advanced Options edits Editor.EditorEngine's defaults: it saves them to
// [Editor.EditorEngine] and pushes them into GEditor for the session. GEditor
// itself, a UnrealEdEngine, loads from [UnrealEd.UnrealEdEngine] and never writes
// those values back, so every change was lost at the next start. The window's
// values win here: its Advanced settings go into GEditor and UnrealEdEngine's
// defaults. The other categories also change from the toolbar, which persists
// through [UnrealEd.UnrealEdEngine], so they are left alone.
void EditorConfigBits::SyncAdvancedOptions()
{
    uintptr_t editor = 0, editorClass = 0, engineClass = 0, property = 0;
    if (!Read(kEditor, editor) || !Read(editor + kObjectClass, editorClass)) return;
    for (uintptr_t cls = editorClass; cls; )
    {
        if (NameOf(cls) == "EditorEngine") { engineClass = cls; break; }
        if (!Read(cls + kSuper, cls)) return;
    }
    const uintptr_t windowValues = DefaultsOf(engineClass), editorDefaults = DefaultsOf(editorClass);
    int engineSize = 0;
    if (!engineClass || !windowValues || !editorDefaults || !Read(engineClass + kPropertiesSize, engineSize)
        || !Read(editorClass + kPropertyLink, property))
    {
        Logger::log("EditorConfigBits: Advanced Options defaults not found, settings not synced");
        return;
    }

    for (int guard = 0; property && guard < 4096; ++guard)
    {
        uintptr_t outer = 0, propertyClass = 0;
        uint16_t arrayDim = 0, elementSize = 0;
        uint32_t flags = 0, offset = 0, mask = 0xFFFFFFFF;
        Read(property + kOuter, outer);
        Read(property + kObjectClass, propertyClass);
        Read(property + kArrayDim, arrayDim);
        Read(property + kElementSize, elementSize);
        Read(property + kPropertyFlags, flags);
        Read(property + kPropertyOffset, offset);
        const std::string type = NameOf(propertyClass);
        const bool scalar = type == "BoolProperty" || type == "ByteProperty" || type == "IntProperty" || type == "FloatProperty";
        if (outer == engineClass && scalar && arrayDim == 1 && (flags & (kEdit | kConfig)) == (kEdit | kConfig)
            && NameAt(property + kCategory) == "Advanced" && offset + 4 <= static_cast<uint32_t>(engineSize)
            && (type != "BoolProperty" || Read(property + kBitMask, mask)))
        {
            const size_t width = type == "BoolProperty" ? 4 : elementSize;
            uint8_t want[4] = {}, live[4] = {};
            if (width <= 4 && Copy(want, reinterpret_cast<const void*>(windowValues + offset), width)
                && Copy(live, reinterpret_cast<const void*>(editor + offset), width))
            {
                if (type == "BoolProperty")
                {
                    const uint32_t bits = *reinterpret_cast<uint32_t*>(want) & mask;
                    *reinterpret_cast<uint32_t*>(want) = (*reinterpret_cast<uint32_t*>(live) & ~mask) | bits;
                }
                if (std::memcmp(want, live, width) != 0)
                {
                    Logger::log("EditorConfigBits: Advanced Options " + NameOf(property) + " " + Describe(type, live, mask)
                                + " -> " + Describe(type, want, mask));
                    Copy(reinterpret_cast<void*>(editor + offset), want, width);
                }
                uint8_t stored[4] = {};
                if (Copy(stored, reinterpret_cast<const void*>(editorDefaults + offset), width))
                {
                    if (type == "BoolProperty")
                        *reinterpret_cast<uint32_t*>(stored) = (*reinterpret_cast<uint32_t*>(stored) & ~mask) | (*reinterpret_cast<uint32_t*>(want) & mask);
                    else
                        std::memcpy(stored, want, width);
                    Copy(reinterpret_cast<void*>(editorDefaults + offset), stored, width);
                }
            }
        }
        if (!Read(property + kPropertyNext, property)) break;
    }
}

bool EditorConfigBits::Locate(const char* name, uint32_t& byteOffset, uint8_t& bit)
{
    uint32_t offset = 0, mask = 0;
    if (!LocateWord(name, offset, mask)) return false;
    const int byte = LowestByte(mask);
    if ((mask >> (8 * byte)) > 0xFF) return false;
    byteOffset = offset + byte;
    bit = static_cast<uint8_t>(mask >> (8 * byte));
    return true;
}

bool EditorConfigBits::Apply()
{
    static bool done = false;
    if (done) return true;

    struct Found { const Flag* flag; uint32_t offset, mask; };
    std::vector<Found> found;
    for (const Flag& flag : Flags())
    {
        uint32_t offset = 0, mask = 0;
        if (!LocateWord(flag.name, offset, mask)) return false;   // Not linked yet: try again later.
        found.push_back({ &flag, offset, mask });
    }

    done = true;
    for (const Found& f : found)
    {
        if (f.offset == f.flag->nativeOffset && f.mask == f.flag->nativeMask) continue;
        const bool ok = Repoint(*f.flag, f.offset, f.mask);
        char where[64];
        sprintf_s(where, "+0x%X mask 0x%X", f.offset, f.mask);
        Logger::log(std::string("EditorConfigBits: ") + f.flag->name + " loads at " + where
                    + (ok ? ", native reads repointed" : ", native reads NOT repointed"));
    }
    SyncAdvancedOptions();
    return true;
}
