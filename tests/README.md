# Regression tests

## HELI02 compiled-map recovery

`tools/test_map_recovery.cmd` covers the exact HELI02 brush whose three-vertex
bevel falls below native `CalcNormal` minimum area. Repair keeps the remaining
supporting planes and a closed brush within the native BSP precision band;
a long tapered brush verifies that distant intersections are rejected without
changing the input. Actor tests cover omitted all-null `MoversToLock` entries
while retaining checks for live references and other struct fields.

The native recovery harness with `-SourceMap <HELI02.sdc>` additionally checks
the map's duplicate portal outlines (cooked surfaces 1165 and 1168), recovery,
save, normal reopen and rebuild in a disposable installation.

## Map Design

`tools/test_workflow_tools.cmd` includes `MapDesignModelTests.cpp`: outward polygon
winding and volume, carve and shell room/corridor construction, zone portal sheets
and their brush flags, stair rise, ramp geometry, invalid dimensions, grid
snapping, bounds, snapping a dragged face or box onto neighbouring geometry,
projected convex outlines, vent and crawlway shapes with crouch clearance,
attaching a new piece to a wall, route journeys with climbs and crouched sections,
both teams' route times, a material on every generated polygon, the design check's unreached rooms, orphan doorways and merging carves, preview resizing against a fixed opposite face, traversal warnings for
steps/slopes/clearance, doorway placement in a room wall including rotated rooms
and rejected offsets, route lengths and spy/merc timings, floor ranges, native
group name handling, portable workspace export/import, image calibration, 3D
measurement, stable distribution, and repeated brush/object/event reference
remapping, mirrored poses checked vertex by vertex against the reflected
geometry, the piece clipboard's anchor and offsets, sightlines through a turned
doorway and through a wall, size presets, and numbered names. `SecurityModelTests.cpp` covers the security device catalogue, Unreal
yaw from two points, laser length and direction from two clicks, beam ends,
sensor boxes, tag allocation, and the wiring report with its unwired detectors,
unfed alarms, dangling events and empty sensors.

The native `-GenerateFixture -WorkflowTools` run creates and resizes real brushes,
checks Undo/Redo identities, imports ramps/stairs, places a carved room and a
portal doorway and confirms the sheet counts as a zone portal, writes and clears
layer membership in the native `Group` field, reads the editor's grid spacing,
toggles native layer flags, and verifies temporary spawn restoration, including a
temporary team start that is created and removed again. The Play Level test
intercepts its launcher in the disposable process, checking the temporary pose at
launch without starting a game. UI checks use the actual blockout, doorway,
reference, calibration, measurement, player-reference, movement-limit, floor-filter, snap,
layer, route, route-comparison and workspace dialogs, box-select by dragging in the
design view, and the direct editing loop: a new blockout opening in the inspector
instead of a dialog, a placed piece loaded back into it, an inspector edit
replacing that piece's brushes rather than adding more, an arrow key nudging it by
one grid step, Escape ending the edit, the panel's Undo and Redo reaching the
editor's history, Copy and Paste placing a copy at the cursor and at a
right-clicked point, Duplicate placing one beside the original, Delete dropping
a copy from the library, a size preset resizing a placed piece in place, a
sightline inside a room reporting clear, the frame's Reloaded selection and
visibility commands (all of a class, invert, hide, unhide all), and the quick-add badge on a hovered wall placing a corridor
against it through its menu, the stale-geometry flag clearing when B runs a
geometry build, the design check window listing its issues, and the security
tools: creating an alarm, a laser and a motion sensor with its volume through the
ops, linking the laser's Event to the alarm's Tag and the alarm's outputs to a
target Tag, then the Security window placing an alarm with one click and a laser
and a motion sensor with two clicks each, the laser wired to that alarm with its
length and direction from the clicks, then moving a laser and a sensor (with its
volume) through `security.move` and by dragging the laser's body and its beam-end
handle in the plan, the badge sitting outside a hovered wall and surviving the
cursor moving onto it, the menu bar replacing most side buttons, and the
right-click menu starting a corridor preview where the plan was clicked. A piece's brushes are also checked against its
preview at a position off the editor grid. Locks, groups, group moves and lifts go
through `design.flags`, `design.group`, `design.translate` and `design.lift`, each
checked to be one Undo step; the lift is inspected for its two keys and its
stand-on state. `map_design_preview.bmp`, `map_design_workspace.bmp`,
`design_workspace.json` and `design_clearances.json` are retained in the fixture.
All native tests use a separate temporary installation, never a working map.

`LevelSnapshotModelTests.cpp` covers the level snapshot's pure parts: the
snapshot package name, the 24-bit BMP handed to the editor's texture importer
(bottom-up rows, padding), the `.utc` container header, the package tables of
a stock-shaped `<Map>-i` package (position-ciphered names, numbered name
references, imports, exports with their classes), a texture export's size,
format and mip count read past struct, bool and float properties, and the
check of the saved file (a 256 x 256 Menu texture, no objects lost, truncated
or foreign files rejected). The editor side imports the BMP with `TEXTURE
IMPORT` into the loaded `<Map>-i` package and saves it with the stock
`SAVEMAPPROP`, so it is exercised in the editor rather than here.

`tools/test_map_package.cmd` also covers packaging a map's Map Design workspace and
the reference images it names, skipping missing or unsafe image names, and
tolerating an unreadable workspace file.

## Brush edge grid snap

The focused native `-BrushGridSnapOnly -GenerateFixture` run also checks Brush
Visibility: category selection, hiding and deselection, Undo, isolation, opening
the panel through the native menu dispatcher, Show all and checkbox toggles.
It saves `brush_visibility.bmp` in the disposable test installation.

`tools/test_workflow_tools.cmd` runs the standalone `BrushGridSnapTests.cpp`
checks for nearest bounds, negative coordinates, ties, per-axis spacing, no-ops
and invalid inputs. The native `-WorkflowTools -GenerateFixture` suite exercises
the brush and BSP-face context menus, axis isolation, preserved dimensions,
Undo/Redo, and rejection of an empty selection.
Use `-BrushGridSnapOnly -GenerateFixture` for the focused native snapping run.
This also checks the vertex right-click popup, selection preservation on cancel,
world-space selected-corner alignment, exact preservation of unselected vertices,
shared polygon corners, and vertex Undo/Redo.

## Local BSP lighting matching

Compile and run `tests/LocalLightingMatchTests.cpp` with C++17 or later. It tests
closest-triangle sampling, degenerate triangles, signed RGB residuals, clipping,
alpha preservation, and retained local variation. The native
`-ReopenOnly -VerifyPreservedLighting -VerifySelectedLighting` suite also invokes
the matching menu, checks cancellation and exact preservation of unselected
BSP/meshes, then saves and reopens the resulting combined bake. These byte checks
do not establish visual fidelity or correct shadows. `-LightingBrush
'Brush3702,Brush3728'` targets named brush faces in that suite instead of its
default single-face fixture. Segment tests cover empty space, solid half-spaces,
an intervening wall, invalid child indices and cyclic BSP data.

Use `-ReopenOnly -MatchLightingAfterLoad -LightingBrush 'Brush3702,Brush3728'`
to test matching immediately after loading a source map, without warming the
collision/lighting state through a preceding rebuild. Native probes also check
all six compressed-collision bounds accessors using the September 13 crash's
exact finite bounds while all eight x87 registers are occupied. Each must return
the correct endpoint pointer and preserve the complete x87 state.

## Map packaging and selective Tag renaming

Run `tools\test_map_package.cmd` from an x86 Visual Studio developer prompt after installing the manifest dependencies. The packaging suite covers compressed and raw SCCT package tables, older name encodings, transitive/cyclic dependencies, missing and ambiguous packages, texture counterparts, byte-identical base comparisons, exclusions, stale files, archive paths, and no-overwrite publication. `MapPackageTests.exe <game-root> <playable-map> [new-zip]` also supports read-only inspection or packaging of real maps.

The workflow model and native suites cover excluded Tag/Event assignments, stale previews, partial rename undo, and the actual popup checkboxes. Native screenshots include `tag_rename_exclusions.bmp` and `map_package_preview.bmp`. The packaging dialog check scans a saved playable map and verifies that its checkbox cannot be cleared.

## Editing workflow tools

The native `-GenerateFixture -WorkflowTools` suite checks mission/objective
context-menu availability and command dispatch, repeated additions preserving
existing links, computer and bomb triggers, paired flag/drop-zone creation,
single-step Undo/Redo, connection discovery, stale/wrong-parent rejection,
and Gameplay Connections button state with Follow Selection enabled/disabled.
It also clicks the native popup with a non-frame owner and the graph node popup
with a different viewport selection, and checks that all created actors are
within 32 units of their parent, including both members of a flag/drop-zone pair.

Map JSON tests cover document validation, nested symbolic references, a native
scene containing light/sound/emitter/trigger/event actors and particle components,
forward references, delayed links, exact placement, stale updates, batch rollback,
Undo/Redo, file dialogs, preview cancellation/application and save/reopen. See
[MapAuthoring.md](../docs/MapAuthoring.md) for the format and fixture artifacts.

SMagicEvent JSON tests cover export/import round trips, field and version
validation using the live schema, protected actor identity/placement, invalid
later actions and missing object references leaving the event unchanged,
stale snapshots, one-step undo/redo, and the actual Save/Open dialogs including
cancellation. `magic_event_export.json` and `event-ui-roundtrip.json` are retained
in the disposable native fixture. See [SMagicEventJson.md](SMagicEventJson.md).

The native workflow suite checks the static-mesh **Position the builder brush
around this** menu action with signed nonuniform scale, rotation and PrePivot,
then verifies the six box faces, centered placement, multiple-mesh bounds,
preserved selection and viewports, one-step undo/redo, and rejection of empty
or mixed selections. It also checks selected-material assignment and a valid
default texture on all six faces when no material is selected. All brush
and mesh changes occur in the disposable fixture.

The SCamNetwork suite checks separate networks, linear and circular ordering,
singleton loops, broken references, inconsistent first flags, duplicate/missing
cameras, and rejection of partial-network edits. Native tests verify camera
creation and reciprocal link edits in a single undo step, stale-edit rejection,
the manager menu, rename, next-camera preview and exact viewport restoration,
reordering, detach, DPI layouts, and camera persistence after restarting the editor.
The isolated test directory includes `camera_network_manager.bmp`,
`camera_network_manager_144dpi.bmp`, and `camera_network_saved.json`.

The SMagicEvent workbench adds model validation tests plus native fixtures for
array edits, precise timeline drags, actor/component and brush-volume creation,
mover keys, rollback, undo/redo, and rejection of edits made from outdated snapshots.
The fixture calls the native activation thunk to check delay,
Sequence, Repeat and ValidOn semantics, then saves and reopens a complete event
with two triggers, mover, sound, emitter and damage volume in a second process.
See [SMagicEventSemantics.md](SMagicEventSemantics.md) for the engine behaviour.
Screenshots and `magic_*.json` diagnostics remain in the isolated test directory.

From an **x86 Native Tools Command Prompt for Visual Studio**:

```bat
tools\test_workflow_tools.cmd
```

The model suite covers nested native T3D, quoted reference paths, authored strings,
canonical member names, internal reference/tag remapping, explicit external bindings,
position/rotation composition, in-place updates, version rejection and atomic persistence.
The graph suite checks separate islands, cyclic/self/parallel links, unresolved targets,
relationship/class filters, bounded neighbourhood expansion, isolated actors, deterministic
layout and a 600-actor fan-out without overlapping nodes. Radial layout checks cover
hub selection independent of actor order and parallel properties, circular fan-out,
and label bounds for nested branches with cyclic cross-links. The native harness
also renders a 16-target event island and verifies hub search and selection.
The native surface-menu checks verify Select Brush availability, single and multiple
source-brush selection after rebuilding, unchanged cameras and face selection, and
rejection of faces without master polygons without disturbing actor selection.
It also checks actor-tag labels and classification of nested EventGroup links in filters
and island grouping. The native fixture imports `SBase.SMagicEvent` with two Groups and
multiple EventGroup entries, verifying incoming links, shared Tags, `None`, and an
unmatched target. `workflow_magic_schema.json` records the native structure layout and
`workflow_magic_edges.json` records the resulting relationships.

Run the native integration suite against a disposable editor copy:

```powershell
./tools/run_native_map_recovery_test.ps1 -EditorDll ./bin/Reloaded.Editor.dll -GenerateFixture -WorkflowTools -TimeoutSeconds 180
```

This uses the existing isolated driver; it never runs the test against the installed
game directory. The report and workflow JSON/BMP artifacts remain in the printed test
directory. The workflow probe verifies native BSP replacement scope, undo/redo and
source-polygon rebuild persistence; cyclic/one-to-many Event/Tag relationships and
instance isolation; brush insertion, undo/redo, rebuild and save/reopen; viewport
restoration and in-place updates; and the actual view/assembly/connection dialogs,
including adding/removing members and selection-based updates without deleting scene actors,
stable pivots and changed future placements. It then starts a second editor process and
checks camera restoration, another in-place view update and placement of the updated
assembly from disk.
The first process also opens the native graph panel, renders whole-level and selected-actor
graphs, searches and focuses an actor, and exercises native double-click navigation.
Graph screenshots are retained as `workflow_graph_*.bmp`.
Tag-rename coverage verifies a shared two-actor group and both ordinary Event and nested
EventGroup dependants, one-step undo/redo of nested arrays, collision/None/stale-preview
rejection, and the graph's rename/confirmation/cancel workflow. Native FName construction
is verified at `0x10fb9610` with `NAME_Add=1`; existing FName spellings are never edited.

Build both solution configurations with `SCCT` cleared. Use **Rebuild** when switching
configurations: the renderer static library currently shares its output path. The new
modules are included unconditionally in the editor project. The native integration
is specific to the supported ChaosTheory editor executable and must be checked again
if native addresses or property layouts change.

Also check these cases manually: static-mesh replacement with material overrides,
game-specific array/structure actor links,
Save As/map switching, GE selections, and external-binding changes during assembly edits.
Use disposable source maps and include stale/deleted instance members and missing packages.

## Map recovery

ClarD soft-body regression (2026-09-13): one native simulation step before
recovery reproduced the import error on ESBStripDoorActor1009. Points and springs
matched, but the old settings comparison included timestep fields at 0x12C/0x130
and the zero-to-0.01 damping change made by Reloaded's realtime hook.
The comparison now skips simulation scratch at 0x11C..0x133 and treats that
specific damping fallback as unchanged only when the actor's EnergyFactor is
zero. Other physical settings still require an exact match.

The portable settings test checks scratch changes, default damping, nonzero
authored damping and changes to every compared byte. For a native reproduction,
add `-TraceSoftBodies -StepCookedSoftBodies` to the ClarD recovery command. This
steps each supported cooked soft body once before capturing its recovery data.
ClarD also exceeded the PC engine's signed 16-bit collision-bound index limit.
On overflow, recovery now sorts its generated structural cells by longest extent
and rebuilds with fewer BSP splits. It leaves polygon coordinates, materials and
non-solid sheet order unchanged. Ordinary rebuilds keep the saved brush order;
maps close to the collision limit start with the same BSP settings.
The fallback produced 32,575 collision slots with a highest start index of
32,564; the final BSP build reduced that to 32,431 slots. Recovery preserved
109 portal outlines and passed 12,206 solid/empty probes.
The native source checks now reject collision indices outside the PC range after
recovery, reopening and rebuilding.
The ClarD run passed recovery after a soft-body simulation step, ordinary
rebuilding, a solid-brush and actor Tag edit, File Save, reopening and export.
A separate fresh-editor run also passed load/rebuild/File Save/reopen without
invoking recovery. Both runs used the Release DLL built on 2026-09-13.

A later user run hit the static-mesh collision-box assertion at `0x10EC27F2`
during lighting. Its retained endpoints were finite and ordered. The compressed
collision decoder now builds bounds through integer comparisons of the float
bits, preserving the coordinates and avoiding x87 register use. Non-finite
endpoints still go through the native constructor and its diagnostics.
The native constructor comparison covers 10,000 endpoint pairs. A full x87 stack
produced incorrect bounds with the stock constructor and exact bounds with the
replacement. Portable tests cover the dump's endpoints, 100,000 finite random
pairs, aliased inputs and non-finite rejection. The original user assertion has
not been reproduced in a full recovery run.
With the replacement, menu-driven ClarD recovery completed its lighting,
save/reopen and retained-data checks. A separate fresh-process test passed
ordinary geometry/BSP/lighting/path rebuilding, File Save, reopening and export.

Use `-RecoveryMenu -VisibleEditor` for the actual recovery menu, file picker and
confirmation dialog in the isolated test. `-FpuBoundsProbe` checks native bounds
with a full x87 stack; combined with `-RecoveryMenu`, it then runs recovery.

Sub18 regression (2026-09-13): retained portal surface 1282 has a vertex
0.074219 units from its cooked BSP plane. Recovery accepts the native BSP
splitter's 0.25-unit coplanar band, using normalized plane distance and retaining
the original vertex coordinates. Hallway and living-room ZoneEffect objects are
preserved in the dependency package. ESBPatchActor simulations regenerate from
their authored settings, with native comparisons of topology, pinned anchors,
springs and physical settings after import, rebuild and ordinary reopening.
The point serializer at `0x110D34C0` puts a transient vector at `0x28..0x33`;
its Z component must not be included in the authored-data comparison.
The isolated Sub18 run passed recovery, saving, ordinary reopening and a further
geometry/BSP/lighting/path rebuild: 17 patches, 71 portal outlines, 333 structural
brushes, 1812 retained actors and 3980 solid/empty probes. The portable recovery
suites also pass, including patch class/setting mismatch rejection and the new
ZoneEffect classes. Visual and gameplay checks were not part of this run.

EDE64 regression (2026-09-13): retained portal 121 faces opposite its cooked
surface, and some portal surfaces lack PF_NotSolid. Recovery preserves authored
portal winding while validating the undirected plane and reconstructing solid
space independently. Arena and Cave audio effects are preserved as dependencies.
The polygon-import suite includes the actual EDE64 edge from approximately
`-5.7e-14` to `384`: unchanged endpoints lie on the edge without needing a rounded
subtraction, while removed intermediate points still require exact collinearity.
The isolated run passed recovery, ordinary save/reopen and subsequent rebuild:
259 structural brushes, 1403 actors, nine strip doors, 80 portal outlines and
4338 solid/empty samples. It preserved 995 original mesh colour streams. Lift
SLift14593 has 72 cooked colours versus 168 rebuilt render vertices, so recovery
retains its newly calculated colours and verifies them after reopening. A mesh
asset identity change, missing instance or invalid stream still stops recovery.

From an **x86 Native Tools Command Prompt for Visual Studio**, run:

```bat
tools\test_map_recovery.cmd
```

The portable suites verify structural BSP classification, closed brush geometry,
texture-region partitioning, native polygon vertex limits, actor and LevelInfo
transfer, GE gameplay data, and lossless asset-package decompression. They include
malformed input, numeric edge cases, deep trees, work limits and no-overwrite cases.
The geometry tests compare independently classified points before and after reconstruction.
The leaf-light table suite checks signed-index overflow, exact ordered lists,
shared suffixes, empty lists, capacity rejection and randomized roundtrips.
Surface fixtures also cover concave and non-planar non-solid sheets: triangulation
retains their original 3D float vertices and supplies each triangle's normal.
Native import applies additional consecutive-vertex and small-area cleanup.
The polygon-import suite reproduces those rules, including their float arithmetic,
and checks exact outline preservation. Recovery checks every exported polygon before import, since native cleanup
can otherwise leave a gap in a brush. Surface tests cover exact convex
coalescing, material divisions at native import and BSP split precision, and removal of collapsed bevels
while keeping the remaining planes and checking that each brush is closed. Brush ordering
tests preserve coordinates and reject mixed CSG operations. Also rebuild and inspect the map in the editor, especially around thin geometry.

The [native editor integration test](#native-editor-integration) also exercises
conversion, geometry edits, ordinary reopening and builds in a disposable editor.

## Native editor integration

Run recovery tests in a temporary copy of the supported editor. The installed
DLL, configuration and maps are left alone. From PowerShell:

```powershell
./tools/run_native_map_recovery_test.ps1 -EditorDll ./bin/Reloaded.Editor.dll -GenerateFixture
./tools/run_native_map_recovery_test.ps1 -EditorDll ./bin/Reloaded.Editor.dll -SourceMap 'C:/path/to/compiled.sdc'
./tools/run_native_map_recovery_test.ps1 -EditorDll ./bin/Reloaded.Editor.dll -SourceMap 'C:/path/to/compiled.sdc' -EditGeometry
./tools/run_native_map_recovery_test.ps1 -EditorDll ./bin/Reloaded.Editor.dll -SourceMap 'C:/path/to/source.sdc' -ReopenOnly -ExtraAssetPackage 'C:/path/to/source_Assets.usx'
```

The harness requires the Visual Studio x86 C++ tools and uses `SCCT` as the
installed System directory (override with `-GameSystem`). It creates a separate
editor installation in a unique temporary directory, copies configuration and
maps, and shares only asset directories that the test does not write. Its
StaticMeshes directory is a complete copy because recovery may create a new
dependency package there. The generated fixture is an ordinary room brush that
the native editor builds and compiles before attempting recovery.
`-StartupConfigSystem` can copy startup INI files from a previously successful
isolated test while retaining the installed assets. Startup critical errors are
recorded separately from recovery failures.

An injected probe invokes the recovery API on the editor's UI thread, then uses
ordinary map loading and geometry/BSP/lighting/path rebuilding. It records
actor, brush, polygon and BSP counts and exports a final T3D. `PASS` requires
geometry to survive recovery, ordinary reopen, and another normal rebuild, and
the normal File Save target to identify the recovered source. The generated
fixture additionally adds a solid pillar to the recovered room, rebuilds it,
then saves and reopens it while checking that the edited BSP survives.
`-EditGeometry` also exercises a real geometry edit on an existing compiled
map: it finds empty BSP space, adds a small solid brush, rebuilds and saves,
then verifies the changed space and surface count after ordinary reopening.
It also changes a retained static mesh actor's Tag through native property
import and checks that the property survives the same save/reopen cycle.
`-GenerateFixture -RootOutside` exercises the complementary solid cube in an
empty world and edits a cavity into it, preserving `UModel::RootOutside=1`.
`-ReopenOnly` starts a fresh editor and exercises ordinary source loading,
rebuilding, saving and export without invoking the recovery API. Optional
`-ExtraAssetPackage` paths copy generated dependencies into that isolated
editor. `-GenerateFixture -ImportBaseline` isolates the native T3D import,
platform actor synchronization, rebuild and source/runtime save path.
`-GenerateFixture -ExpectRecoveryFailure` blocks geometry interchange writing
after cooked loading with an owned directory. It checks that the rejected
conversion clears the previous normal File Save target, then loads the original
source and verifies that a new geometry edit rebuilds normally.
`-ImportTextOnly -SourceMap <fixture.t3d>` isolates ordinary native T3D import
and export without recovery, rebuilding or saving. It writes
`System/import_only.t3d`; compare that output with the fixture to check its properties. This mode confirmed that
spaced object paths require nested quotes, for example
`StaticMesh=StaticMesh'"Oilrig_SM.Third Floor.topcatwalk"'`, and polygon material
paths require double quotes, for example
`Begin Polygon Texture="Oilrig_TXT.Top floor.yellowmetal"`.
Adding `-BuildImportedText` runs ordinary geometry and BSP rebuilding before
export and writes `System/NativeImportedBsp.json` for spatial comparisons.
The probe also writes cooked and failed BSP trees as JSON for spatial diagnosis.
`-InspectCookedOnly` loads and exports a compiled file, records its native BSP
arrays and player-start occupancy, then stops before reconstruction.
`-TraceSoftBodies` additionally records native soft-body fields and strip-door
point/spring arrays for comparing procedural regeneration with cooked data.
`-TraceLeafLights` writes the native leaf indices and light tables after builds
and reopening to diagnose visibility-table limits.
`-TraceLighting` records reflected light settings, loaded light-actor membership,
mesh leaf-light candidates, and each mesh instance's baked BGRA colour stream at
cooked/imported/built/reopened stages in `System/lighting_*`. The trace includes the colour bytes. `-InspectCookedOnly -TraceLighting
-RelightCookedMeshes` additionally refreshes mesh render data and runs native
mesh shadow-mask generation and colour baking against the original BSP in the
disposable editor. This diagnostic skips the full lighting command, BSP rebuild and map save.
Lighting traces also include per-light shadow-mask visibility and before/after
snapshots around an ordinary full rebuild. Add `-AllLeafLightCandidates` to the
mesh-only diagnostic to test candidate gathering independently of spatial leaf
membership; add `-SkipMeshShadowOcclusion` to isolate shadow rejection. Both
experiments restore temporary actor state and do not save a map.
The current OffsD findings and remaining limitations are recorded in
[MapRecoveryLightingStatus.md](MapRecoveryLightingStatus.md).
`-ReopenOnly -VerifyPreservedLighting` checks a recovered source with nontrivial
lighting: mesh bytes must survive an ordinary build, mesh/BSP bytes must survive
File Save/reopen, cancelling Recalculate Lighting must change nothing, accepting
it must replace the bake, and later ordinary lighting commands must preserve the
replacement. `-TraceLighting` also dumps BSP atlas bytes before/after the build
and after saving/reopening for independent comparison.
Add `-GeometryOnlyBuild` to the reopen test to issue only MAP REBUILD in the
native build bracket. This covers preservation's internal BSP finalization and
lighting transfer without a subsequent explicit BSP build masking a sequencing
bug. Use an edited recovered source for the stale visibility-index regression.
`-ReopenOnly -VerifyPlayMapSave` exercises the native Play Level save call and
its temporary runtime package repair without launching the game. Test a map
larger than the 15 MB uncompressed writer buffer and a small map. Inspect the
resulting `Packages/Maps/Autoplay.sdc` with `-InspectCookedOnly` to verify that
the native compiled-map reader can load it.
Ordinary recovery verifies strip-door topology, rest lengths, fixed anchors,
physical settings and all exported authored actor properties at import, build
and reopening. Decorative sheet tests check native polygon acceptance and
exact boundary-edge preservation across alternative triangulations.
Actor tests also cover stale Xbox labels on confirmed PC runtime actors,
retained particle components and references, and exclusion without native PC
membership evidence. Recovery verifies rebuilt mesh-lighting bindings after
normal saving/reopening. The native rebuild probe includes the normal Build
UI's platform-lighting cache finalizer before subsequent saves.
`-CompactPoints` is a separate native compaction experiment that wraps completed
CSG operation boundaries; it is excluded from ordinary source recovery checks.
The first trial reclaimed unused points but later encountered a stale point
reference, so it is not ready for normal builds.
Reports, source/runtime maps, interchange files and crash dumps remain in the
printed `TestRoot` for inspection. The disposable editor is stopped on finish
or timeout. The harness tests map editing and saving. Test gameplay separately.

## Play Level resolution regressions

For a selective-lighting regression, add `-VerifySelectedLighting` to a native
`-ReopenOnly -VerifyPreservedLighting` run with a recovered map containing lit
static meshes. It checks cancellation, a changed selected mesh, exact unchanged
mesh and BSP chart texels, selected BSP recalculation, and preservation of the
combined bake through File Save/reopen. Atlas packing may change; chart texels
are compared independently of their atlas locations.

After adding or subtracting geometry, build geometry, select affected BSP faces
or their source brushes and static meshes, then use **Build > Recalculate
Selected Lighting...**. Include surfaces where shadows should appear or disappear.
Selecting a light alone does not select its receivers. The selected areas use
the native baker and may differ from the original bake. Unselected chart texels
are copied without resampling; incompatible chart identities/dimensions fail
with a message to reopen the saved map. Shared charts require selecting their
adjoining faces too.

Reloaded's `labs_borderless_fullscreen` setting in `SCCT_Versus.config` controls
borderless presentation for both play commands and ordinary game launches.

Compile `PlayLevelConfigTests.cpp` and `PlayLevelCommandTests.cpp` with MSVC
`/std:c++17 /W4 /WX /EHsc` (link `user32.lib` for the configuration test).
They cover resolution selection, preserving source INI bytes and unrelated
settings, configuration failure paths, and recognizing explicit INI overrides.
Play Level clones `Default.ini` to `Reloaded_PlayLevel.ini` and sets viewport
and menu dimensions before launching. It defaults to the editor monitor's
current display mode; `[PlayLevel] ResolutionX/ResolutionY` in
`Reloaded_Editor.ini` can override both dimensions. An explicit launch `INI=`
is respected. The legacy executable-token repair remains separate and active.
