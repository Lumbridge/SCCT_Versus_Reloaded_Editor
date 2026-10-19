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
- `appearance`: pictures of the whole editor for comparing the RE+ Options
  Appearance settings: with `shot = '<file>.png'` in `-Extra`, the frame with the
  Static Mesh Browser and an Actor Properties window (`<file>.png`), RE+ Options
  open (`-options`), a message box (`-box`), the File menu open (`-menu`), and
  Map Check, Render Budget, Emitter Library and Gameplay Connections
  (`-tools`). Each window is captured on its own and drawn where it sits, so
  nothing else on the desktop is in them. Set `[Appearance] Theme=Classic`,
  `ModernLight` or `ModernDark` in the test install's `Reloaded_Editor.ini`
  first.
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
- `objexport` (ShipD): File > Export holds the stock Export... and Map to OBJ...; an export of the whole map writes an OBJ whose every face corner points at a vertex, texture coordinate and normal it wrote, an MTL that defines every material used and names only PNGs that exist; Selection only with three static mesh actors selected writes just those three objects; a scale that is no number is refused in the status line; the folder is offered again after the window is closed. With `shot = <file.bmp>` it saves the editor with the window over it (PrintWindow).
- `objimport`: Import OBJ, on a copy of ShipD. It writes a small OBJ (a crate, a lid and a sign) with
  an MTL, a 100 x 60 BMP and a 64 x 64 TGA into `System\objimport test`; the preview counts 26 triangles and
  3 hulls of 36 triangles and plans both images; the import with hulls makes the mesh, the editor keeps 36
  hull triangles, its bounds are the file's (the hulls land on the mesh), the slots take the imported
  textures and the default texture, and the placed actor stands at the click point in one Undo step; the
  same name again is refused; per-polygon collision has no hull and no collision has neither; a wall of
  12000 triangles is split into three parts placed together in one Undo step, and is refused unsplit;
  File > Import OBJ Mesh... opens the window, which shows the summary and imports; the Static Mesh
  Browser's File menu has Import OBJ... and opens it too. Pictures of the window are left in `System`.
- `reach`: Map Check opened on BankD says reachability is being worked out
  and refreshes with its rows when the run ends (both teams reach cells);
  Build > Reachability Map is checked and unchecked, and on again shows the
  same result at once. With `map2` (ShipD): turning it on starts a run there,
  turning it off cancels it (Map Check says so), on again works it out, and
  Map Design's Tools > Show reachability map shows it in its status line.

`shot=<folder>` saves pictures (BMP) of the windows a suite captures, and
`hold=<seconds>` gives the editor its own message loop back for that long at
the end (so the viewports repaint) before the frame and the open tool windows
are saved as `hold-*.bmp` and the editor ends. A frame that is still busy
starting up when the probe asks to run is asked again.
- `outliner`: the Map Outliner lists every actor but the viewport cameras in
  a virtual list; grouped by class the headers' counts add up and a header
  folds and opens; a search keeps rows with every word; clicking rows selects
  those actors and the list follows a selection made in the editor; a
  double-click frames; Hide and Show are one Undo step each and a hidden row is
  never selected; Delete asks (No keeps them), deletes two actors in one Undo
  step that Edit > Undo brings back, and refuses the level info; reading the
  map again takes milliseconds; the window reopens where it was left. With
  `shot = '1'` it stays open for 15 seconds at the end, for a picture.
- `scatter`: Scatter Props takes a small mesh from the map as the Static Mesh
  Browser's current one (once only), Re-roll changes the seed, and props are
  placed in a circle round a player start (also aligned to the slope) and in
  the builder brush's footprint (the default cube moved over the start): each
  time inside the area, in the Scatter group, not hidden, selected, as one
  Undo step that Undo removes, Redo brings back and Undo removes again. A count
  over 500 is refused; the window comes back where it was closed.
  `hold = '<ms>'` also points the viewport cameras at the busiest player start
  and leaves the editor running that long after the report with the preview
  up, then places the props, for a picture.
- `compare`: File > Save keeps a backup; a mesh is moved, another deleted and a light's
  brightness changed. File > Compare with Backup lists exactly those three, with the backup's
  value beside the map's; the loaded copy is unloaded and its file removed. Ticking the deleted
  mesh and Bring Back Selected adds it again in one Undo step (and Undo takes it away). After an
  emitter is deleted and a mesh duplicated, Tick All brings everything back (the emitter with
  its particle components, the copy deleted) and a second comparison finds nothing. With
  `shot = '<file>.bmp'` it saves a picture of the editor with the comparison over it (ShipD).
- `collision`: Map Check's Collision rows match the counts the probe reads from
  the map's static meshes (over 5512 collision triangles, a full per-polygon
  or hull BSP, per-polygon collision at 15000); a click on the first row
  selects every actor using its mesh and only those, a double-click frames
  them. Run it on a map with heavy meshes (AeroD, MoonD). `sweep = '<folder>'`
  then opens every other map there in turn (`sweepfrom = '<name>'` resumes).
- `mover` (on ShipD, or any map with a two-key mover): the actor menu offers Mover
  Workbench for a selected mover; the window lists its keys; Add Key Here after the mover
  was moved off its key and after a move at key 0 that took its base along (the path stays,
  the new key is the move), Go to Key, Update Key, Move Up and Delete Key, each one Undo step,
  and six Undos give the mover back; MoveTime and InitialState (picked by its plain name)
  applied in one step and a bad time refused; Wire to This Mover points a trigger's Event at
  the mover's Tag in one step; Preview moves it and puts it back with no Undo step. With
  `picture = '1'` it also leaves a three-key path with a turn and the window open, and saves
  `mover_editor.bmp` and `mover_window.bmp` once the editor has repainted its viewports
  (PrintWindow shows stale viewports while a suite holds the UI thread).
