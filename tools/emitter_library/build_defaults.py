"""Builds the built-in Emitter Library that ships inside Reloaded.Editor.dll.

defaults_manifest.json lists every built-in entry: its id, name, category, description, the stock
map actors it comes from and the tweaks applied to them. The actors are captured natively by the
editor itself (the emitterlib.capture workflow op), because SCCT's T3D import rejects numeric enum
values (UByteProperty::ImportText 0x10fe6700), so hand-written text would silently lose DrawStyle
and similar settings. The captures are kept in captures/<slug>.json; this script turns them into
Reloaded.Editor/EmitterLibraryDefaults.gen.h, which EmitterLibraryModel.h's Builtins() parses.

    python build_defaults.py jobs [--out DIR]           write the capture jobs (MapsEd maps) and the MapRecovery
                                                        actor-export jobs (compiled maps) and print how to run them
    python build_defaults.py decode MAP ACTOR... --out F  write a compiled map's actors from the package itself,
                                                        for maps MapRecovery stops on before exporting actors
    python build_defaults.py imports RUN_DIR [...]      turn the exported (or decoded) actors into import files
                                                        and the capture job for compiled maps
    python build_defaults.py collect RUN_DIR [...]      copy capture_<slug>.json from job output into captures/
    python build_defaults.py build                      tweak, check and embed: writes EmitterLibraryDefaults.gen.h
    python build_defaults.py verifyjob [--out DIR]      write the headless job that places every built-in natively
    python build_defaults.py checkexport RUN_DIR        compare that job's MAP EXPORT output with the entries

build checks that every dependency lives in a stock shared package of the game copy (--game,
default the UE Upgrade test copy): never a map, and when the SCCT_Versus_UE5_Bridge scctpkg reader
is found (--scctpkg), that the object really is in that package. See README.md for the steps.
"""
import argparse
import collections
import copy
import json
import math
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
MANIFEST = HERE / 'defaults_manifest.json'
CAPTURES = HERE / 'captures'
HEADER = ROOT / 'Reloaded.Editor' / 'EmitterLibraryDefaults.gen.h'
GAME = pathlib.Path(r'C:\Users\ryans\Desktop\Enhanced SCCT Versus 4.0 Public Beta - UE Upgrade')
LIVE = pathlib.Path(r'C:\Users\ryans\Desktop\Enhanced SCCT Versus 4.0 Public Beta')
SCCTPKG = pathlib.Path(r'C:\Users\ryans\source\repos\SCCT_Versus_UE5_Bridge')
CATEGORIES = ['Fire', 'Smoke', 'Steam', 'Water', 'Weather', 'Sparks & Electrical', 'Dust & Debris', 'Insects',
              'Lights & Glows', 'Explosions & Bursts', 'Other']
MODES = {'continuous', 'periodic', 'triggered'}
# Package folders an entry may depend on, by extension; System\_PC_\*.u holds the classes.
PACKAGE_FOLDERS = [('System/_PC_', '.u'), ('System', '.u'), ('Packages/Textures', '.utx'), ('Packages/StaticMeshes', '.usx'),
                   ('Packages/Sounds', '.uax'), ('Packages/Sounds', '.uas'), ('Packages/Animations', '.ukx')]
LITERAL = 8000  # MSVC string literals stop at 16 KB


def fail(message):
    sys.exit('build_defaults: ' + message)


def slug(entry):
    return entry['id'].split('.', 1)[1]


MANIFEST_DECODED = {}  # map -> decoded source, from the manifest's "decoded"


def load_manifest():
    manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
    MANIFEST_DECODED.clear()
    MANIFEST_DECODED.update(manifest.get('decoded', {}))
    ids, names = set(), set()
    for entry in manifest['entries']:
        where = entry.get('id', '?')
        if not re.fullmatch(r'builtin\.[a-z0-9][a-z0-9_.-]{0,62}', entry.get('id', '')):
            fail(f'{where}: ids have the form builtin.<slug>')
        if entry['id'] in ids or entry['name'].lower() in names:
            fail(f'{where}: id or name used twice')
        ids.add(entry['id'])
        names.add(entry['name'].lower())
        if entry['category'] not in CATEGORIES:
            fail(f'{where}: category {entry["category"]!r} is not a default category')
        if entry['mode'] not in MODES:
            fail(f'{where}: mode is one of {sorted(MODES)}')
        if entry['source']['map'] not in manifest['maps']:
            fail(f'{where}: map {entry["source"]["map"]} has no folder in "maps"')
        if not 1 <= len(entry['name']) <= 120 or len(entry['description']) >= 2000:
            fail(f'{where}: name or description too long')
    grouped = [m for group in manifest['jobs'] for m in group]
    editable = sorted(set(e['source']['map'] for e in manifest['entries'] if manifest['maps'][e['source']['map']] == 'MapsEd'))
    if sorted(grouped) != editable:
        fail('"jobs" must list every MapsEd source map exactly once, and no Maps-only map')
    return manifest


def compiled_maps(manifest, recoverable=None):
    """Source maps that only exist compiled (Packages\\Maps): the editor asserts loading them.
    recoverable True: those MapRecovery exports; False: those decoded from the package instead."""
    maps = sorted(set(e['source']['map'] for e in manifest['entries'] if manifest['maps'][e['source']['map']] == 'Maps'))
    return [m for m in maps if recoverable is None or (m not in manifest.get('decoded', {})) == recoverable]


# ---- T3D text ------------------------------------------------------------------------------------
# Native export text as the capture holds it: one declaration per line, "\r\n" or "\n" endings (the
# canonicalizer rewrites Location/Rotation with "\n"), nested Begin Object blocks for sub-emitters.

def split_lines(text):
    return text.split('\n')  # the last piece is '' when the text ends with a newline


def ending(line):
    return '\r' if line.endswith('\r') else ''


def scan(lines):
    """Yields (index, depth, stripped line, owning sub-object name or None) for every line."""
    depth, block = 0, None
    for i, line in enumerate(lines):
        s = line.strip()
        low = s.lower()
        if low.startswith('begin '):
            depth += 1
            if depth == 2 and low.startswith('begin object'):
                match = re.search(r'\bName=("?)([^\s"]+)\1', s, re.I)
                block = match.group(2) if match else None
            yield i, depth, s, block
            continue
        if low.startswith('end '):
            yield i, depth, s, block
            if depth == 2:
                block = None
            depth -= 1
            continue
        yield i, depth, s, block


def emitter_names(text):
    """The actor's Emitters(i) in index order, as inline object names."""
    found = {}
    for _, depth, s, _ in scan(split_lines(text)):
        match = re.match(r'Emitters\((\d+)\)=\w+\'"?([^\'"]+)"?\'', s, re.I)
        if depth == 1 and match:
            found[int(match.group(1))] = match.group(2).split('.')[-1]
    return [found[i] for i in sorted(found)]


def key_of(stripped):
    return stripped.split('=', 1)[0].strip().lower() if '=' in stripped else None


def set_line(text, key, value, block=None):
    """Sets key=value on the actor (block None) or inside sub-object block: replaces the line if
    present, else inserts it before the block's Name="..." line or its End line."""
    lines = split_lines(text)
    depth_wanted = 1 if block is None else 2
    hits, anchor, indent, end = [], None, None, None
    for i, depth, s, owner in scan(lines):
        if depth != depth_wanted or (block is not None and (owner or '').lower() != block.lower()):
            continue
        low = s.lower()
        if low.startswith('end '):
            end = i if end is None else end
            continue
        if low.startswith('begin '):
            continue
        if indent is None:
            indent = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
        if key_of(s) == key.lower():
            hits.append(i)
        if block is not None and key_of(s) == 'name' and anchor is None:
            anchor = i
    if block is not None and end is None:
        fail(f'no sub-object {block}')
    if hits:
        for i in hits[1:]:
            lines[i] = None
        lines[hits[0]] = lines[hits[0]][:len(lines[hits[0]]) - len(lines[hits[0]].lstrip())] + f'{key}={value}' + ending(lines[hits[0]])
    else:
        at = anchor if anchor is not None else end
        if at is None:  # actor text: before End Actor
            at = max(i for i, d, s, _ in scan(lines) if d == 1 and s.lower().startswith('end actor'))
        pad = indent if indent is not None else ('        ' if block else '    ')
        lines.insert(at, pad + f'{key}={value}' + ending(lines[at]))
    return '\n'.join(l for l in lines if l is not None)


def remove_line(text, key, block=None):
    lines = split_lines(text)
    depth_wanted = 1 if block is None else 2
    drop = [i for i, depth, s, owner in scan(lines)
            if depth == depth_wanted and key_of(s) == key.lower() and not s.lower().startswith(('begin ', 'end '))
            and (block is None or (owner or '').lower() == block.lower())]
    if not drop:
        fail(f'{key} is not set on {block or "the actor"}')
    return '\n'.join(l for i, l in enumerate(lines) if i not in drop)


def get_line(text, key, block=None):
    depth_wanted = 1 if block is None else 2
    for _, depth, s, owner in scan(split_lines(text)):
        if depth == depth_wanted and key_of(s) == key.lower() and (block is None or (owner or '').lower() == block.lower()):
            return s.split('=', 1)[1]
    return None


def references(text):
    """(class, path) of every object reference, quoted ('"a.b"') or not."""
    return [(m.group(1), m.group(2)) for m in re.finditer(r'\b([A-Za-z_]\w*)\'"?([A-Za-z0-9_.\-]+)"?\'', text)]


def vector_text(v):
    return '(X=%s,Y=%s,Z=%s)' % tuple(format(float(x), '.9g') for x in v)


# ---- struct values for the preview extent --------------------------------------------------------

def parse_struct(value):
    """Parses (X=1,Y=(Min=2,Max=3)) into nested dicts; bare numbers become floats."""
    value = value.strip()
    if not value.startswith('('):
        try:
            return float(value)
        except ValueError:
            return value
    out, depth, start = {}, 0, 1
    for i, c in enumerate(value):
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        if (c == ',' and depth == 1) or (c == ')' and depth == 0):
            part = value[start:i]
            if '=' in part:
                k, v = part.split('=', 1)
                out[k.strip()] = parse_struct(v)
            start = i + 1
    return out


def rng(struct, axis, default=0.0):
    v = struct.get(axis, {}) if isinstance(struct, dict) else {}
    if isinstance(v, dict):
        return (float(v.get('Min', 0.0)), float(v.get('Max', 0.0))) if v else (default, default)
    return (float(v), float(v))


def rotation_matrix(rotation):
    pitch, yaw, roll = (r * math.pi / 32768 for r in rotation)
    cp, sp, cy, sy, cr, sr = math.cos(pitch), math.sin(pitch), math.cos(yaw), math.sin(yaw), math.cos(roll), math.sin(roll)
    x = (cp * cy, cp * sy, sp)
    y = (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp)
    z = (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp)
    return x, y, z


def rotate(axes, v):
    return tuple(axes[0][i] * v[0] + axes[1][i] * v[1] + axes[2][i] * v[2] for i in range(3))


def extent(entry):
    """World-axis bounding box (relative to the pivot) of where the particles can be over one life:
    spawn box, start and radial velocity slowed by VelocityLoss (v -= v * loss * dt, so a particle
    travels v (1 - e^-kt) / k), acceleration and sprite size. The smallest loss of each range is used,
    so it is an upper bound."""
    low, high = [math.inf] * 3, [-math.inf] * 3
    for actor in entry['actors']:
        text, origin = actor['text'], actor['position']
        axes = rotation_matrix(actor['rotation'])
        for name in emitter_names(text):
            props = {}
            for _, depth, s, owner in scan(split_lines(text)):
                if depth == 2 and (owner or '').lower() == name.lower() and '=' in s and not s.lower().startswith(('begin ', 'end ')):
                    k, v = s.split('=', 1)
                    props[k.strip().lower()] = v
            def struct(key):
                value = parse_struct(props[key]) if key in props else {}
                return value if isinstance(value, dict) else {}
            # UE2 defaults: 4 s life, 100-unit sprites, no offset, velocity or acceleration.
            actor_space = props.get('userotationfrom', '').strip().lower() == 'ptrs_actor'
            offset = [float(struct('startlocationoffset').get(a, 0)) for a in 'XYZ']
            box = [rng(struct('startlocationrange'), a) for a in 'XYZ']
            velocity = [rng(struct('startvelocityrange'), a) for a in 'XYZ']
            radial = max(abs(x) for x in rng({'R': struct('startvelocityradialrange')}, 'R')) if 'startvelocityradialrange' in props else 0.0
            acceleration = [float(struct('acceleration').get(a, 0)) for a in 'XYZ']
            loss = [max(min(rng(struct('velocitylossrange'), a)), 0.0) for a in 'XYZ']
            travel = lambda k, t: t if k < 1e-6 else (1 - math.exp(-k * t)) / k
            life = max(rng({'L': struct('lifetimerange')}, 'L')) if 'lifetimerange' in props else 4.0
            size = max(abs(x) for x in rng(struct('startsizerange'), 'X', 100.0)) if 'startsizerange' in props else 100.0
            scale = 1.0
            if props.get('usesizescale', '').strip().lower() == 'true':
                # Points past RelativeTime 1 (the OrphD plume's 220 and 450) are only reached in part.
                points = [struct(k) for k in props if k.startswith('sizescale(')]
                scale = max([float(p.get('RelativeSize', 1)) / max(float(p.get('RelativeTime', 0)), 1.0) for p in points] + [1.0])
            pad = size * scale / 2
            for t in (0.0, life / 2, life):
                for corner in range(8):
                    p = [box[a][(corner >> a) & 1] + offset[a] for a in range(3)]
                    for vc in range(8):
                        v = [velocity[a][(vc >> a) & 1] for a in range(3)]
                        local = [p[a] + v[a] * travel(loss[a], t) for a in range(3)]
                        world = rotate(axes, local) if actor_space else tuple(local)
                        for a in range(3):
                            c = origin[a] + world[a] + 0.5 * acceleration[a] * t * t
                            spread = radial * travel(min(loss), t) + pad
                            low[a], high[a] = min(low[a], c - spread), max(high[a], c + spread)
    return low, high


def preview_hint(entry, override):
    low, high = extent(entry)
    target = [round((l + h) / 2, 1) for l, h in zip(low, high)]
    radius = min(max(math.dist(low, high) / 2, 24.0), 6000.0)
    hint = {'target': target, 'radius': round(radius), 'distance': round(radius * 2.2),
            'pitch': -2730, 'yaw': 8192}
    hint.update(override or {})
    return hint


# ---- entries ---------------------------------------------------------------------------------------

def dependencies(entry):
    found = {actor['class'] for actor in entry['actors']}
    for actor in entry['actors']:
        for _, path in references(actor['text']):
            if not path.lower().startswith('assembly.'):
                found.add(path)
    return sorted(found, key=str.lower)


def apply_tweak(entry, tweak):
    op = tweak['op']
    actors = entry['actors']
    targets = [a for a in actors if a['name'] == tweak['actor']] if 'actor' in tweak else actors[:1] if op in ('set', 'remove', 'tag') else actors
    if not targets:
        fail(f'{entry["id"]}: no actor {tweak.get("actor")}')
    if op == 'texture':
        hits = 0
        for actor in actors:
            pattern = re.compile(r"(\w+')(\"?)" + re.escape(tweak['from']) + r"(\"?')", re.I)
            actor['text'], n = pattern.subn(lambda m: m.group(1) + m.group(2) + tweak['to'] + m.group(3), actor['text'])
            hits += n
        if not hits:
            fail(f'{entry["id"]}: {tweak["from"]} is not used')
    elif op == 'untag':
        for actor in targets:
            if get_line(actor['text'], 'Tag') is not None:
                actor['text'] = remove_line(actor['text'], 'Tag')
            actor['tag'] = 'None'
    elif op == 'tag':
        for actor in targets:
            actor['text'] = set_line(actor['text'], 'Tag', tweak['value'])
            actor['tag'] = tweak['value']
    elif op == 'offset':
        for actor in targets:
            actor['position'] = [p + d for p, d in zip(actor['position'], tweak['by'])]
            actor['text'] = set_line(actor['text'], 'Location', vector_text(actor['position']))
        entry['pivot'] = [p - d for p, d in zip(entry['pivot'], tweak['by'])]
    elif op in ('set', 'remove'):
        for actor in targets:
            names = emitter_names(actor['text'])
            if tweak['emitter'] >= len(names):
                fail(f'{entry["id"]}: {actor["name"]} has no Emitters({tweak["emitter"]})')
            block = names[tweak['emitter']]
            actor['text'] = set_line(actor['text'], tweak['property'], tweak['value'], block) if op == 'set' else remove_line(actor['text'], tweak['property'], block)
    elif op in ('actorset', 'actorremove'):
        for actor in targets:
            actor['text'] = set_line(actor['text'], tweak['property'], tweak['value']) if op == 'actorset' else remove_line(actor['text'], tweak['property'])
    else:
        fail(f'{entry["id"]}: unknown tweak {op}')


def build_entry(spec):
    path = CAPTURES / f'{slug(spec)}.json'
    if not path.exists():
        fail(f'{spec["id"]}: no capture {path.name}; run jobs and collect first')
    captured = json.loads(path.read_text(encoding='utf-8'))
    wanted = {f'{spec["source"]["map"]}.{a}'.lower() for a in spec['source']['actors']}
    if {s.lower() for s in captured['source'].split(',')} != wanted:
        fail(f'{spec["id"]}: capture is of {captured["source"]}, not {sorted(wanted)}')
    if captured.get('bindings') or captured.get('local'):
        fail(f'{spec["id"]}: capture has bindings or map-local assets')
    if [d.lower() for d in dependencies(captured)] != [d.lower() for d in sorted(captured['dependencies'], key=str.lower)]:
        fail(f'{spec["id"]}: the reference scan {dependencies(captured)} disagrees with the capture {captured["dependencies"]}')
    decoded = MANIFEST_DECODED.get(spec['source']['map'])
    if decoded:
        verify_decoded(spec, captured, (HERE / decoded).read_text(encoding='latin1'))
    entry = copy.deepcopy(captured)
    for tweak in spec.get('tweaks', []):
        apply_tweak(entry, tweak)
    for actor in entry['actors']:
        if not actor['class'].lower().endswith('.emitter'):
            fail(f'{spec["id"]}: {actor["name"]} is a {actor["class"]}; built-ins hold emitters only')
        for key in ('Platform', 'Group', 'AttachTag', 'Base'):
            if get_line(actor['text'], key) is not None:
                fail(f'{spec["id"]}: {actor["name"]} still sets {key}')
        disabled = 'disabled=true' in actor['text'].lower()
        if (spec['mode'] == 'triggered') != (disabled and actor['tag'] not in ('', 'None')):
            fail(f'{spec["id"]}: a triggered entry needs Disabled sub-emitters and a Tag, and only it')
    ordered = collections.OrderedDict()
    for key in ('id', 'name', 'category', 'description'):
        ordered[key] = spec[key]
    ordered['source'] = captured['source']
    ordered['pivot'] = [round(v, 3) for v in entry['pivot']]
    ordered['dependencies'] = dependencies(entry)
    ordered['bindings'] = []
    ordered['actors'] = entry['actors']
    ordered['preview'] = preview_hint(entry, spec.get('preview'))
    return ordered


class PackageIndex:
    def __init__(self, game, scctpkg):
        if game.resolve() == LIVE.resolve():
            fail('refusing to read the live installation; use the UE Upgrade test copy')
        self.game, self.files, self.loaded, self.verified = game, {}, {}, 0
        for folder, extension in PACKAGE_FOLDERS:
            directory = game / folder
            for f in directory.glob('*' + extension) if directory.exists() else []:
                self.files.setdefault(f.stem.lower(), f)
        self.maps = {f.stem.lower() for sub in ('Maps', 'MapsEd') for f in (game / 'Packages' / sub).glob('*.sdc')}
        self.reader = None
        if scctpkg and (scctpkg / 'scctpkg' / 'package.py').exists():
            sys.path.insert(0, str(scctpkg))
            from scctpkg.package import Package
            self.reader = Package

    def check(self, entry_id, path):
        package = path.split('.')[0].lower()
        if package in self.maps or package == 'mylevel':
            fail(f'{entry_id}: {path} is stored in a map')
        found = self.files.get(package)
        if not found:
            fail(f'{entry_id}: no shared package holds {path}')
        if self.reader and found.suffix.lower() != '.u':
            if found not in self.loaded:
                try:
                    pkg = self.reader.load(found)
                    self.loaded[found] = {e.full_name.lower() for e in pkg.exports}
                except Exception as error:  # noqa: BLE001 - an unreadable package only skips the object check
                    print(f'  note: {found.name} not readable here ({error}); object check skipped')
                    self.loaded[found] = None
            names = self.loaded[found]
            if names is not None and path.lower() not in names:
                fail(f'{entry_id}: {found.name} has no {path}')
            self.verified += names is not None
        return found


def build(args):
    manifest = load_manifest()
    index = PackageIndex(pathlib.Path(args.game), pathlib.Path(args.scctpkg) if args.scctpkg else None)
    entries, rows = [], []
    for spec in manifest['entries']:
        entry = build_entry(spec)
        packages = sorted({index.check(spec['id'], d).name for d in entry['dependencies']})
        entries.append(entry)
        rows.append((entry['id'], entry['name'], entry['category'], spec['mode'], ', '.join(p for p in packages if not p.endswith('.u'))))
    document = {'version': 1, 'emitters': entries}
    text = json.dumps(document, ensure_ascii=True, separators=(',', ':'))
    if ')EL"' in text:
        fail('the JSON contains the raw-literal terminator )EL"')
    parts = [text[i:i + LITERAL] for i in range(0, len(text), LITERAL)]
    lines = [
        '#pragma once',
        '#include <string>',
        '',
        '// Generated by tools/emitter_library/build_defaults.py from defaults_manifest.json and the',
        '// native captures in tools/emitter_library/captures; do not edit. The built-in Emitter Library',
        f'// ({len(entries)} entries, {len(text)} bytes of JSON) in raw literals below the 16 KB string-literal limit.',
        'namespace Workflow::EmitterLibrary',
        '{',
        'inline std::string DefaultsText()',
        '{',
        '    static const char* const parts[]={',
    ]
    lines += [f'        R"EL({part})EL",' for part in parts]
    lines += ['    };', '    std::string text;for(const auto* part:parts)text+=part;return text;', '}', '}', '']
    with open(args.header, 'w', encoding='ascii', newline='\r\n') as out:
        out.write('\n'.join(lines))
    for row in rows:
        print('  %-34s %-40s %-20s %-10s %s' % row)
    print(f'{args.header}: {len(entries)} entries, {len(text)} bytes of JSON in {len(parts)} literals, '
          f'{pathlib.Path(args.header).stat().st_size} bytes on disk'
          + (f'; {index.verified} texture, sound and mesh references found in their packages' if index.reader
             else ' (object check skipped: scctpkg not found)'))
    return entries


# ---- headless jobs ---------------------------------------------------------------------------------

def run_command(job, out, sources, worktree, extra=''):
    tools = worktree / 'out' / 'tools'
    maps = ';'.join(str(s) for s in sources)
    return (f'pwsh -NoProfile -ExecutionPolicy Bypass -File "{tools / "Run-EmitterLibHeadless.ps1"}" '
            f'-EditorDll "{worktree / "out" / "bin" / "Reloaded.Editor.dll"}" -EditorRepo "{worktree}" '
            f'-Job "{job}" -OutDir "{out}" -TimeoutSeconds 900 -Clean{extra}' + (f' -SourceMap "{maps}"' if maps else ''))


def capture_lines(manifest, map_name):
    lines = []
    for spec in manifest['entries']:
        if spec['source']['map'] != map_name:
            continue
        name = slug(spec)
        members = [{'path': f'MyLevel.{a}', 'class': 'Engine.Emitter'} for a in spec['source']['actors']]
        request = {'op': 'emitterlib.capture', 'members': members, 'map': map_name}
        lines.append(f'workflow@{name} {json.dumps(request, separators=(",", ":"))}')
        lines.append(f'expect {name} | /dependencies | lacks | "MyLevel"')
        lines.append(f'expect {name} | /actors | == | {len(members)}')
        lines.append(f'dump {name} {{OUT}}/capture_{name}.json')
    return lines


def jobs(args):
    """Editable (MapsEd) maps: load and capture. Compiled-only maps: MapRecovery exports the cooked
    level's actors natively (Recovery\\<map>\\Actors.t3d), stopped before it writes any package; the
    imports command then turns those into capture jobs."""
    manifest = load_manifest()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    game, worktree = pathlib.Path(args.game), pathlib.Path(args.worktree)
    for number, group in enumerate(manifest['jobs'], 1):
        lines = [f'# Emitter Library defaults: native capture of {", ".join(group)} (build_defaults.py jobs)']
        sources = []
        for map_name in group:
            sources.append(game / 'Packages' / 'MapsEd' / f'{map_name}.sdc')
            lines.append(f'load {{MAPSEDDIR}}\\{map_name}.sdc')
            lines += capture_lines(manifest, map_name)
        job = out / f'capture_{number}.txt'
        job.write_text('\n'.join(lines) + '\n', encoding='ascii')
        print(run_command(job, worktree / 'out' / 'tools' / f'run_capture_{number}', sources, worktree))
    for map_name in compiled_maps(manifest, recoverable=True):
        lines = [f'# Emitter Library defaults: {map_name} is compiled; MapRecovery exports its actors (build_defaults.py jobs)',
                 f'recoveractors {{MAPSEDDIR}}\\{map_name}.sdc | {{MAPSEDDIR}}\\{map_name}_Library.sdc',
                 f'copyfile {{MAPSEDDIR}}\\Recovery\\{map_name}_Library\\Actors.t3d | {{OUT}}/Actors_{map_name}.t3d']
        job = out / f'recover_{map_name}.txt'
        job.write_text('\n'.join(lines) + '\n', encoding='ascii')
        print(run_command(job, worktree / 'out' / 'tools' / f'run_recover_{map_name}', [game / 'Packages' / 'Maps' / f'{map_name}.sdc'],
                          worktree, ' -LinkStaticMeshes'))


def actor_blocks(t3d, names):
    """The named actors of a native MAP EXPORT, ready for MAP IMPORT into a new map: the export's
    diagnostic name markers are removed, and so are lines that only make sense in the source level
    (its LevelInfo, zone, volume and net state, the object's own Name) and the cooked actor's
    explicit editor icon (Texture=S_Emitter, the class default in the editor)."""
    text = re.sub(r'\xa7\(\d+\)', '', t3d)
    wanted = {n.lower() for n in names}
    blocks, current, depth = {}, None, 0
    for line in text.splitlines():
        s = line.strip()
        low = s.lower()
        if current is None:
            match = re.match(r'Begin Actor Class=\S+ Name=(\S+)', s, re.I)
            if match and match.group(1).lower() in wanted:
                current, depth = [line], 1
                blocks[match.group(1).lower()] = current
            continue
        if low.startswith('begin '):
            depth += 1
        elif low.startswith('end '):
            depth -= 1
        elif depth == 1 and key_of(s) in ('level', 'region', 'physicsvolume', 'base', 'owner', 'bmustinitnetchannels', 'name', 'texture'):
            continue
        current.append(line)
        if depth == 0:
            current = None
    missing = wanted - set(blocks)
    if missing:
        fail(f'the export has no {sorted(missing)}')
    return [blocks[n.lower()] for n in names]


# ---- decoding a compiled map whose actors MapRecovery cannot export -----------------------------------
# Enum values confirmed against native exports of the stock maps (DrawStyle=1 is PTDS_AlphaBlend in the
# StatD capture, and so on); a value not listed here stops the decode rather than guess a name.
ENUMS = {
    'drawstyle': {1: 'PTDS_AlphaBlend', 3: 'PTDS_Translucent', 6: 'PTDS_Brighten'},
    'coordinatesystem': {1: 'PTCS_Relative'},
    'userotationfrom': {1: 'PTRS_Actor'},
    'usedirectionas': {1: 'PTDU_Up', 2: 'PTDU_Right', 4: 'PTDU_Normal'},
    'getvelocitydirectionfrom': {3: 'PTVD_AddRadial'},
}
OBJECT_PROPERTIES = {'texture', 'textureinthermicvisionpsx2', 'staticmesh', 'ambientsound', 'emitters'}
DROPPED_ACTOR_PROPERTIES = {'level', 'region', 'group', 'bmustinitnetchannels', 'emitters'}


def decode_value(pkg, key, value):
    low = key.lower()
    if isinstance(value, bool):
        return 'True' if value else 'False'
    if low in OBJECT_PROPERTIES and isinstance(value, int):
        if value >= 0:
            fail(f'{key} names an object inside the map')
        parts, ref = [], value
        while ref < 0:
            parts.append(pkg.imports[-ref - 1].name)
            ref = pkg.imports[-ref - 1].outer
        return f"{pkg.imports[-value - 1].class_name}'{'.'.join(reversed(parts))}'"
    if low in ENUMS:
        if value not in ENUMS[low]:
            fail(f'{key}={value} has no confirmed enum name')
        return ENUMS[low][value]
    if isinstance(value, float):
        return '%.6f' % value
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return f'"{value}"'
    if isinstance(value, tuple) and low == 'rotation':
        return '(Pitch=%d,Yaw=%d,Roll=%d)' % value
    if isinstance(value, tuple) and len(value) == 3:
        return '(X=%.6f,Y=%.6f,Z=%.6f)' % value
    if isinstance(value, dict):
        kind = value.get('_struct')
        if kind == 'ParticleColorScale':
            r, g, b, a = value['Color']  # the reader returns R,G,B,A (CpusS: [200,150,0,200] exports as G=150,R=200,A=200)
            return '(RelativeTime=%.6f,Color=(B=%d,G=%d,R=%d,A=%d))' % (value['RelativeTime'], b, g, r, a)
        members = [(k, v) for k, v in value.items() if k != '_struct']
        return '(' + ','.join(f'{k}={decode_value(pkg, k, v)}' for k, v in members) + ')'
    fail(f'{key}: cannot write {value!r}')


def decode(args):
    """Writes a MAP EXPORT-style T3D of compiled-map actors and their sub-emitters from the package
    itself, for maps MapRecovery stops on before its actor export. The build then checks the native
    capture of the imported text against it line by line."""
    sys.path.insert(0, str(args.scctpkg))
    from scctpkg.package import Package
    from scctpkg.properties import read_properties, skip_state_frame
    pkg = Package.load(pathlib.Path(args.game) / 'Packages' / 'Maps' / f'{args.map}.sdc')

    def properties(name):
        export = pkg.find_export(name)
        if export is None:
            fail(f'{args.map} has no {name}')
        reader = pkg.export_reader(export)
        skip_state_frame(reader, export.flags)
        return export, read_properties(reader)

    lines = ['Begin Map']
    for actor in args.actors:
        export, props = properties(actor)
        lines.append(f'Begin Actor Class={export.class_name} Name={actor}')
        references = []
        for i, ref in enumerate(props.get('Emitters', [])):
            sub = pkg.exports[ref - 1]
            _, sub_props = properties(sub.name)
            lines.append(f'    Begin Object Class={sub.class_name} Name={sub.name}')
            for key, value in sub_props.items():
                for k, v in ([(f'{key}({j})', x) for j, x in enumerate(value)] if isinstance(value, list) else [(key, value)]):
                    lines.append(f'        {k}={decode_value(pkg, key, v)}')
            lines.append(f'        Name="{sub.name}"')
            lines.append('    End Object')
            references.append(f"    Emitters({i})={sub.class_name}'MyLevel.{sub.name}'")
        lines += references
        for key, value in props.items():
            if key.lower() not in DROPPED_ACTOR_PROPERTIES:
                lines.append(f'    {key}={decode_value(pkg, key, value)}')
        lines.append('End Actor')
    lines.append('End Map')
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text('\r\n'.join(lines) + '\r\n', encoding='latin1', newline='')
    print(f'{out}: {len(args.actors)} actor(s) decoded from {args.map}.sdc')


def verify_decoded(spec, captured, source):
    """The native capture of imported decoded text must hold every decoded property (a compiled
    package stores only values that differ from the class defaults, so each one exports again), with
    enum, object and bool lines verbatim and every number it lists equal to the decoded value. An
    import that rejected a value would have left the default, which native export omits."""
    blocks = actor_blocks(source, spec['source']['actors'])
    for block, actor in zip(blocks, sorted(captured['actors'], key=lambda a: spec['source']['actors'].index(a['name']))):
        text = actor['text']
        decoded_lines = split_lines('\n'.join(block))
        for _, depth, s, owner in scan(decoded_lines):
            key = key_of(s)
            if not key or s.lower().startswith(('begin ', 'end ')) or key in ('name', 'location', 'rotation', 'tag') or key.startswith('emitters('):
                continue
            want = s.split('=', 1)[1]
            got = get_line(text, s.split('=', 1)[0], owner if depth == 2 else None)
            base = key.split('(')[0]
            if got is None:
                fail(f'{spec["id"]}: {owner or actor["name"]} {key}={want} did not survive native import')
            if base in ENUMS or base in OBJECT_PROPERTIES or want in ('True', 'False'):
                if got != want:
                    fail(f'{spec["id"]}: {owner or actor["name"]} {key}={want} came back as {got}')
            elif not numbers_match(parse_struct(want), parse_struct(got)):
                fail(f'{spec["id"]}: {owner or actor["name"]} {key}={want} came back as {got}')


def numbers_match(want, got):
    if isinstance(got, dict):
        return isinstance(want, dict) and all(k in want and numbers_match(want[k], v) for k, v in got.items())
    try:
        return abs(float(want) - float(got)) <= 1e-4 * max(1.0, abs(float(want)))
    except (TypeError, ValueError):
        return want == got


def imports(args):
    manifest = load_manifest()
    out = pathlib.Path(args.out)
    worktree = pathlib.Path(args.worktree)
    found = {}
    for run in args.runs:
        for f in pathlib.Path(run).glob('Actors_*.t3d'):
            found[f.stem[len('Actors_'):]] = f
    lines = ['# Emitter Library defaults: actors of compiled maps, as MapRecovery exported them (or as decoded from the',
             '# package where recovery stops early), imported into a new map and captured (build_defaults.py imports)']
    for map_name in compiled_maps(manifest):
        decoded = manifest.get('decoded', {}).get(map_name)
        if not decoded and map_name not in found:
            fail(f'no Actors_{map_name}.t3d in {args.runs}')
        names = [a for spec in manifest['entries'] if spec['source']['map'] == map_name for a in spec['source']['actors']]
        blocks = actor_blocks((HERE / decoded if decoded else found[map_name]).read_text(encoding='latin1'), names)
        t3d = out / f'import_{map_name}.t3d'
        t3d.write_text('Begin Map\r\n' + ''.join('\r\n'.join(b) + '\r\n' for b in blocks) + 'End Map\r\n', encoding='latin1', newline='')
        lines.append(f'import {t3d}')
        lines += capture_lines(manifest, map_name)
    job = out / 'capture_compiled.txt'
    job.write_text('\n'.join(lines) + '\n', encoding='ascii')
    print(run_command(job, worktree / 'out' / 'tools' / 'run_capture_compiled', [], worktree))


def collect(args):
    manifest = load_manifest()
    CAPTURES.mkdir(exist_ok=True)
    wanted = {slug(spec) for spec in manifest['entries']}
    got = set()
    for run in args.runs:
        for f in pathlib.Path(run).glob('capture_*.json'):
            name = f.stem[len('capture_'):]
            if name not in wanted:
                continue
            data = json.loads(f.read_text(encoding='utf-8'))
            (CAPTURES / f'{name}.json').write_text(json.dumps(data, indent=1, ensure_ascii=True) + '\n', encoding='ascii', newline='\n')
            got.add(name)
    for stale in CAPTURES.glob('*.json'):
        if stale.stem not in wanted:
            print(f'  stale capture {stale.name} (not in the manifest)')
    print(f'collected {len(got)} captures; missing: {sorted(wanted - got) or "none"}')


def verifyjob(args):
    manifest = load_manifest()
    entries = [build_entry(spec) for spec in manifest['entries']]
    specs = {spec['id']: spec for spec in manifest['entries']}
    lines = ['# Emitter Library defaults: every built-in placed natively (build_defaults.py verifyjob).',
             '# Phase 1: each entry alone in a new map. Phase 2: all of them in one map, saved and loaded again.',
             'workflow@list {"op":"emitterlib.list"}',
             f'expect list | / | >= | {len(entries)}']
    for i, entry in enumerate(entries):
        lines.append(f'expect list | /{i}/id | == | "{entry["id"]}"')
    loaded = {'engine', 'sfx'}  # Engine holds the class; SBase imports sfx, so it is always loaded
    for entry in entries:
        name = slug(entry)
        lines += [f'# {entry["id"]}', 'new', 'workflow@before {"op":"actors"}']
        # The first entry to use a package finds it unloaded; placing loads it.
        for dependency in entry['dependencies']:
            if dependency.split('.')[0].lower() not in loaded:
                lines.append(f'reject {{"op":"usages","asset":"{dependency}"}}')
        loaded |= {d.split('.')[0].lower() for d in entry['dependencies']}
        lines += [f'workflow@p_{name} {{"op":"emitterlib.place","id":"{entry["id"]}","position":[0,0,256]}}',
                  f'expect p_{name} | / | == | {len(entry["actors"])}',
                  f'dump p_{name} {{OUT}}/placed_{name}.json']
        lines += checks(entry, name, specs[entry['id']])
        lines += [f'export {{OUT}}/entry_{name}.t3d']
    lines += ['# Phase 2', 'new']
    for i, entry in enumerate(entries):
        name = slug(entry)
        position = [(i % 6) * 1536, (i // 6) * 1536, 512]
        lines += [f'workflow@a_{name} {{"op":"emitterlib.place","id":"{entry["id"]}","position":{json.dumps(position)}}}',
                  f'expect a_{name} | / | == | {len(entry["actors"])}',
                  f'dump a_{name} {{OUT}}/all_{name}.json']
    lines += ['workflow@allactors {"op":"actors"}', 'dump allactors {OUT}/all_actors.json',
              'export {OUT}/all_placed.t3d', 'save {MAPSED}', 'new', 'load {MAPSED}',
              'workflow@reloaded {"op":"actors"}', 'dump reloaded {OUT}/all_reloaded_actors.json',
              'export {OUT}/all_reloaded.t3d']
    for entry in entries:
        name = slug(entry)
        for k, actor in enumerate(entry['actors']):
            lines.append(f'expect reloaded | / | has | {{"$from":"a_{name}","pointer":"/{k}/path"}}')
            count = len(emitter_names(actor['text']))
            lines += [f'workflow@r_{name}_{k} {{"op":"magic.inspect","actor":{{"$from":"a_{name}","pointer":"/{k}"}}}}',
                      f'expect r_{name}_{k} | /values/Emitters | == | {count}']
            lines += [f'expect r_{name}_{k} | /values/Emitters/{j} | has | "MyLevel.RE_"' for j in range(count)]
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    job = out / 'verify_defaults.txt'
    job.write_text('\n'.join(lines) + '\n', encoding='ascii')
    print(run_command(job, pathlib.Path(args.worktree) / 'out' / 'tools' / 'run_verify_defaults', [], pathlib.Path(args.worktree)))


def checks(entry, name, spec):
    """Per placed actor: its Emitters(i) resolve to live sub-emitters in the map, and each texture,
    mesh and sound it names is loaded and reached from that actor (usages walks actor -> sub-emitter
    -> asset)."""
    lines = []
    for k, actor in enumerate(entry['actors']):
        count = len(emitter_names(actor['text']))
        lines += [f'workflow@i_{name}_{k} {{"op":"magic.inspect","actor":{{"$from":"p_{name}","pointer":"/{k}"}}}}',
                  f'expect i_{name}_{k} | /values/Emitters | == | {count}']
        lines += [f'expect i_{name}_{k} | /values/Emitters/{j} | has | "SpriteEmitter\'MyLevel.RE_"' for j in range(count)]
        if spec['mode'] == 'triggered':
            lines.append(f'expect i_{name}_{k} | /values/Tag | has | "{actor["tag"]}"')
        # usages finds an asset only once it is loaded (Find); it lists actors through materials and
        # meshes only, so the sub-emitters' textures are compared in the export by checkexport.
        assets = sorted({path for cls, path in references(actor['text']) if not path.lower().startswith('assembly.')}, key=str.lower)
        lines += [f'workflow@u_{name}_{k}_{j} {{"op":"usages","asset":"{asset}"}}' for j, asset in enumerate(assets)]
    return lines


# ---- export comparison -----------------------------------------------------------------------------

def exported_actors(t3d):
    """name -> (actor lines, {sub-object name: lines}) from a MAP EXPORT file."""
    text = re.sub(r'\xa7\(\d+\)', '', t3d)
    result, current = {}, None
    for raw in text.splitlines():
        s = raw.strip()
        low = s.lower()
        if low.startswith('begin actor'):
            match = re.search(r'\bName=("?)([^\s"]+)\1', s, re.I)
            current = (match.group(2), [], {})
            result[current[0].lower()] = current
            block = None
            continue
        if current is None:
            continue
        if low.startswith('begin object'):
            match = re.search(r'\bName=("?)([^\s"]+)\1', s, re.I)
            block = match.group(2).lower()
            current[2][block] = []
        elif low.startswith('end object'):
            block = None
        elif low.startswith('end actor'):
            current = None
        elif block is not None:
            current[2][block].append(s)
        else:
            current[1].append(s)
    return result


def compare(entry, placed, exported, label):
    """Every property line of each sub-emitter and every effect line of the actor survive, with
    enum names, textures and sounds as the entry has them."""
    problems = []
    skip_actor = {'location', 'rotation', 'tag', 'emitters'}
    for k, actor in enumerate(entry['actors']):
        live = placed[k]['path'].split('.')[-1]
        prefix = live[:len(live) - len(actor['name'])]
        found = exported.get(live.lower())
        if not found:
            problems.append(f'{label}: {live} is not in the export')
            continue
        _, actor_lines, blocks = found
        lines = split_lines(actor['text'])
        expected_actor = [s for _, depth, s, _ in scan(lines) if depth == 1 and '=' in s and key_of(s) not in skip_actor
                          and not key_of(s).startswith('emitters(') and not s.lower().startswith(('begin ', 'end '))]
        for line in expected_actor:
            if line not in actor_lines:
                problems.append(f'{label}: {live} lost "{line}"')
        tag = [s for s in actor_lines if key_of(s) == 'tag']
        if actor['tag'] not in ('', 'None') and not any(prefix + actor['tag'] in s for s in tag):
            problems.append(f'{label}: {live} Tag is {tag}, expected {prefix + actor["tag"]}')
        for obj in emitter_names(actor['text']):
            got = blocks.get((prefix + obj).lower())
            if got is None:
                problems.append(f'{label}: {live} has no sub-emitter {prefix + obj}')
                continue
            want = [s for _, depth, s, owner in scan(lines) if depth == 2 and (owner or '').lower() == obj.lower()
                    and '=' in s and key_of(s) != 'name' and not s.lower().startswith(('begin ', 'end '))]
            for line in want:
                if line in got:
                    continue
                if zero(line):
                    TOLERATED[line.split('=', 1)[0]] += 1
                else:
                    problems.append(f'{label}: {prefix + obj} lost "{line}"')
    return problems


TOLERATED = collections.Counter()


def zero(line):
    """A line whose numbers are all zero. Stock maps export some values that print as 0.000000 but
    are not exactly zero (SizeScaleRepeats); imported, they become the class default 0 and native
    export leaves them out, which changes nothing."""
    numbers = re.findall(r'-?\d+(?:\.\d+)?', line.split('=', 1)[1])
    return bool(numbers) and all(float(n) == 0 for n in numbers) and not re.search(r"'", line)


def checkexport(args):
    manifest = load_manifest()
    run = pathlib.Path(args.run)
    problems, compared = [], 0
    all_placed = exported_actors((run / 'all_placed.t3d').read_text(encoding='latin1'))
    all_reloaded = exported_actors((run / 'all_reloaded.t3d').read_text(encoding='latin1'))
    for spec in manifest['entries']:
        entry = build_entry(spec)
        name = slug(spec)
        single = json.loads((run / f'placed_{name}.json').read_text())
        problems += compare(entry, single, exported_actors((run / f'entry_{name}.t3d').read_text(encoding='latin1')), f'{name} alone')
        together = json.loads((run / f'all_{name}.json').read_text())
        problems += compare(entry, together, all_placed, f'{name} in the full map')
        problems += compare(entry, together, all_reloaded, f'{name} after save and load')
        compared += 3
    for p in problems:
        print('  ' + p)
    if TOLERATED:
        print('  zero values left to the class default: ' + ', '.join(f'{k} x{n}' for k, n in sorted(TOLERATED.items())))
    print(f'{compared} placements compared; {len(problems)} problem(s)')
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', default=str(GAME), help='game copy holding the stock maps and packages')
    parser.add_argument('--scctpkg', default=str(SCCTPKG), help='SCCT_Versus_UE5_Bridge checkout with the scctpkg reader ("" to skip)')
    parser.add_argument('--worktree', default=str(ROOT), help='repository root whose out\\tools harness runs the jobs')
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('jobs').add_argument('--out', default=str(ROOT / 'out' / 'tools' / 'jobs'))
    decode_parser = sub.add_parser('decode')
    decode_parser.add_argument('map')
    decode_parser.add_argument('actors', nargs='+')
    decode_parser.add_argument('--out', required=True)
    imports_parser = sub.add_parser('imports')
    imports_parser.add_argument('runs', nargs='+')
    imports_parser.add_argument('--out', default=str(ROOT / 'out' / 'tools' / 'jobs'))
    sub.add_parser('collect').add_argument('runs', nargs='+')
    sub.add_parser('build').add_argument('--header', default=str(HEADER))
    sub.add_parser('verifyjob').add_argument('--out', default=str(ROOT / 'out' / 'tools' / 'jobs'))
    sub.add_parser('checkexport').add_argument('run')
    args = parser.parse_args()
    if pathlib.Path(args.game).resolve() == LIVE.resolve():
        fail('refusing to use the live installation; use the UE Upgrade test copy')
    commands = {'jobs': jobs, 'decode': decode, 'imports': imports, 'collect': collect, 'build': build, 'verifyjob': verifyjob,
                'checkexport': checkexport}
    result = commands[args.command](args)
    return result if isinstance(result, int) else 0


if __name__ == '__main__':
    sys.exit(main())
