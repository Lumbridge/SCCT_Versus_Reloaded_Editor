# SCCT Versus Reloaded Editor

An unofficial editor patch for Splinter Cell: Chaos Theory Versus, compatible with the stock game and [Enhanced SCCT Versus](https://github.com/Joshhhuaaa/EnhancedSCCTVersus). Based on [AllyPal's Reloaded Editor](https://github.com/AllyPal/SCCT_Versus_Reloaded_Editor).

## Added in this fork

After adding or subtracting geometry, build it, select the affected faces or
brushes and static meshes (including surfaces receiving changed shadows), and
choose **Build → Recalculate Selected Lighting…**. Selected areas receive a
fresh native bake; the rest keeps its existing lighting. Save a backup first:
the native bake can look different from recovered lighting.

For dark BSP wall or floor extensions, **Build → Match Selected BSP Lighting…**
is an experimental alternative. Select the new faces and any affected shadow
receivers after building geometry. Leave nearby original surfaces unselected:
the tool uses references with the same material and facing direction, within
16 units of the same plane and 512 units of each lighting sample, with a BSP
visibility check between points offset in front of the surfaces. It adds the
local difference between original and freshly calculated reference lighting to
the selected bake. Unselected BSP and static meshes retain their existing bake.
The completion message reports coverage; samples without a reference retain
native lighting. This does not support static-mesh matching, automatically find
changed shadows, or reconstruct missing lights. Check the approximation in game.

| Feature | What it does |
| --- | --- |
| **Compiled-map recovery** | Turns compiled maps into editable brushes, actors, and assets. |
| **Selective lighting** | Recalculate selected BSP faces, brushes and static meshes while preserving lighting elsewhere. |
| **SMagicEvent Workbench** | Edit event groups, triggers, actions, and timing in one panel. |
| **Map JSON import/export** | Export map data to JSON; preview and import actor, effect and event-connection changes with batch Undo. |
| **Map Design workspace** | Reference images, parametric blockout brushes, clearance guides, measurements, layers, repeated placement, temporary playtest positions and route/objective annotations. |
| **SMagicEvent JSON exchange** | Export event settings for assisted editing, then import changes with validation and one-step Undo. |
| **SCamNetwork Manager** | Create, name, preview and reorder cameras, with automatic reciprocal links and first-camera flags. |
| **Gameplay Connections** | View actor links in a list or graph, jump to connected actors, and rename Tags with their linked events. |
| **Objective creation** | Right-click an SMission to add an SObjective, or an SObjective to add a computer trigger, bomb target, or flag/drop-zone pair, with automatic links. Also available in Gameplay Connections. |
| **Selective Tag renaming** | Uncheck individual actor Tags or linked Event assignments before renaming a shared group. |
| **Map packaging** | Create a playable-map ZIP with dependencies, map-selection images, and an installation report. |
| **Working Views** | Save and restore viewport cameras, display settings, and visibility. |
| **Actor Assemblies** | Save groups of actors and brushes, edit their contents, and reuse them across maps. |
| **Find Usages** | Find where textures and meshes are used, then replace selected assignments. |
| **Texture and mesh favorites** | Keep frequently used materials and meshes together across packages. |
| **BSP texture copy/paste** | Copy a surface's material while keeping the destination's texture alignment. |
| **Select Brush** | Select the source brush directly from a BSP surface. |
| **Fit builder brush to meshes** | Right-click selected static meshes and choose **Position the builder brush around this** to create a surrounding box. |
| **Quick grid adjustment** | Change grid size with **Ctrl + mouse wheel** over a viewport. |
| **Brush edge grid snap** | Right-click a brush or BSP face → **Snap brush edge to grid** → **X axis**, **Y axis**, **Z axis**, or **All axes**. |
| **Selected vertex grid snap** | In **Vertex Editing**, select vertices, then right-click a vertex and choose **X axis**, **Y axis**, **Z axis**, or **All axes**. |
| **Play Level resolution** | Launch playtests at the current resolution of the editor's monitor. |
| **Property-window reset** | Bring off-screen property windows back onto the editor monitor. |
| **Builder-brush repair** | Restore a missing or damaged builder brush as a default cube. |
| **BSP fixes and crash diagnostics** | Support larger BSP point counts and record build failures and crashes. |

Also includes AllyPal's original fixes: faster selection, restored Echelon lighting, improved lightmaps, and larger WAV imports.

Brush edge snapping uses the current grid spacing and moves the nearest outer
bound onto a grid line along each chosen axis. It translates the whole brush;
multiple selected brushes move together using their combined bounds. Shapes and
spacing are preserved, and each move supports Undo/Redo. A face selection targets
its source brush. Rebuild geometry after moving BSP brushes as usual.

Vertex snapping moves each selected vertex to its nearest world grid line on the
chosen axes, reshaping the brush. Unselected vertices stay in place. Right-click
opens the axis menu while preserving the vertex selection; cancelling changes
nothing. The move supports Undo/Redo. Rebuild geometry after editing BSP vertices.

Select one mission or objective for the objective context-menu actions. In
**Gameplay Connections**, use **Add Objective / Trigger...** or right-click the
panel to add to the actor shown in its status line, or right-click a graph node.
New actors appear 32 units beside their parent mission or objective. A flag and
its drop zone appear on different sides, each 32 units from the objective.
Existing mission/objective links are preserved. Each action supports one-step
Undo/Redo, including creation of both flag actors and their drop-zone link.

## Map Design

For a new map, open **View > Map Design...**. Create and preview rooms, corridors,
doorways, stairs, ramps and platforms, then place ordinary brushes in one Undo
step. The workspace provides top/front/side design views with calibrated image
references and planning overlays. See [Map Design](docs/MapDesign.md) for the eight
tool groups, controls, persistence and current limits.

## Install

Download the ZIP from [Releases](https://github.com/Lumbridge/SCCT_Versus_Reloaded_Editor/releases) and extract it into the same folder as `SCCT_Versus.exe`. Run `Reloaded_Editor.exe`.

Choose **View > SCamNetwork Manager...** to manage surveillance cameras. Each
connected network appears separately. **New network** creates its first camera;
**Add camera to network** inserts another at the builder brush. Navigate the
perspective viewport and use **Place at viewport position + aim** to position it.
Rename changes the in-game `CamName` without changing actor Tags.

Use **Move up**, **Move down**, or **Make first camera** to change the game order.
The manager updates `NextCam`, `PrevCam`, and `bFirstCam` together in one undo step.
Check **Loop last camera back to first** and **Apply order** for a circular network.
Broken links are flagged for review; **Apply order / repair links** rewires the
displayed network. **Detach** keeps the actor as a separate network. To merge
networks, select all their cameras in the map and choose **Link selected cameras**.

Double-click a camera or use **Previous / Next** to preview its position and aim
in the first perspective viewport. **Return to original view** or closing the panel
restores that viewport. **More camera properties** opens native properties for
rotation constraints, materials and other settings. Save the map to retain edits.

Recovered maps, including Clarity Soft, are available in [SCCT-Maps](https://github.com/Lumbridge/SCCT-Maps).

Use **View > Export Map to JSON...** to export the loaded map's actors, properties,
available classes and assets, and geometry context. Use **View > Import Map from JSON...**
to preview and apply a JSON change file in one Undo step. Actors, particle components,
property edits and delayed event links are supported. The exported snapshot includes
a change-file template; importing uses that change format rather than replacing the
whole map with the snapshot. Save afterwards; lighting and geometry are not rebuilt
automatically. See the [map JSON guide](docs/MapAuthoring.md).

In **SMagicEvent Workbench**, use **Export JSON...** to share an event for help
editing it. Use **Import JSON...** to apply the edited settings to the event open
in the workbench, or choose **New Event** first for a separate event. Import keeps
the destination Tag and placement; linked actors must be created separately.
Undo reverses the import. See [the JSON format guide](tests/SMagicEventJson.md).

To box in a static mesh, select it and right-click **Position the builder brush
around this**. The builder brush becomes a world-aligned box enclosing the
mesh's transformed bounds. Rotation, scale and pivot offsets are included;
multiple selected meshes are enclosed together. Flat bounds get a minimum
thickness of one unit. Faces use the selected material, or the default texture
if none is selected. The meshes stay selected and unchanged. Undo restores
the builder brush's previous shape and placement in one step.

<details>
<summary>Package a map for sharing</summary>

Save/build your latest edits, then choose **File > Package Map for Sharing...** and select the saved playable `.sdc` from `Packages/Maps`. Review the dependency list and choose **Create ZIP...**. The archive keeps the game's folder layout and includes `MapPackage-README.txt` with installation instructions and dependencies.

All discovered asset files are checked initially. Use **Exclude base files...** with a separate base installation to uncheck byte-identical dependencies, or uncheck files manually when recipients already have them. Omitted dependencies remain listed as required in the report. The playable map always stays included.

Missing, ambiguous, unreadable, or changed dependencies prevent packaging. The scan follows package imports and includes the map's `-i` image package when present; files loaded only through script string paths may need to be supplied separately. Existing ZIPs are never overwritten. Archives are limited to 4 GB and currently require a destination filesystem that supports hard links, such as NTFS.

</details>

<details>
<summary>Map recovery instructions</summary>


Save your work, then choose **File > Recover Compiled Map...**. Recovery can take several minutes and replaces the active map without overwriting the original files.

The editable copy goes in `Packages/MapsEd`, the playable copy in `Packages/Maps`, and any extracted mesh assets in `Packages/StaticMeshes`. Keep those assets with the map when sharing it. Reports and T3D exports are saved in `Packages/MapsEd/Recovery`.

Recovered source maps preserve their existing baked lighting by default, including after reopening the editor. Ordinary builds retain mesh colours and transfer BSP lighting onto matching rebuilt surfaces. **Build > Recalculate Lighting...** explicitly replaces the bake after confirmation; subsequent builds protect that new bake. New surfaces or incompatible mesh vertex layouts use recalculated lighting. Geometry and light changes can leave preserved lighting outdated; review the result and use the explicit action when needed. If an earlier build already darkened a map, recover again from the original compiled file to restore its bake.

Recovery is experimental. Original brush history cannot be restored and reconstructed geometry may be fragmented. Check the result in the editor and in game. Use **File > Convert Legacy Recovered Map...** for files made by the older recovery mode.

</details>

<details>
<summary>Build from source</summary>


Use a Visual Studio C++ developer PowerShell with v143 and the manifest's vcpkg dependencies installed:

```powershell
$env:SCCT = $null # Skip deployment into an installed game.
msbuild SCCT_Versus_Reloaded_Editor.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x86 /p:PlatformToolset=v143
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
./tools/package_release.ps1
```

The three-file release ZIP is written to `bin/Reloaded_Editor.zip`. Packaging downloads the unchanged AllyPal v1.2 launcher and verifies both archive and executable SHA-256 hashes. To use a local copy of that exact launcher, pass `-LauncherPath 'path/to/Reloaded_Editor.exe'`. The newly built DLL supplies the editor updates; packaging does not use the rebuilt launcher. See [tests/README.md](tests/README.md) for test instructions.

</details>
