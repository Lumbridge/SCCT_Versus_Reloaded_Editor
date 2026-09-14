# OffsE lighting reproduction map

Saved map snapshot uploaded on 14 September 2026 for investigating static lighting on an added wall.

- `MapsEd/OffsE.sdc`: editable source map.
- `Maps/OffsE.sdc`: corresponding playable map.

These files were copied from the map-editing installation without rebuilding or changing them. The reported issue is that a static light illuminates the added wall before a build but the wall becomes dark afterwards; a dynamic light remains effective after rebuilding. This snapshot has not yet been verified to reproduce that issue or to have the test light set to Static.

The map requires its existing game and custom asset packages; those dependencies are not bundled here.
