# SCCT Versus Reloaded Editor

An unofficial editor patch for Splinter Cell: Chaos Theory Versus, compatible with the stock game and [Enhanced SCCT Versus](https://github.com/Joshhhuaaa/EnhancedSCCTVersus). Based on [AllyPal's Reloaded Editor](https://github.com/AllyPal/SCCT_Versus_Reloaded_Editor).

## Added in this fork

| Feature | What it does |
| --- | --- |
| **Compiled-map recovery** | Turns compiled maps into editable brushes, actors, and assets. |
| **SMagicEvent Workbench** | Edit event groups, triggers, actions, and timing in one panel. |
| **Gameplay Connections** | View actor links in a list or graph, jump to connected actors, and rename Tags with their linked events. |
| **Selective Tag renaming** | Uncheck individual actor Tags or linked Event assignments before renaming a shared group. |
| **Map packaging** | Create a playable-map ZIP with dependencies, map-selection images, and an installation report. |
| **Working Views** | Save and restore viewport cameras, display settings, and visibility. |
| **Actor Assemblies** | Save groups of actors and brushes, edit their contents, and reuse them across maps. |
| **Find Usages** | Find where textures and meshes are used, then replace selected assignments. |
| **Texture and mesh favorites** | Keep frequently used materials and meshes together across packages. |
| **BSP texture copy/paste** | Copy a surface's material while keeping the destination's texture alignment. |
| **Select Brush** | Select the source brush directly from a BSP surface. |
| **Quick grid adjustment** | Change grid size with **Ctrl + mouse wheel** over a viewport. |
| **Property-window reset** | Bring off-screen property windows back onto the editor monitor. |
| **Builder-brush repair** | Restore a missing or damaged builder brush as a default cube. |
| **BSP fixes and crash diagnostics** | Support larger BSP point counts and record build failures and crashes. |

Also includes AllyPal's original fixes: faster selection, restored Echelon lighting, improved lightmaps, and larger WAV imports.

## Install

Download the ZIP from [Releases](https://github.com/Lumbridge/SCCT_Versus_Reloaded_Editor/releases) and extract it into the same folder as `SCCT_Versus.exe`. Run `Reloaded_Editor.exe`.

Recovered maps, including Clarity Soft, are available in [SCCT-Maps](https://github.com/Lumbridge/SCCT-Maps).

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
