"""Synthetic Emitter Library effect packs for tests, made from the built-in entries.

An effect pack is <editor>/System/ReloadedEditor/EmitterPacks/<file>.json (contract at the
top of Reloaded.Editor/EmitterLibraryModel.h). This script writes test packs only: every
entry is a copy of built-in entries (stock SCCT emitters), so the packs place in any map.

  python tools/emitter_library/make_test_pack.py big --out big.json [--id testbig] [--count 900] [--max-parts 5]
      Entry i combines 1 + i % max-parts built-ins (renamed, side by side), so 900 entries
      with max-parts 5 are about 8 MB, the size of a converted game's effect set.
  python tools/emitter_library/make_test_pack.py missing --out small.json [--id testmissing]
      Five entries: three need packages that are not installed (SWRC_testmissing.utx and
      SWRC_testmissing_SM.usx), two do not; plus three damaged entries (another pack's id,
      a repeated id, no actors) that the editor skips and reports.
  python tools/emitter_library/make_test_pack.py broken --out broken.json      (not JSON)
  python tools/emitter_library/make_test_pack.py newer --out newer.json        (version 2)
  python tools/emitter_library/make_test_pack.py duplicate --out dupe.json --id testbig
      A valid one-entry pack reusing another pack's id; the first file by name is listed.
  python tools/emitter_library/make_test_pack.py olduser --out emitter_library.json
      A user file as editors before packs wrote it (no hiddenPacks): one user entry, one
      hidden built-in (builtin.fire_with_smoke).
  python tools/emitter_library/make_test_pack.py jobs --out out\tools\packs_test
      All of the above (big.json, small.json, broken.json, newer.json, dupe.json,
      older_user.json) and the headless jobs of pack_jobs/ (ops, ui, scroll) with {PACKS}
      replaced by that folder; prints the command that runs each job.

Run from the repository root (the built-ins are read from Reloaded.Editor/EmitterLibraryDefaults.gen.h).
"""
import argparse
import copy
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULTS = ROOT / 'Reloaded.Editor' / 'EmitterLibraryDefaults.gen.h'


def builtins():
    text = DEFAULTS.read_text(encoding='utf-8')
    parts = re.findall(r'R"EL\((.*?)\)EL"', text, re.S)
    entries = json.loads(''.join(parts))['emitters']
    if len(entries) < 30:
        sys.exit('fewer built-ins than expected in ' + str(DEFAULTS))
    return entries


def slug(text, limit):
    out = re.sub(r'[^a-z0-9]+', '_', text.lower()).strip('_')
    return out[:limit].rstrip('_') or 'x'


def renamed(entry, suffix, offset):
    """The entry's actors with every actor and sub-object name given suffix, moved by offset."""
    actors = copy.deepcopy(entry['actors'])
    names = []
    for actor in actors:
        names.append(actor['name'])
        names += re.findall(r'Begin Object Class=\S+ Name=(\S+)', actor['text'])
    if not suffix:
        return actors
    pattern = re.compile(r'(?<![A-Za-z0-9_])(' + '|'.join(re.escape(n) for n in sorted(set(names), key=len, reverse=True)) + r')(?![A-Za-z0-9_])')
    for actor in actors:
        actor['text'] = pattern.sub(lambda m: m.group(1) + suffix, actor['text'])
        actor['name'] += suffix
        actor['path'] = 'Assembly.' + actor['name']
        actor['position'] = [actor['position'][0] + offset, actor['position'][1], actor['position'][2]]
    return actors


def combined(parts, entry_id, name, category):
    first = parts[0]
    entry = {'id': entry_id, 'name': name, 'category': category,
             'description': 'Test pack entry made from: ' + ', '.join(p['name'] for p in parts) + '.',
             'source': first.get('source', ''), 'pivot': first['pivot'], 'bindings': [], 'dependencies': [], 'actors': []}
    for k, part in enumerate(parts):
        entry['actors'] += renamed(part, '' if k == 0 else '_p%d' % k, 160 * k)
        for dependency in part['dependencies']:
            if dependency not in entry['dependencies']:
                entry['dependencies'].append(dependency)
    if 'preview' in first:
        entry['preview'] = first['preview']
    return entry


def pack(pack_id, name, entries, requires=(), description='Synthetic test pack made from built-in entries.'):
    return {'version': 1, 'pack': {'id': pack_id, 'name': name, 'description': description, 'requires': list(requires)}, 'emitters': entries}


def write(kind, out, pack_id=None, name=None, count=900, max_parts=5):
    """Writes one test file of the given kind to out."""
    source = builtins()
    if kind == 'broken':
        out.write_text('{"version":1,"pack":{"id":"broken","name":"Broken"},"emitters":[', encoding='utf-8')
        return
    if kind == 'olduser':
        entry = combined([source[1]], '0123456789abcdef0123456789abcdef', 'Older user entry', source[1]['category'])
        entry['modified'] = '1759000000000'
        out.write_text(json.dumps({'version': 1, 'emitters': [entry], 'hiddenBuiltins': ['builtin.fire_with_smoke']}, indent=2), encoding='utf-8')
        return
    if kind == 'newer':
        out.write_text(json.dumps(pack(pack_id or 'newer', name or 'Newer format', [])).replace('"version": 1', '"version": 2'), encoding='utf-8')
        return
    pack_id = pack_id or {'big': 'testbig', 'missing': 'testmissing', 'duplicate': 'testbig'}[kind]
    prefix = 'pack.' + pack_id + '.'
    entries = []
    if kind == 'big':
        for i in range(count):
            parts = [source[(i + k * 7) % len(source)] for k in range(1 + i % max(1, max_parts))]
            # Every seventh entry is in a category the built-ins do not use.
            category = 'Holograms' if i % 7 == 6 else parts[0]['category']
            entry_id = (prefix + 'e%d_' % i + slug(parts[0]['name'], 40))[:64].rstrip('_.-')
            entries.append(combined(parts, entry_id, '%s %d' % (parts[0]['name'], i), category))
        document = pack(pack_id, name or 'Test effects', entries, ['sfx.utx'])
    elif kind == 'missing':
        for i in range(5):
            entry = combined([source[i]], prefix + 'm%d_' % i + slug(source[i]['name'], 30), 'Needs test %d' % i, source[i]['category'])
            if i < 3:
                entry['dependencies'].append('SWRC_testmissing.FX.Placeholder' if i < 2 else 'SWRC_testmissing_SM.Props.Placeholder')
            entries.append(entry)
        other = copy.deepcopy(entries[3]); other['id'] = 'pack.otherpack.wrong'; other['name'] = 'Wrong pack id'
        repeated = copy.deepcopy(entries[4]); repeated['name'] = 'Repeated id'
        damaged = copy.deepcopy(entries[4]); damaged['id'] = prefix + 'damaged'; damaged['name'] = 'No actors'; damaged['actors'] = []
        entries += [other, repeated, damaged]
        document = pack(pack_id, name or 'Missing packages test', entries, ['SWRC_testmissing.utx', 'SWRC_testmissing_SM.usx', 'sfx.utx'])
    else:
        entries.append(combined([source[0]], prefix + 'dup', 'Duplicate pack id', source[0]['category']))
        document = pack(pack_id, name or 'Duplicate id', entries)
    out.write_text(json.dumps(document, indent=1), encoding='utf-8')
    print('%s: %d entries, %d bytes' % (out, len(entries), out.stat().st_size))


def jobs(folder):
    """Every test file the headless jobs use, and the jobs (pack_jobs/*.txt with {PACKS} filled in)."""
    folder.mkdir(parents=True, exist_ok=True)
    for kind, file, pack_id in [('big', 'big.json', None), ('missing', 'small.json', None), ('broken', 'broken.json', None),
                                ('newer', 'newer.json', None), ('duplicate', 'dupe.json', 'testbig'), ('olduser', 'older_user.json', None)]:
        write(kind, folder / file, pack_id)
    tools = ROOT / 'out' / 'tools'
    for template in sorted((Path(__file__).resolve().parent / 'pack_jobs').glob('*.txt')):
        job = folder / ('job_packs_%s.txt' % template.stem)
        job.write_text(template.read_text(encoding='utf-8').replace('{PACKS}', str(folder.resolve())), encoding='utf-8')
        print('pwsh -NoProfile -ExecutionPolicy Bypass -File %s -EditorDll %s -Job %s -EditorRepo %s -Probe %s -OutDir %s -Clean'
              % (tools / 'Run-EmitterLibHeadless.ps1', ROOT / 'out' / 'bin' / 'Reloaded.Editor.dll', job.resolve(), ROOT,
                 tools / 'probe_emitterpacks.cpp', tools / ('run_packs_' + template.stem)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('kind', choices=['big', 'missing', 'broken', 'newer', 'duplicate', 'olduser', 'jobs'])
    parser.add_argument('--out', required=True)
    parser.add_argument('--id')
    parser.add_argument('--name')
    parser.add_argument('--count', type=int, default=900)
    parser.add_argument('--max-parts', type=int, default=5)
    args = parser.parse_args()
    if args.kind == 'jobs':
        jobs(Path(args.out))
    else:
        write(args.kind, Path(args.out), args.id, args.name, args.count, args.max_parts)


if __name__ == '__main__':
    main()
