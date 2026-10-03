# RE+ (Reloaded Editor Plus)

RE+ is an unofficial patch for the **Splinter Cell: Chaos Theory Versus** map editor. It works with the stock game and with [Enhanced SCCT Versus](https://github.com/Joshhhuaaa/EnhancedSCCTVersus), and builds on [AllyPal's Reloaded Editor](https://github.com/AllyPal/SCCT_Versus_Reloaded_Editor).

## Install

1. Download the ZIP from [Releases](https://github.com/Lumbridge/SCCT_Versus_Reloaded_Editor/releases).
2. Extract it into the folder that contains `SCCT_Versus.exe`.
3. Run `Reloaded_Editor.exe`.

The editor keeps itself up to date: shortly after it starts, it checks the releases and offers to install a newer version, which takes effect the next time you start it. **Help > Check for RE+ Updates...** checks on demand, and **Help > Check for Updates at Startup** turns the automatic check off. After an update, the first start shows that release's notes once; **Help > What's New in RE+...** shows them again. The version an update replaced is kept as `Reloaded.Editor.previous.dll` (with `Reloaded_Editor.previous.exe` when the launcher changed): **Help > Roll Back to RE+ x.y.z...** puts it back at the next start, keeps the newer version to switch forward to again, and stops the startup check offering the version you left (**Check for RE+ Updates...** still can). The window title and **Help > About RE+...** show the version you are running.

### Versions

RE+ releases are numbered `MAJOR.MINOR.PATCH`, starting at **RE+ 2.0.0** (1.x was AllyPal's Reloaded Editor and the 1.3.0 betas that led to RE+):

- **MINOR** (2.1.0, 2.2.0) is a feature release: new tools, windows or workflows.
- **PATCH** (2.1.1) only fixes bugs.
- **MAJOR** changes only when something stops being compatible, such as the install layout or files an earlier version saved.

A release that needs testing first goes out as a release candidate (`2.1.0-rc.1`), marked as a pre-release on GitHub. The updater offers release candidates only to editors already running one, so everyone else gets the release.

Looking for maps to open? Recovered, enhanced and community maps are in [SCCT-Maps](https://github.com/Lumbridge/SCCT-Maps), and [SCCT Map Manager](https://github.com/Lumbridge/SCCT-Map-Manager) installs them for you.

## What it adds

### Map Design workspace

![Map Design workspace](docs/images/map-design.png)

**View > Reloaded Tools > Map Design...** is a floor-plan view for planning and greyboxing a map. Draw rooms, corridors, vents and doorways as parametric pieces and drag, resize or type to change them; the brushes in the map follow every edit in one Undo step. Right-click anywhere for what applies at that point. Copy, paste, duplicate and mirror pieces, pick common sizes from presets, draw sightlines that show where a wall blocks the view, or start from a **Versus starter layout** with spawn rooms, corridors, a vent route, starts, a mission and an objective already placed. It also has reference images, player-size guides, routes timed for spies and mercs, playtest spawns, groups and locks in a Scene panel, a design check, a **Security** window that places lasers, cameras, motion sensors, mines and alarms and wires them together, and lights placed from presets, dragged into reach, and checked for the dark spots spies will use. Full guide: [docs/MapDesign.md](docs/MapDesign.md).

### Gameplay tools

![Gameplay Connection Graph](docs/images/connection-graph.png)

| Tool | What it does |
| --- | --- |
| **Gameplay Connections** | See every Tag/Event link in the map as a list or a graph, jump to connected actors, rename Tags together with the events that use them. |
| **SMagicEvent Workbench** | Edit event groups, triggers, actions and timing in one panel, with JSON export/import for sharing an event. See [the JSON format](tests/SMagicEventJson.md). |
| **SCamNetwork Manager** | Create, name, preview and reorder security cameras; links and first-camera flags are kept consistent. |
| **Objective creation** | Right-click an SMission or SObjective to add objectives, computer triggers, bomb targets or flag/drop-zone pairs, already linked. |
| **Zones** | In Map Design, list the match's zones: which objectives each holds, how many must be done, and what happens when it completes (doors open, lights switch, sounds play, a message on both HUDs, any actor triggered). One click gives each zone its own SMission, all of its objectives playable at once, under the map's chained top mission, in one Undo step, and reads them back for later edits. See [docs/MapDesign.md](docs/MapDesign.md#15-zones). |
| **Map JSON import/export** | Export a map to JSON, edit it (or have something edit it for you), and import the changes in one Undo step. See [docs/MapAuthoring.md](docs/MapAuthoring.md). |

### Map recovery, lighting and sharing

| Tool | What it does |
| --- | --- |
| **Recover Compiled Map** (File menu) | Turns a compiled `.sdc` into an editable map with brushes, actors and extracted meshes. Takes a few minutes; the original file is untouched. |
| **Recalculate Selected Lighting** (Build menu) | Re-bakes lighting only for the selected faces, brushes and meshes, keeping the recovered lighting everywhere else. |
| **Match Selected BSP Lighting** (Build menu) | Experimental: blends new BSP faces into the surrounding original lighting. |
| **Package Map for Sharing** (File menu) | Builds a ZIP of the map with all its dependencies and an install readme. |

Recovered maps keep their original baked lighting through ordinary builds. Recovery is experimental: brush history cannot be restored and geometry may come back fragmented, so check the result in the editor and in game.

### Everyday editing

| Tool | What it does |
| --- | --- |
| **Open Recent** | **File > Open Recent** lists the last ten maps opened or saved, and warns before discarding unsaved changes. (The editor's own autosave, in Advanced Options, still writes `Auto0` to `Auto9.sdc` into MapsEd every five minutes.) |
| **Play From Camera** | **Build > Play From Camera as Spy / Merc** starts a playtest at the perspective viewport's camera, from that team's start or a temporary one that is removed afterwards. |
| **Level Snapshot** | **Build > Set Level Snapshot from Viewport** captures the perspective viewport as the picture the game shows in map selection; **from Image File...** takes a PNG, JPG or BMP instead. Either goes into the map's `<Map>-i` package next to its loading screens and map settings, with the previous package backed up under `System\ReloadedEditor\snapshot-backups`. |
| **Select and hide** | Right-click an actor for **Reloaded: Select** (all of this class, all with this Tag, same static mesh, invert the selection) and **Reloaded: Visibility** (hide selected, isolate selected, unhide all); Brush Visibility has the same three buttons. |
| **Brush Visibility** | Show, hide, isolate or select zones, portals, volumes, brushes and movers from one panel. What is hidden stays hidden through a geometry build, which the stock editor makes visible again. |
| **Storeys** | The Map Design plan's floor slider, for the editor's own viewports: pick a storey and everything standing on the other floors is hidden, so a multi-level map is worked on one floor at a time. **Page Up / Page Down / Home** over a viewport step through them while the palette is open. Surfaces already built into the BSP stay, so it reads best in the wireframe views. |
| **Working Views** | Save and restore viewport cameras and display settings. |
| **Actor Assemblies** | Save groups of actors and brushes and reuse them across maps. |
| **Emitter Library** | **RE+ Tools > Emitter Library...** browses particle effects by category with search and a live, engine-rendered preview (drag to orbit, wheel to zoom). **Place at builder brush** (or double-click) adds the effect in one Undo step. Tick **Loop** to place any effect, triggered ones included, so it plays again on its own a random wait of your chosen seconds after it ends. Ships with 37 effects captured from the stock maps: fire with smoke, torches, steam, drips, splashes, bubbles, rain, snow, sparks, dust, moths and triggered bursts. Select emitters and right-click **Save to Emitter Library...** to add your own; read-only effect packs in `System\ReloadedEditor\EmitterPacks` appear as their own group. |
| **Find Usages** | Find where a texture or mesh is used, and replace it. |
| **Favorites** | Keep frequently used materials and meshes together. In the Static Mesh Browser, **Favorites** lists your favourite meshes from every package, whichever map is open (their packages load as needed); filter it to one package and sort by package, name, or when you added them. Favourites that cannot be loaded, such as meshes stored inside a map, stay listed as *not loaded* and can still be removed. |
| **BSP texture copy/paste** | Copy a surface's material without disturbing the target's alignment. |
| **Select Brush** | Select the source brush from a BSP surface. |
| **Grid snapping** | Right-click a brush face or vertices to snap them to the grid per axis; **Ctrl + mouse wheel** changes the grid size. |
| **Fit builder brush** | Right-click selected meshes or brushes to enclose them in the builder brush, including rotated brushes. |
| **Quick portal** | In Vertex Editing, select four coplanar corners and right-click **Add portal sheet**. Creates a single invisible, non-solid zone portal polygon whose corners are the selected corners, like the stock Add Special zone portal on a sheet brush, with Undo/Redo. |
| **Self-updater** | Offers newer releases at start-up and installs them in place; **Help > Check for RE+ Updates...** checks on demand. The first start of a new version shows its release notes once (**Help > What's New in RE+...** on demand), and **Help > Roll Back to RE+ x.y.z...** swaps back to the version the update replaced, keeping the newer one to roll forward again. |
| **Reloaded Shortcuts** | **Help > Reloaded Shortcuts...** lists every key, mouse gesture and menu the patch adds, with the Geometric Event keys as configured. |
| **Fixes** | Playtests launch at the monitor's resolution, off-screen property windows come back, a missing builder brush is repaired, larger BSP point counts are supported, and crashes are logged. |

AllyPal's original fixes are included: faster selection, restored Echelon lighting, improved lightmaps and larger WAV imports.

<details>
<summary>More screenshots</summary>

![SMagicEvent Workbench](docs/images/magic-event-workbench.png)

![SCamNetwork Manager](docs/images/camera-network-manager.png)

![Package Map for Sharing](docs/images/package-map.png)

</details>

<details>
<summary>Build from source</summary>

Use a Visual Studio C++ developer PowerShell with the v143 toolset and the manifest's vcpkg dependencies installed:

```powershell
$env:SCCT = $null # Skip deployment into an installed game.
msbuild SCCT_Versus_Reloaded_Editor.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x86 /p:PlatformToolset=v143
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
./tools/package_release.ps1
```

Set the version in `Reloaded.Editor/Version.h` first: the title, About box, updater, DLL properties and archive name all come from it. The release ZIP is written to `bin/Reloaded_Editor_Plus_v<version>.zip`; tag the release `v<version>`. Packaging downloads the unchanged AllyPal v1.2 launcher and verifies its hash; pass `-LauncherPath` to use a local copy. Tests are described in [tests/README.md](tests/README.md).

</details>
