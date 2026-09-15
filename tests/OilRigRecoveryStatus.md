# Oil Rig recovery fix — 2026-09-15

The reported OffsD candidate 62 / material 2044 had a backtracking boundary and was also outside the diagonal face being processed despite overlapping bounding boxes.

Implemented:
- Skip material candidates separated from the reconstructed face by an edge.
- Clean boundary reversals within source float precision (or native coincident-point precision in editor mode); retain rejection of larger crossings.
- Preserve importable parent faces when a material cut would produce unusable microscopic fragments.
- Enable bspAddPoint's native existing-point lookup during fast rebuilds, with an executable fingerprint and instruction-byte guard. This prevents the point array overflowing its 16-bit index space without renumbering live points or changing editor flags.
- Structural closing faces do not inherit non-solid/semisolid flags from material donors.

The final fix does not compact/remap point tables. Those experiments changed later CSG behavior and were removed.

Full native OffsD test passed with all material subdivisions enabled:
- 1,147 structural brushes, 65 sheet brushes, 10,774 polygons.
- All 9,050 original solid/empty probes matched.
- 629 original baked mesh colour streams preserved and verified.
- Geometry/BSP/lighting/navigation builds, save, ordinary reopen, another build, and T3D export succeeded.
- Validated collision-bound indices after recovery and subsequent rebuild.

Input SHA256: 6216CAB6537C9D047E604C5AB49CF52408AF19112C8F81C0DEA23FAC9E54B1E8
Successful isolated run: C:/Users/ryans/AppData/Local/Temp/scct-source-recovery-78da25ec77d8428e9931671b129fde70
Native log: build/oilrig-reuse-final-native.log
Release build: build/oilrig-reuse-final-build.log
Staged tested DLL: build/oilrig-tested/Reloaded.Editor.dll

The failing original crash was a write access violation at 110D17A4 after more than 100,000 points accumulated during CSG. The final implementation preserves native point identities and uses the engine's own lookup when inserting points.

Portable recovery suites and the generated-room recovery/edit/save/reopen test also passed. Installed into the map-editing copy; deployment hashes and backup location are recorded in build/oilrig-deployment.json.
