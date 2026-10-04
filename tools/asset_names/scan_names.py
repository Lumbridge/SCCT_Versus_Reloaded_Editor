"""Lists the asset names in an SCCT install and counts their words.

The research step behind fr-en.txt: reads the name table of every texture,
static mesh, sound and animation package, the editable maps and the game's
script packages (minus the engine's own vocabulary), splits each name into words
the way the editor does (separators, CamelCase, digits) and writes

    names.txt   every distinct name, one per line (Windows-1252, as the editor sees them)
    tokens.tsv  word, how many names use it, a few example names

    python tools/asset_names/scan_names.py "<SCCT install>" <output folder>

Words that are French but not yet in fr-en.txt show up near the top of
tokens.tsv; tests/AssetNameGlossModelTests.cpp checks the dictionary itself.
"""
import collections
import os
import re
import struct
import sys

PACKAGES = {
    'Textures': '.utx', 'StaticMeshes': '.usx', 'Sounds': '.uax', 'Animations': '.ukx', 'MapsEd': '.sdc',
}
SCRIPTS = ('SBase.u', 'SoftBody.u', 'GUI.u')
ENGINE = ('Core.u', 'Engine.u', 'Editor.u', 'UnrealEd.u', 'IpDrv.u')
TOKEN = re.compile(r'[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+|\d+')


def compact(data, at):
    first = data[at]
    at += 1
    value = first & 0x3F
    if first & 0x40:
        shift = 6
        for _ in range(4):
            byte = data[at]
            at += 1
            value |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                break
    return (-value if first & 0x80 else value), at


def names_of(path):
    """The package's name table. Versions 175 and later XOR each name byte with
    the low byte of its file offset (see Reloaded.Editor/MapPackage.cpp)."""
    data = open(path, 'rb').read()
    if len(data) < 36 or struct.unpack_from('<I', data, 0)[0] != 0x9E2A83C1:
        return []
    version = struct.unpack_from('<I', data, 4)[0]
    count, offset = struct.unpack_from('<II', data, 12)
    names, at = [], offset
    try:
        for _ in range(count):
            length, at = compact(data, at)
            name = bytearray()
            for i in range(length):
                byte = data[at] ^ ((at & 255) if version >= 175 else 0)
                at += 1
                if i < length - 1:
                    name.append(byte)
            at += 4  # flags
            names.append(name.decode('cp1252', errors='replace'))
    except IndexError:
        print('truncated name table:', path, file=sys.stderr)
    return names


def words(name):
    out = []
    for part in re.split(r'[^A-Za-z0-9]+', name):
        out += TOKEN.findall(part)
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    game, output = sys.argv[1], sys.argv[2]
    scripts = os.path.join(game, 'System', '_PC_')
    engine = set()
    for script in ENGINE:
        engine |= set(names_of(os.path.join(scripts, script)))
    names = set()
    for folder, extension in PACKAGES.items():
        directory = os.path.join(game, 'Packages', folder)
        for file in sorted(os.listdir(directory)) if os.path.isdir(directory) else []:
            if file.lower().endswith(extension):
                package = os.path.splitext(file)[0]
                names |= {n for n in names_of(os.path.join(directory, file)) if n not in engine} | {package}
    for script in SCRIPTS:
        names |= {n for n in names_of(os.path.join(scripts, script)) if n not in engine}
    counts = collections.Counter()
    examples = collections.defaultdict(list)
    for name in names:
        for word in words(name):
            if word.isdigit():
                continue
            key = word.lower()
            counts[key] += 1
            if len(examples[key]) < 4:
                examples[key].append(name)
    os.makedirs(output, exist_ok=True)
    with open(os.path.join(output, 'names.txt'), 'w', encoding='cp1252', errors='replace') as out:
        out.write('\n'.join(sorted(names)) + '\n')
    with open(os.path.join(output, 'tokens.tsv'), 'w', encoding='utf-8') as out:
        for key, count in counts.most_common():
            out.write(f"{key}\t{count}\t{' | '.join(examples[key])}\n")
    print(f'{len(names)} names, {len(counts)} distinct words, {sum(counts.values())} uses')


if __name__ == '__main__':
    main()
