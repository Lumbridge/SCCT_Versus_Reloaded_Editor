"""Regression checks that require no game installation."""
import importlib.util, json, struct, sys, tempfile, types, unittest
from unittest.mock import patch
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Backend'))
from convert_pskx import convert
from inspect_vegas import Package, Reader


def triangle():
    def chunk(name,size,rows):return struct.pack('<20siii',name.encode(),0,size,len(rows))+b''.join(rows)
    return b''.join([
        chunk('PNTS0000',12,[struct.pack('<3f',*v) for v in [(0,0,0),(3,0,0),(0,2,0)]]),
        chunk('VTXW0000',16,[struct.pack('<IffBBH',i,u,v,0,0,0) for i,(u,v) in enumerate([(0,0),(1,0),(0,1)])]),
        chunk('FACE0000',12,[struct.pack('<3HBBI',0,1,2,0,0,1)]),
        chunk('MATT0000',88,[b'TestMaterial'.ljust(88,b'\0')])])


class BackendTests(unittest.TestCase):
    def test_winding_and_uv_corners_reverse_together(self):
        # Prevent the inside-out statue regression: geometry B/C and UV B/C must both flip.
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'triangle.pskx';source.write_bytes(triangle())
            summary=convert(source,root/'out');text=(root/'out/triangle.ase').read_text()
            self.assertIn('*MESH_FACE 0: A: 0 B: 2 C: 1',text)
            self.assertIn('*MESH_TFACE 0 0 2 1',text)
            self.assertIn('\t\t\t*MESH_TVERT 2 0.0 0.0 0',text)
            self.assertNotIn('*TM_ROW0',text)
            self.assertEqual(summary['bounds'],[[0,0,0],[3,2,0]])
            self.assertEqual(summary['triangles'],1)

    def test_truncated_mesh_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'truncated.pskx';source.write_bytes(triangle()[:-1])
            with self.assertRaises(AssertionError):convert(source,Path(directory)/'out')

    def test_wrong_package_version_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'wrong.rmpc';source.write_bytes(struct.pack('<2I',0x9e2a83c1,0x004200f2))
            with self.assertRaisesRegex(AssertionError,'4200f2'):Package(source)

    def test_reader_rejects_overrun_and_negative_lengths(self):
        for count in [-1,5]:
            with self.assertRaises(AssertionError):Reader(b'1234').take(count)

    def test_component_rotation_scale_pivot_and_mirror(self):
        # A 90-degree local turn under a reflected, nonuniform actor scale cannot
        # be represented by adding Euler angles. Check the baked triangle itself.
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);batch=root/'assets';batch.mkdir()
            source=root/'triangle.pskx';source.write_bytes(triangle());summary=convert(source,root/'ase')
            actor='Level.Actor';component=actor+'.Component'
            ap={k:{'value':v} for k,v in dict(Location=[10,20,30],Rotation=[0,0,0],DrawScale=1,DrawScale3D=[-2,3,1],PrePivot=[0,1,0]).items()}
            cp={k:{'value':v} for k,v in dict(Rotation=[0,16384,0],Scale3D=[1,2,1],Translation=[1,2,3]).items()}
            class FakePackage:
                exports=[{'kind':'actor'},{'kind':'component','offset':0}]
                data=b'\0'*24+struct.pack('<i',-1)
                def __init__(self,path):pass
                def ref(self,n):return {-1:'Source.Mesh',1:actor,2:component}[n]
                def props(self,e,skip=0):return (ap if e['kind']=='actor' else cp),0
            row=dict(component=component,actor=actor,mesh='Source.Mesh',location=[10,20,30],rotation=[0,0,0],scale=1,scale3d=[-2,3,1],component_properties={'Rotation':[0,16384,0]})
            mesh=dict(source='Source.Mesh',native='Test.Mesh',name='Mesh',ase=str(root/'ase/triangle.ase'),bounds=summary['bounds'],material_slots=['Material'],triangles=1)
            for path,value in [(root/'placements.json',[row]),(batch/'delivery-inventory.json',[mesh]),(batch/'material-conversion.json',{}),(batch/'build-job.json',{'meshCommands':[],'expected':[]})]:path.write_text(json.dumps(value))
            config=types.ModuleType('config');config.WORK=root;config.BATCH=batch;config.MAP=root/'Map.rmpc';config.PACKAGE='Test'
            spec=importlib.util.spec_from_file_location('placement_fixture',Path(__file__).resolve().parents[1]/'Backend/placements.py')
            with patch.dict(sys.modules,{'config':config}):
                module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);module.Package=FakePackage;module.prepare_placements()
            result=json.loads((root/'placement-inventory.json').read_text())[0]
            self.assertEqual(result['native_properties']['Location'],{'X':'8.0','Y':'23.0','Z':'33.0'})
            self.assertEqual(result['native_properties']['DrawScale3D'],{'X':'2','Y':'3','Z':'1'})
            self.assertLess(result['transform_corner_error'],1e-10)
            variant=json.loads((root/'mirrored-variants.json').read_text())[0];text=Path(variant['ase']).read_text()
            self.assertLess(variant['determinant'],0)
            self.assertIn('*MESH_FACE 0: A: 0 B: 1 C: 2',text)
            self.assertIn('*MESH_TFACE 0 0 1 2',text)
            import re
            vertices={int(m[1]):list(map(float,m[2].split())) for m in re.finditer(r'\*MESH_VERTEX\s+(\d+)\s+([^\r\n]+)',text)}
            for a,b in zip(vertices[1],[0,-3,0]):self.assertAlmostEqual(a,b)
            for a,b in zip(vertices[2],[-4,0,0]):self.assertAlmostEqual(a,b)


if __name__=='__main__':unittest.main()
