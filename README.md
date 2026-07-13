# model-tools

LEGO Universe 3D model viewers for NIF meshes, HKX collision data, and LXFML brick models.

> **Note:** This project was developed with significant AI assistance (Claude by Anthropic). All code has been reviewed and validated by the project maintainer, but AI-generated code may contain subtle issues. Contributions and reviews are welcome.

Part of the [LU-Rebuilt](https://github.com/LU-Rebuilt) project.

## Tools

### nif_viewer

3D viewer for Gamebryo NIF mesh files.

```
nif_viewer [file.nif]
```

**Features:**
- Lit OpenGL rendering with per-mesh coloring
- HKX collision overlay (semi-transparent orange, togglable)
- Generate HKX collision shapes from NIF geometry (File > Generate HKX)
- Save generated HKX files (File > Save HKX)
- NIF structure tree (blocks, meshes, nodes, materials, textures, animations)
- Click-to-select mesh highlighting with tree sync
- Wireframe toggle

**Keyboard shortcuts:**
- `Ctrl+O` — Open file
- `Ctrl+G` — Generate HKX from loaded NIF
- `Ctrl+Shift+S` — Save HKX
- `Esc` — Deselect

### hkx_viewer

Viewer for Havok HKX physics/collision files (binary packfile and XML formats).

```
hkx_viewer [file.hkx]
```

**Features:**
- Renders all shape types: box, sphere, capsule, convex hull, triangle mesh, compressed mesh, extended mesh
- Scene data rendering (hkxScene node hierarchy with meshes)
- Transparent solid + wireframe two-pass rendering
- HKX structure tree with shape hierarchy, rigid bodies, physics systems
- Checkbox visibility toggles per shape in tree
- Click mesh in viewport to jump to tree node and vice versa
- Supports both binary packfile (.hkx) and XML format

**Keyboard shortcuts:**
- `Ctrl+O` — Open file
- `Esc` — Deselect

### lxfml_viewer

Viewer for LEGO brick models in LXFML and LXF formats.

```
lxfml_viewer [file.lxfml or file.lxf]
```

**Features:**
- Loads .lxfml (XML) and .lxf (ZIP archive containing IMAGE100.LXFML)
- Supports all three LXFML versions: v2 (Models), v4 (Scene/LDD), v5 (Bricks/LU)
- Brick geometry loaded from .g primitive files via configurable client root
- Per-brick material colors from LEGO color database (80+ colors)
- Model structure tree (bricks, parts, cameras, rigid systems)

**Keyboard shortcuts:**
- `Ctrl+O` — Open file
- `Ctrl+R` — Set client root directory
- `Esc` — Deselect

**Setup:** Set the client root to the game's `res/` directory so the viewer can find brick geometry files in `brickprimitives/lod0/`.

### nif2usd

Console tool (no Qt) that exports a NIF model to a [Universal Scene
Description](https://openusd.org) stage, so LU meshes can be opened in
Blender, Houdini, Maya or `usdview`.

```
nif2usd <input.nif> [output.usd|.usda|.usdc] [--y-up]
```

**Exports:**
- One `UsdGeomMesh` per render mesh, in world space, triangulated, with
  per-vertex normals, the first UV set (`st`), and authored vertex colors
  (`displayColor`/`displayOpacity`) when present
- A `UsdPreviewSurface` material per mesh, driven by the NIF's
  `NiMaterialProperty` colors, with the diffuse texture wired through a
  `UsdUVTexture` (texture paths preserved verbatim as asset references)
- Stage metadata: Z-up (NIF/Gamebryo convention; `--y-up` bakes a -90° X
  rotation to USD's Y-up instead), meters, and source provenance
- LU-specific data USD has no native slot for (LOD bands, skin bone
  bindings) as `lu:*` custom attributes, so a future USD→NIF importer can
  round-trip them

**Output format:** `.usda` (text, default) or `.usdc` (binary crate),
inferred from the output extension. Output defaults to `<input>.usda`.

See
[`lu-assets/src/usd/`](https://github.com/LU-Rebuilt/lu-assets/tree/main/src/usd)
for the translation library.

### usd2nif

The inverse of `nif2usd`: import a USD stage back into a NIF the client loads.

```
usd2nif <input.usd|.usda|.usdc> [output.nif] [--z-up]
```

**Imports:** every `UsdGeomMesh` becomes a `NiTriShape`/`NiTriShapeData`
(positions, normals, `st` UVs, vertex colors); the bound `UsdPreviewSurface`
becomes a `NiMaterialProperty`, and a connected texture becomes a
`NiSourceTexture`. A flat root-node scene is emitted (per-mesh transforms baked
into vertices). `--z-up` rotates a Y-up stage back to NIF's Z-up.

Not byte-identical to any original NIF — USD doesn't carry Gamebryo's exact
block layout — but produces a valid, loadable model. Round-trip verified on the
client corpus (geometry, material colors and texture paths survive
NIF → USD → NIF). Skeletons/animation/collision import lands in later phases.

### kf2usd

Export a KF animation to a UsdSkel animation stage.

```
kf2usd <input.kf> [output.usda|.usdc] [--fps N] [--clip N]
```

Each `NiControllerSequence` clip in the KF becomes a `UsdSkelAnimation` (per-joint
translate/rotate/scale time samples) under one skeleton, so a model's whole
animation set exports to a single stage. `--fps` sets time codes per second
(default 30); `--clip N` exports only clip index N. Opens as skeletal animation
in Blender/Houdini/Maya or plays in `usdview`.

**Building:** `nif2usd`, `usd2nif` and `kf2usd` require OpenUSD (see Requirements)
and are built by default; pass `-DMODEL_TOOLS_BUILD_USD=OFF` to skip them (and the
OpenUSD dependency) if you only want the Qt viewers.

## Requirements

- Qt6 (Widgets, OpenGLWidgets)
- OpenGL
- zlib (for LXFML ZIP extraction)
- OpenUSD 24.x+ — for `nif2usd` only. A system install is used when
  present; otherwise it is built from source (slow). Skippable via
  `-DMODEL_TOOLS_BUILD_USD=OFF`.

## Building

```bash
cmake -B build
cmake --build build -j$(nproc)
```

For local development:

```bash
cmake -B build -DFETCHCONTENT_SOURCE_DIR_LU_ASSETS=/path/to/local/lu-assets \
               -DFETCHCONTENT_SOURCE_DIR_TOOL_COMMON=/path/to/local/tool-common
```

## Acknowledgments

Format parsers built from:
- **[nif.xml](https://github.com/niftools/nifxml)** / **[NifSkope](https://github.com/niftools/nifskope)** — Authoritative Gamebryo NIF/KF format definitions
- **[HKXDocs](https://github.com/SimonNitzsche/HKXDocs)** — HKX binary format documentation
- **[lu-toolbox](https://github.com/Squareville/lu-toolbox)** — Blender LXFML reference for brick geometry assembly and material colors
- **[NexusDashboard](https://github.com/DarkflameUniverse/NexusDashboard)** — LXFML rendering pipeline reference
- **[lcdr/lu_formats](https://github.com/lcdr/lu_formats)** — Kaitai Struct format definitions
- **Ghidra reverse engineering** of the original LEGO Universe client binary

## License

[GNU Affero General Public License v3.0](https://www.gnu.org/licenses/agpl-3.0.html) (AGPLv3)

