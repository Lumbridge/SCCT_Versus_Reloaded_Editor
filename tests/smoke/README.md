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
- `palette`: Help > Command Palette lists the menu bar's commands (stock, RE+ and
  submenus, with their shortcuts, but not itself); typing finds Render Budget from
  scattered letters and Map Check from its words; Enter and a click each run the
  first row; the command run last is listed first; the arrows move and Esc
  closes; Ctrl+Shift+P opens and closes it (when the editor is in front).
  `shot=<file.bmp>` (and `shotquery=<text>`) saves a picture of it over the editor.
  Any map will do (ShipD was used).
- `prefabs`: two meshes (one Tagged, the other's Event on that Tag) saved as an
  assembly and placed linked twice, one turned: one Undo step each, fresh Tags
  per copy, 2 linked instances in the Actor Assemblies list; Select Instance
  and Select All Instances; the second copy moved, a mesh of the first lifted
  and Update Prefab from This Instance rebuilds the second where it stands in
  one Undo step, which Undo takes back; both copies are still linked after
  File > Save and Open Recent; Break Link drops one, and Update then refuses.
- `lightshadow`: the Light and Shadow Map turned on has patches, some on static
  meshes; after an in-game light and then a static mesh near it are moved, the
  next sample keeps the other lights (the log line says how many); switching
  static mesh floors off and on in its settings window changes the summary and
  is saved to `[LightShadowMap]`; `System\lightshadow-1.bmp` and `-2.bmp` are
  pictures of the editor and the settings window (the hidden editor is shown,
  without taking the focus, while they are taken). Any map with lights works.
