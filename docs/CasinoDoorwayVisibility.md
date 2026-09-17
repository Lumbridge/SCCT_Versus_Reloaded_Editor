# Casino doorway visibility repair (2026-09-17)

The imported `R6V_MP_Casino_01Placements.sdc` showed a black void through
doorways, with the next room appearing when the camera crossed the threshold.
An isolated editor reproduced this at position `(3616.8345, 1223.8838, 737.9448)`
and rotator `(63816, 2375948, 0)`, textured mode 6.

The converter's `Backend/bsp_geometry.py` synthesized `PF_Invisible` (1) for
caulk materials. All 1,634 invisible brush polygons in this map used
`R6V_MP_Casino_01.RVM0489`, converted from `k_Common_Mat.Editor.caulk_Mat`.
The compiled BSP put the rooms in zones 6 and 7, with zone 0 in the open
passage. There were no antiportal actors.

Clearing only these caulk polygon flags on the master brushes, then running
`MAP REBUILD` and `BSP REBUILD OPTIMAL BALANCE=15 PORTALBIAS=70`, restored
visibility from the identical camera pose. All nonzero compiled node zones
became zone 1. No node-zone override was used in the delivered map.

The repair preserved all 3,334 actors, polygon vertices, UVs, materials,
gameplay links and other authored actor properties. Derived region and
lighting data were regenerated. `LIGHT APPLY` and native save completed.
The repaired map is delivered separately in the mapping installation as
`Packages/MapsEd/R6V_MP_Casino_01DoorFix.sdc`.
The original map and open mapping session were left intact.

Relighting pushed the source package past the native 15 MB archive buffer.
The working copy of `LightmapFix.cpp` had lost the `test eax, eax` instructions
before four conditional jumps, disabling summary capture/repair. These were
restored, and the final map was regenerated from the intact original and saved
through File > Save As. The resulting single-chunk archive retains a valid
summary and does not overwrite the data at the former chunk boundary. The
updated DLL is installed in the mapping copy and requires an editor restart.
Raw diagnostic `MAP SAVE` bypasses the menu's post-save repair and must not be
used as proof of a working normal save for a package above the buffer limit.
The final installed map passed a fresh native load and the same camera render.
Its 15,968,926-byte uncompressed package has a valid summary; comparison after
reload confirms the actor inventory and authored properties, excluding expected
derived region/lighting data, editor timing and the saved doorway camera pose.

The source converter in `C:/Users/ryans/source/repos/R6V_Map_Converter` now
preserves authored face flags rather than generating invisible flags from
material names. Its BSP test suite includes the caulk regression. Exposed
caulk keeps its source material; making it invisible with BSP flags is not
a safe way to hide it. Existing maps require repair and rebuild independently
of the converter source change.

Diagnostic jobs, topology dumps, camera pose, repair source and exports are
under `build/doorway-visibility`. The native topology dump's child pointers
are front at `0x34` and back at `0x30`; earlier dumps mislabeled these fields.
This editor reproduction does not constitute an in-game traversal test.
