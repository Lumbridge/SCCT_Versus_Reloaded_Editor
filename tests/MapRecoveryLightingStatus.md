# OffsD lighting investigation — 12 September 2026

> Status update — 13 September 2026: the user confirms that the Play Level profile limitation and recovered-map Play Here crash are no longer current limitations. References below describe the earlier investigation.

## Confirmed window visibility and map-check findings

User confirms the office-window visibility issue is fixed by the separate
solid-side portal visit records. User also checked the original: the office
alarm does not close its windows there either, so no alarm behaviour change is
required. Exported SAlarm2100 Events target maindoor1..5 and ventdoor1; shutters
use the separate volet_c/volet_d chain through SMagicEvent263. Relevant alarm,
mover and switch properties match the recovered export.

All three reported map-check warnings originate in the original:
- SInterrupteur1905 and SInterrupteur12587 have Event=Ascensseur, with no actor
  bearing that Tag. Both are Call Lift buttons and are also referenced directly
  by SLift1713.DoorsAndSwitches. These warnings do not identify alarm links.
- ZoneInfo2634 at (3456,1792,0) already has iLeaf=-1 and zone0 in the original.
  Moving/removing it merely to clear the warning would change original content.
No actor events or zone placement were changed for these findings.

## Office-window visibility follow-up

User reports recovered15 is nearly identical, but office interiors disappear
when viewed through windows from outside and look correct after entering.
The log confirms successful recovery, all 629 original mesh colour streams,
BSP atlas equality and 44 original portal outlines after ordinary save/reopen.

Portal1504 -> rebuilt3502 now first has solid-side pair(0,1), followed by the
room pair(2,1); original starts with room pair(5,1). Portal1505 ->3497 similarly
starts with(0,1) before(10,1); original has only(7,1). Native111AA91B tests a
per-surface visited bit and111AAC18 marks it after portal processing. A solid
fragment can therefore consume a visit needed by the room fragment. This is
a supported visibility hypothesis; the user's visual result remains pending.

The next recovery build gives solid-side fragments of mixed portal surfaces
their own surface/visited identity, retaining the flags and full original
outline. FPolys are deep-copied with engine-owned allocations to avoid shared
ownership. Node geometry, zone pairs, BSP hierarchy and masks are unchanged.
Checks after build and ordinary reopen reject shared solid/room visit records.
This is recovery-scoped; a later BSP rebuild may merge surfaces again.
Release compilation passed. All runtime testing remains with the user.

The user confirms that initial recovered lighting now looks great after mesh
colour and BSP lightmap transfer. Water remains invisible in game and inside the
outer cube, but is visible from outside the cube in the editor. Later ordinary
lighting rebuild fidelity and the separate Play Here crash remain unresolved.

## Retained portal outline fix (awaiting user test)

User confirms water is back. Recovery14 failed portal verification at built
surface2399 before TransferMeshLighting and normal saving. The user then saved
the working map as OffsD_Recovered_1 (confirmed in the log); the bottom-floor
comparison therefore does not represent a completed lighting-preserving run.
Remaining darkness is not yet resolved or ruled out.

Audit comparison finds 25/44 original portal outlines intact. Surface2399 is
an extra tiny portal on a structural closing face inheriting fallback flags;
19 other outlines became triangles during generic sheet preparation of slightly
nonplanar original quads. Structural emission now strips portal/antiportal flags
from generated faces. Authored portal outlines bypass generic sheet triangulation
while still passing native importer cleanup/outline checks. Full outline checks
after build/reopen remain enforced. Release compilation passed; user testing pending.

The first outline build rejected valid vertices because validation used the
surface texture origin as a geometry-plane point. Original dumped surface1533
has texture origin Z=-480 while its node plane and retained vertices are Z=-448;
other original portals also have off-plane texture origins. Validation now uses
the node plane equation with double-precision accumulation and the same 0.05
tolerance. Release compilation passed; the corrected DLL is installed. Runtime
recovery and water visibility still await the user's test.

OffsD_Recovered_11 still hides water inside the outer cube in unlit textured
view. The coplanar ordering workaround did not fix it and has been removed.
The earlier visible-sheet subdivision experiment was also unsuccessful.

Native visibility at 111AA91B marks portals once per surface; 111AA951 copies
the retained FPoly at surface+20 to clip visibility into the adjacent zone.
This uses the authored portal outline, not the current BSP node polygon.
Read-only package decoding found the original water portal at logical offset
929751: four vertices spanning X=-16000..12032, Y=-13316..12796, Z=-448.
Recovered_11's retained portal at offset1082935 instead has five vertices in a
western strip, X=-15616..-10784.041. This is a concrete loss of the portal's
visibility aperture, consistent with the user's inside/outside screenshots.

Extraction now takes one retained authored FPoly per original portal surface,
instead of rebuilding that portal from cooked fragments. The native FPoly
layout is vertices+18, count ushort+148, normal+C; serializer110CBDF0 and
surface serializer110CDC27 confirm the retained payload. Native save refreshes
it from the source brush polygon (110CDB93), so fixing only a runtime copy would
not persist. The fix therefore restores the editable source polygon itself.
Outlines must match after build and ordinary save/reopen, including winding
and vertex positions within 0.02 units. Audits include portalOutline vertices.
Release compilation passed. The user performs all editor/game testing.

Other read-only checks found valid node bounds, subtree zone masks and zone
connectivity in original/recovered maps. Those did not reveal the outline loss.

## Verified observations

- The fresh original export and recovered export retain 113 `Light` and 21
  `STriggerLight` actors. Their exported lighting settings match. Native reflected
  scalar/boolean light settings also match, including static/in-game lighting
  flags. The loaded original package inventory contains those same 134 actors;
  no inactive additional `Light`/`STriggerLight` actors were found. This is a loaded
  object inventory, not proof that every possible package export was examined.
- Relevant actors retain their named ZoneInfo assignments. Zone identifiers are
  renumbered during reconstruction; the 437 actors in original zone 14 map to
  recovered zone 20. The out-of-zones ZoneInfo2634 warning also exists in the
  original exported state and alone does not establish a recovery regression.
- The same 629 mesh instances contain 158,619 baked vertex colours before and
  after recovery. Mean stored B/G/R byte values fall from approximately
  5.675/5.721/5.778 to 0.78/0.72/0.64 during rebuilding. Black RGB vertices rise
  from 105,069 to 145,393. These are colour-buffer statistics, not perceptual
  brightness measurements of screenshots.
- The rebuilt colour buffers survive ordinary saving and reopening with that
  darker content. Checking non-null StaticMeshInstance pointers is insufficient.
- A diagnostic mesh-only relight on the original BSP, with refreshed actor
  leaf membership, native shadow-mask generation and native colour baking, also
  loses illumination at spy-base meshes. This rules out reconstructed brushes
  as the sole explanation, but does not establish why recalculation differs.

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
setup. In particular, its brighter Mover10 result is not evidence of an improvement.
No conclusion that original light sources were stripped during cooking has been
established yet. Preserving original baked colours would preserve data, but would
not by itself provide matching results after a later lighting rebuild.

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

## Outstanding work

### OffsD_Recovered_9 visual result and sheet subdivision candidate

User confirms lighting now looks great; screenshot shows restored floor/ramp
and pillar shading. Water is still missing, so lighting transfer did not solve
water visibility. Original water portal pairs are (14,30), with some (0,30);
recovered pairs are (20,30), with some (0,30), consistent with zone renumbering.
Original and rebuilt root zone masks are both 0x7FFFFFFE. Read-only inspection
of representative serialized water render sections also finds flags 0x80100
in both. Render-vertex material UVs are present in both packages. This does not
prove every render field matches.

Next candidate disables coplanar coalescing for visible non-solid sheets,
retaining their cooked polygon subdivision through source emission. Invisible
divider coalescing remains enabled. Native polygon preparation still enforces
the importer vertex limit. This is a focused experiment in translucent BSP
subdivision/draw ordering, not an established root cause or confirmed fix.
Lighting transfer remains enabled. Release compilation passed; the user's next
fresh recovery and normal-game test will determine whether water improves.

### BSP lighting transfer test build

RecoveredBspLighting now captures the original cooked RGBA8 lightmap mip data
and each rendered node's positions, lightmap UVs, material and atlas bindings.
It keeps owned values across MAP NEW, without retaining cooked UObject pointers.
During recovery's atlas compression, after filtering, it maps rebuilt chart
texels to world positions, matches original coplanar triangles of the same
material and facing direction, and samples their baked lighting bilinearly.
Original texture dimensions are used (including stock downsampled textures).
Unmatched pixels retain the newly calculated values; no global brightness or
unlit material override is applied. Ordinary later user builds are unaffected.

The transfer validates native arrays/indices, chart bounds, transform inversion,
surface-plane consistency and supported texture format. Failure stops recovery.
Atlas sizes and full pixel-content FNV-1a fingerprints must survive normal
save/reopen. Transfer activation is scoped to synchronous recovery, including
any compression performed during saving; it is cleared on failure too.

Native evidence: FMatrix row-vector transform at 10EB2D40 (thunk 10E022E9);
render sections at model+D4, stride38, vertex array+4 with stride28 and UVs+20/+24;
node section/start fields+50/+52; lightmap chart matrix+28 and atlas rect+14..20;
texture entries model+E0, stride70, embedded compressed texture+14. Mip zero
loads through 111B2400; format5 is native CompressLightmaps' RGBA8 output.

Release compilation passed. Runtime recovery and visual fidelity need the
user's test. This is an implementation candidate, not a confirmed water fix.
Test a fresh recovery through the normal game selector, before any extra build.
Play Here's separate crash remains unresolved.

### OffsD_Recovered_8 BSP audit

The user collected the three snapshots successfully. Original BSP has 6,159
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
Area agreement alone is not a proof of pointwise coverage or visibility.

Original model+EC has zero authoring lightmap entries; it retains cooked render
sections and texture data instead (earlier original model dump: 55 sections at
D4, 8 texture entries at E0). Built/reopened models contain 3,141 authoring
lightmap entries. Original node+3C values must NOT be treated as valid indices
into an absent EC array or as evidence of damaged input. The next investigation
is the regenerated render-section/texture data and its binding to node polygons.
No BSP lighting or water fix has been deployed based on this audit.

Read-only serialized-node decoding was identified from FBspNode serializer
110CDC80: 4 floats, 8-byte mask, byte flags; seven compact signed indices;
two 16-byte spheres; two zone bytes and vertex count; five int32 values
(native fields 4C,4E,50,52,3C). No UObject table parsing was required: candidate
streams were accepted only after matching every node to the corresponding
audit. Search must tolerate signed zero in plane normals; JSON may erase it.

Latest visual check through the normal game selector confirms improved static
mesh lighting and visible sky. The floor, ramp and pillar remain black; a second
screenshot shows missing water and broad invisible areas. Full lighting and
surface fidelity remain unresolved. The water material Aquarium_TXT.Mer.pan_2
is present in Geometry/Imported/Built/Reopened T3D, with matching polygon flags
and counts (1,486 polygons). This verifies source polygons only, not the actual
rendered BSP after CSG. Do not conclude that water geometry survived from T3D alone.

The next diagnostic build writes BspCooked.json, BspBuilt.json and
BspReopened.json to the recovery folder. These record final node polygons,
surface material paths/flags and node lightmap indices. Native BuildRenderData
at 110D141C uses node+3C as an index into model+EC with A4-byte stride. This build
does not change BSP lighting or water behaviour. Release compilation passed;
the user must run a fresh recovery to collect the comparison snapshots.

### Initial colour preservation implementation

Recovery now snapshots original mesh colour bytes by actor name and mesh asset
path before destroying the cooked level. After the normal build it restores
those bytes only when the imported asset path and colour count match. Embedded
asset paths are mapped to the recovery asset package. The ordinary save/reopen
check now requires byte-for-byte equality for every captured mesh stream.
Missing actors, mismatched assets/counts, or changed saved colours stop recovery
with an error instead of reporting success. Existing per-platform finalization
still runs after restoration. The snapshot owns bytes, not cooked UObject pointers.

The Release solution builds successfully. Runtime validation is pending the
user's testing. This addresses initial mesh colour loss only: BSP lightmaps are
still rebuilt, and subsequent explicit lighting builds can still darken meshes.
No hook suppresses future user-requested lighting changes.

User follow-up: the tested scene remained dark. OffsD_Recovered_7's recovery
report confirms all 629 original streams passed save/reopen equality. The later
editor session journal (pid12304) records another geometry/BSP/lighting build at
18:53:40–18:53:49, followed by Play Level of Autoplay.sdc at 18:54:22. The named
runtime OffsD_Recovered_7.sdc still has its initial 18:52:10 timestamp. Therefore
this result does not yet establish how the initially preserved map looks; the
later lighting build can replace preserved colours. User clarification of the
tested launch route was subsequently clarified: Play Here crashes on recovered
maps. SCCT_Versus_crash.log records access violation 0xC0000005 at 0x109EA378,
where the game reads [eax+0x2C] with eax=0. This is distinct from the editor's
mesh-bound assertion. The existing log does not establish why that pointer is
null. Initial preserved lighting still needs a check through the normal game
map selector. No additional DLL change was made for this report.

Manual check: restart the editor with the new DLL, recover the original OffsD
under a fresh name, and compare spy base in game before another lighting build.
Recovery itself already performs its normal build/save/reopen. If it reports a
colour verification error, retain that error and Recovery folder for diagnosis.
Then test a later ordinary lighting build separately to distinguish retained
original colours from recalculated lighting. Do not infer full fidelity from
successful colour preservation alone.

Trace the missing contribution at a spy-base mesh through light-candidate
selection, shadow visibility, and colour accumulation. Establish whether the
original bake depends on unavailable authoring data or whether the current
native build path has a repairable difference. Validate both initial recovery
and subsequent ordinary lighting rebuilds before declaring lighting fixed.

The separate intermittent recovery crash also remains unresolved. Two user
attempts asserted at `10EC27F2` during mesh lighting; two subsequent menu-driven
attempts completed with diagnostics attached without hitting the pre-assertion
breakpoint. No production fix was applied. The debugger was detached and its
temporary breakpoint removed.
