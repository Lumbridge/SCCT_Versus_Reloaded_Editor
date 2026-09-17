# Map Design

Open **View > Map Design...** while a source map is open. The workspace has its
own top (XY), front (XZ) and side (YZ) design views, showing native brush wireframes
and actor origins. Wheel zooms; middle-drag pans. Click an unlocked actor origin
to select it in the editor. **Refresh from editor** updates selection and geometry.
Native transactions refresh automatically. These views supplement the editor's
normal textured/perspective view; image overlays are drawn in the design workspace.

## 1. References and floor plans

**Reference image...** copies a PNG, JPEG or BMP into `System/ReloadedEditor/References`.
Set units per image pixel, opacity and the top-left image position in the chosen
view's axes. **Calibrate image** lets you click two known points and enter their
real distance in Unreal units. **Fit map / preview** includes the reference image.
There is one reference per map; choosing another replaces the displayed reference.
The source image is never changed. Images are limited to 32 MiB, 16 megapixels,
and 16000 pixels per side to keep memory use bounded in the 32-bit editor.

## 2. Blockout

**New blockout...** creates a green preview, with a name, exact dimensions,
placement position, yaw, and floor-centre or lower-left-floor-corner anchor.
**Place / Apply preview** creates ordinary native brushes in one Undo step.
Right-click the design view to discard a preview without touching the map.

- **Room:** subtracts the outer volume, then adds a floor, four walls and an
  optional ceiling. Width/length/height describe the interior space.
- **Corridor:** the same construction with open ends along local Y.
- **Doorway:** a subtractive opening. Length is its depth through a wall.
- **Stairs:** 1–128 solid steps rising along local +Y; height is the total rise.
- **Ramp:** a solid wedge rising along local +Y.
- **Platform:** a solid rectangular slab; height is its thickness.

This engine starts with solid space. Additive stairs, ramps and platforms belong
inside carved space. A missing ceiling does not automatically carve the world
above it. **Rebuild geometry** after placement, resizing or moving brushes to
update rendered BSP and collision. The design view previews source brushes, not
the final CSG result.

Select any generated brush and choose **Edit selected piece...** to regenerate
its whole blockout piece. The toolkit checks all original brush data before
replacement and refuses to overwrite manual edits. **Detach selected piece**
retains the ordinary brushes for unrestricted editing. Native Undo/Redo restores
the brushes; historical piece identities allow editing an earlier restored version.

## 3. Player scale and clearance

**Player clearance...** queries loaded spy/merc pawn classes for standing and,
when exposed, crouching collision defaults. The resulting profile chooser uses
diameter and full height, rather than the engine's radius and half-height.
You can override the dimensions and label, and add several guides at different
positions. Front/side views show a simple human-scale guide. When class defaults
are unavailable, the custom guide starts at an illustrative 48 × 96 units.
Collision defaults alone do not establish traversal limits: configure clearances
from movement tests and verify openings in game. Guides never become gameplay actors.

## 4. Measurement and alignment

**Measure two points** saves a distance annotation in Unreal units. **Depth...**
sets the hidden-axis coordinate for new points (floor Z in the top view).
**Align / distribute...** works on native selected actor/brush origins along X,
Y or Z. Align uses the first actor in native selection order; distribute sorts
by that axis and applies exact spacing from the lowest position. Other axes are
preserved. Both use one Undo step and reject locked actors or stale positions.

## 5. Layers

**Layers...** creates named layers from selection, selects their members, adds or
removes selection, hides/shows them, and locks/unlocks movement using the native
`bLockLocation` flag. Membership is exclusive to avoid conflicting layer states.
Missing/deleted actors are skipped. Resizing a blockout retains its logical layer
membership, including historical identities needed by Undo.

These are editor visibility and movement locks, not a prohibition on deleting
actors or editing their properties. Visibility/lock changes are native undoable
operations. Layer names/membership are workspace metadata, not native Undo items.

## 6. Modular assemblies and repeated placement

Saving an assembly now offers a selection-centre, builder-brush or first-selected-
actor pivot. The existing assembly placement dialog adds a copy count, XYZ
spacing, and yaw snapping. **Repeat selection...** in Map Design also supports
a yaw step per copy, useful for repeated columns or radial arrangements.
The original selection is preserved as existing geometry; copies are new actors.
Each batch is one Undo step. Limits: 128 copies and 2000 total actors.
Repeated groups must include their linked actors. Assemblies requiring external
bindings can still be placed singly through the existing library dialog.

## 7. Temporary playtest positions

**Playtest from here...** chooses an existing team spawn and either the first
perspective viewport's position/aim or the builder-brush position. During native
Play Level export, that team's starts temporarily use the test pose; their exact
positions and rotations are restored afterwards, including if launching fails.
Choose the matching player type in the native **Play Map Options** dialog.
The tool does not force the game's team menu
or create missing team starts: place the required PlayerStart actors first.
Rebuild geometry first, and position the test point clear of floors and walls.

## 8. Routes and objectives

**Route / objective...** names a route, objective, spy-spawn marker or merc-spawn
marker. Click points in the design view and choose **Finish route / marker**.
Routes show total length. Use the top view's **Depth...** for different floors,
or front/side views for vertical route planning. **Remove annotation...** removes
individual measurements/routes/markers or clears player guides. These are design
annotations, not automatically created game objectives or navigation paths.

## Persistence and portability

Saved-map workspace data lives alongside Working Views and assemblies in
`System/ReloadedEditor/library.json`, keyed by map path. Keep that file and the
References folder with your editor workspace. Untitled-map metadata stays in
memory until the map is first saved; Save As carries the current workspace to
the new map path. The `.sdc` itself contains ordinary generated brushes and actors.
Other users can open it without the design workspace, but need the workspace
files for reference images, annotations and parametric editing history.
