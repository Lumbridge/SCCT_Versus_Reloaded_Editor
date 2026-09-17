"""Map-independent entry point for the verified Vegas 241/66 conversion pipeline."""
import argparse, collections, html, json, math, os, re, runpy, struct, subprocess, sys
from pathlib import Path


def save(path, value):
    path.write_text(json.dumps(value, indent=2), encoding='utf-8')


def scan():
    from config import WORK, BATCH, MAP, COOKED
    from inspect_vegas import Package
    BATCH.mkdir(parents=True, exist_ok=True)
    p = Package(MAP)
    rows, overrides, errors = [], {}, []
    for i, e in enumerate(p.exports):
        if e['cls'] != 'Engine.StaticMeshComponent':
            continue
        try:
            if e['outer'] <= 0:
                raise ValueError('Component has no local actor owner')
            actor = p.exports[e['outer']-1]
            # Components in class defaults are not level placements.
            if 'PersistentLevel.' not in p.ref(i+1):
                continue
            ap, _ = p.props(actor, 28)
            cp, _ = p.props(e, 28)
            ref = struct.unpack_from('<i', p.data, e['offset']+24)[0]
            mesh = p.ref(ref)
            if ref:
                referenced=p.exports[ref-1] if ref>0 else p.imports[-ref-1]
                if referenced['cls'].split('.')[-1]!='StaticMesh':
                    raise ValueError('Unsupported component layout: mesh field points to '+referenced['cls']+' '+mesh)
            if ref > 0:
                mesh = MAP.stem + '.' + mesh
            row = dict(component=p.ref(i+1), actor=p.ref(e['outer']), actor_class=actor['cls'], mesh=mesh,
                       location=ap.get('Location', {}).get('value', [0,0,0]),
                       rotation=ap.get('Rotation', {}).get('value', [0,0,0]),
                       scale=ap.get('DrawScale', {}).get('value', 1),
                       scale3d=ap.get('DrawScale3D', {}).get('value', [1,1,1]),
                       component_properties={k:v['value'] for k,v in cp.items() if k in ['Rotation','Translation','Scale','Scale3D']})
            for prop in ['bAbsoluteTranslation','bAbsoluteRotation','bAbsoluteScale']:
                if cp.get(prop,{}).get('value',False):
                    raise ValueError('Absolute component transforms are not supported: '+prop)
            for prop in ['Translation','Rotation','Scale3D']:
                if prop in cp and not all(math.isfinite(v) for v in cp[prop]['value']):
                    raise ValueError('Non-finite component '+prop)
            if not math.isfinite(cp.get('Scale',{}).get('value',1)):
                raise ValueError('Non-finite component scale')
            if not all(math.isfinite(v) for key in ['location','rotation','scale3d'] for v in row[key]) or not math.isfinite(row['scale']):
                raise ValueError('Non-finite transform')
            if 'Materials' in cp:
                a = cp['Materials']; count = struct.unpack_from('<i',p.data,a['offset'])[0]
                assert 0 <= count <= 1024 and a['size'] == 4 + 4*count
                refs = struct.unpack_from('<'+'i'*count,p.data,a['offset']+4)
                overrides[row['component']] = [(MAP.stem+'.' if ref>0 else '')+p.ref(ref) for ref in refs]
            rows.append(row)
        except Exception as ex:
            errors.append(dict(component=p.ref(i+1), error=('Unsupported serialized record: ' if isinstance(ex,AssertionError) else '')+str(ex)))
    files = collections.defaultdict(list)
    for path in list(COOKED.rglob('*.uppc'))+[MAP]:
        files[path.stem].append(str(path))
    required = sorted({r['mesh'].split('.')[0] for r in rows if r['mesh'] != 'None'})
    for name in required:
        if len(files[name]) != 1:
            errors.append(dict(package=name, error='Missing or ambiguous media package', paths=files[name]))
    report = dict(map=str(MAP), version=hex(p.version), components=len(rows), placements=sum(r['mesh']!='None' for r in rows),
                  meshes=len({r['mesh'] for r in rows if r['mesh']!='None'}), packages=len(required), errors=errors,
                  geometry='BSP, terrain, lighting and gameplay are not converted.')
    save(WORK/'placements.json', rows); save(WORK/'overrides.json', overrides); save(WORK/'scan.json', report)
    print(json.dumps(report), flush=True)
    if errors:
        raise RuntimeError(f'{len(errors)} unsupported records or missing packages. See scan.json; conversion has not started.')
    if not report['placements']:
        raise RuntimeError('No supported static mesh placements found in this map.')
    return required, files


def prepare():
    from config import WORK, BATCH, CONFIG
    required, files = scan()
    for i, name in enumerate(required):
        print(f'Exporting package {i+1}/{len(required)}: {name}', flush=True)
        command = [CONFIG['umodel'], '-export', '-groups', '-game=r6v2', '-notex', '-noanim', '-nomesh',
                   '-out='+str(BATCH/'export'), files[name][0]]
        result = subprocess.run(command, capture_output=True, timeout=600)
        (BATCH/(name+'.log')).write_bytes(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError('UEViewer failed for '+name+'; see its log.')
    import inventory_full_batch as inventory_module
    inventory = json.loads((BATCH/'mesh-inventory.json').read_text())
    if any(r['status'] != 'mapped' for r in inventory):
        raise RuntimeError('Mesh section mapping failed; see mesh-inventory.json. No package was built.')
    # Include per-instance materials in the same package, eliminating a fragile second build.
    materials = json.loads((BATCH/'source-materials.json').read_text())
    for values in json.loads((WORK/'overrides.json').read_text()).values():
        for name in values:
            if name != 'None':
                materials[name] = inventory_module.material(name)
    save(BATCH/'source-materials.json', materials)
    runpy.run_module('prepare_batch_assets', run_name='__main__')
    from placements import prepare_placements
    prepare_placements()
    print('Prepared textures, meshes, materials and original placements.', flush=True)


def report():
    from config import WORK, BATCH, PACKAGE
    meshes=json.loads((BATCH/'delivery-inventory.json').read_text())
    materials=json.loads((BATCH/'material-conversion.json').read_text())
    placements=json.loads((WORK/'placement-inventory.json').read_text())
    missing=[dict(source=k, **v) for k,v in materials.items() if v['quality']!='source texture']
    save(WORK/'material-exceptions.json',missing)
    summary=dict(meshes=len(meshes),placements=len(placements),materials=len(materials),
                 material_exceptions=len(missing),package=PACKAGE,
                 limitations=['Static meshes only: no BSP, terrain, baked lighting, navigation or gameplay.',
                              'Source blend and emissive materials are approximated; shader graphs are not reproduced.',
                              'Unresolved colour uses a mauve checker. Review material-exceptions.json.'])
    save(WORK/'summary.json',summary)
    rows=''.join('<tr><td>'+html.escape(m['source'])+'</td><td>'+html.escape(m['native'])+'</td><td>'+str(m['triangles'])+'</td></tr>' for m in meshes)
    text='''<!doctype html><meta charset="utf-8"><title>R6V conversion report</title><style>body{font:16px Segoe UI;margin:40px;background:#121b27;color:#e5edf5}input{padding:12px;width:60%}table{border-collapse:collapse;width:100%;margin-top:24px}td,th{text-align:left;padding:10px;border-bottom:1px solid #394353}h1{color:#78d8bf}</style>'''
    text+='<h1>'+html.escape(PACKAGE)+'</h1><p>'+f'{len(meshes)} meshes · {len(placements)} placements · {len(missing)} material exceptions'+'</p>'
    text+='<p>'+html.escape(' '.join(summary['limitations']))+'</p><input placeholder="Filter meshes" oninput="for(const r of document.querySelectorAll(\'tbody tr\'))r.hidden=!r.textContent.toLowerCase().includes(this.value.toLowerCase())"><table><thead><tr><th>Source mesh</th><th>SCCT reference</th><th>Triangles</th></tr></thead><tbody>'+rows+'</tbody></table>'
    (WORK/'Catalogue.html').write_text(text,encoding='utf-8')
    print(json.dumps(summary),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['scan','prepare','report']);parser.add_argument('config')
    args=parser.parse_args();os.environ['R6V_CONVERTER_CONFIG']=str(Path(args.config).resolve())
    try:
        {'scan':scan,'prepare':prepare,'report':report}[args.action]()
    except Exception as ex:
        import traceback
        traceback.print_exc();sys.exit(1)
