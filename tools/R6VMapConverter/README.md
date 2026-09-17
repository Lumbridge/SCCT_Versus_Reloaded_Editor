# R6V Map Converter

A .NET 10 Windows desktop app for converting local Rainbow Six Vegas PC maps into SCCT editor static-mesh packages and placed scenery. The backend packages the conversion tools used for the Calypso import, including corrected triangle winding and mirrored placements.

## Use

1. Launch **R6V Map Converter.exe**.
2. The app detects Vegas through Steam libraries and installation records, then fills the **Source map** dropdown. Choose a map; your last valid selection is remembered. **Find paths** rescans, and **Browse** remains available for custom installations.
3. Select your SCCT **map editing folder** (the folder containing `System` and `Packages`).
4. Choose a short output directory and a unique package name. Use a different package name for each map to avoid native package name collisions.
5. **Inspect map** checks the package version, media dependencies and supported transforms.
6. Optionally load that map in Vegas and use **Recover textures**. This reads texture metadata from the supported running game build and decodes its `.usdx` file into a reusable cache. It never writes to or injects into Vegas. The resulting cache is selected automatically.
7. **Convert to SCCT** extracts assets, builds a `.usx`, creates a `.sdc` with original placements, reopens it in a fresh isolated editor, and checks the saved transforms and references.
8. Open the output folder. Copy the contents of **SCCT Packages** into your editing folder's `Packages` directory, then open the generated map from `MapsEd`.

Existing texture caches are matched to their source map using previous run metadata or a remembered manual selection. Switching maps selects that map's cache, or clears the field if none is found. Invalid manifests and caches with no available recovered images are ignored. The scan checks the selected output folder and standard R6VExports locations, without searching entire drives.

Vegas only needs to run for **Recover textures**. Offline conversion uses resident texture pixels and any cache you select. Unresolved colour is shown with a mauve checker and listed in `material-exceptions.json`.

## Output

| File | Purpose |
| --- | --- |
| `SCCT Packages/StaticMeshes/<name>.usx` | Referenced meshes, shader materials, embedded textures and mirrored variants |
| `SCCT Packages/MapsEd/<name>Placements.sdc` | Static scenery at the source positions, rotations and scales |
| `Catalogue.html` | Searchable source-to-SCCT mesh catalogue and conversion totals |
| `Placements.t3d` | Text version of the generated actor placements |
| `material-exceptions.json` | Missing colours and source-derived approximations |
| `validation.json` | Fresh native reload and placement verification result |
| `scan.json`, inventories and logs | Diagnostics, source mappings and unsupported records |

Each run uses a new folder. Cancelling retains diagnostic/partial output and closes only that run's isolated worker. Partial runs do not create the final **SCCT Packages** delivery folder.

## Supported scope and limits

- Source package version **241/66**, used by the local Vegas 1 PC maps. Other versions fail inspection.
- Static mesh assets referenced by the selected map, including per-instance material overrides, actor/component scale, actor pivots, component translations and rotations, and negative-scale variants. Component rotations are baked into dedicated mesh variants to preserve nonuniform scaling.
- Explicit source collision/lighting exclusions are carried across. Collision still needs in-game review.
- Absolute transform flags, unsupported serialized records, missing/ambiguous packages, or failed mesh section mappings stop the run with a diagnostic. They are not silently dropped.
- Moving actors become static scenery at their stored transform. No animation or gameplay logic is recreated.
- Original BSP, terrain, baked lighting, navigation, spawns and other gameplay are **not included**. The map has an empty subtractive review space.
- Material graphs, normal-map effects, reflections, UV animation and bloom are not reconstructed. Masking, transparency, tint and static emissive inputs are approximated with SCCT materials.
- Native import is tied to the verified SCCT executable hash in `Native/compatibility.json` and requires the Reloaded editor workflow API. Live texture recovery similarly checks the Vegas executable hash and runtime tables. A new game/editor build needs a separately verified compatibility update.
- Native engine commands use legacy paths; choose a short ASCII output path such as `C:\R6VExports`.

## Build

Requires .NET 10 SDK, Visual Studio C++ x86 tools, the working SCCT editing build and the research UEViewer executable. Python 3.10+ with Pillow is required by the backend; it can be bundled using `-PythonRoot`.

```powershell
./build.ps1 -ScctRoot 'C:\Games\SCCT Editing' `
  -Umodel 'C:\Tools\UEViewer\umodel-research.exe' `
  -PythonRoot 'C:\Python310' -Output 'C:\Tools\R6V Map Converter'
```

The publish output includes the .NET runtime. Keep its supporting folders beside the executable. `-PythonRoot` copies the Python standard library and Pillow only, not unrelated installed packages.

Developer integration runs use `"R6V Map Converter.exe" --run options.json`, where the JSON contains the `ConversionOptions` fields. This runs the same conversion service as the UI and writes an adjacent `.log`.

## Provenance

The backend derives from this repository's Calypso conversion experiments. UEViewer is a separate tool by Konstantin Nosov: [source and documentation](https://github.com/gildor2/UEViewer). Its licence is included beside the bundled executable. The streamed texture decoder follows the format described in the [Vegas USDX extraction research](https://www.gildor.org/smf/index.php?topic=8810.0). Python and Pillow retain their bundled licences.

The application contains conversion tooling, not Vegas or SCCT game assets. Output is generated from the installations selected by the user.

## Validation of this release

- End-to-end native conversion and fresh reload: **Casino 2 (297 meshes / 2,156 placements)** and **Dante's (346 / 1,846)**.
- All ten installed multiplayer maps were inspected. Eight pass initial inspection. **LVU 1 and Mexico 1 currently stop on unsupported component records**; see `VALIDATION.json`. Inspection success alone is not proof that all later stages will succeed on an untested map.
- Five backend regression tests pass, including winding/UV pairing and component rotation under reflected, nonuniform scale.
- Native cancellation was exercised against a disposable worker and preserved the existing user editor.
- The generalized stream decoder reproduced all **781** previously recovered texture files byte-for-byte. The new live-process recovery wrapper has **not** been exercised with Vegas running in this session.

Run backend tests with `python -m unittest discover -s Tests -v`. The native cancellation integration test accepts a disposable conversion run: `dotnet run --project Tests/NativeCancellation.csproj -- C:\R6VExports\DisposableRun`.

### Path discovery (v0.2.0)

Twelve discovery regression checks pass, covering Steam library formats, source map enumeration, cache validation, and map associations. On this machine detection found the E: Steam installation and 60 source maps; the remembered Calypso cache was selected. Run `dotnet run --project Tests/DiscoveryTests.csproj` to repeat the fixture checks.
