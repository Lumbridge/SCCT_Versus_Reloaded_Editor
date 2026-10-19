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
./tests/smoke/run_smoke.ps1 -TestRoot <TestRoot printed above> -Isolate -Map <BigMap>.sdc -EditorDll bin\Reloaded.Editor.dll -Extra @{ map2 = "<TestRoot>\Packages\MapsEd\ShipD.sdc" }
```

`-PrepareOnly` links `Packages\Textures`, `Sounds`, `Animations` and `Menus` to
the real install. Saving a map also writes its `<Map>-i.utc` into
`Packages\Textures`, so `-Isolate` replaces those links with copies (removing
only the links), and the script refuses to run while any link is left.

## What it checks

`SmokeSteps.inl` lists the steps. On a big map (a few thousand actors) and a
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

## Suites

`suite = '<name>[,<name>...]'` in `-Extra` runs the named suites from
`SmokeSuites.inl` instead of the steps above. They need a small map with more
than one storey and a second map; copies of BankD and BankP were used:

```powershell
./tests/smoke/run_smoke.ps1 -TestRoot <TestRoot> -Isolate -Map BankD.sdc -EditorDll bin\Reloaded.Editor.dll -Extra @{ suite = 'unsaved'; map2 = "<TestRoot>\Packages\MapsEd\BankP.sdc" }
```

The suites save the map, so give each run fresh copies of the maps.

- `storeys`: a floor stepped down in the Storeys palette hides the others;
  File > Save, the stock autosave's own command and Save As keep the floor
  showing, and none of the saved files comes back with anything hidden (Open
  Recent, `MAP LOAD`).
- `unsaved`: Open Recent warns about unsaved changes after an edit and keeps
  warning after No; still after the stock autosave, a Save As that was
  cancelled and a File > Save of a read-only map ("try again?" answered No).
  It stops warning after the stock FileSave called directly (the route Ctrl+L
  takes), File > Save and a Save As under a new name. A map opened with the
  stock File > Open (its "Save changes?" answered No) is clean, and an edit made
  straight after one still warns once the ten-second check has seen the map.
- `placement`: Map Check, Lighting Budget, Render Budget, Undo History,
  Storeys, Emitter Library, SMagicEvent Workbench, SCamNetwork Manager,
  Gameplay Connections and its graph, Working Views, Actor Assemblies, Find
  Usages (from the Static Mesh Browser's command, for a mesh in the map), Find
  Usages in All Maps and Light and Shadow Map are each moved, resized, closed
  and opened again at the same place and size; Map Check comes back maximised;
  a `[WindowPlacement]` entry off every monitor or that cannot be read is
  ignored.
- `meshcost`: Render Budget's lower list switches to Meshes by cost; the rows
  are heaviest first and add up to the Whole map row's mesh triangles; a click
  selects every copy of that mesh and a double-click frames them; a zone row
  narrows the list to that zone's meshes and the Whole map row widens it again;
  the list switches back to the hotspots.
- `objimport`: Import OBJ, on a copy of ShipD. It writes a small OBJ (a crate, a lid and a sign) with
  an MTL, a 100 x 60 BMP and a 64 x 64 TGA into `System\objimport test`; the preview counts 26 triangles and
  3 hulls of 36 triangles and plans both images; the import with hulls makes the mesh, the editor keeps 36
  hull triangles, its bounds are the file's (the hulls land on the mesh), the slots take the imported
  textures and the default texture, and the placed actor stands at the click point in one Undo step; the
  same name again is refused; per-polygon collision has no hull and no collision has neither; a wall of
  12000 triangles is split into three parts placed together in one Undo step, and is refused unsplit;
  File > Import OBJ Mesh... opens the window, which shows the summary and imports; the Static Mesh
  Browser's File menu has Import OBJ... and opens it too. Pictures of the window are left in `System`.
