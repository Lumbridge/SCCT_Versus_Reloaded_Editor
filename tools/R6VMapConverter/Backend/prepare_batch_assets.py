import os
from pathlib import Path
import sys,json,hashlib,collections,re
from PIL import Image,ImageChops,ImageDraw
sys.path.insert(0,str(Path(__file__).parent))
from convert_pskx import convert
import resolve_batch_textures as resolve
from config import WORK as r, BATCH as out, PACKAGE
prepared=out/'prepared';prepared.mkdir(exist_ok=True);ase=out/'ase';ase.mkdir(exist_ok=True)
materials=json.loads((out/'source-materials-resolved.json').read_text());textures=json.loads((out/'texture-inventory.json').read_text());meshes=json.loads((out/'mesh-inventory.json').read_text());package=PACKAGE
teximports=[];shaderdefs=[];results={};textureedits=[]
def settings(full,seen=None):
 seen=set() if seen is None else seen
 if full in seen:return {}
 seen.add(full)
 try:
  p,e=resolve.get(full);props,_=p.props(e,28 if p.path.suffix.lower()=='.rmpc' else 0);parent=props.get('Parent',{}).get('value');d=settings(resolve.qualify(p,parent),seen) if parent and parent!='None' else {}
  for key in ['BlendMode','TwoSided','LightingModel']:
   if key in props:d[key]=props[key]['value']
  return d
 except:return {}
def imported_image(im,name):
 alpha=im.mode=='RGBA' and im.getextrema()[3][0]<255;file=prepared/(name+('.tga' if alpha else '.bmp'));im.convert('RGBA' if alpha else 'RGB').save(file)
 teximports.append({'name':name,'file':str(file),'alpha':alpha,'width':im.width,'height':im.height});
 if alpha:textureedits.append({'object':package+'.'+name,'properties':{'bAlphaTexture':'True'}})
 return "Texture'"+package+'.'+name+"'"
for i,(name,m) in enumerate(sorted(materials.items())):
 native=f'RVM{i:04d}';tn=f'RVT{i:04d}';pars=m.get('parameters',{});opts=settings(name);blend=opts.get('BlendMode',0);notes=[];candidates=m['colour_candidates'];chosen=next((c for c in candidates if textures[c['texture']]['status']=='recovered'),None)
 if chosen:
  image=Image.open(textures[chosen['texture']]['file']).convert('RGBA');quality='source texture'
  if candidates[0]!=chosen:notes.append('Preferred colour input missing; using '+chosen['channel']+' input.');quality='approximation'
 else:
  color=m.get('colour_constant');tint=pars.get('GlassTint') or pars.get('Color') or pars.get('DiffuseColor')
  if color is not None:
   image=Image.new('RGBA',(4,4),tuple(round(max(0,min(1,c))*255) for c in color)+(255,));quality='constant approximation';notes.append('Constant extracted from source graph; graph itself is not reconstructed.')
  elif isinstance(tint,dict):
   image=Image.new('RGBA',(4,4),tuple(round(max(0,min(1,tint.get(c,0.5)))*255) for c in ['R','G','B'])+(255,));quality='tint approximation';notes.append('Source tint retained; textured effect unavailable.')
  else:
   image=Image.new('RGBA',(32,32),(120,110,120,255));dr=ImageDraw.Draw(image);dr.rectangle((0,0,15,15),fill=(160,80,145,255));dr.rectangle((16,16,31,31),fill=(160,80,145,255));quality='missing colour';notes.append('Visible mauve checker marks unresolved source colour; no substitute claimed.')
 maskname=next((pars[k] for k in ['Om','OpacityMask','O'] if isinstance(pars.get(k),str) and pars[k]!='None'),None)
 if maskname and textures.get(maskname,{}).get('status')=='recovered':
  mask=Image.open(textures[maskname]['file']).convert('RGBA');alpha=mask.getchannel('A') if mask.getextrema()[3][0]<255 else mask.convert('L');image.putalpha(alpha.resize(image.size,Image.Resampling.BILINEAR));notes.append('Recovered opacity mask combined into colour alpha.')
 elif maskname:notes.append('Opacity mask missing: '+maskname)
 if blend==2 and image.getextrema()[3][0]==255:
  opacity=pars.get('Opacity',pars.get('MasterOpacity',0.35 if 'glass' in (name+' '+str(m.get('parent'))).lower() else 1.0))
  if isinstance(opacity,(float,int)) and opacity<1:image.putalpha(round(255*max(0,opacity)));notes.append('Constant source/default opacity approximation.')
  elif not maskname and chosen and chosen['channel']=='E':image.putalpha(image.convert('L'));notes.append('Glow opacity approximated from luminance.')
 diffuse=imported_image(image,tn);props={'Diffuse':diffuse,'TwoSided':'True' if opts.get('TwoSided',False) else 'False'}
 # UE3 blend enum: opaque0, masked1, translucent2, additive3, modulate4.
 if blend in [1,2,3,4]:
  props['OutputBlending']={1:'OB_Masked',2:'OB_Translucent',3:'OB_Brighten',4:'OB_Modulate'}[blend]
  if image.getextrema()[3][0]<255:props['Opacity']=diffuse
  notes.append('Native SCCT blend approximation; Vegas material graph is not reproduced.')
 emissive=pars.get('E')
 if isinstance(emissive,str) and textures.get(emissive,{}).get('status')=='recovered':
  em=Image.open(textures[emissive]['file']).convert('RGBA');props['SelfIllumination']=imported_image(em,f'RVE{i:04d}');notes.append('Static emissive input; animation/bloom not reconstructed.')
 elif chosen and chosen['channel'] in ['E','EmissiveColor']:props['SelfIllumination']=diffuse;notes.append('Static emissive input; animation/bloom not reconstructed.')
 if any(k in pars for k in ['Speed','ImgVert','ImgHoriz','UV_X_Scale','UV_X_offset','D-Tiling','D-Rotation']):notes.append('Source UV animation/transform requires visual review.')
 shaderdefs.append({'class':'Engine.Shader','package':package,'name':native,'properties':props})
 results[name]={'native':package+'.'+native,'colour_source':chosen,'quality':quality,'source_blend':blend,'source_two_sided':opts.get('TwoSided',False),'notes':notes,'properties':props}

for i,row in enumerate(meshes):
 folder=ase/f'{i:04d}';summary=convert(Path(row['pskx']),folder);path=folder/(Path(row['pskx']).stem+'.ase');s=path.read_text()
 # Replace every submaterial's mandatory bitmap while retaining slots, even unused ones.
 slots=[results[x['material']]['native'].split('.')[-1] for x in row['sections']];counter=[0]
 def sub(m):
  name=slots[counter[0]];counter[0]+=1;return '*BITMAP "'+name+'.tga"'
 s=re.sub(r'\*BITMAP "[^"]+"',sub,s);assert counter[0]==len(slots)
 s=s.replace('*MESH_SMOOTHING 1','*MESH_SMOOTHING 1')
 final=ase/f'M{i:04d}.ase';final.write_text(s)
 leaf=row['source'].split('.')[-1];leaf=re.sub('[^A-Za-z0-9_]','_',leaf)
 if len(leaf)>48:leaf=leaf[:39]+'_'+hashlib.sha1(row['source'].encode()).hexdigest()[:8]
 # Names must be globally unique for native export's leaf-only lookup.
 leaf=f'M{i:04d}_{leaf}'
 group=row['source'].split('.')[0]
 row.update(native=package+'.'+group+'.'+leaf,name=leaf,group=group,ase=str(final),bounds=summary['bounds'],material_slots=slots,issues=[{'slot':j,**results[x['material']]} for j,x in enumerate(row['sections']) if x['triangles'] and (results[x['material']]['quality']!='source texture' or results[x['material']]['notes'])])
job={'commands':[f'TEXTURE IMPORT FILE="{t["file"]}" NAME={t["name"]} PACKAGE={package} MIPS=1' for t in teximports], 'materials':shaderdefs,'edits':textureedits,'meshCommands':[f'STATICMESH IMPORT FILE="{row["ase"]}" NAME={row["name"]} PACKAGE={package} GROUP={row["group"]}' for row in meshes], 'after':[f'OBJ SAVEPACKAGE PACKAGE={package} FILE="{out / (package+".usx")}"'],'inspect':[m['native'] for m in meshes]+[v['native'] for v in results.values()],'expected':[m['native'] for m in meshes],'skipSnapshot':True,'snapshot':'native-imported.json','inspectionFile':'native-imported-properties.json'}
(out/'native-job.json').write_text(json.dumps(job,indent=2));(out/'build-job.json').write_text(json.dumps(job,indent=2));(out/'delivery-inventory.json').write_text(json.dumps(meshes,indent=2));(out/'material-conversion.json').write_text(json.dumps(results,indent=2));(out/'import-textures.json').write_text(json.dumps(teximports,indent=2))
print('PREPARED',len(meshes),'meshes',len(shaderdefs),'materials',len(teximports),'images',collections.Counter(v['quality'] for v in results.values()))
