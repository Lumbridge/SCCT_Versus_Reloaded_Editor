# Map Design

Open **View > Reloaded Tools > Map Design...** while a source map is open. The workspace has its
own top (XY), front (XZ) and side (YZ) design views, showing native brush wireframes
and actor origins. Wheel zooms; middle-drag pans. **Refresh from editor** updates
selection and geometry. Native transactions refresh automatically. These views
supplement the editor's normal textured/perspective view; image overlays are drawn
in the design workspace. The window reopens where it was last closed.

## Selecting and editing

Click a brush edge or an unlocked actor origin to select it in the editor. Hold
**Shift** or **Ctrl** to add to or remove from the selection. Drag across empty
space to box-select every actor and brush that lies entirely inside the
rectangle, so a large enclosing brush is not caught by a rectangle drawn inside
it.

Clicking a brush that a blockout generated picks up **the whole piece** for
editing: it becomes a live green preview over the brushes it will replace, with
the dashed outline of its current shape underneath. Drag it to move it, drag one
of its squares to resize that face while the opposite face stays put, or nudge it
with the **arrow keys** — one grid step, or one unit with **Ctrl** held. Each
change replaces the piece's brushes in a single Undo step, so the map always
matches what the view shows. Moving a room, corridor or vent takes the doorways
cut into its walls along with it, in that same step. **Escape** ends the edit. Brushes moved in the editor's own viewports keep
their piece: the library follows them, so the outline stays on the brushes. A rotated blockout
resizes along its own axes. **Undo** and **Redo**, or **Ctrl+Z** and **Ctrl+Y**
in the design view, step through the editor's own history — and through the
workspace's annotations, references, guides and layers when one of those was the
last thing changed.

Point at the wall of a placed room, corridor or vent and a **+** appears just
outside it. The wall itself still selects the piece or drags its face; click the
**+** to add a doorway through that wall, or a corridor, vent or room against
it, sized from the movement limits and already in the right place. It arrives as
a preview, so adjust it before pressing Place / Apply.

A new preview carries a green **tick** and a red **cross** beside its top-right
corner: click the tick to place it (**Enter** does the same) or the cross to
discard it (**Escape**). **Delete** removes whatever the panel is showing — a
light, device, reference or route point — or the piece being edited with its
brushes, or a new preview, or otherwise the editor's current selection.

The panel on the left is contextual: the six everyday buttons, then the
properties of whatever was clicked last — a piece, a light, a security device,
a player reference, a route point, or the route being drawn (with its **Finish**
button). Edits apply as they are made; nothing opens a separate window.

In the front and side views the **+** badge appears on a piece's top and bottom
as well as its walls: from the top it offers a **room above** (same footprint,
one floor up) or **stairs up into a room above**; from the bottom the same
downwards; from a wall seen edge-on the usual doorway, corridor, vent and room.

**Right-click** anywhere in a design view for a menu of what applies at that
point: edit or remove the player reference or route point under the cursor,
select, wire or delete the security device there, add through the hovered wall,
edit, select, detach or delete the piece there, place or discard the current preview, and start a new
room, corridor or vent, a player reference, a route, a measurement, a playtest or
a security device at that exact point. While something is being placed,
right-click cancels it instead. The menu bar (Workspace, Blockout, Annotate,
Tools) holds every action; the side panel keeps only the everyday ones.

The design view knows when built BSP no longer matches the brushes: a banner
says so, and **B** (or **Build geometry**) runs the editor's Rebuild Geometry
Only from inside the panel. Lighting is left alone until the layout settles.
**Overlays** draws each light's reach as a disc, brighter lights more opaque,
and each SCamNetwork camera's view cone in the top view, so dark routes and
blind spots are planned rather than discovered.

**Check design...** lists layout problems the plan hides — a room no doorway,
corridor or vent reaches, a doorway that cuts into nothing, two carved spaces
that merge, vents a standing pawn walks through, and the traversal warnings —
together with what a Versus map needs before it can be played: a PlayerStart per
team, an SMission, an SObjective and a camera network. It also checks the
objective flow: a mission with no objectives, an objective no mission lists or
that has no terminal, bomb target or trigger, a flag without a drop zone, spy
starts within 512 units of an objective, the two teams' starts within 768 units
of each other, and objectives more than 4096 units from any merc start.
Double-click an issue to select its piece.

The preview always describes what is in the map. If the piece being edited is
deleted, moved or reshaped elsewhere, the preview is dropped with a note rather
than drifting away from its brushes, and an edit that the editor refuses leaves
the map untouched and says why.

The **inspector** below the buttons holds that piece as live fields: name, shape,
construction, dimensions, thickness, stair count, ceiling, zone portal, position
and yaw. Press **Enter** or move the focus to apply a field. For a piece already
in the map the change lands immediately; a new preview waits for **Place / Apply
preview** (or Enter in the design view). **Discard preview** throws an unplaced
preview away. No dialog appears anywhere in this loop.

**Snap to grid** uses the editor's current grid spacing for clicks, drags and
nudges, and draws the design view's grid to match. Change the spacing with **Ctrl
+ mouse wheel** over a viewport, or uncheck the box for exact positions. While
snapping is on, a dragged piece or face also lines up with the faces of brushes
already in the map when it comes within a few pixels of one, so rooms meet without
arithmetic. Locked actors are never picked or dragged.

The design views fill each brush's outline: carved space is light, added solid
geometry is dark, and zone portals, volumes and movers have their own tints
against a background that stands for the engine's solid space. A top view
therefore reads as a floor plan rather than a wireframe tangle.

The **floor slider** down the right edge of the canvas limits every design view to
one storey, keeping a multi-level map readable. It applies to brush edges, actor
origins, clearance guides, annotations and reference images placed on other
floors. Right-click the slider for **Custom height range...**, which takes any
two heights. The slider has
a detent for each storey the map's rooms, corridors and vents stand on (highest
at the top) and **All** above them. Drag it, click a detent, or press **Page Up /
Page Down** to step between storeys and **Home** to show them all. In the top
view the chosen storey is also where new pieces are placed.

Clicking any brush of a placed piece selects the whole piece, in every view, so a
staircase with a brush per step selects and edits as one. Dragging a rectangle
from left to right selects what lies wholly inside it; from right to left, anything
it touches. Whole pieces, their brushes, and the devices, lights and game actors
inside are selected together; a single piece on its own opens for editing, and
The **Top / Front / Side** buttons under the view caption switch the view with a
click, and the **brushes changed** banner at the top right rebuilds geometry when
clicked. Pieces move and resize in the elevations too: dragging inside a piece
counts wherever the view's depth is set.

Every piece, stairs and spirals included, turns about its base: drag the round
handle beyond its +Y edge (15 degree steps, Ctrl for free rotation), press **R**
or **Shift+R** for a quarter turn either way, use the right-click menu's **Turn**
entries, or type a yaw in the panel. A placed piece's brushes are replaced in one
Undo step. **Delete** removes whatever is selected. With several pieces selected, dragging
inside any of them moves them all, and the rest of the selection is drawn
dashed where it will land; arrow keys and grouped members follow the same way.

**Tools > Keyboard and mouse** (or the **Keys...** button) lists every shortcut,
and each control in the panel has a tooltip.

### Locking

Anything can be locked so it is not selected or moved by accident: right-click
it in the plan and choose **Lock**, press **Ctrl+L** with a selection (Ctrl+Shift+L
unlocks), or use the Scene panel. Locked pieces, brushes, lights, devices and game
actors are skipped by clicks, box selection and drags in the plan, are drawn with
a small padlock, and the editor itself refuses to move them (the native
`bLockLocation` flag). The right-click menu on a locked thing offers **Unlock**.

### The Scene panel

The **Scene panel** is docked down the right side of the design window from
the start, grouped by type; **Workspace > Scene panel** (also on the plan's
right-click menu) or its **Hide** button puts it away and brings it back. It
lists every
piece, brush, light, security device and game actor in the map, the way a layers
panel does. Each row has a tick for visibility and a **LOCKED** cell to toggle a
lock; a filter box narrows the list by name, type or group; **Show** limits it to
selected, hidden or locked entries or one category; **Group by** folds the list
under group, type or floor headers, which click to collapse. A header's tick and
lock apply to everything under it. Selecting rows selects the actors in the
editor and the plan; double-clicking shows an entry in the plan (and opens a
piece for editing).

Groups behave like folders in a layers panel: **New group from selection** (or
Ctrl+G in the list) makes one, the right-click menu adds entries to a group,
removes them, renames or ungroups it, and selects the whole group. Clicking any
member of a group in the plan selects the whole group, and dragging or nudging
one member moves the rest with it in the same Undo step (locked members stay; a
motion sensor brings its volumes). Groups are kept in the workspace
and in the map's native Group field, so they travel with the .sdc. The
right-click menu also offers **Solo** (hide everything else), **Show all**,
**Unlock all**, **Select every <type>**, renaming a piece, and **Delete**.

## 1. References and floor plans

**Reference image...** copies a PNG, JPEG or BMP into `System/ReloadedEditor/References`.
Set a name, units per image pixel, opacity, the top-left image position in the
chosen view's axes, and the floor height it belongs to. A map can hold several
references: each is shown in the view it was placed in, and the floor filter hides
the ones on other storeys, so a plan per floor and an elevation can live together.
**Calibrate image** lets you click two known points on a chosen reference and
enter their real distance in Unreal units. **Remove reference...** deletes one.
**Fit map / preview** includes the visible references. The source image is never
changed. Images are limited to 32 MiB, 16 megapixels, and 16000 pixels per side to
keep memory use bounded in the 32-bit editor.

## 2. Blockout

**New blockout...** drops a green preview at the builder brush straight away — a
1024 x 1024 x 256 carved room — and fills the inspector with its values. Placed
pieces take the texture browser's current material on every face, like a native
builder-brush addition, so a blockout arrives as a greybox. Shape it
by dragging or by typing, then **Place / Apply preview** creates ordinary native
brushes in one Undo step. The piece stays selected afterwards so it can be
adjusted again. Right-click or press Escape in the design view to discard a
preview without touching the map.

- **Room:** carves the interior space. Width/length/height describe that space.
- **Corridor:** the same carve, extended by the wall thickness at each end so it
  cuts through the walls of the rooms it joins.
- **Vent:** a corridor at crouch height (the crouching clearance from
  **Movement limits...**, 125 by default), checked against crouch clearance.
- **Crawlway:** lower still, for a slow walk (the crawl clearance, 105 by default). Switching a piece to either shape
  brings its height down with it, and back up when you switch away.
- **Doorway:** a subtractive opening through a wall, starting at the interior
  face. Length is its depth. It can carry a zone portal.
- **Stairs:** 1–128 solid steps rising along local +Y; height is the total rise.
  Every flight, spirals included, also gets an invisible, semi-solid ramp laid
  over the steps' front edges, so pawns glide up rather than bump from tread to
  tread. Resizing any stair recounts its treads: about 32 units of run each and
  no more than 24 of rise, so a longer or taller flight gains steps and a shorter
  one loses them (type a count in the panel to override). A spiral is round:
  dragging either side handle changes its diameter.
- **Stairs L:** two flights with a landing, turning right; width is one flight,
  length the footprint of the first flight (at least 8 more than the width).
- **Stairs U:** two flights side by side with a half landing, coming back the
  way they went; the footprint is twice the width by the length.
- **Spiral:** wedge steps round a post, one full turn over the height; width is
  the outer diameter (64 or more).
- **Ramp:** a solid wedge rising along local +Y.
- **Platform:** a solid rectangular slab; height is its thickness.

### Construction

This engine starts with solid space, so **Carve** builds a room or corridor from
a single subtractive brush. That is the default, and it leaves far fewer surfaces
to cut, light and go wrong than the older **Shell** construction, which carves the
outer volume and then adds a floor, four walls and an optional ceiling back into
it. Shell remains available, and blockouts placed by earlier versions keep it, so
editing an existing piece regenerates exactly what was placed. Wall thickness
still sets the gap a carved room leaves between itself and its neighbours, and the
depth its doorways cut. The ceiling option applies to shell construction only.

A piece's rotation is built into the shape of its brushes; the brush actors
themselves stay unrotated. This editor's wireframes apply an actor's Rotation but
its geometry build does not, so a rotated brush would draw correctly and then
build sideways. Rotated pieces placed by earlier versions still carry actor
rotation: select one and nudge it, or apply any field, to regenerate it.

Additive stairs, ramps and platforms belong inside carved space. **Rebuild
geometry** after placement, resizing or moving brushes to update rendered BSP and
collision. The design view previews source brushes, not the final CSG result.

### Doorways in a room

Select a brush of a placed room or corridor and choose **Doorway in room...**.
Pick a wall in the room's own local axes, an offset along it, the opening's width,
height and sill, and how deep it cuts. The doorway starts at the interior face, so
a depth equal to the wall thickness removes exactly that wall. Offsets that run
past the wall, or an opening that reaches above the ceiling, are rejected.

**Zone portal** adds an invisible, non-solid portal sheet in the middle of the
opening, flagged as the editor's own Add Special zone portal. Zoning is what keeps
visibility cheap in these maps. Portal sheets appear under **Zones / portals** in
Brush Visibility. Rebuild geometry, then check the zones in game.

Clicking a generated brush in a design view starts editing its whole piece, and
**Edit selected piece...** does the same for a selection made in the editor's own
viewports. The toolkit checks all original brush data before
replacement and refuses to overwrite manual edits. **Detach selected piece**
retains the ordinary brushes for unrestricted editing. Native Undo/Redo restores
the brushes; historical piece identities allow editing an earlier restored version.

## 3. Movement limits and traversal warnings

**Movement limits...** holds this map's step-up height, walkable slope, standing
and crouching clearance, the speed penalty for moving crouched, the climb speed
used for ladders and drops, and the spy and merc movement speeds. It is prefilled from the
loaded pawn classes where they expose those values, and from starting values
otherwise. Blockout previews then warn when stairs rise faster than a pawn can
step, a ramp is steeper than the walkable slope, a platform is out of step reach,
or a doorway, room or corridor is narrower or lower than the clearance. Warnings
never block placing a blockout, and they are planning aids: confirm real limits
with movement tests in game.

## 4. Player scale and clearance

**Player reference...** adds a player reference: standing or crouching, sized
from this map's movement limits, or from a loaded spy/merc pawn class where one
exposes its collision (as diameter and full height, not the engine's radius and
half-height). Add as many as you like.

A reference is drawn as a body of that size with a head, so the posture reads at
a glance. **Drag it** in any design view to move it, and **double-click it** to
rename it, change its size or posture, or remove it. Collision defaults alone do
not establish traversal limits: set the sizes from movement tests and verify
openings in game. References never become gameplay actors.

## 5. Measurement and alignment

**Measure two points** saves a distance annotation in Unreal units. The **Z**
field beside the view combo (Y or X in an elevation) is the hidden-axis
coordinate new points and pieces land on; type a value and press Enter, or let
the storey slider set it.
**Align / distribute...** works on native selected actor/brush origins along X,
Y or Z. Align uses the first actor in native selection order; distribute sorts
by that axis and applies exact spacing from the lowest position. Other axes are
preserved. Both use one Undo step and reject locked actors or stale positions.

## 6. Groups

Groups live in the **Scene panel** (Workspace menu or the plan's right-click
menu), which replaced the old Layers dialog: make a group from the selected rows,
add to or remove from one, rename or ungroup it, and hide, show, lock or unlock
it as a whole. Membership is exclusive. It is written into each actor's native
`Group` field in the same undoable transaction, so it travels inside the `.sdc`,
and the workspace keeps the same list so a resized piece keeps its group along
with the historical identities Undo needs.

## 7. Modular assemblies and repeated placement

Saving an assembly now offers a selection-centre, builder-brush or first-selected-
actor pivot. The existing assembly placement dialog adds a copy count, XYZ
spacing, and yaw snapping. **Repeat selection...** in Map Design also supports
a yaw step per copy, useful for repeated columns or radial arrangements.
The original selection is preserved as existing geometry; copies are new actors.
Each batch is one Undo step. Limits: 128 copies and 2000 total actors.
Repeated groups must include their linked actors. Assemblies requiring external
bindings can still be placed singly through the existing library dialog.

## 8. Temporary playtest positions

**Playtest from here...** (right-click a point in the plan, or the Tools menu)
chooses an existing team spawn and the test location: the right-clicked point,
the first perspective viewport's position/aim, or the builder-brush position. During native
Play Level export, that team's starts temporarily use the test pose; their exact
positions and rotations are restored afterwards, including if launching fails.
Choose the matching player type in the native **Play Map Options** dialog.

From the right-click menu the point is the spot you clicked, at the current
**Depth**; the **Spawn facing** field sets its aim.

A team with no start of its own can still be tested: choose **Temporary start for
team 0** or **team 1**. That creates a PlayerStart at the test pose, of the same
class as an existing start where there is one, and removes it again as soon as
Play Level returns, so the saved map keeps only the spawns you placed. The removal
is an Undo of that creation, so a Redo straight afterwards would restore it.

The tool does not force the game's team menu. Rebuild geometry first, and position
the test point clear of floors and walls.

## 9. Routes and objectives

**Route / objective...** names a route, objective, spy-spawn marker or merc-spawn
marker. Click points in the design view to draw it and choose **Finish route /
marker** to keep it. Every click reports the distance so far and **both teams'
times**, and the finished route carries the same line: how far it runs and how
long a spy and a merc each take on it.

Times come from the speeds in **Movement limits...**. A climb or a drop — a
vertical step higher than the step-up height — is timed at the climb speed rather
than at a run, and a route marked **Crouched** (for vents) takes the crouch
penalty. Tag a route **Spy** or **Merc** to say whose route it is; it still shows
both times. Use the top view's **Z** field or the storey slider to place points on different floors,
or the front/side views for vertical planning. **Drag a route point** to adjust
it, and **double-click one** to rename the route, change its team or crouch flag,
or remove it. **Remove annotation...** also removes whole annotations.

**Compare routes...** puts two routes side by side and reports which arrives
first and by how much, for the asymmetric spy/merc timings that decide whether an
objective is reachable in time. A route with no team of its own is timed for
whichever team the other route is not. Timings are straight-line travel along the
clicked points: they ignore stealth, doors, lifts, waiting and anything that slows
a player down other than crouching and climbing. These are design annotations, not
automatically created game objectives or navigation paths.

## 10. Security devices

**Security...** opens a window for the map's detection layer: lasers, security
cameras, motion sensors, presence detectors, mines and alarms, placed from clicks
in the design view and wired together as they are placed.

Each **Add** button starts a placement; right-click or Escape ends it. **Add
alarm** takes one click and becomes the alarm that new detectors are wired to
(the **Wire new detectors to** list shows every alarm in the map, so a different
one can be chosen at any time). **Add laser** takes two clicks — where the beam
starts and where it ends — and sets the laser's length and direction to match.
**Add camera** takes the camera position and then a point it watches. **Add
motion sensor** takes two opposite corners of the floor area it covers, and
builds the `SVolumetricSensor` together with its detection volume, sized to the
standing clearance from **Movement limits...** unless **Height above floor** says
otherwise. **Add presence detector** and **Add mine** take one click. Lasers and
presence detectors sit at the **Height above floor** field (48 units by default),
cameras at 200 units, all above the current **Depth** floor.

Placement uses the same snapping as blockouts, so a beam drawn along a wall lands
on the grid. The plan draws each device with its coverage: the red beam of a
laser, a camera's viewing cone at its cone angle and range, the radius of a
presence detector or mine, and a dotted wire from every detector to the alarm its
Event reaches. Hide them with **Overlays**.

Devices are edited in place: **drag a device** to move it (a motion sensor's
volume moves with it), and **drag the white handle** at the end of a laser beam
or in front of a camera to turn it — the laser's beam also ends where the handle
is dropped, so one drag aims it and sets its length. In the top view the handle
sets the direction; in the front and side views it sets the pitch, so a laser can
be tilted up a stairwell or a camera angled down. A click selects the device in
the editor. Each drag is one Undo step.

**Click a device** (or right-click it and choose **Edit...**) and its settings
appear in the panel on the left, applied as you change them — the ones that
decide how it plays: a camera's cone angle, view distance, patrol sweep, turn speed
and detection delay; a laser's length, what it detects and whether it hides in
normal vision; a sensor's delay and minimum speed; a presence detector's radius
and height; a mine's arming delay; an alarm's name, messages and duration.
Anything else lives in the editor's own property window. A detector's panel also
has a **Wired to alarm** choice.

**Snap to the nearest wall, facing in** (right-click a laser, camera or motion
sensor) moves it onto the closest wall of a placed room, corridor or vent, flush
with the inner face, and turns it to face the room; a laser's beam is set to span
to the far wall.

**Right-click a device** to wire it: a detector's menu lists every alarm under
**Wire to alarm** (the current one is ticked, and **Nothing** unwires it), and an
alarm's menu lists every detector under **Wire a detector to this alarm**, plus
**Use this alarm for new detectors** and **This alarm triggers the selected
actors**. **Delete** is there too, and removes a motion sensor's volume with it.
Right-clicking a piece offers **Delete** for it and its brushes as well.

Wiring follows the game's convention: a detector's `Event` names the alarm's
`Tag`, and the alarm's `Events` list names the tags of whatever it triggers. The
list in the Security window shows each alarm with the detectors feeding it and
the actors it triggers; double-click a row to select that actor in the editor.
**Wire selected detectors to alarm** points every selected detector at the chosen
alarm, and **Alarm triggers selected actors** adds the selected lights, doors,
movers or events (their Tags are assigned if they have none) to the alarm's
outputs. **Check design...** lists detectors that trigger nothing, alarms that
nothing feeds or that trigger nothing, dangling Events and sensors without a
volume.

The tools set the geometry and wiring; an alarm's name, duration and the movers
and objectives it locks are edited in the editor's own property window, and
detection classes default to spies (`SPawnAttaque`).

## 11. Game mode: starts, mission, objectives

Right-click a point in the plan for **Player start here** (a **Spy** or **Merc**
start, with team icons; new starts use the class of the starts already in the
map), **Mission here** when the map has none, **Objective here** under the
mission (or a submenu when there are several), and **Objective device here** —
a computer terminal, bomb target, or flag with its drop zone — under the nearest
objective, already linked the way the editor's own objective creation links them.
Starts, missions, objectives, terminals, bomb targets and flags are drawn on the
plan with icons and can be dragged; a start's white handle turns it to face where
the player should look. Right-click one to select or delete it; names
and settings live in the editor's property window.

Right-click an **alarm** for **When triggered, lock the selected doors, lifts and
objective triggers**: the selected movers join its `MoversToLock` and the selected
objective triggers its `ObjectiveTriggersToLock`, so they lock while it sounds.

## 12. Gameplay elements

Zip lines, hand-over-hand bars, pipes, ladders, climbable fences, poles and
ledge grabs are placed in stages from **Gameplay element from here** in the
right-click menu, and built by the editor's own `GE ADD` command:

1. The point you right-clicked is the **start**. Click the **end** in the top
   view (for a vertical ladder or pipe, click the same spot).
2. The view switches side-on to the line — the front view when it runs mostly
   along X, the side view along Y — with its depth through the line's middle.
   Click the **height of the start**, then the **height of the end**.
3. For a pipe or ladder the top view returns: click **the side the player
   climbs from**.

The element is created with a brush that shows it — a bar along a zip line,
pipe, pole or hand-over-hand run, a slab for a ladder, a wall for a fence, a lip
for a ledge — and the plan draws the element's line in every view as long as
that brush exists. The top view comes back. Right-click or Escape cancels and
restores the view. Undo twice removes the brush and the element.

## 13. Floors

**Floors** in the right-click menu works in floor steps: the height of the room
at that point plus its wall thickness (272 units when there is no room there).
**Go up / down one floor** moves the plan's depth by one step. **Duplicate this
floor above / below** copies every piece whose base sits on the current floor to
the next one and moves the plan there — the quick way to stack storeys. **Stairs
up / down from here** offers straight, L-shaped, U-shaped or spiral stairs and
places them with a stairwell carved to exactly their footprint (32-unit treads,
rise from the floor spacing). The **+** badge on a room's top or bottom in the
front and side views offers the same shapes. **Lift up / down** places an SLift
mover: a 192 x 192 platform in a shaft cut through the slab, rising by the floor
spacing when a pawn stands on it and returning after three seconds (two Undo
steps: the shaft, then the lift). MoveTime, StayOpenTime and InitialState can be
changed in the Magic Event workbench. **Ladder up** and
**Pipe up from here** cut a 96 x 96 shaft and place the gameplay element with its
visual brush. **Opening through the floor above** cuts a 128 x 128 hole. Each is
one Undo step (the climbs two).

## 14. Lights

Lighting decides where a spy can hide, so the plan shows it. Every light in the
map is drawn as a small sun with its reach as a disc (brighter lights are more
opaque), labelled with its reach and brightness.

**Right-click** an empty spot and choose **Light here** for a preset: a ceiling
light, small lamp, bright floodlight, dim blue night light, red pulsing alarm
light, flickering light, or a **switchable light** (`STriggerLight`). The light
lands at the ceiling of the room it is placed in, or 160 units above the floor
being shown. **Drag a light** to move it and **drag the white handle on its
rim** to set its reach. Reach sets both the bake's `LightRadius` and the game's
own `EchelonRange`, so the change shows in play as well as in the lightmaps.
**Click a light** to edit its brightness, reach, hue, saturation, type
(steady, pulse, blink, flicker, strobe) and effect in the panel on the left;
**right-click** it to select it, delete it, or have an alarm switch it on — that adds the light's Tag to the alarm's
Events, which only a switchable light responds to. Each change is one Undo step.

**Tools → Show unlit areas** shades every part of the map's brushes that no
light reaches at the current **Depth**. A light's reach shrinks with its height
above that floor, so a ceiling light 224 units up with a 400-unit reach lights a
331-unit circle on the floor. This is reach, not shadow: walls and props are not
considered, and the real bake decides the final look. **Check design...** lists
rooms and corridors that no light reaches at all.

The plan reads and writes the actors; it does not rebuild lighting. Use
**Build → Rebuild Lighting** (or the selective lighting tools) when the layout
settles, and check the result in game.

## Persistence and portability

Saved-map workspace data lives alongside Working Views and assemblies in
`System/ReloadedEditor/library.json`, keyed by map path. Untitled-map metadata
stays in memory until the map is first saved; Save As carries the current
workspace to the new map path. The `.sdc` itself contains ordinary generated
brushes and actors, and layer membership travels inside it in each actor's
`Group` field.

Everything else — reference images, annotations, movement limits and the
parametric blockout history — is workspace data, and it is kept portable two ways.
Each save also writes a copy to `System/ReloadedEditor/Workspaces/<map>.json`,
which **File > Package Map for Sharing...** includes in the ZIP along with the
reference images it names. And **Workspace...** exports or imports that same file
by hand, for sending a workspace on its own or restoring one into a map. Importing
replaces this map's workspace; reference images are resolved by file name in
`System/ReloadedEditor/References`, and the panel reports any that are missing.
