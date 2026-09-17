"""Build the ShipD override change file without loading the full export into chat."""
import copy, itertools, json, sys
from pathlib import Path
source=Path(sys.argv[1]); catalogue=Path(sys.argv[2])
d=json.loads(source.read_text()); live=json.loads(catalogue.read_text())
classes={c['class']:c['schema'] for c in live['classes']}
actors={a['values'].get('Tag'):a for a in d['actors']}
flood=actors['FloodEvent']; operations=[]
patch=copy.deepcopy(d['changes']);patch['description']='Wet Room manual flood override: four covered buttons, usable by either team, green pulsing beacons, any-order unique presses, full flood cleanup and reset.'
patch['operations']=operations
V=lambda x=0,y=0,z=0:{'X':str(x),'Y':str(y),'Z':str(z)}
R=lambda yaw=0:{'Pitch':'0','Yaw':str(yaw),'Roll':'0'}
def action(tag='None',delay=0,typ='EVT_Trigger',valid='EVT_Trigger'):
 return {'Event':tag,'Delay':str(delay),'Type':typ,'ValidOn':valid}
def group(actions,sequence=False):
 return {'EventGroup':actions,'Sequence':'True' if sequence else 'False','Repeat':'0','SequenceIndex':'0'}
def create(id,cls,props):
 p={'Location':V(-2200,-1523,-1400)};p.update(props)
 operations.append({'op':'create','id':id,'class':cls,'properties':p})
def magic(id,actions,seq=False,**props):
 create(id,'SBase.SMagicEvent',{'Groups':[group(actions,seq)],**props})
def update(a,properties):
 operations.append({'op':'update','actor':a['actor'],'before':copy.deepcopy(a['values']),'properties':properties})
# Every button has a two-state latch: only Trigger in the ready state counts;
# Untrigger resets the spent state. A timeout completes missing counts before reset.
magic('FO_Count',[action(),action(),action(),action('FO_CancelRelay')],True)
magic('FO_Expire',[action('FO_Latch'+str(i),i*.01) for i in range(1,5)])
magic('FO_EffectsGate',[action('FloodWaterEffect'),action('FloodWaterEffect',valid='EVT_Untrigger')],True)
cleanup=[action('FloodEvent')]
for tag in ['FloodDamage','FloodWater','WetRoomBarriers','FloodWaterSound','FloodSiren','SirenLights','FO_EffectsGate']:
 cleanup.append(action(tag,.01,typ='EVT_Untrigger'))
cleanup.append(action('DefaultWetRoomLights',.01))
for i in range(1,5):
 for kind in ['Button','Light','Cover']:
  cleanup.append(action('FO_'+kind+str(i),.02,typ='EVT_Untrigger'))
 cleanup.append(action('FO_Latch'+str(i),.04,typ='EVT_Untrigger'))
cleanup.append(action('FloodEventHackPanel',20))
magic('FO_Cleanup',cleanup)
# A mover's OpenedEvent supplies one fixed Other actor for StopActor, whichever
# physical button finished the count. The relay stays hidden and non-colliding.
keys=[V() for _ in range(8)];keys[1]=V(z=1)
create('FO_CancelRelay','Engine.Mover',{'Location':V(-2200,-1523,-4500),'InitialState':'TriggerOpenTimed','NumKeys':'2','KeyPos':keys,'MoveTime':'0.05','StayOpenTime':'0.1','OpenedEvent':'FO_Cleanup','DrawType':'DT_None','bHidden':'True','bCollideActors':'False','bBlockActors':'False','bBlockPlayers':'False','bBlockCamera':'False','MoverEncroachType':'ME_IgnoreWhenEncroach'})
# Walkway elevations come from source brush top faces, not a shared guessed Z.
sides=[('West',(-3620,-1228,-2010),(1,0),0),('East',(-82,-1228,-1645),(-1,0),32768),('North',(-1800,-340,-1789),(0,-1),49152),('South',(-1800,-2116,-1789),(0,1),16384)]
mesh=next(a for a in live['assets']['StaticMesh'] if a['path']=='TSP01_STM.Object.tsp_elevatormetalpanel');bounds=mesh['bounds']
center=[(bounds[i]+bounds[i+3])/2 for i in range(3)];scale=[x/(bounds[i+3]-bounds[i]) for i,x in enumerate([12,112,152])]
for i,(name,xyz,normal,yaw) in enumerate(sides,1):
 magic('FO_Latch'+str(i),[action('FO_Press'+str(i)),action(valid='EVT_Untrigger')],True)
 magic('FO_Press'+str(i),[action('FO_Button'+str(i),typ='EVT_Untrigger'),action('FO_Light'+str(i),typ='EVT_Untrigger'),action('FO_Count',.01)])
 create('FO_Button'+str(i),'SBase.SInterrupteur',{'Location':V(*xyz),'Rotation':R(yaw),'StaticMesh':"StaticMesh'SGameplayObjects.SGPO_SM.trigger_standard'",'DrawType':'DT_StaticMesh','DrawScale':'1.5','Event':'FO_Latch'+str(i),'UsableBy':'U_BOTH','TriggerMethode':'TriggerControl','bInitialyUsable':'False','bInitialyActive':'False','bTriggerOnceOnly':'False','iHackingTime':'0','iReusableTime':'0','iAttenteAvantActivation':'0','iAttenteAvantDesactivation':'0','bMaintainsSwitch':'False','bCanBeChaffed':'False','bDamageable':'False','bInteractInFront':'True','UseRelativeLocation':V(55,0,0),'UseRelativeRotation':R(32768),'fInteractionRadius':'70','fInteractionHeight':'80','sDescriptionText':json.dumps('Flood override - '+name),'sWarnHackingText':'""','InteractionSound':"Sound'Sound_Persos.divers.RA_interrupteur'",'bHidden':'False','bUseDynamicLights':'True'})
 create('FO_Light'+str(i),'SBase.STriggerLight',{'Location':V(xyz[0]+normal[0]*80,xyz[1]+normal[1]*80,xyz[2]+42),'LightHue':'85','LightSaturation':'0','LightBrightness':'96','LightRadius':'8','LightType':'LT_Pulse','LightPeriod':'48','LightPhase':'0','TriggerMethode':'TriggerControl','bInitialyOn':'False','bDynamicLight':'True','bStaticLighting':'False','bApplyToStaticLighting':'False','bApplyToInGameLighting':'True','bMovable':'True','bHidden':'True'})
 keys=[V() for _ in range(8)];keys[1]=V(z=176)
 create('FO_Cover'+str(i),'Engine.Mover',{'Location':V(xyz[0]+normal[0]*22,xyz[1]+normal[1]*22,xyz[2]),'Rotation':R(yaw),'PrePivot':V(*center),'StaticMesh':"StaticMesh'TSP01_STM.Object.tsp_elevatormetalpanel'",'Skins':["Texture'ShipD_TXT.Walls.pool_tiles_color'"],'DrawType':'DT_StaticMesh','DrawScale3D':V(*scale),'InitialState':'TriggerControl','NumKeys':'2','KeyPos':keys,'MoveTime':'0.6','bHidden':'False','bCollideActors':'True','bBlockActors':'True','bBlockPlayers':'True','bUseTriggered':'False','bDamageTriggered':'False','bTriggerOnceOnly':'False','MoverEncroachType':'ME_IgnoreWhenEncroach','bUseDynamicLights':'True','bStaticLighting':'False'})
# Preserve flood timing, replacing toggle-only cleanup with explicit on/off.
groups=copy.deepcopy(flood['values']['Groups'])
for g in groups:
 kept=[]
 for a in g['EventGroup']:
  if a['Event']=='FloodEventHackPanel' and float(a['Delay'])>0:continue # rearm 20s after cleanup
  if a['Event']=='FloodWaterEffect':
   a['Event']='FO_EffectsGate'
   if float(a['Delay'])>0:a['Type']='EVT_Untrigger'
  if a['Event'] in ['FloodWaterSound','FloodWater','FloodDamage','WetRoomBarriers','FloodSiren'] and float(a['Delay'])>0 and not (a['Event']=='FloodDamage' and float(a['Delay'])==20):a['Type']='EVT_Untrigger'
  kept.append(a)
 g['EventGroup']=kept
for i in range(1,5):
 for kind in ['Button','Light','Cover']:groups[0]['EventGroup'].append(action('FO_'+kind+str(i)))
groups[0]['EventGroup'].append(action('FO_Expire',40))
update(flood,{'Groups':groups,'StopActor':{'$ref':'FO_CancelRelay'}})
for a in d['actors']:
 tag=a['values'].get('Tag')
 if tag in ['FloodWater','WetRoomBarriers'] and a['actor']['class']=='Engine.Mover':update(a,{'InitialState':'TriggerControl'})
 if tag in ['FloodWaterSound','FloodSiren'] and a['actor']['class']=='SBase.SAmbientSoundTrigger':update(a,{'bToggle':'False'})
 if tag=='FloodDamage' and a['actor']['class']=='SBase.SDamageVolume':update(a,{'bToggle':'False'})
# Validate every supplied leaf against the live native schema, including complete structs.
created={o['id']:o['class'] for o in operations if o['op']=='create'}
def validate(s,v):
 k=s['kind']
 if k in ['ArrayProperty','FixedArray']:
  assert isinstance(v,list)
  if k=='FixedArray':assert len(v)==s['dimension']
  for x in v:validate(s['inner'],x)
 elif k=='StructProperty':
  assert set(v)==set(s['fields'])
  for key in v:validate(s['fields'][key],v[key])
 elif isinstance(v,dict):assert set(v)=={'$ref'} and v['$ref'] in created and k=='ObjectProperty'
 else:
  assert isinstance(v,str)
  if s.get('choices'):assert v in s['choices'],(v,s['choices'])
  if k in ['FloatProperty','IntProperty']:float(v)
for o in operations:
 cls=o.get('class',o.get('actor',{}).get('class'));sch=classes[cls]
 for k,v in o['properties'].items():assert k in sch,(cls,k);validate(sch[k],v)
# Exhaust all unique press orders, duplicate presses, timeout at any partial count,
# and repeated cycles. This verifies graph design; it is not an in-game playtest.
for order in itertools.permutations(range(4)):
 for prefix in range(5):
  for cycle in range(3):
   used=set();count=0;finishes=0
   def press(n):
    global count,finishes
    if n in used:return
    used.add(n);count=(count+1)%4;finishes+=count==0
   for n in order[:prefix]:press(n);press(n)
   assert finishes==(prefix==4)
   for n in range(4):press(n)
   assert finishes==1 and count==0 and len(used)==4
out=source.with_name('ShipD-flood-override.changes.json');out.write_text(json.dumps(patch,indent=2)+'\n')
print(out);print('operations',len(operations),'create',len(created),'update',len(operations)-len(created),'bytes',out.stat().st_size)
print('PASS: live-schema validation; 24 press orders x 5 timeout points x 3 cycles with duplicate presses')
