# SCCT Versus Reloaded Editor

An unofficial editor patch for Splinter Cell: Chaos Theory Versus, compatible with the stock game and [Enhanced SCCT Versus](https://github.com/Joshhhuaaa/EnhancedSCCTVersus). Based on [AllyPal's Reloaded Editor](https://github.com/AllyPal/SCCT_Versus_Reloaded_Editor).

## Added in this fork

| Feature | What it does |
| --- | --- |
| **Compiled-map recovery** | Turns compiled maps into editable brushes, actors, and assets. |
| **SMagicEvent Workbench** | Edit event groups, triggers, actions, and timing in one panel. |
| **Gameplay Connections** | View actor links in a list or graph, jump to connected actors, and rename Tags with their linked events. |
| **Working Views** | Save and restore viewport cameras, display settings, and visibility. |
| **Actor Assemblies** | Save groups of actors and brushes, edit their contents, and reuse them across maps. |
| **Find Usages** | Find where textures and meshes are used, then replace selected assignments. |
| **Texture and mesh favorites** | Keep frequently used materials and meshes together across packages. |
| **BSP texture copy/paste** | Copy a surface's material while keeping the destination's texture alignment. |
| **Select Brush** | Select the source brush directly from a BSP surface. |
| **Quick grid adjustment** | Change grid size with **Ctrl + mouse wheel** over a viewport. |
| **Play Level** | Launch playtests through Reloaded at the current display resolution. |
| **Property-window reset** | Bring off-screen property windows back onto the editor monitor. |
| **Builder-brush repair** | Restore a missing or damaged builder brush as a default cube. |
| **BSP fixes and crash diagnostics** | Support larger BSP point counts and record build failures and crashes. |

Also includes AllyPal's original fixes: faster selection, restored Echelon lighting, improved lightmaps, and larger WAV imports.

## Install

Download the ZIP from [Releases](https://github.com/Lumbridge/SCCT_Versus_Reloaded_Editor/releases) and extract it into the same folder as `SCCT_Versus.exe`. Run `Reloaded_Editor.exe`.

Recovered maps, including Clarity Soft, are available in [SCCT-Maps](https://github.com/Lumbridge/SCCT-Maps).

<details>
<summary>Map recovery instructions</summary>


Save your work, then choose **File > Recover Compiled Map...**. Recovery can take several minutes and replaces the active map without overwriting the original files.

The editable copy goes in `Packages/MapsEd`, the playable copy in `Packages/Maps`, and any extracted mesh assets in `Packages/StaticMeshes`. Keep those assets with the map when sharing it. Reports and T3D exports are saved in `Packages/MapsEd/Recovery`.

Recovery is experimental. Original brush history cannot be restored, reconstructed geometry may be fragmented, and later lighting rebuilds can change the appearance. Check the result in the editor and in game. Use **File > Convert Legacy Recovered Map...** for files made by the older recovery mode.

</details>

<details>
<summary>Play Level setup</summary>


Play Level requires a separately installed Reloaded Core. For the supported Reloaded v3.0a build, close the game and run the repository's guard script:

```powershell
python tools/patch_reloaded_play_level.py "<System>/Reloaded.Core.dll" --check
python tools/patch_reloaded_play_level.py "<System>/Reloaded.Core.dll" --apply
```

The script checks compatibility and backs up the original DLL. Playtests use the current display resolution by default.

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

The three-file release ZIP is written to `bin/Reloaded_Editor.zip`. See [tests/README.md](tests/README.md) for test instructions.

</details>
