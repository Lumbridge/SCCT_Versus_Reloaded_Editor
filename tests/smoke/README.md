# Editor smoke test

A quick check of the RE+ tools in a real editor, about 100 seconds against the
8 minutes of the native suite. `SmokeProbe.dll` is injected after
`Reloaded.Editor.dll` into a disposable copy of the game, opens maps the way a
user does (File > Open Recent), runs RE+ commands through the frame's
`WM_COMMAND`, answers the message boxes they show (and records their text),
writes `System\smoke_report.txt` and ends the editor. It never touches a working
install.

## Run it

From an x86 Native Tools Command Prompt for Visual Studio:

```bat
tests\smoke\build_probe.cmd
```

In PowerShell, prepare a disposable install once, copy the maps to test into its
`Packages\MapsEd`, and run:

```powershell
./tools/run_native_map_recovery_test.ps1 -EditorDll bin\Reloaded.Editor.dll -GameSystem "<game>\System" -GenerateFixture -PrepareOnly
./tests/smoke/run_smoke.ps1 -TestRoot <TestRoot printed above> -Isolate -Map AeroD.sdc -EditorDll bin\Reloaded.Editor.dll -Extra @{ map2 = "<TestRoot>\Packages\MapsEd\ShipD.sdc" }
```

`-PrepareOnly` links `Packages\Textures`, `Sounds`, `Animations` and `Menus` to
the real install. Saving a map also writes its `<Map>-i.utc` into
`Packages\Textures`, so `-Isolate` replaces those links with copies (removing
only the links), and the script refuses to run while any link is left.

## What it checks

`SmokeSteps.inl` lists the steps. On a big map (AeroD, about 4,800 actors) and a
second one (ShipD):

- Isolate selected with nothing selected refuses; Invert, Isolate and Unhide all
  finish in well under three seconds; a second Isolate adds no Undo step.
- Snap to Grid (pivots), again with nothing to do, Undo; Snap Yaw to 90 and Undo.
- Copy Properties and Paste Properties to two meshes in one Undo step, and Undo.
- Map Check lists static meshes lifted 20 units as floating.
- Set Bookmark 1 is kept in the library; Go to an empty slot explains; Go to 1.
- File > Save twice keeps two backups; Open Map Backup can be cancelled.
- The Static Mesh Browser search box filters the list.
- Map JSON export, then an import whose preview row was clicked still applies.
- With `conveyor = '1'` in `-Extra`, Add Conveyor Belt (on a build that has it).
