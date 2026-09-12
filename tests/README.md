# Regression tests

## Editing workflow tools

The SMagicEvent workbench adds model validation tests plus native fixtures for
array edits, precise timeline drags, actor/component and brush-volume creation,
mover keys, compound rollback and undo/redo, and stale snapshot rejection.
The fixture exercises the actual native activation thunk to verify delay,
Sequence, Repeat and ValidOn semantics, then saves and reopens a complete event
with two triggers, mover, sound, emitter and damage volume in a second process.
See [SMagicEventSemantics.md](SMagicEventSemantics.md) for the verified contract.
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
is specific to the supported ChaosTheory editor executable and must be reverified
before changing native addresses or property layouts.

Additional manual acceptance coverage: static-mesh replacement with material overrides,
game-specific array/structure actor links,
Save As/map switching, GE selections, and external-binding changes during assembly edits.
Use disposable source maps and include stale/deleted instance members and missing packages.

## Map recovery

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
and checks exact outline preservation. Recovery preflights every emitted polygon
so native cleanup cannot silently open a brush. Surface tests cover exact convex
coalescing, material divisions at native import and BSP split precision, and removal of collapsed bevels
while preserving the remaining planes and proving closed geometry. Brush ordering
tests preserve coordinates and reject mixed CSG operations. Native rebuild/space
checks and visual inspection remain necessary, especially around thin geometry.

The [native editor integration test](#native-editor-integration) also exercises
conversion, geometry edits, ordinary reopening and builds in a disposable editor.

## Play Level

Run from the repository root in an **x86 Native Tools Command Prompt for Visual Studio**.

```bat
cl /nologo /std:c++17 /W4 /WX /EHsc tests\PlayLevelCommandTests.cpp /Fo"%TEMP%\PlayLevelCommandTests.obj" /Fe"%TEMP%\PlayLevelCommandTests.exe"
"%TEMP%\PlayLevelCommandTests.exe"

cl /nologo /std:c++14 /W4 /WX /EHsc /c tests\PlayLevelPresentationTests.cpp /Fo"%TEMP%\PlayLevelPresentationTests.obj"
cl /nologo /std:c++14 /W4 /WX /EHsc /c Reloaded.Editor\Include\d3d8to9\source\d3d8types.cpp /Fo"%TEMP%\PlayLevelPresentationConverter.obj"
link /nologo "%TEMP%\PlayLevelPresentationTests.obj" "%TEMP%\PlayLevelPresentationConverter.obj" user32.lib gdi32.lib /out:"%TEMP%\PlayLevelPresentationTests.exe"
"%TEMP%\PlayLevelPresentationTests.exe"
"%TEMP%\PlayLevelPresentationTests.exe" -ReloadedEditorPlay

cl /nologo /std:c++14 /W4 /WX /EHsc tests\PlayLevelWindowTests.cpp /Fo"%TEMP%\PlayLevelWindowTests.obj" /Fe"%TEMP%\PlayLevelWindowTests.exe"
"%TEMP%\PlayLevelWindowTests.exe"

cl /nologo /std:c++17 /W4 /WX /EHsc tests\PlayLevelConfigTests.cpp /Fo"%TEMP%\PlayLevelConfigTests.obj" /Fe"%TEMP%\PlayLevelConfigTests.exe" user32.lib
"%TEMP%\PlayLevelConfigTests.exe"
```

These tests create no windows or Direct3D devices and cannot change display modes.
The command tests preserve the captured editor map URL, game mode, team and platform
while retaining `HWND=0`. Removing `HWND=` altogether makes the game discard its
playtest command and open the normal front end.

The Reloaded launch regression keeps two quoted executable tokens in the outer
command: the launcher's token and the raw game's token. Each process removes its
own token, so `Autoplay.sdc?...` remains the first actual game argument. The
installed Reloaded launcher was also checked using an isolated synthetic child
executable and DLL: bare map arguments lose the map in the child's `WinMain`,
while the prefixed command preserves the complete URL. No game was run for that
check.

The presentation tests link the production parameter converter. Separate processes
verify ordinary launches and marked playtests, including repeated fullscreen
requests, refresh rates and swap effects. All three wrapper device creation/reset
paths use this converter. Marked requests must always become windowed, with zero
fullscreen refresh rate and a windowed-compatible presentation interval.
These converter/window tests cover the separate d3d8 wrapper; Reloaded embeds its
own renderer and uses its own borderless setting for actual Reloaded playtests.

The window tests verify enlargement, aspect ratio, frame-inclusive work-area
limits, centring on monitors with negative origins, and placement/reset reentrancy.
The renderer applies placement after the first successful presentation and after
successful resets, without repeatedly recentering an already placed window.

The configuration tests write only temporary INIs and verify actual native
viewport resolution settings, display/fixed resolution selection, original-file
preservation and error handling. No game is launched.

The Reloaded v3.0a scoreboard and overlay regressions can be checked without launching the
game or initializing its DLL. First create a patched copy from the supported
original DLL (the patcher also accepts an already patched DLL):

```bat
python tools\patch_reloaded_play_level.py "%SCCT%\Reloaded.Core.dll" --output "%TEMP%\Reloaded.Core.play-level-guard.dll"
cl /nologo /std:c++17 /W4 /WX /EHsc tests\ReloadedCorePlayLevelGuardTests.cpp /Fo"%TEMP%\ReloadedCorePlayLevelGuardTests.obj" /Fe"%TEMP%\ReloadedCorePlayLevelGuardTests.exe"
"%TEMP%\ReloadedCorePlayLevelGuardTests.exe" "%TEMP%\Reloaded.Core.play-level-guard.dll"
```

This test maps the DLL with `DONT_RESOLVE_DLL_REFERENCES` and supplies synthetic
`SPlayerProfile`, player-owner and level-context objects. Eight scoreboard cases
check null profile, null owner, eligibility, blocked state and unchanged visibility.
Eight cases for each of the overlay and controller callbacks check deferral while
required objects are absent and dispatch once they exist. Ready dispatch uses an
in-memory callee probe, avoiding graphics/UI execution; it never changes the DLL
file. Graphics modules remain unloaded throughout. All 24 cases pass.

The original DLL reproduced the first recorded crash, and the scoreboard-only
revision reproduced the overlay crash. The initialized scoreboard cases exclude
the unrelated settings-persistence call. These isolated checks do not establish
complete profile initialization or control parity.

Timed live launches under a debugger reached the selected level and accepted
keyboard input with the guards installed. Memory inspection showed that direct
Play Level startup still had no `SPlayerProfile`; normal frontend startup did
construct one. Overlay/controller deferral therefore persists in direct playtests.
Full Reloaded controls and other profile-dependent behavior remain unfinished.

End-to-end verification additionally requires a live editor playtest with both
updated editor DLL installed and the guarded Reloaded Core present: the selected level should load,
Reloaded.Core.dll should be loaded, its configured controls and frame timing should work,
the render should be sharp at the selected resolution, the window should be large
and centred, mouse input should remain correct,
and switching back to the editor should not change the desktop display mode.

## Native editor integration

Native source-map recovery can be exercised against the supported installed
editor without replacing its DLL, configuration, or maps. From PowerShell:

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
`System/import_only.t3d`; its completion result requires a separate comparison
of that output to verify the fixture's properties. This mode confirmed that
spaced object paths require nested quotes, for example
`StaticMesh=StaticMesh'"Oilrig_SM.Third Floor.topcatwalk"'`, and polygon material
paths require double quotes, for example
`Begin Polygon Texture="Oilrig_TXT.Top floor.yellowmetal"`.
Adding `-BuildImportedText` runs ordinary geometry and BSP rebuilding before
export and writes `System/NativeImportedBsp.json` for spatial comparisons.
The probe also writes cooked and failed BSP trees as JSON for spatial diagnosis.
`-InspectCookedOnly` loads and exports a compiled file, records its native BSP
arrays and player-start occupancy, then deliberately stops before reconstruction.
Its completion result confirms inspection only, not conversion or gameplay.
`-TraceSoftBodies` additionally records native soft-body fields and strip-door
point/spring arrays for comparing procedural regeneration with cooked data.
`-TraceLeafLights` writes the native leaf indices and light tables after builds
and reopening to diagnose visibility-table limits.
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
reference, so this experiment is not a validated rebuild fix.
Reports, source/runtime maps, interchange files and crash dumps remain in the
printed `TestRoot` for inspection. The disposable editor is stopped on finish
or timeout. This checks the authoring pipeline; it does not launch a gameplay
session or validate a map's complete gameplay behavior.

Map-name assignment regression (x86 native tools prompt):

```bat
cl /nologo /std:c++17 /W4 /WX /EHsc tests\ReloadedCoreMapNameTests.cpp /Fo"%TEMP%\ReloadedCoreMapNameTests.obj" /Fe"%TEMP%\ReloadedCoreMapNameTests.exe"
"%TEMP%\ReloadedCoreMapNameTests.exe" "%TEMP%\Reloaded.Core.play-level-guard.dll" "%SCCT%\SCCT Versus"
```

This executes all three patched filename-copy blocks with the stock game's actual FString assignment helper, a test allocator, and only the helper's imports resolved. It checks long/short names, the inline-string boundary, 240-character names, empty strings and absent destination owners. The game and Core DLL entry points are never run. Existing callback guard tests should also pass with the combined patch.
