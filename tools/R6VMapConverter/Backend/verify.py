"""Compare source-derived transforms and asset references with a fresh native reload."""
import json, math, os, sys
from pathlib import Path
os.environ['R6V_CONVERTER_CONFIG']=str(Path(sys.argv[1]).resolve())
from config import WORK, BATCH, PACKAGE

def equal(actual, expected):
    if isinstance(expected,dict):
        return isinstance(actual,dict) and all(k in actual and equal(actual[k],v) for k,v in expected.items())
    if isinstance(expected,list):
        return isinstance(actual,list) and len(actual)==len(expected) and all(equal(a,b) for a,b in zip(actual,expected))
    if isinstance(actual,bool):return actual==(str(expected).lower()=='true')
    if isinstance(actual,(float,int)):return math.isclose(actual,float(expected),rel_tol=1e-6,abs_tol=1e-4)
    ref=str(expected)
    if "'" in ref:ref=ref.split("'",1)[1].rstrip("'")
    return str(actual).casefold()==ref.casefold()

rows=json.loads((WORK/'placement-inventory.json').read_text())
created=json.loads((WORK/'native-created.json').read_text())
loaded=json.loads((WORK/'native-reloaded.json').read_text())
assert len(created)==len(loaded)==len(rows),'Actor count differs after reload'
for row in rows:
    actor=row['actor']
    assert created[actor]==loaded[actor],f'Changed on reload: {actor}'
    for key in ['Location','Rotation','DrawScale','DrawScale3D','StaticMesh','Skins','bHidden']:
        if key in row['native_properties']:
            assert equal(loaded[actor][key],row['native_properties'][key]),(actor,key,loaded[actor][key],row['native_properties'][key])
meshes=json.loads((BATCH/'delivery-inventory.json').read_text())
assets=json.loads((WORK/'worker/System/native-imported-properties.json').read_text())
for mesh in meshes:
    slots=assets[mesh['native']]['Materials']
    assert len(slots)==len(mesh['material_slots']),(mesh['native'],'material slot count')
    for actual, expected in zip(slots,mesh['material_slots']):
        assert equal(actual['Material'],PACKAGE+'.'+expected),(mesh['native'],actual,expected)
result=dict(passed=True,placements=len(rows),meshes=len(meshes),verification='Source transforms, asset and material references matched; placements unchanged after fresh native reload.')
(WORK/'validation.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result))
