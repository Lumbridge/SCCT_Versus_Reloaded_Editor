# Built-in Emitter Library

`Reloaded.Editor.dll` ships with the built-in Emitter Library entries compiled in.
- `Reloaded.Editor/EmitterLibraryDefaults.gen.h` holds them as JSON in raw string literals.
- `Workflow::EmitterLibrary::Builtins()` in `EmitterLibraryModel.h` parses and validates them when the library first loads.
- Users can hide a built-in (Delete) and bring it back (Restore). The entries are never written to disk.

Every entry is a stock emitter captured by the editor itself (the `emitterlib.capture` workflow op). Hand-written T3D is not safe here.
- SCCT's T3D import rejects numeric enum values (`UByteProperty::ImportText`, 0x10fe6700), so text written by hand would silently lose `DrawStyle` and similar settings.
- Native export writes the names (`DrawStyle=PTDS_AlphaBlend`), and a capture keeps them.

## Files

| File | Purpose |
|---|---|
| `defaults_manifest.json` | One record per entry, in library order: id, name, category, mode (`continuous`, `periodic` or `triggered`), description, source map and actors, tweaks (each with its reason), and an optional preview camera override. It also says which `Packages` folder each source map is in. |
| `captures/<slug>.json` | The raw `emitterlib.capture` result for each entry (about 100 KB in all), so `build` runs without the editor. |
| `sources/PyroD.t3d` | The PyroD torch, decoded from the compiled package (see below). |
| `build_defaults.py` | Every step below. |
| `../../Reloaded.Editor/EmitterLibraryDefaults.gen.h` | Generated. Do not edit it by hand. |

## Tweaks

| Op | What it does |
|---|---|
| `texture` | Swaps in a pixel-identical texture, or a look-alike, from a smaller shared package. |
| `untag` | Drops a Tag that only grouped actors in the source map. |
| `tag` | Renames a trigger Tag. |
| `offset` | Moves the actors so the effect sits on the pivot. |
| `set` / `remove` | Edit one line of a sub-emitter, by its index in `Emitters`. |
| `actorset` / `actorremove` | Edit one actor line. |

## Checks in `build`

`build` refuses an entry if any of these is true:
- It holds anything other than `Emitter` actors.
- It still sets `Platform`, `Group`, `AttachTag` or `Base`.
- Its mode disagrees with its text. A triggered entry, and only a triggered entry, has `Disabled=True` sub-emitters and a Tag.
- A dependency is not in a stock shared package of the game copy: a `.u` file in `System\_PC_`, or a `.utx`, `.usx`, `.uax`, `.uas` or `.ukx` file under `Packages\`. A map never counts.
- The `SCCT_Versus_UE5_Bridge` checkout (its `scctpkg` reader) is present, and an object the entry uses is not in its package. Without that checkout, `build` skips this check.

## Where the sources come from

- **Maps in `Packages\MapsEd`** load in the editor and are captured directly.
- **Maps only in `Packages\Maps`** are compiled (OffsD, ShadD, PrisD, ClarD). Loading one in the editor asserts. For these:
  1. `Reloaded.Editor`'s MapRecovery (`ReloadedRecoverMapToSource`) loads the compiled level and exports its actors natively as `Recovery\<map>\Actors.t3d`.
  2. It is stopped right after that export: a directory where it would write `Geometry.t3d` makes that write fail, the same way the native harness's `-ExpectRecoveryFailure` does. So no package is ever saved.
  3. The needed actor blocks are imported into a new map (`MAP IMPORT`) and captured.
- **PyroD** stops inside MapRecovery ("A retained portal outline has invalid vertices") before the actor export. `decode` reads the torch straight from the package with `scctpkg` into `sources/PyroD.t3d`:
  - Enum names are used only where a native export of the stock maps confirms them.
  - The file goes through the same import and capture as the other compiled maps.
  - `build` then checks the native capture against it, line by line. A value the importer rejected would be missing or different.

## Regenerating

All commands run from the repository root. `--game` defaults to the UE Upgrade test copy. The script refuses the live installation.

The jobs run in the headless editor harness in `out\tools`, which is not in the repository:
- `Run-EmitterLibHeadless.ps1` makes a disposable editor install under `%TEMP%` and never writes to the game copy.
  - `-SourceMap` takes several maps, separated by `;`.
  - `-LinkStaticMeshes` gives the install its own hard-linked `StaticMeshes` folder, so MapRecovery could never add a package to the game copy.
- `probe_emitterlib.cpp` needs the `recoveractors` and `copyfile` verbs, beside `load`, `import`, `workflow`, `expect`, `dump`, `reject`, `export` and `save`.

In each job's `report.txt` there must be no `STEPFAIL` or `workflow_error` lines. The last line, `RESULT PASS`, alone is not enough.

1. **Edit `defaults_manifest.json`.** A new entry needs a new `builtin.<slug>` id and its source map and actors. List the map under `maps`, and put a MapsEd map in one of the `jobs`.
2. **Capture editor maps, and export compiled maps' actors.**
   ```
   python tools/emitter_library/build_defaults.py jobs
   ```
   This prints one run command per job. Run them; they can run at the same time:
   - `capture_<n>.txt` loads the MapsEd maps and captures their entries.
   - `recover_<map>.txt` exports a compiled map's actors to `Actors_<map>.t3d`.
3. **Import and capture the compiled maps.**
   ```
   python tools/emitter_library/build_defaults.py imports out\tools\run_recover_ClarD out\tools\run_recover_OffsD out\tools\run_recover_PrisD out\tools\run_recover_ShadD
   ```
   Run the printed `capture_compiled.txt` command.
   - To refresh the PyroD torch first, run `python tools/emitter_library/build_defaults.py decode PyroD Emitter23529 --out tools/emitter_library/sources/PyroD.t3d`.
4. **Collect** the captures into `captures\`:
   ```
   python tools/emitter_library/build_defaults.py collect out\tools\run_capture_1 out\tools\run_capture_2 out\tools\run_capture_3 out\tools\run_capture_compiled
   ```
5. **Build** the header:
   ```
   python tools/emitter_library/build_defaults.py build
   ```
   It prints each entry's packages and the header size.
6. **Test.** Run `tools\test_workflow_tools.cmd`: `EmitterLibraryModelTests` parses and validates every embedded entry. Then rebuild the DLL.
7. **Verify natively.**
   ```
   python tools/emitter_library/build_defaults.py verifyjob
   ```
   Run the printed command against the new DLL. The job first places every built-in alone in a new map and checks it:
   - A package the entry is first to use is not loaded before placement and is loaded after.
   - Each `Emitters(i)` resolves to a live sub-emitter in the map package.
   - A triggered entry keeps its prefixed Tag.

   It then places all of them in one map, saves it, loads it again, and checks that every placed emitter is still there.

   Then run:
   ```
   python tools/emitter_library/build_defaults.py checkexport out\tools\run_verify_defaults
   ```
   This compares every sub-emitter line and every actor line of the exports with the entries, for each placement alone, all together, and after save and load. That covers textures, sounds, enum names and Tags.
   - One difference is expected and allowed. Some stock sub-emitters export `SizeScaleRepeats=0.000000` for a value that is not exactly zero. It imports as the class default 0, which native export omits.

## Placement conventions

- Each entry's pivot is where the effect starts, so a placed effect sits on the point it is placed at. Fires, drips, dust motes and the waterfall foam were moved to make this true; their tweaks say how.
- Rotation is kept. Many effects spray along the actor's +X (`UseRotationFrom=PTRS_Actor`), and their descriptions say which way that is.
- Entries hold emitters only, with no lights or meshes. The fire descriptions suggest adding a Light.
- Continuous entries have no Tag.
- Triggered entries keep `Disabled=True` sub-emitters and a readable Tag, such as `GasExplosion`. Placement prefixes the Tag with `RE_<id>_`, so every copy has its own Tag.
- `preview` is a camera hint for the explorer: a target relative to the pivot, plus radius, distance, pitch and yaw. The target and distance come from each effect's particle extent. The manifest overrides them for effects covering thousands of units.
