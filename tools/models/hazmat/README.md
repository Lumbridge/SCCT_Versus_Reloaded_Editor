# Hazmat suit for the merc

A yellow Level A hazmat suit for the Versus merc: a baggy rubber suit with taped seams, a hood with a
bubble visor, a respirator with two filter canisters, black gloves and boots, and warning labels.
It uses the stock merc skeleton (all 42 `DEF_01` bones), so the merc's own animations
(`SPerso.Def`) drive it with no changes, including the gun on `B R Hand`.

`assets/` holds the finished model, ready to import:

| File | What it is |
|---|---|
| `HazmatMerc.psk` | the mesh: 3610 points, 6882 triangles, 2 materials (`HazmatSuit`, `HazmatGear`) |
| `HazmatSuit.tga` | 1024x1024 suit texture |
| `HazmatGear.tga` | 512x512 visor and respirator texture |
| `hazmat.json` | counts, the mesh rotation and the goggle light offset |
| `preview_*.png`, `pose_*.png` | software renders, T-pose and posed by merc animations |

## Put it on ShipD (or any map)

The model goes into the map itself (MyLevel), so the map works for everyone without an extra package.

1. Open the map in the editor (from `Packages\MapsEd`).
2. **Texture Browser > File > Import**: import `HazmatSuit.tga` and `HazmatGear.tga` with
   Package `MyLevel`, Group `Hazmat`, and the names left as `HazmatSuit` and `HazmatGear`.
   Compress both as DXT1 if you like (right-click > Compress); it is not required.
3. **View > Show Animation Browser > File > Import Mesh**: pick `HazmatMerc.psk`, Package `MyLevel`,
   Name `HazmatMerc`. The editor log should say `Found texture for material 0: [HazmatSuit]` and
   `material 1: [HazmatGear]`; that links the textures. Import the textures first, or the mesh is untextured.
4. Still in the Animation Browser, on the **Mesh** tab, set **Rotation** Yaw to **-16384** (the stock
   merc's value; without it the suit lies on its side).
5. **RE+ Tools > Character Skins**: in **Merc model** type or pick `MyLevel.HazmatMerc`, set the merc's
   goggle lights to **X -0.2, Y -1.4, Z 0** (they then sit on the visor), and press **Apply**.
6. Save the map. Play it (Play Level as a spy, then type `addbot` in the console to see a merc in it).

## Rebuild it

Needs Python 3.10+ with numpy, scipy, Pillow, scikit-image and fast-simplification
(`python -m pip install --user numpy scipy pillow scikit-image fast-simplification`). No modelling
program is used.

```
python build_hazmat.py --install "C:\path\to\SVM 4.0 Public Beta Mapping Ver" --out out --preview
python pose_preview.py --install "C:\path\to\SVM 4.0 Public Beta Mapping Ver" --out out --merc
```

`build_hazmat.py` reads the stock merc (`merc_mesh.py` parses `SPerso.ukx` directly), builds a distance
field a few units out from the merc's body (baggier on the torso and legs, tight on hands and boots),
joins a hood and an air-pack hump, extracts the surface with marching cubes and simplifies it to about
6200 triangles. Every vertex copies the bone weights of the nearest merc skin; the hood and gear are
rigid on `B Head`. UVs are one chart per body part, and the texture is painted procedurally in 3D and
baked into them. The numbers at the top of the script (stand-off per bone, hood size, crotch gap,
triangle budget) and `paint_suit` (colours, seams, labels) are the knobs to turn.
`pose_preview.py` poses the result with any `SPerso.Def` sequence to check the rigging.

## Known limits

- Procedural art: it reads as a hazmat suit at game distance, but up close the folds are low-poly and
  the creases are painted, not modelled. The gloves are mittens shaped like the merc's hands.
- The suit is bulkier than the merc, so some poses clip a little (forearms into the chest when reloading,
  the inside of the thighs when crouching).
- Thermal vision shows the whole suit evenly hot (the stock heat masks fit only the stock texture layout).
- Put on a spy (Spy model), it follows spy animations, but the camera and proportions were not made for it.
