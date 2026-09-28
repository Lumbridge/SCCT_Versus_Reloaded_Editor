# Map JSON import and export

Use **View > Export Map to JSON...** to save the loaded map's data as `map.json`
for inspection, editing tools, or sharing. Selecting actors before export marks
them in the snapshot and helps identify the intended area.

Prepare a JSON **change file** using the exported template. Open **View > Import Map from JSON...**,
read the scrollable preview, and choose **Apply changes**. The editor applies the
batch in one native transaction. Undo reverses the batch; Redo restores it.
Save the map to retain the changes.

## What is supported

- Create loaded actor classes: lights, sound actors, emitters, triggers,
  SMagicEvents, meshes, and other actors exposed by the class catalogue.
- Set absolute positions, rotations and reflected editable properties.
- Create and configure owned particle-emitter components.
- Edit existing actors and particle components, checking their exported values
  against the current map before changing them.
- Connect triggers to events and append delayed actions to event groups.
- Resolve forward and cyclic object references between newly created actors.
- Create box-shaped brush actors/volumes using `geometry: "box"`; configure their
  scale and transform through properties.

The JSON export contains editable map data and reference context. It is not a
complete binary `.maped` replacement.
The original map retains embedded assets and engine-generated data. Import does
not rebuild BSP, navigation or lighting. Static-light changes may require a
lighting rebuild; dynamic effects still need an in-game check. Brush creation or
movement may require rebuilding geometry. Version 1 does not delete actors,
import asset files, load packages automatically, edit arbitrary brush polygons,
or execute commands/scripts from JSON.
Version 2 adds package loads, texture imports, package saves, brushes made of
textured polygons and BSP surface texturing; see
[Version 2](#version-2-packages-textures-polygon-brushes-and-surfaces).

## Snapshot (`scct.map-authoring`, version 1)

- `map`: destination identity. Saved maps use their canonical path; unsaved maps
  use an identity valid for the current editor session. Re-export after Save As,
  moving the file or opening a different unsaved map.
- `level`: native object-path prefix.
- `actors`: actor/component identities, complete exposed `values`, live `schema`,
  and `unavailable` properties with reasons. Actors also include numeric position,
  rotation and selection context. Engine infrastructure is excluded.
- `classes`: loaded actor and particle-component classes with property schemas.
  Schemas describe valid types and enum choices, **not class default values**.
- `assets`: loaded sound, material and static-mesh paths. Load additional packages
  in the editor and export again if the required assets are absent.
- `geometryT3d`: native text copy of map actors and brush geometry for spatial
  context. Meshes refer to their assets; this does not contain mesh vertex data.
  If native copying fails, `geometryUnavailable` explains why. Unsupported data
  is not silently claimed to be editable or losslessly exported.
- `changes`: an empty change-file template containing the correct map identity.

Exports are limited to 64 MiB. Change-file import is limited to 128 MiB, 64 JSON
nesting levels and 2,000 operations. Individual property limits also apply.
Selection is restored after export and import. No map save happens automatically.

## Change file (`scct.map-changes`, version 1)

Copy `snapshot.changes` into a separate file, add a description, and populate
`operations`. Keep the `map` string exactly as exported. Unknown operation fields
are rejected. The snapshot, its schemas, and its geometry cannot be imported as
executable changes.

### Create an actor

```json
{
  "op": "create",
  "id": "CorridorSteam",
  "class": "SBase.SSpawnableEmitter",
  "properties": {
    "Location": {"X": "128", "Y": "96", "Z": "64"},
    "Rotation": {"Pitch": "0", "Yaw": "16384", "Roll": "0"}
  }
}
```

Use a class from the exported catalogue. `Location` is mandatory; omitted
properties retain native class defaults. New actors can also set `StaticMesh`,
`DrawType`, `DrawScale` and `DrawScale3D` using the live class schema. Loaded
static-mesh assets now include their local bounding boxes in the catalogue.
Coordinates use absolute Unreal units;
rotations use 65,536 units per turn. Use `geometry: "point"` (the default) for
ordinary actors and `geometry: "box"` for classes marked `brush`.

IDs use up to 63 letters, digits and underscores and cannot start with a digit.
IDs are unique within the file, ignoring case. The initial actor Tag equals its
ID unless explicitly overridden in properties. A live actor with that name or
Tag blocks duplicate creation. Native object names can acquire a suffix when
Undo has retained an earlier object. Use `$ref` instead of guessing those names.

### Add a particle component

```json
{
  "op": "component",
  "id": "SteamParticles",
  "owner": "CorridorSteam",
  "class": "Engine.SpriteEmitter",
  "properties": {}
}
```

The component is appended to its owner's `Emitters` array. Configure its
properties using the exported class schema or an existing component's values.
An empty component demonstrates attachment; it does not specify a finished steam
effect. Do not also replace the owner's `Emitters` array in the same batch.

### Update an existing actor or component

```json
{
  "op": "update",
  "actor": {"path": "MyLevel.Light1", "class": "Engine.Light"},
  "before": {},
  "properties": {"LightBrightness": "96"}
}
```

Replace the illustrative identity with the exported identity and replace
`before: {}` with that object's **complete exported `values` object**. Any change
to exposed values since export rejects the update. Combine changes to one
existing object into a single update operation.

Property leaves are strings, including numbers and booleans. Structs require
every field; supplied arrays replace the entire array. String properties contain
quoted text (for example `"\"Door opens\""`). Use the live schema's enum choices.
Unspecified top-level properties retain their values.

### Object references

Use `{"$ref": "CorridorSteam"}` in an object-reference property to refer to an ID
created in the same file. It also accepts an existing actor's exact exported path.
The importer checks class compatibility and resolves the actual native path after
creation. Ordinary existing asset references retain the exported text form, such
as `"Sound'Package.Group.SoundName'"`. Referenced assets must already be loaded.

`$ref` is supported inside nested structs and arrays, but not in Tag/Event names
or class-reference properties. Tag/Event values are literal actor Tags; assigning
a shared Tag intentionally targets all matching actors. Updating a Tag does not
automatically rename its dependants.

### Connect events

```json
{
  "op": "link",
  "event": "CorridorSequence",
  "target": "CorridorTrigger",
  "trigger": true
}
```

This sets the trigger actor's Event to the SMagicEvent's Tag.

```json
{
  "op": "link",
  "event": "CorridorSequence",
  "target": "CorridorSteam",
  "trigger": false,
  "group": 0,
  "delay": "1.5"
}
```

This appends an action targeting the steam actor's Tag. `group` defaults to zero;
`delay` defaults to zero seconds and accepts values from zero to 86,400. Delay is
only valid on action links. An event with no groups gets its first group when an
action is appended. Other group indices must exist. Missing receiver Tags are
allocated by the editor.

Set `Groups` explicitly on the event when authoring Sequence, Repeat, ValidOn,
reset logic or action settings beyond Delay/Event. Copy the complete reflected
structure from an export and use the field meanings in
[SMagicEventSemantics.md](../tests/SMagicEventSemantics.md). Link operations append
actions after property updates, in file order; avoid also listing the same action
in `Groups`.

When a link modifies an **existing** event/target, or a component attaches to an
**existing** emitter, add its exported values to the change file's top-level
`expect` object, keyed by its exact path. An update's `before` also supplies this
expectation for that object. This rejects stale links and attachments:

```json
"expect": {
  "MyLevel.ExistingEmitter": {"COPY": "the entire exported values object here"}
}
```

## Version 2: packages, textures, polygon brushes and surfaces

Set `"version": 2` to use the operations below. Version 1 files keep working
unchanged, and every version 1 operation is also valid in version 2. A version 2
file may set `"map": "*"` to mean "the map that is open", so a generated file can
build a new map without exporting it first. The preview names it "The map that is
open". Version 2 files may hold up to 20,000 operations.

Package loads, texture imports and package saves run **first, in file order,
before any map change**. They change packages, not the map, so Undo does not
reverse them. The map changes then apply in one Undo step as in version 1. Before
the preview, classes and materials that only exist once the file's packages load
are listed as "checked after the file's packages load"; Apply checks them in full.

### Load a package

```json
{"op": "load", "package": "Bunker"}
```

Finds the package by name: `System\<name>.u`, then `Packages\Textures\<name>.utx`,
`Packages\StaticMeshes\<name>.usx`, `Packages\Animations\<name>.ukx`,
`Packages\Sounds\<name>.uax`/`.uas`. Loading makes its classes, materials, meshes
and sounds available to later operations.

### Import a texture

```json
{"op": "texture", "package": "CisternHR", "group": "Walls", "name": "Brick",
 "file": "textures/brick.tga", "format": "DXT1", "mips": true}
```

- `file`: `.bmp`, `.tga`, `.pcx` or `.dds`. A relative path is relative to the
  change file's folder (the workflow API takes `baseDirectory` for this).
- Both sides must be powers of two, each up to 8,192. The texture keeps the file's
  full resolution. 4,096 is the practical size: the 32-bit editor and game hold a
  system-memory copy of every texture.
- `format`: `DXT1` (default), `DXT3`, `DXT5` (alpha) or `RGBA8` (uncompressed).
  A `.dds` file keeps its own DXT format and mip chain, so omit `format` for it.
- `mips` (default `true`) builds the whole mip chain down to 1x1. `masked` and
  `alphaTexture` (default `false`) are the importer's MASKED and ALPHATEXTURE
  flags. `lodSet` (0-15) is passed through to the importer.
- `group` is optional. The texture's path is `Package.Group.Name` (or
  `Package.Name`). Importing over an existing texture is rejected.

The import is checked after it runs: the texture must exist with the file's size
and the requested format.

### Save a package

```json
{"op": "save", "package": "CisternHR", "overwrite": false}
```

Saves the package to `Packages\Textures\<package>.utx`. Only a package that an
earlier `load` or `texture` operation in the same file names can be saved. An
existing file is replaced only with `"overwrite": true`, and is first copied to
`System\ReloadedEditor\map-json-backups`. Save the package before saving a map
that uses its textures; the map stores references, not the images.

### Create a brush from polygons

```json
{
  "op": "create", "id": "Hall", "class": "Engine.Brush", "geometry": "polygons",
  "properties": {"Location": {"X": "0", "Y": "0", "Z": "256"}, "CsgOper": "CSG_Subtract"},
  "polygons": [
    {"texture": "CisternHR.Walls.Brick", "flags": 0,
     "origin": [0, 0, 0], "textureU": [0, 1, 0], "textureV": [0, 0, -1], "pan": [0, 0],
     "vertices": [[768, -768, -256], [768, 768, -256], [768, 768, 256], [768, -768, 256]]}
  ]
}
```

`geometry: "polygons"` works for every brush class (CSG brushes and volumes),
beside `"box"`. Each polygon has 3-16 `vertices` as numbers in the brush's local
space (relative to `Location`), in the same winding the editor uses for brush
faces (the box brush's faces are an example). The other keys are optional:
`texture` (`Package.Group.Name`, loaded or imported by this file), `flags`
(PolyFlags, unsigned 32-bit), `origin`, `textureU`, `textureV` (the UV basis; the
editor picks one when both are absent) and `pan` (`[U, V]` integers). A brush holds
up to 16,384 polygons.

The editor's text importer converts vertices to floats, drops coincident points
and computes each normal. A polygon it would discard or reshape (too few distinct
points, collinear points, a 17th vertex) rejects the file, with the operation id
and polygon index. Split larger faces yourself. Closing and convexity of each brush
are not checked; CSG needs closed, convex brushes. CSG brushes take `CsgOper`
(default `CSG_Add`) and `PolyFlags` as properties. Rebuild geometry after import.

### Texture BSP surfaces

```json
{"op": "surface", "surfaces": [0, 1, 17], "texture": "CisternHR.Walls.Brick"}
```

Applies a material to BSP surfaces by index, as **Apply Texture** does, which also
updates the brush polygons that own them. The indices are those of the map's
current BSP, so rebuild geometry first; a file that also creates brushes changes
the BSP only after its own rebuild. Up to 2,000,000 unique indices per operation.

### Example

A generated map usually uses two files: the first loads packages, imports and
saves its textures, and creates the brushes and actors; then rebuild geometry and
optionally apply a second file with `surface` operations; then save the map.

## Version 2 verification

Validation on 2026-09-28: Release/Win32 build and `MapAuthoringModelTests` passed.
A headless editor run on a disposable game copy (Packages\Textures copied, not
linked) applied a version 2 file to a new map (`"map": "*"`): loaded `Bunker`,
imported a 2048x1024 TGA as DXT5, a 512x512 BMP as DXT1 and as RGBA8 with and
without mips, and a 256x256 DXT1 DDS with its mips, saved the package, pasted a
subtractive room and an additive pillar from textured polygons plus a light and a
player start, rebuilt, textured two BSP surfaces, then reloaded and re-saved the
package with `overwrite` (backup written). The saved package held every texture
at full size with a complete mip chain (12 levels for 2048x1024), decoded within
1.5/255 mean error of the source images; the map export showed the polygon textures
and the two retextured faces.

This run also found that pasting a plain `Engine.Brush` without a CSG operation
created no actor (the paste treats a `CSG_Active` brush as the builder brush), so
version 1 `"box"` brushes of class `Engine.Brush` failed. The paste now carries
`CsgOper` (default `CSG_Add`) for CSG brushes; volumes are unchanged.

## Ordering, validation and failure

The editor validates the document, loaded classes, exposed property types,
existing values and references before showing the preview. Apply repeats those
checks. Actors are created first, then components, then properties are assigned,
then links are added. Forward references therefore work regardless of operation
order. Late native or event-semantic errors roll back the whole transaction.
The UI also rejects a map revision changed while the preview was open.

Review exact placements, sound assets, light settings and timing in the preview.
The editor cannot infer whether an effect looks right, whether sound is audible
through the intended space, or whether a trigger works for the intended team.
Those are playtest checks.

## Verification

`tools/test_workflow_tools.cmd` includes engine-independent format and reference
validation. The disposable native workflow suite checks map export, menu file
dialogs, preview cancellation/application, multi-actor creation, particles,
forward references, delays, stale updates, rollback, exact placement, Undo/Redo,
and saving/reopening a batch-authored scene. Its artifacts include
`map-authoring.json`, `map-changes.json`, `map_authoring_preview.bmp`, and
`map_authoring_saved.json`.

Validation on 2026-09-15: Release/Win32 build, all workflow model suites, and the
full disposable native workflow suite passed, including a second editor process
reopening the saved scene. Preview screenshots were inspected. An earlier full
run crashed during the existing assembly-membership save test, after the new
authoring UI checks had passed; that crash did not recur in the full rerun and
was not diagnosed or fixed by this change.
