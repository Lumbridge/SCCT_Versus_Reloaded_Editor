# SCCT Versus Reloaded Editor

SCCT Versus Reloaded Editor is an unofficial patch for the Unreal Level Editor used by Splinter Cell: Chaos Theory's Versus mode. It is compatible with the stock game as well as [Enhanced SCCT Versus](https://github.com/Joshhhuaaa/EnhancedSCCTVersus).

This is [Lumbridge's fork](https://github.com/Lumbridge/SCCT_Versus_Reloaded_Editor) of [AllyPal's Reloaded Editor](https://github.com/AllyPal/SCCT_Versus_Reloaded_Editor), adding compiled-map recovery, build diagnostics, and tools to speed up everyday map editing.

## What's New in This Fork

| Addition | What it helps you do | Source status |
| --- | --- | --- |
| [Compiled-map and editable BSP recovery](#compiled-map-recovery-experimental) | Open cooked maps, recover actors and asset references, and reconstruct editable brushes. | Published on `main` |
| [BSP and crash diagnostics](#bsp-and-crash-diagnostics) | Investigate failed builds and work with BSP point indices beyond the stock signed 16-bit limit. | Published on `main` |
| [Property-window and Play Level fixes](#property-window-and-play-level-fixes) | Bring misplaced property windows back into view and improve Enhanced SCCT playtesting compatibility. | Published on `main` |
| [Texture Browser Favorites](#texture-browser-favorites) | Keep frequently used materials together in a persistent thumbnail tab. | Local development |
| [Static Mesh Browser Favorites](#static-mesh-browser-favorites) | Browse saved meshes across packages without repeatedly switching package filters. | Local development |
| [BSP texture copy and paste](#bsp-texture-copy-and-paste) | Reuse a surface's material on other surfaces while retaining their alignment. | Local development |
| [Quick grid size adjustment](#quick-grid-size-adjustment) | Change the grid preset with **Ctrl + mouse wheel** directly over a level viewport. | Local development |
| [Builder-brush repair](#builder-brush-repair) | Rebuild a missing or damaged builder brush as a default cube. | Local development |

**Availability:** Features marked **Local development** are implemented in the development checkout but their code has not yet been published to this fork. They are documented here as a preview of the next additions. Published source status does not imply availability in a prebuilt release.

## Install

- Copy the built patch files, or extract a release archive, into the game's `System` directory, where `ChaosTheory_Editor.exe` is located.
- Run `Reloaded_Editor.exe` and check that the title bar displays **Reloaded Chaos Theory Editor** to confirm the patch is active.

## Published Fork Features

### Compiled-Map Recovery (Experimental)

The **File** menu now provides four recovery commands:

| Command | Use it to |
| --- | --- |
| **Recover Compiled Map... (Experimental)** | Copy a compiled `.sdc` map into `Packages/MapsEd` under a unique name and open the copy with support for its compiled data layout. |
| **Open Recovered Map... (Experimental)** | Reopen an existing recovered `.sdc` in place without making another copy. |
| **Export Recovered BSP as Brushes... (Experimental)** | Export the active recovered map's final BSP geometry to `.t3d`, with an option to create a normal editable map. |
| **Recover Compiled Map as Editable... (Experimental)** | Combine recovery, actor snapshots, and BSP reconstruction into one workflow that creates a normal editable source map. |

Runtime actors, asset references, materials, and final geometry are recovered where possible. Brush reconstruction retains surface materials and UV axes where available, creating separate 1-unit-thick additive brushes from the cooked BSP polygons.

Save current work before starting recovery, because it replaces the open map. **Save** and **Save As** preserve the compiled layout while working on a recovered map; reopen those files with **Open Recovered Map**, rather than the normal Open command. After creating an editable source map, use **Save As** with a new map name before rebuilding.

Recovery is an approximation: compiled maps no longer contain the original construction-brush grouping, additive/subtractive history, names, pivots, or every editor-only object. The exporter skips malformed or degenerate BSP nodes. If you import the exported brush fragment manually, use `MAP IMPORTADD` in a new normal source map; the standard **File > Import** command expects a complete map export.

### BSP and Crash Diagnostics

Reloaded keeps a lightweight journal while geometry, BSP, and lighting builds
are running. It records each CSG brush, its source-polygon preflight results,
temporary BSP array sizes, and a fixed-size flight recorder of the most recent
polygon splits and vertex lookups. Normal hot-path events remain in memory, so
the diagnostics do not turn every polygon operation into disk I/O.

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

The diagnostics report suspicious geometry but do not silently modify brushes
or continue past invalid BSP data, avoiding damaged map output. Reloaded also
applies fingerprint-checked compatibility fixes to every verified
`FVert::pVertex` reader used by model rendering and the CSG filter, adjacency,
remap, optimization, and `FindNearestVertex` paths. The stock editor
sign-extends these 16-bit point indices and crashes or corrupts lookup tables
once a rebuild passes 32,767 BSP points. Reloaded treats the fields as unsigned,
retains the format's `0xFFFF` invalid-index sentinel, extends the usable range
to 65,535, and journals when either threshold is crossed.

### Property-Window and Play Level Fixes

Use **View > Reset Property Window Positions** to bring misplaced property windows back to the current editor monitor while preserving their size and visibility.

Play Level compatibility fixes address Enhanced SCCT's launcher handling of the editor playtest command and the Direct3D 9 wrapper's use of the game window for presentation.

## Latest Editing Tools (Local Development)

These additions are implemented locally and await publication of their source code.

### Texture Browser Favorites

The Texture Browser adds a **Favorites** tab alongside Full, In Use, and Recent. Right-click a material and choose **Add to Favorites** or **Remove from Favorites** to build a reusable collection displayed in the native thumbnail browser.

Opening Favorites attempts to load missing favorite `.utx` packages from `Packages/Textures`. The list is saved in `System/Reloaded_Editor.ini` and persists between editor sessions. Unavailable materials remain in the saved list until their packages can be found.

### Static Mesh Browser Favorites

The Static Mesh Browser adds a **Favorites** filter beside the package selector. Right-click a mesh and choose **Add to Favorites** or **Remove from Favorites**. With the filter enabled, favorites from every package appear by full object path, with the existing mesh preview and properties available when selecting an entry.

Favorites attempts to load missing `.usx` packages from `Packages/StaticMeshes` and saves the list in `System/Reloaded_Editor.ini`. Normal package and group filtering returns when Favorites is turned off.

### BSP Texture Copy and Paste

Select exactly one BSP surface, open its context menu, and choose **Copy Texture**. Select the destination surfaces and choose **Paste Texture** to apply the copied material while retaining each surface's existing texture alignment.

Pasting uses the editor's native undo path and preserves the Texture Browser's current material selection. The copied material is available within the current editor session while that material remains loaded.

### Quick Grid Size Adjustment

Hold **Ctrl** and scroll over a 2D or 3D level viewport to change the grid size. Scroll up selects the next larger grid preset; scroll down selects the next smaller one. Each wheel notch moves one preset, stopping at the smallest or largest size.

The grid selector updates automatically. This shortcut preserves the snapping settings and consumes the wheel input without zooming or moving the camera.

### Builder-Brush Repair

Use **Brush > Rebuild Builder Brush as Default Cube** to reset the builder-brush transform and reconstruct missing or damaged geometry as a solid **256-unit cube** through the editor's native brush importer.

## Original Reloaded Editor Features

The fork also retains the original Reloaded Editor improvements below.

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

### Increase Audio File Size Limit

In the stock editor, importing WAV audio files larger than 200KB would immediately crash the editor. The Reloaded Editor removes that limitation and allows larger WAV files to be imported.
