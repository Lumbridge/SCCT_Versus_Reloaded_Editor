# SCCT Versus Reloaded Editor

An unofficial editor patch for Splinter Cell: Chaos Theory Versus, compatible with the stock game and [Enhanced SCCT Versus](https://github.com/Joshhhuaaa/EnhancedSCCTVersus). Based on [AllyPal's Reloaded Editor](https://github.com/AllyPal/SCCT_Versus_Reloaded_Editor).

## Install

Download the ZIP from [Releases](https://github.com/Lumbridge/SCCT_Versus_Reloaded_Editor/releases) and extract it into the same folder as `SCCT_Versus.exe`. Run `Reloaded_Editor.exe`.

Recovered maps, including Clarity Soft, are available in [SCCT-Maps](https://github.com/Lumbridge/SCCT-Maps).

## Features

- **Map recovery:** convert compiled maps into editable brushes, actors, and assets.
- **Editing tools:** texture and mesh favorites, BSP texture copy/paste, **Select Brush**, and **Ctrl + mouse wheel** grid adjustment.
- **Workflow panels:** SMagicEvent Workbench, Gameplay Connections, saved Working Views, reusable Actor Assemblies, and Find Usages with replacement tools.
- **Repairs and diagnostics:** reset misplaced property windows, repair the builder brush, and capture build/crash logs in `System/Diagnostics`.
- **Original Reloaded fixes:** faster object selection, restored Echelon lighting, uncompressed lightmaps up to 512×512, and support for WAV imports over 200 KB.

## Recover a map

Save your work, then choose **File > Recover Compiled Map...**. Recovery can take several minutes and replaces the active map without overwriting the original files.

The editable copy goes in `Packages/MapsEd`, the playable copy in `Packages/Maps`, and any extracted mesh assets in `Packages/StaticMeshes`. Keep those assets with the map when sharing it. Reports and T3D exports are saved in `Packages/MapsEd/Recovery`.

Recovery is experimental. Original brush history cannot be restored, reconstructed geometry may be fragmented, and later lighting rebuilds can change the appearance. Check the result in the editor and in game. Use **File > Convert Legacy Recovered Map...** for files made by the older recovery mode.

## Play Level

Play Level requires a separately installed Reloaded Core. For the supported Reloaded v3.0a build, close the game and run the repository's guard script:

```powershell
python tools/patch_reloaded_play_level.py "<System>/Reloaded.Core.dll" --check
python tools/patch_reloaded_play_level.py "<System>/Reloaded.Core.dll" --apply
```

The script checks compatibility and backs up the original DLL. Playtests use the current display resolution by default.

## Build

Use a Visual Studio C++ developer PowerShell with v143 and the manifest's vcpkg dependencies installed:

```powershell
$env:SCCT = $null # Skip deployment into an installed game.
msbuild SCCT_Versus_Reloaded_Editor.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x86 /p:PlatformToolset=v143
if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
./tools/package_release.ps1
```

The three-file release ZIP is written to `bin/Reloaded_Editor.zip`. See [tests/README.md](tests/README.md) for test instructions.
