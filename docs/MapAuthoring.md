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
