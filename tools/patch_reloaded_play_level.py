"""Guard Reloaded v3.0a's player-dependent callbacks during editor loading.

This patches one known DLL build, never the game executable. It checks the whole
file hash, keeps an original backup for --apply, and rejects unknown versions.
The added x86 instructions use relative branches, so ASLR remains supported.
"""

import argparse
import hashlib
import os
from pathlib import Path
import struct
import tempfile


ORIGINAL_SHA256 = "7026816bb6a4e41eb651c0c0f3f5169e0843e1eaedf5964e87f17c35c64cb149"
CHECKSUM_OFFSET = 0x180
ORIGINAL_CHECKSUM = 0x34898B
GUARD_RVA = 0xE703
CAVE_RVA = 0x23E700

# Existing false-return epilogue: pop esi; mov esp,ebp; pop ebp; ret.
# Both new early exits have EAX=0, so AL already contains the false result.
guard = bytes.fromhex("85 C0 74 7C 8B 40 28 85 C0 74 75 E9")
guard += struct.pack("<i", CAVE_RVA - (GUARD_RVA + 16))

# Relocate only the original eligibility test, preserving its two destinations.
# test byte ptr [eax+C44h],2; jz E788h; jmp E713h
cave = bytes.fromhex("F6 80 44 0C 00 00 02 0F 84")
cave += struct.pack("<i", 0xE788 - (CAVE_RVA + 13))
cave += b"\xE9" + struct.pack("<i", 0xE713 - (CAVE_RVA + 18))

# The .text tail has 268 zero padding bytes. Use 18 at an aligned location,
# extend VirtualSize within its existing raw allocation, and leave sections,
# imports, relocations, exports and file length unchanged.
SCOREBOARD_PATCHES = (
    (0xDB03, bytes.fromhex("85 C0 74 03 8B 40 28 F6 80 44 0C 00 00 02 74 75"), guard),
    (0x23DB00, bytes(18), cave),
    (0x228, struct.pack("<I", 0x23D6F4), struct.pack("<I", 0x23D712)),
)

# The overlay hook calls a large set of player-dependent drawing helpers.
# Gate its single call site until the level context, player profile and owner exist,
# instead of modifying individual overlay helpers. The call/pop obtains a
# module-relative address without adding an absolute address or relocation.
OVERLAY_CALL_RVA = 0x580F1
OVERLAY_HELPER_RVA = 0x4BDD0
OVERLAY_CAVE_RVA = 0x23E720
overlay = bytes.fromhex("50 E8 00 00 00 00 58 83 B8")
overlay += struct.pack("<i", 0x322FB0 - (OVERLAY_CAVE_RVA + 6))
overlay += bytes.fromhex("00 74 16 8B 80")
overlay += struct.pack("<i", 0x322FB4 - (OVERLAY_CAVE_RVA + 6))
overlay += bytes.fromhex("85 C0 74 0C 83 78 28 00 74 06 58 E9")
overlay += struct.pack("<i", OVERLAY_HELPER_RVA - (OVERLAY_CAVE_RVA + 38))
overlay += bytes.fromhex("58 C3")
assert len(overlay) == 40
OVERLAY_PATCHES = SCOREBOARD_PATCHES[:2] + (
    (0x574F1, b"\xE8" + struct.pack("<i", OVERLAY_HELPER_RVA - (OVERLAY_CALL_RVA + 5)),
     b"\xE8" + struct.pack("<i", OVERLAY_CAVE_RVA - (OVERLAY_CALL_RVA + 5))),
    (0x23DB20, bytes(len(overlay)), overlay),
    (0x228, struct.pack("<I", 0x23D6F4), struct.pack("<I", 0x23D748)),
)

# The controller frame callback is independent of the overlay callback and
# runs during loading too. Its initialization and registered input handlers
# require the same level/profile context. Defer the complete callback; native
# profile construction is separate and remains untouched.
CONTROLLER_CALL_RVA = 0x580B0
CONTROLLER_HELPER_RVA = 0x67DC0
CONTROLLER_CAVE_RVA = 0x23E750
controller = bytearray(overlay)
struct.pack_into("<i", controller, 9, 0x322FB0 - (CONTROLLER_CAVE_RVA + 6))
struct.pack_into("<i", controller, 18, 0x322FB4 - (CONTROLLER_CAVE_RVA + 6))
struct.pack_into("<i", controller, 34, CONTROLLER_HELPER_RVA - (CONTROLLER_CAVE_RVA + 38))
PATCHES = OVERLAY_PATCHES[:-1] + (
    (0x574B0, b"\xE8" + struct.pack("<i", CONTROLLER_HELPER_RVA - (CONTROLLER_CALL_RVA + 5)),
     b"\xE8" + struct.pack("<i", CONTROLLER_CAVE_RVA - (CONTROLLER_CALL_RVA + 5))),
    (0x23DB50, bytes(len(controller)), bytes(controller)),
    (0x228, struct.pack("<I", 0x23D6F4), struct.pack("<I", 0x23D778)),
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def checksum(data):
    total = 0
    for offset in range(0, len(data), 2):
        if CHECKSUM_OFFSET <= offset < CHECKSUM_OFFSET + 4:
            continue
        word = int.from_bytes(data[offset:offset + 2], "little")
        total += word
        total = (total & 0xFFFF) + (total >> 16)
    total = (total & 0xFFFF) + (total >> 16)
    return total + len(data)


def matches_patches(data, patches):
    if len(data) < 0x23DB78:
        return False
    original = bytearray(data)
    for offset, before, after in patches:
        if data[offset:offset + len(after)] != after:
            return False
        original[offset:offset + len(before)] = before
    if struct.unpack_from("<I", data, CHECKSUM_OFFSET)[0] != checksum(data):
        return False
    struct.pack_into("<I", original, CHECKSUM_OFFSET, ORIGINAL_CHECKSUM)
    return sha256(original) == ORIGINAL_SHA256


def is_patched(data):
    return matches_patches(data, PATCHES)


def original_bytes(data):
    if sha256(data) == ORIGINAL_SHA256:
        return data
    for patches in (PATCHES, OVERLAY_PATCHES, SCOREBOARD_PATCHES):
        if matches_patches(data, patches):
            result = bytearray(data)
            for offset, before, _ in patches:
                result[offset:offset + len(before)] = before
            struct.pack_into("<I", result, CHECKSUM_OFFSET, ORIGINAL_CHECKSUM)
            return bytes(result)
    raise ValueError("Unsupported Reloaded.Core.dll build; no files changed.")


def patch(data):
    if is_patched(data):
        return data
    data = original_bytes(data)
    result = bytearray(data)
    for offset, before, after in PATCHES:
        if data[offset:offset + len(before)] != before:
            raise ValueError("Unexpected DLL layout; no files changed.")
        result[offset:offset + len(after)] = after
    struct.pack_into("<I", result, CHECKSUM_OFFSET, checksum(result))
    assert is_patched(result)
    return bytes(result)


def write_atomic(path, data):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".reloaded-guard-", delete=False) as output:
            temporary = Path(output.name)
            output.write(data)
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--output", type=Path, help="Build a separate patched DLL for review/testing")
    mode.add_argument("--apply", action="store_true", help="Back up and replace the supplied DLL")
    mode.add_argument("--check", action="store_true", help="Check compatibility without writing files")
    args = parser.parse_args()
    source = args.dll.resolve(strict=True)
    data = source.read_bytes()
    result = patch(data)
    if args.check:
        print("Already patched (scoreboard + overlay + controller)" if is_patched(data)
              else "Supported Reloaded v3.0a; startup guard update available")
        return
    if args.apply:
        if is_patched(data):
            print("Already patched; no files changed.")
            return
        backup = source.with_name(source.name + ".before-play-level-guard.bak")
        if backup.exists():
            if sha256(backup.read_bytes()) != ORIGINAL_SHA256:
                raise ValueError("Existing backup differs from the supported original; not overwriting it.")
        else:
            with backup.open("xb") as output:
                output.write(original_bytes(data))
        destination = source
        print(f"Original backup: {backup}")
    else:
        destination = args.output.resolve()
        if destination == source:
            raise ValueError("Use --apply to replace the original DLL with a backup.")
    write_atomic(destination, result)
    if destination.read_bytes() != result:
        raise OSError("Patched DLL verification failed.")
    print(f"Patched DLL: {destination}")
    print(f"SHA256: {sha256(result)}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
