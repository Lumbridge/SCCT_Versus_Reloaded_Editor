# SCCT Versus Reloaded Editor

SCCT Versus Reloaded Editor is an unofficial patch for the Unreal Level Editor used by Splinter Cell: Chaos Theory's Versus mode. It works with the stock game and [Enhanced SCCT Versus](https://github.com/Joshhhuaaa/EnhancedSCCTVersus).

This is my fork of [AllyPal's Reloaded Editor](https://github.com/AllyPal/SCCT_Versus_Reloaded_Editor). I've added compiled-map recovery, build diagnostics, and tools to speed up everyday map editing.

## What's New in This Fork

| Feature | Description | Release build |
| --- | --- | --- |
| [Compiled-map recovery](#compiled-map-recovery-experimental) | Recover cooked maps into normal source maps with editable geometry, actors and assets. | Included |
| [BSP and crash diagnostics](#bsp-and-crash-diagnostics) | Investigate failed builds and work with BSP point indices beyond the stock signed 16-bit limit. | Included |
| [Property-window fixes](#property-window-fixes) | Move off-screen property windows back onto the editor monitor. | Included |
| [Play Level compatibility](#play-level-compatibility) | Launch editor playtests through Reloaded with startup guards. | Included |
| [Texture Browser Favorites](#texture-browser-favorites) | Save frequently used materials in a Favorites tab. | Included |
| [Static Mesh Browser Favorites](#static-mesh-browser-favorites) | Browse saved meshes across packages without repeatedly switching package filters. | Included |
| [BSP texture copy and paste](#bsp-texture-copy-and-paste) | Reuse a surface's material on other surfaces while retaining their alignment. | Included |
| [Quick grid size adjustment](#quick-grid-size-adjustment) | Change the grid preset with **Ctrl + mouse wheel** directly over a level viewport. | Included |
| [Builder-brush repair](#builder-brush-repair) | Rebuild a missing or damaged builder brush as a default cube. | Included |

All listed features are included in current Release builds. See below for the remaining compiled-map recovery limitations.

## Install

Recovered map files: [Clarity Soft (ClarD)](maps/Clarity-Soft).

- Copy the built patch files, or extract a release archive, into the game's `System` directory, where `ChaosTheory_Editor.exe` is located.
- Run `Reloaded_Editor.exe` and check that the title bar displays **Reloaded Chaos Theory Editor** to confirm the patch is active.

### Build and Package a Release

From a Visual Studio C++ developer PowerShell with v143 and the manifest's vcpkg dependencies installed:

```powershell
$env:SCCT = $null # Build without copying into an installed game.
msbuild SCCT_Versus_Reloaded_Editor.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x86 /p:PlatformToolset=v143
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
./tools/package_release.ps1
```

The archive is written to `bin/Reloaded_Editor.zip`. Use `-ArchivePath` to choose a new filename for subsequent builds. It contains only `Reloaded_Editor.exe`, `Reloaded.Editor.dll` and `README.md`. The GitHub Actions build packages the same three files. Play Level requires a separately installed Reloaded Core and the manual guard step described below.

Release builds also include Sound Browser diagnostics: hold **Shift** when opening sound Properties to dump the current package's sound flags, and inspect `Packages/Sounds/<map>.uas.buildlog.txt` after a streaming-audio build.

## Editor Features

### Compiled-Map Recovery (Experimental)

Recovery converts a compiled map into an editable source map. Open it with **File > Open**, edit brushes or actors, rebuild, and save as usual.

Recovery uses the compiled PC level's actor list. Actors in that list are kept even if old editor labels mark them as Xbox-only; recovery clears those labels so the actors survive PC saves. Xbox-only actors outside that list are excluded.

Zone and portal divider brushes are kept but hidden by default. They control where ZoneInfo lighting, fog and sound settings apply. You can edit them, and they take part in rebuilds. Recovery also keeps ZoneInfo actors and occlusion volumes.

Recovery preserves the compiled map's original baked mesh colours after building the source geometry. It checks mesh asset identities and vertex counts before restoring colours, writes them to the platform cache, then checks every restored byte after saving and reopening. If a matching mesh has a different render-vertex count, recovery keeps and verifies its newly calculated lighting instead, and records that fallback in the recovery report. For supported RGBA8 BSP lightmaps, recovery also resamples the original baked lighting onto matching rebuilt surfaces and checks atlas contents after reopening. Unmatched lightmap texels keep their recalculated lighting. A later lighting rebuild replaces the preserved bake and may make the map darker.

Supported procedural strip-curtain doors are regenerated from their editable settings. Recovery checks their point connections, rest lengths, fixed anchors and physical settings against the original; moving cloth starts from its rest pose. Unsupported custom cloth constraints stop conversion. Embedded occlusion volumes are retained in the recovered asset package.

Visibility builds share identical lighting lists when needed to stay within the stock game's signed index limit. Each region keeps the same lights in the same order, including after later builds and saves.

Recovery checks the PC engine's collision-bound index limit before navigation building and saving. Maps such as ClarD retry with a different brush order and fewer BSP splits to fit within the limit. If a map still exceeds it, conversion stops and keeps its T3D export.

The **File** menu provides two commands:

| Command | Use it to |
| --- | --- |
| **Recover Compiled Map...** | Convert a compiled `.sdc` into a uniquely named normal source map in `Packages/MapsEd` and its playable copy in `Packages/Maps`. |
| **Convert Legacy Recovered Map...** | Convert a map saved by the earlier cooked-layout recovery mode into the same normal source format. |

The converter reads the final BSP tree and reconstructs closed convex brush volumes representing its empty or solid space. It merges compatible adjacent volumes and groups decorative fragments by their original surface to reduce the number of brushes. Coplanar faces are partitioned to retain their materials and texture alignment. Very small texture divisions are simplified within the native BSP builder's 0.25-unit distance band; shared structural corners and collapsed bevels are reconciled at the editor's 0.002-unit point precision. Recovery copies the remaining actors, supported embedded assets, actor relationships, level settings, and GE ledges/pipes to the new map. The original construction-brush names, grouping, pivots and editing history cannot be recovered.

Recovery rebuilds geometry, BSP, lighting and paths, checks solid/empty space samples and actor/gameplay data, then saves and reopens the map. It stops with an explanation when the input has unsupported references, unbounded or invalid structural geometry, exceeds reconstruction limits, or fails verification. Check the result in the editor and in game. Reconstructed brushes can be more fragmented than the original source.

Save current work first: recovery replaces the active map and may take several minutes. Original map files are not overwritten. Recovery inputs and complete T3D exports are retained under `Packages/MapsEd/Recovery/<output name>` for diagnosis. When an embedded asset dependency is created under `Packages/StaticMeshes`, distribute that package alongside the playable map. Previously saved cooked-layout recovery files still require conversion; opening them with regular Open does not convert them automatically.

### BSP and Crash Diagnostics

Reloaded logs geometry, BSP, and lighting builds. It records each CSG brush, its source-polygon preflight results,
temporary BSP array sizes, and a fixed-size flight recorder of the most recent
polygon splits and vertex lookups. Recent events stay in memory to avoid writing to disk for every polygon operation.

If the legacy editor faults during a build, Reloaded captures the first-chance
exception before Unreal's guard history hides the original state. The
`System/Diagnostics` directory then contains:

- `BspBuildSession_*.log`: the map, build stages, brushes, and preflight warnings.
- `BspCrash_*.log`: the active brush/source polygon, BSP indices and array
  bounds, polygon vertices, registers, stack words, and recent flight-recorder
  events.
- `BspCrash_*.dmp`: a WinDbg/Visual Studio minidump for native debugging.
- `BspInvariant_*.log`: an early snapshot when Reloaded detects a bad BSP index
  or malformed transient polygon immediately before the stock code uses it.
- `EditorCrash_*.log` and `.dmp`: equivalent artifacts for unhandled faults
  outside a BSP build.

The diagnostics report suspicious geometry and stop on invalid BSP data. They do not modify brushes. Reloaded also
applies fingerprint-checked compatibility fixes to every verified
`FVert::pVertex` reader used by model rendering and the CSG filter, adjacency,
remap, optimization, and `FindNearestVertex` paths. The stock editor
sign-extends these 16-bit point indices and crashes or corrupts lookup tables
once a rebuild passes 32,767 BSP points. Reloaded treats the fields as unsigned,
retains the format's `0xFFFF` invalid-index sentinel, extends the usable range
to 65,535, and journals when either threshold is crossed.

### Property-Window Fixes

Use **View > Reset Property Window Positions** to bring misplaced property windows back to the current editor monitor while preserving their size and visibility.

### Play Level Compatibility

Play Level launches through `SCCT_Versus.exe`, which injects Reloaded. The editor supplies the raw game executable name before the map arguments because Reloaded's launcher forwards those arguments as the game's complete command line. Without that executable token, native startup discards the map URL. The map, game mode and selected team are preserved, and `HWND=0` gives the game its own window. `Reloaded.Core.dll` is required.

Reloaded uses its own renderer and the existing `SCCT_Versus.config` settings. Its `labs_borderless_fullscreen` option keeps presentation windowed without changing the desktop display mode.

The tested Reloaded v3.0a build needs guards for three recorded null dereferences: the scoreboard query, overlay callback and controller callback assume `SPlayerProfile` and its player owner exist. The optional [Play Level guard script](https://github.com/Lumbridge/SCCT_Versus_Reloaded_Editor/blob/main/tools/patch_reloaded_play_level.py), available in this repository, guards the query and defers the two callbacks until the level context, profile and owner are available. When they are available, the callbacks use their original implementations.

The same patch fixes three unsafe filename copies in Reloaded's normal map selector. Longer custom map names could overwrite buffers allocated for a previously selected short name and crash during selection or loading. The selector now uses the stock game's string assignment to resize each buffer and update its length, including when selecting recovered maps.

With the game closed, run `python tools/patch_reloaded_play_level.py "<System>/Reloaded.Core.dll" --check` to verify compatibility, then use `--apply` to install the fixes. The script accepts the supported original build or earlier guard revisions, verifies the entire original DLL hash, preserves an unmodified `.before-play-level-guard.bak` backup, and refuses unknown builds. It does not change `SCCT_Versus.config`. Restore the backup with the game closed to undo the fixes. This is a separate, manual step: building the editor does not patch Reloaded Core automatically. Reloaded Core must be installed separately.

See [tests/README.md](tests/README.md) for the startup-guard tests and debugger results.

Playtests render at the current display resolution by default. The editor generates `System/Reloaded_PlayLevel.ini` from the game's `Default.ini`, sets all three native viewport resolution pairs, and supplies it through the game's `INI=` argument. This changes the actual game resolution instead of stretching a 640×480 render; the original configuration is left intact. An explicit `INI=` argument is respected. To use a fixed playtest resolution, add `ResolutionX=1920` and `ResolutionY=1080` under `[PlayLevel]` in `System/Reloaded_Editor.ini`. Set both to `0` to follow the display again.

### Texture Browser Favorites

The Texture Browser adds a **Favorites** tab alongside Full, In Use, and Recent. Right-click a material and choose **Add to Favorites** or **Remove from Favorites** to save it in the thumbnail list.

Opening Favorites attempts to load missing favorite `.utx` packages from `Packages/Textures`. The list is saved in `System/Reloaded_Editor.ini` and persists between editor sessions. Unavailable materials remain in the saved list until their packages can be found.

## Additional Editing Tools

These tools are included in both Debug and Release builds.

### Static Mesh Browser Favorites

The Static Mesh Browser adds a **Favorites** filter beside the package selector. Right-click a mesh and choose **Add to Favorites** or **Remove from Favorites**. With the filter enabled, favorites from every package appear by full object path, with the existing mesh preview and properties available when selecting an entry.

Favorites attempts to load missing `.usx` packages from `Packages/StaticMeshes` and saves the list in `System/Reloaded_Editor.ini`. Normal package and group filtering returns when Favorites is turned off.

### BSP Texture Copy and Paste

Select exactly one BSP surface, open its context menu, and choose **Copy Texture**. Select the destination surfaces and choose **Paste Texture** to apply the copied material while retaining each surface's existing texture alignment.

Pasting uses the editor's native undo path and preserves the Texture Browser's current material selection. The copied material stays available until it is unloaded or the editor closes.

To find the brush behind a BSP face, select the face in a viewport, right-click it,
and choose **Select Brush**. Its source brush becomes the actor selection in all
viewports without moving the cameras. Multiple selected faces select each owning
brush once. The action is disabled if a selected face has no editable source brush.

### Quick Grid Size Adjustment

Hold **Ctrl** and scroll over a 2D or 3D level viewport to change the grid size. Scroll up selects the next larger grid preset; scroll down selects the next smaller one. Each wheel notch moves one preset, stopping at the smallest or largest size.

The grid selector updates automatically. This shortcut preserves the snapping settings and consumes the wheel input without zooming or moving the camera.

### Builder-Brush Repair

Use **Brush > Rebuild Builder Brush as Default Cube** to reset the builder-brush transform and reconstruct missing or damaged geometry as a solid **256-unit cube** through the editor's native brush importer.

## Original Reloaded Editor Features

This fork includes the original Reloaded Editor fixes.

### Fix Object Selection Delay

The Reloaded Editor removes the long delay that occurs when selecting objects in the editor viewports on modern hardware.
<div align="center">
  <table>
    <tr>
      <td width="50%"><img style="width:100%" src="https://github.com/user-attachments/assets/b7e62711-54ec-40a1-a4f4-4a2d7a29b4ff"></td>
      <td width="50%"><img style="width:100%" src="https://github.com/user-attachments/assets/e6678ff8-3d73-42ed-9104-5e05bcd055ef"></td>
    </tr>
    <tr>
      <td align="center">Stock</td>
      <td align="center">Reloaded</td>
    </tr>
  </table>
</div>

### Lighting Fixes

Echelon lights (those using `LightEffect=LT_ESpotShadow` in the editor) do not render on modern graphics cards by default. The Reloaded Editor restores their visibility.
<div align="center">
  <table>
    <tr>
      <td width="50%"><img style="width:100%" src="https://github.com/user-attachments/assets/4b2d0246-b865-4cf8-995c-d19209188e7b"></td>
      <td width="50%"><img style="width:100%" src="https://github.com/user-attachments/assets/112e32fc-0572-40c4-9761-4acbc71be45e"></td>
    </tr>
    <tr>
      <td align="center">Stock</td>
      <td align="center">Reloaded</td>
    </tr>
  </table>
</div>

### Increased Lightmap Resolution and Quality

The Reloaded Editor increases all selectable lightmap resolutions by 2x. The previous maximum of 256x256 has been raised to 512x512. Compression has also been disabled. In the stock editor, although the maximum resolution was 256x256, compression reduced the actual quality to something closer to 128x128.
<div align="center">
  <table>
    <tr>
      <td width="50%"><img style="width:100%" src="https://github.com/user-attachments/assets/eafc2ff9-dc05-479a-aa8e-07718d1ba12d"></td>
      <td width="50%"><img style="width:100%" src="https://github.com/user-attachments/assets/262e1139-fd49-4ef0-85ff-d69a91699352"></td>
    </tr>
    <tr>
      <td align="center">Stock</td>
      <td align="center">Reloaded</td>
    </tr>
  </table>
</div>

### Editing workflow tools

**View > SMagicEvent Workbench…** opens the event editor. You can also open an
existing event from the event picker, actor context menu, or Gameplay Connections
graph. Changes apply to the map immediately and support undo.

- Create an event, add groups, then create actors or search existing actors by
  name, Tag or class. Attach multiple triggers with **Attach as trigger**, and
  schedule targets with **Add action using actor**. The actor filter can show
  participants, current map selection, or individual actor categories.
- Drag actions to change their delay, or select an action and edit its precise
  settings in the inspector. Snap uses 0.1 seconds. Duplicate, remove and reorder
  groups/actions, zoom with Ctrl+wheel, scroll rows with the wheel, or Fit the
  timeline. Removing actions and detaching triggers keeps their actors.
- **Sequence** means one action per matching activation, not automatic playback
  of a sequence. **Repeat** is -1 for disabled, 0 for unlimited cycles, or a
  positive cycle limit. Delays are relative to activation. Type and ValidOn
  control Trigger/Untrigger behaviour; the status bar explains the selected
  setting. The preview shows timing only; use
  **Play Level** to check gameplay, effects and audio.
- The inspector shows the main settings for
  movers, triggers, sounds, emitters and volumes, with other editable fields
  under Advanced. Search includes those categories. Apply or Enter saves
  a field; enum choices apply immediately. Native undo groups each completed
  edit or timeline drag into one operation.
- Create damage volumes from the builder brush or a 256-unit box. Use transforms
  and scale to position/resize them. Movers use mesh assets; **Capture key from
  builder pose** records the builder's position/rotation into a selected
  KeyPos[1] or later key, relative to the mover's base pose.
- **Assets / actors / components…** searches loaded compatible references,
  uses a selected map actor/Tag, adds particle emitter components, or opens their
  properties. Load additional asset packages through the existing browsers.

Event data is saved in the map actors. Splitter, zoom and snap preferences are
stored separately in `System/ReloadedEditor/magic-workbench.json`. The workbench
rejects stale edits after external changes and map switches. Engine behaviour and
tests are described in [tests/SMagicEventSemantics.md](tests/SMagicEventSemantics.md).

Both Debug and Release builds include these panels:

- **Find Usages…** in the Texture and Static Mesh browser context menus lists direct map assignments and indirect shared-asset dependencies. Select a replacement in the same browser, exclude individual rows, then use **Replace Checked…**. Selection scope uses the actors selected when you click Refresh. Shared mesh/material contents are read-only. BSP replacements update their source polygons and use one native undo operation per batch.
- **View > Gameplay Connections…** lists incoming and outgoing actor references and Event/Tag relationships. Double-click a resolved relationship to select and focus its counterpart; use Follow Selection or Refresh to inspect another actor. Unmatched events are identified separately from optional null properties.
- **View > Working Views…** saves level viewport cameras, display modes, zoom, show flags and editor visibility. Restore a view, adjust the workspace and choose **Update Selected from Current** to overwrite that same entry. Use Save New to create another view, or Rename and Delete to manage existing views.
- **Save Selection as Assembly…**, also available from the actor context menu, saves selected actors and ordinary brushes for reuse. **View > Actor Assemblies…** places them at a supplied position and rotation, and lets you choose how to connect references to actors outside the assembly. Required asset packages remain external; rebuild BSP after inserting brushes.

To update an assembly, choose **Edit Contents…** and select an existing tracked instance or place an editable copy. Edit actors normally, use **Add Selected** / **Remove Selected** to adjust membership, and **Save Changes…** to review and update the same library entry. Remove Selected leaves the actor in the map. **Update from Selection…** is a shortcut for replacing the saved contents with your current selection. Updates preserve the entry's name, identity and pivot; they affect future placements only. Cancel leaves the saved definition unchanged, while scene edits use ordinary editor undo.

The library is saved atomically to `System/ReloadedEditor/library.json`; the map format is unchanged. Working views and assembly instances are stored by source-map path. Untitled-map views stay in memory until first save, and Save As copies them to the new map path. Deleted actors are skipped on restore. Keep the library when moving an editor installation. GE geometry and editor infrastructure are not assembly contents; legacy recovered BSP maps must first be converted to ordinary source maps.

In **Gameplay Connections…**, click **Graph…** for a connection diagram:

- **Selected Actor** shows incoming/outgoing neighbours; **Expand** adds another step, up to ten. Follow Selection tracks the actor selected in the level.
- **Whole Level** lays out separate connection islands around their most-connected actor. Related branches spread outward in rings, with spacing that grows to accommodate busy event groups without wrapping into extra columns. Unconnected actors are hidden until **Show unconnected** is enabled. The relationship and class filters control which actors appear in each island.
- Type icons identify lights, triggers, sound actors, movers and brushes, with a generic actor icon for other classes. Blue arrows indicate actor references, purple arrows indicate Event/Tag relationships, and orange warning nodes identify unresolved targets. Hover for actor paths or connection properties, including parallel links.
- Actor nodes include their **Tag**, with the full value in hover details and tag matching in **Find Next**. Both the list and graph include `SMagicEvent.Groups[i].EventGroup[j].Event` links to actors with matching Tags. Every matching actor is retained, `None` is skipped, and unmatched names appear as unresolved targets. These links are included in the Event/Tag filter and island grouping.
- Drag the canvas to pan, use the mouse wheel to zoom, and choose **Fit Graph** or click a node and **Fit Island**. Search the visible graph with **Find Next**. Double-click a node or use **Select and Focus** to navigate to its actor. Home fits the graph, arrow keys pan and Enter focuses the highlighted node.

Right-click an actor node and choose **Rename Tag…** to rename its Tag and dependent current-map `Event` and `SMagicEvent.Groups[].EventGroup[].Event` assignments in one undoable operation. If several actors share the old Tag, **all actors in that group are renamed together**, so events still reach every actor with that Tag. The scrollable preview lists every changed assignment and highlights shared groups; Cancel changes nothing. New Tags must be unused, with 1–63 letters/digits/underscores, starting with a letter or underscore. `None`, case-only renames and stale previews are rejected. Assigning a Tag to a previously untagged actor does not redirect `None` events. Direct actor references remain unchanged, and saved assembly definitions are independent.

You can still use the connection list. Graph positions are temporary; map fields change only when a Tag rename is confirmed. The graph supports up to 20,000 actors and 100,000 connections; large graphs may require zooming into an island for readable names.

Build and test instructions are in [tests/README.md](tests/README.md#editing-workflow-tools). Clear the `SCCT` environment variable before building when you do not want the existing post-build deployment steps to copy binaries into your installed game. Rebuild the solution when switching Debug/Release, because its native renderer library shares the output directory.

### Increase Audio File Size Limit

In the stock editor, importing WAV audio files larger than 200KB would immediately crash the editor. The Reloaded Editor removes that limitation and allows larger WAV files to be imported.
