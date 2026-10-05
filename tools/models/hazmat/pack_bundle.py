"""Packs the finished suit (assets/HazmatMerc.psk, HazmatSuit.tga, HazmatGear.tga) into
assets/HazmatMerc.bundle, which the editor DLL embeds (Reloaded.Editor.rc) so Character Skins can
add the suit to a map in one click.

Format: b"RHZ1", uint32 count, then per file: uint16 name length, name (ASCII), uint32 size,
uint32 compressed size, zlib data. Little-endian.
"""
import struct
import zlib
from pathlib import Path

ASSETS = Path(__file__).resolve().parent / "assets"
FILES = ["HazmatMerc.psk", "HazmatSuit.tga", "HazmatGear.tga"]


def main():
    out = [b"RHZ1", struct.pack("<I", len(FILES))]
    for name in FILES:
        data = (ASSETS / name).read_bytes()
        packed = zlib.compress(data, 9)
        out += [struct.pack("<H", len(name)), name.encode("ascii"), struct.pack("<II", len(data), len(packed)), packed]
        print(f"{name}: {len(data)} -> {len(packed)} bytes")
    (ASSETS / "HazmatMerc.bundle").write_bytes(b"".join(out))
    print("wrote", ASSETS / "HazmatMerc.bundle")


if __name__ == "__main__":
    main()
