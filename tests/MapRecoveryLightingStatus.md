# OffsD lighting investigation

Updated 13 September 2026. Follow-up testing confirmed that the Play Level profile issue and recovered-map Play Here crash are no longer present. The notes below record the earlier lighting and visibility investigation; later results supersede the test plans and open questions in older entries.

## Default preservation during ordinary builds (13 September)

Recovered source maps now protect their existing bake automatically. Recognition
uses the recovery's serialized structural brush/model identities, so reopening
or saving under another filename does not depend on a session flag or filename
convention. Initial recovery keeps its original capture/transfer pipeline.

The editor captures BSP lighting on source load, before brush edits can replace
the old render sections. Ordinary MAP/BSP builds rebuild chart structures and
transfer the captured lighting to matching surfaces, restore compatible mesh
colour streams, and commit the platform lighting cache. A single session
snapshot avoids repeated resampling from successive build results. New surfaces
and incompatible vertex layouts keep their newly calculated lighting. The Build
menu flags that a preserved bake may be outdated after geometry/light edits.

Ordinary LIGHT APPLY preserves the bake. Build > Recalculate Lighting explicitly
runs the native bake after confirmation and replaces the protected baseline.
This is a preservation policy, **not a fix for the native bake's darkening**.
An already darkened saved map needs recovery from the original compiled map to
recover its original colours.

Native testing on OffsD_Recovered_12 verified all 629 mesh colour streams remain
byte-identical across an ordinary full rebuild. All mesh streams and 14 rebuilt
BSP atlas textures remain byte-identical across File Save/reopen. Cancelling
recalculation leaves all bytes intact; accepting it changes the bake, and an
ordinary lighting command then preserves that replacement. The generated fixture
also passes initial recovery, geometry editing, saving and reopening. Workflow
tests pass initial/restarted processes, including the actual File packaging menu.

The source-load test exposed an inactive platform binding: render-section+2C
is the secondary Xbox lightmap index and can exceed the PC atlas count. Native
BuildRenderData at 110D1500 compares it only on platform 1. Capture now follows
that condition instead of treating an unused Xbox index as a PC atlas.

Verified runs: `scct-source-recovery-bd363945940746df9a7e523e794c3f6a` (OffsD, final load-time capture),
`scct-source-recovery-dd6aa512a8f649c78c022ab250552413` (geometry edit), and
`scct-source-recovery-1bd4ca48e63844d2b48593188dad0f8b` (workflows/restart).

## Fresh rebuild and light-candidate isolation (13 September)

The ordinary source rebuild still reproduces the darkening. An isolated
`-ReopenOnly -TraceLighting` run on `OffsD_Recovered_12.sdc` loaded the preserved
bake, ran the native geometry/BSP/lighting/path build with the normal actor-state
bracket and platform cache finalizer, then saved and reopened successfully.
`lighting_before_normal_build` and `lighting_after_normal_build` record the
actual colour buffers before and after that build.

Additional original-BSP tests used the native mesh Illuminate and colour-baking
functions. `-AllLeafLightCandidates` temporarily supplies every valid BSP leaf
to each mesh's candidate gathering, then restores its real leaf membership.
`-SkipMeshShadowOcclusion` additionally disables the mesh's shadow-occlusion
test only during that diagnostic call; its original flags are restored.
These switches require `-RelightCookedMeshes`, affect only the isolated probe,
and never save the experimental lighting.

Mean stored RGB component values:

| Actor | Original bake | Full source rebuild | Original BSP, all leaf candidates | All candidates, no shadow occlusion |
| --- | ---: | ---: | ---: | ---: |
| StaticMeshActor266 | 3.543 | 0 | 0 | 0.549 |
| StaticMeshActor273 | 7.667 | 0 | 0 | 0.511 |
| Mover10 | 39.860 | 3.519 | 51.076 | 51.076 |

The crate meshes have ScaleGlow=1. Native shadow records for Light1567 and
Light1616 contain no visible vertices in the ordinary original-BSP relight.
Supplying all leaves also considers Light3332 and Light1607, but their masks
are likewise empty. Disabling occlusion makes vertices visible while still
producing substantially less colour than the original bake. Mover10 gains
Light1548 against the original BSP; this contribution is absent after the
source rebuild.

A separate cooked-file inspection also checked the static-mesh asset colour
stream and the native bake's multiply-vertex-colours setting. `fac_woodbox`
has 105 asset colours and multiplication is disabled, so an extra multiplication
by dark asset colours does not explain these crates' loss. The trace now records
this setting and dumps the asset colour stream alongside instance colours.

These results rule out candidate-list expansion or disabling occlusion alone
as a complete fix. They do not establish whether the original authoring data,
build settings, or a different lighting calculation supplied the missing
contribution. No production lighting change has been made from these experiments.
Matching a fresh bake still needs investigation; preserving an existing bake
is a separate behaviour choice rather than a demonstrated recalculation fix.

Isolated results for this investigation:

- Ordinary rebuild: `scct-source-recovery-ad6fb33e39724a2bb00c0edab248a3a9`
- Original-BSP relight: `scct-source-recovery-bb5b149fc53c44ed8934c45c11e5650b`
- All leaf candidates: `scct-source-recovery-1a360ef27dfa4dcba92d8cd336d80141`
- All candidates, unoccluded: `scct-source-recovery-b8c6929e45a946d5b533484dcdc423dc`
- Asset colour inspection: `scct-source-recovery-ed95152aa6b04f158eed9457a8cc3f4e`

## Current window visibility and map-check results

Separate solid-side portal visit records fixed office-window visibility in
follow-up testing. The office alarm does not close the windows in the original
map either, so it needs no change. Exported SAlarm2100 Events target maindoor1..5 and ventdoor1; shutters
use the separate volet_c/volet_d chain through SMagicEvent263. Relevant alarm,
mover and switch properties match the recovered export.

All three reported map-check warnings also occur in the original:

- SInterrupteur1905 and SInterrupteur12587 have Event=Ascensseur, with no actor
  bearing that Tag. Both are Call Lift buttons and are also referenced directly
  by SLift1713.DoorsAndSwitches. These are lift links, not alarm links.
- ZoneInfo2634 at (3456,1792,0) already has iLeaf=-1 and zone0 in the original.
  It was left in its original position.

Actor events and zone placement were left unchanged.

## Earlier office-window tests

In recovered15, office interiors disappeared when viewed through windows from
outside but looked correct after entering.
The log confirms successful recovery, all 629 original mesh colour streams,
BSP atlas equality and 44 original portal outlines after ordinary save/reopen.

Portal1504 -> rebuilt3502 first has solid-side pair (0,1), followed by the
room pair(2,1); original starts with room pair(5,1). Portal1505 ->3497 similarly
starts with(0,1) before(10,1); original has only(7,1). Native routine 111AA91B tests a
per-surface visited bit and 111AAC18 marks it after portal processing. A solid
fragment can therefore consume a visit needed by the room fragment. This was the suspected cause before the follow-up test confirmed the fix.

The fix gives solid-side fragments of mixed portal surfaces
their own surface and visited bit, retaining the flags and full original
outline. FPolys are deep-copied with engine-owned allocations to avoid shared
ownership. Node geometry, zone pairs, BSP hierarchy and masks are unchanged.
Checks after build and ordinary reopen reject shared solid/room visit records.
This runs only during recovery; a later BSP rebuild may merge surfaces again.
The Release build passed. Editor and game checks were done manually.

Earlier tests showed improved initial lighting after mesh colour and BSP
lightmap transfer. At that stage, water was invisible in game and inside the
outer cube, but visible from outside it in the editor. Later tests covered
water visibility and the separate Play Here crash; lighting after a rebuild
still needs investigation.

## Portal outline fix and earlier water tests

Water became visible after the outline fix. Recovery14 failed portal verification at built
surface2399 before TransferMeshLighting and normal saving. The working map was then saved as OffsD_Recovered_1, as recorded in the log.
That run stopped before lighting transfer, so the bottom-floor comparison
could not show whether the transfer fixed the darkness.

The comparison found 25/44 original portal outlines intact. Surface2399 is
an extra tiny portal on a structural closing face inheriting fallback flags;
19 other outlines became triangles during generic sheet preparation of slightly
nonplanar original quads. Structural emission now strips portal/antiportal flags
from generated faces. Authored portal outlines bypass generic sheet triangulation
while still passing native importer cleanup/outline checks. Outline checks still run after build/reopen. The Release build passed;
manual testing was pending at this point.

The first outline build rejected valid vertices because validation used the
surface texture origin as a geometry-plane point. Original dumped surface1533
has texture origin Z=-480 while its node plane and retained vertices are Z=-448;
other original portals also have off-plane texture origins. Validation now uses
the node plane equation with double-precision accumulation and the same 0.05
tolerance. The corrected DLL built and was installed for the next recovery and water test.

OffsD_Recovered_11 still hides water inside the outer cube in unlit textured
view. The coplanar ordering workaround did not fix it and has been removed.
The earlier visible-sheet subdivision experiment was also unsuccessful.

Native visibility at 111AA91B marks portals once per surface; 111AA951 copies
the retained FPoly at surface+20 to clip visibility into the adjacent zone.
This uses the authored portal outline, not the current BSP node polygon.
Read-only package decoding found the original water portal at logical offset
929751: four vertices spanning X=-16000..12032, Y=-13316..12796, Z=-448.
Recovered_11's retained portal at offset1082935 instead has five vertices in a
western strip, X=-15616..-10784.041. The smaller portal opening matched the missing visibility in the
inside/outside screenshots.

Extraction now takes one retained authored FPoly per original portal surface,
instead of rebuilding that portal from cooked fragments. The native FPoly
layout is vertices+18, count ushort+148, normal+C; serializer110CBDF0 and
surface serializer110CDC27 confirm the retained payload. Native save refreshes
it from the source brush polygon (110CDB93), so fixing only a runtime copy would
not persist. The fix therefore restores the editable source polygon itself.
Outlines must match after build and ordinary save/reopen, including winding
and vertex positions within 0.02 units. Audits include portalOutline vertices.
The Release build passed. Editor and game checks were done manually.

Other read-only checks found valid node bounds, subtree zone masks and zone
connectivity in original/recovered maps. Those did not reveal the outline loss.

## Lighting measurements

- The fresh original export and recovered export retain 113 `Light` and 21
  `STriggerLight` actors. Their exported lighting settings match. Native reflected
  scalar/boolean light settings also match, including static/in-game lighting
  flags. The loaded original package inventory contains those same 134 actors;
  no inactive additional `Light`/`STriggerLight` actors were found. The count covers loaded objects only.
- Relevant actors retain their named ZoneInfo assignments. Zone identifiers are
  renumbered during reconstruction; the 437 actors in original zone 14 map to
  recovered zone 20. The out-of-zones ZoneInfo2634 warning also exists in the
  original exported state.
- The same 629 mesh instances contain 158,619 baked vertex colours before and
  after recovery. Mean stored B/G/R byte values fall from approximately
  5.675/5.721/5.778 to 0.78/0.72/0.64 during rebuilding. Black RGB vertices rise
  from 105,069 to 145,393. These values measure stored colour bytes, not screenshot brightness.
- The rebuilt colour buffers survive ordinary saving and reopening with that
  darker content. A non-null StaticMeshInstance pointer alone would not catch this colour loss.
- A diagnostic mesh-only relight on the original BSP, with refreshed actor
  leaf membership, native shadow-mask generation and native colour baking, also
  loses illumination at spy-base meshes. Reconstructed brushes therefore cannot be the only cause. The reason for the
  different lighting is still unknown.

Mean stored RGB component values for selected meshes:

| Actor | Original baked | Mesh-only relight on original BSP | Recovery rebuild |
| --- | ---: | ---: | ---: |
| StaticMeshActor266 | 3.543 | 0 | 0 |
| StaticMeshActor273 | 7.667 | 0 | 0 |
| StaticMeshActor2735 | 3.905 | 0 | 0 |
| StaticMeshActor254 | 3.595 | 0 | 0 |
| Mover10 | 39.860 | 51.076 | 3.519 |

The mesh-only diagnostic is not equivalent to the complete build: it omits BSP
lightmap generation and the full lighting command's temporary actor-collision
setup. The brighter Mover10 result needs a full-build comparison. These tests have
not shown that cooking removed any original lights. Restoring baked colours
also does not fix differences introduced by a later lighting rebuild.

## Reproduction

Run the isolated native harness with `-TraceLighting` on the original OffsD.
Inspect `lighting_cooked`, `lighting_built`, and `lighting_reopened`. Each
`*_colors.bin` is the native StaticMeshInstance colour stream (four bytes per
vertex); match actors and vertex counts before comparing values.

For the original-BSP diagnostic add `-InspectCookedOnly -RelightCookedMeshes`.
The native calls were identified from this supported editor executable:
GetRenderData `110B2690`, UpdateRenderData `110B15A0`, mesh Illuminate `110E8190`,
actor finalization via vtable `8C`, and colour baking `11195230` (called by normal
LIGHT APPLY at `110087B2`). Illuminate alone produces shadow masks and leaves a
new colour stream unbaked; comparing it without the colour-baking call is invalid.

## Earlier builds and test plans

### OffsD_Recovered_9: lighting and sheet subdivision

The screenshot showed restored floor, ramp and pillar shading, and manual
testing confirmed the lighting improvement. Water is still missing, so lighting transfer did not solve
water visibility. Original water portal pairs are (14,30), with some (0,30);
recovered pairs are (20,30), with some (0,30), consistent with zone renumbering.
Original and rebuilt root zone masks are both 0x7FFFFFFE. Read-only inspection
of representative serialized water render sections also finds flags 0x80100
in both. Render-vertex material UVs are present in both packages. Other render fields were not checked.

The next experiment disabled coplanar coalescing for visible non-solid sheets,
retaining their cooked polygon subdivision through source emission. Invisible
divider coalescing remains enabled. Native polygon preparation still enforces
the importer vertex limit. The experiment tested whether translucent BSP subdivision or draw order
affected water visibility. Lighting transfer stayed enabled. The Release build
passed; a fresh recovery and game test were next.

### BSP lighting transfer test build

RecoveredBspLighting now captures the original cooked RGBA8 lightmap mip data
and each rendered node's positions, lightmap UVs, material and atlas bindings.
It copies the data before MAP NEW destroys the cooked UObjects.
During recovery's atlas compression, after filtering, it maps rebuilt chart
texels to world positions, matches original coplanar triangles of the same
material and facing direction, and samples their baked lighting bilinearly.
Original texture dimensions are used (including stock downsampled textures).
Unmatched pixels keep their recalculated values. The transfer changes neither
global brightness nor material lighting flags, and does not run on later builds.

The transfer validates native arrays/indices, chart bounds, transform inversion,
surface-plane consistency and supported texture format. Failure stops recovery.
Atlas sizes and full pixel-content FNV-1a fingerprints must survive normal
save/reopen. The transfer stays active through recovery and save-time compression, then
turns off whether recovery succeeds or fails.

Engine addresses and layouts: FMatrix row-vector transform at 10EB2D40 (thunk 10E022E9);
render sections at model+D4, stride38, vertex array+4 with stride28 and UVs+20/+24;
node section/start fields+50/+52; lightmap chart matrix+28 and atlas rect+14..20;
texture entries model+E0, stride70, embedded compressed texture+14. Mip zero
loads through 111B2400; format5 is native CompressLightmaps' RGBA8 output.

The Release build passed. The next test was a fresh recovery launched through
the normal game selector before any extra build. Water visibility and the
separate Play Here crash were still open at this stage.

### OffsD_Recovered_8 BSP audit

Three snapshots were collected. Original BSP has 6,159
rendered nodes; built/reopened BSP has 6,019. Rebuilt and reopened snapshots
agree. Water is split differently, but each of its four horizontal visible
layers retains approximately 678,743,840 square units of polygon area:

| Plane Z | Flags | Original fragments | Rebuilt fragments | Zone pair |
| --- | ---: | ---: | ---: | --- |
| -449 | 1048840 | 307 | 183 | 30,30 |
| -450 | 264 | 307 | 191 | 30,30 |
| -467 | 1048840 | 307 | 182 | 30,30 |
| -468 | 264 | 307 | 191 | 30,30 |

The zone pairs were independently read from serialized nodes in the original,
saved MapsEd and playable Maps packages. Every decoded node was checked against
the audit's surface index, vertex count and plane, including all 6,019 nodes
in each saved copy. Water retains material Aquarium_TXT.Mer.pan_2 and zone 30.
Matching total area does not show whether the same points are covered or visible.

Original model+EC has zero authoring lightmap entries; it retains cooked render
sections and texture data instead (earlier original model dump: 55 sections at
D4, 8 texture entries at E0). Built/reopened models contain 3,141 authoring
lightmap entries. The original node+3C values are not indices into EC when that array is absent;
this is normal cooked data. The next investigation
is the regenerated render-section/texture data and its binding to node polygons.
This audit did not change the lighting or water code.

Read-only serialized-node decoding was identified from FBspNode serializer
110CDC80: 4 floats, 8-byte mask, byte flags; seven compact signed indices;
two 16-byte spheres; two zone bytes and vertex count; five int32 values
(native fields 4C,4E,50,52,3C). No UObject table parsing was required: candidate
streams were accepted only after matching every node to the corresponding
audit. Search must tolerate signed zero in plane normals; JSON may erase it.

At this stage, a test through the normal game selector showed improved static
mesh lighting and a visible sky. The floor, ramp and pillar remain black; a second
screenshot shows missing water and broad invisible areas. Full lighting and
surface fidelity remain unresolved. The water material Aquarium_TXT.Mer.pan_2
is present in Geometry/Imported/Built/Reopened T3D, with matching polygon flags
and counts (1,486 polygons). T3D records the source polygons. The BSP after CSG still needed inspection.

The next diagnostic build writes BspCooked.json, BspBuilt.json and
BspReopened.json to the recovery folder. These record final node polygons,
surface material paths/flags and node lightmap indices. Native BuildRenderData
at 110D141C uses node+3C as an index into model+EC with A4-byte stride. This build
does not change BSP lighting or water behaviour. The Release build passed;
a fresh recovery was needed to collect the comparison snapshots.

### Initial colour preservation implementation

Recovery now snapshots original mesh colour bytes by actor name and mesh asset
path before destroying the cooked level. After the normal build it restores
those bytes only when the imported asset path and colour count match. Embedded
asset paths are mapped to the recovery asset package. The ordinary save/reopen
check now requires byte-for-byte equality for every captured mesh stream.
Missing actors, mismatched assets/counts, or changed saved colours stop recovery
with an error instead of reporting success. Existing per-platform finalization
still runs after restoration. The snapshot copies the bytes before the cooked UObjects are destroyed.

The Release build passed and manual testing was next. This first version
restored mesh colours; it still rebuilt BSP lightmaps. Later lighting builds
could still darken the meshes.

The next test still showed a dark scene. OffsD_Recovered_7's recovery
report confirms all 629 original streams passed save/reopen equality. The later
editor session journal (pid12304) records another geometry/BSP/lighting build at
18:53:40–18:53:49, followed by Play Level of Autoplay.sdc at 18:54:22. The named
runtime OffsD_Recovered_7.sdc still has its initial 18:52:10 timestamp. The later lighting build could have replaced the preserved colours, so this
test did not show the initial recovery result. A follow-up identified a Play
Here crash on recovered maps. SCCT_Versus_crash.log records access violation 0xC0000005 at 0x109EA378,
where the game reads [eax+0x2C] with eax=0. This is distinct from the editor's
mesh-bound assertion. The log did not show why the pointer was null. Initial preserved lighting still needs a check through the normal game
map selector. No additional DLL change was made for this report.

Manual check: restart the editor with the new DLL, recover the original OffsD
under a fresh name, and compare spy base in game before another lighting build.
Recovery itself already performs its normal build/save/reopen. If it reports a
colour verification error, retain that error and Recovery folder for diagnosis.
Then test a later ordinary lighting build separately to distinguish retained
original colours from recalculated lighting. Compare the appearance in both cases.

Trace the missing contribution at a spy-base mesh through light-candidate
selection, shadow visibility, and colour accumulation. Check whether the original bake needs missing authoring data or whether the
native lighting build has a bug. Compare both the initial recovery and a later
lighting rebuild.

The separate intermittent recovery crash also remains unresolved. Two
attempts asserted at `10EC27F2` during mesh lighting; two subsequent menu-driven
attempts completed with diagnostics attached without hitting the pre-assertion
breakpoint. No production fix was applied. The debugger was detached and its
temporary breakpoint removed.
