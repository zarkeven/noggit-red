# Rendering profiling guide

Noggit ships with [Tracy](https://github.com/wolfpld/tracy) integrated (`src/external/tracy`). Use it to measure rendering cost before changing draw behavior.

## Build configuration

Use a **Release build with debug symbols** for representative performance and readable stacks:

```powershell
cmake -S . -B build-profile -DCMAKE_BUILD_TYPE=Release -DNOGGIT_OPENGL_ERROR_CHECK=OFF
cmake --build build-profile --config Release
```

| Option | Recommendation |
|--------|----------------|
| `CMAKE_BUILD_TYPE=Release` | Representative frame times |
| `/Zi` (Release) | Already enabled on Windows in root `CMakeLists.txt` |
| `-DNOGGIT_OPENGL_ERROR_CHECK=OFF` | Removes per-GL-call error verification overhead in `src/opengl/context.inl` |

Deploy the built `noggit.exe` as usual (Qt DLLs, `themes/`, etc.).

## Capturing with Tracy

1. Download or build [Tracy Profiler](https://github.com/wolfpld/tracy/releases) (Windows: `Tracy.exe`).
2. Start **Tracy Profiler** first, or leave it open on the **Connect** screen.
3. Launch Noggit from the profile build directory.
4. In Tracy, connect to the running `noggit` process.
5. Load a heavy map, reproduce the scenario, then save the capture (`.tracy`).

### Key zones to inspect

| Zone | Meaning |
|------|---------|
| `MapView::paintGL` | Full frame |
| `MapView::tick` | Tile streaming, tools, animation between draws |
| `World::draw() : Frustum cull + tile sort` | ADT visibility + front-to-back sort |
| `World::draw() : Collect visible objects` | M2/WMO list build |
| `World::draw() : Reuse visible objects cache` | Static-camera fast path |
| `WorldRender::drawSunShadowDepthPass` | GPU sun shadow depth pass |
| `World::draw() : Draw terrain` / `Draw WMOs` / `Draw M2s` | Main geometry |
| `World::draw() : Tile occlusion queries` | Per-tile occlusion (camera moving only) |
| `World::draw() : WMO terrain blend FBO` | Top-down terrain bake for WMO blending |
| `WorldRender::updatePointLightsUniformBlock` | Point-light UBO sort/upload |
| `LiquidRender::draw` | Per-tile water (aggregated in parent water zone) |

### Tracy plots

Each frame emits:

- `Loaded tiles`
- `Rendered tiles`
- `Rendered objects`

Correlate spikes in frame time with culling effectiveness (large gap between loaded and rendered counts suggests culling is working; similar counts suggest heavy overdraw or missing culls).

Status bar text mirrors tile/object counts during capture.

## Profiling scenarios

Use a **dense city area** (Stormwind, Orgrimmar, or similar) with high view distance and loading radius.

For each scenario:

1. Capture **60+ seconds** while orbiting a fixed viewpoint.
2. Capture **30 seconds** with the camera **held still**.
3. Record average FPS / frame time from the status bar.
4. Note the **top 5 Tracy zones** by self time.
5. Save the `.tracy` file with the scenario name.

| ID | Scenario | Settings |
|----|----------|----------|
| A | Baseline | Shadows off, point lights off, fog off, editor overlays off |
| B | Heavy scene | Max view distance, max tile load radius, models + WMO on |
| C | + Realtime shadows | Enable realtime sun shadows in view settings |
| D | + Point lights GPU | Enable point-light shading (not just editor spheres) |
| E | + Modern fog stack | Fog on, WMO terrain blend on (`wmo_terrain_blend`), volumetric fog debug if used |
| F | Worst case | B + C + D + E + point-light sphere viz + MCCV / texture-layer billboards |

### Results template

Copy this table once per capture session:

| Scenario | Avg FPS | Avg frame ms | Loaded tiles | Rendered tiles | Loaded objs | Rendered objs | Top zone 1 | Top zone 2 | Top zone 3 | CPU or GPU bound? |
|----------|---------|--------------|--------------|----------------|-------------|---------------|------------|------------|------------|-------------------|
| A | | | | | | | | | | |
| B | | | | | | | | | | |
| C | | | | | | | | | | |
| D | | | | | | | | | | |
| E | | | | | | | | | | |
| F | | | | | | | | | | |

**Decision rule:** Only optimize passes that rank in the **top 3** for scenarios B–F. If CPU zones are small but frame time is high, follow up with RenderDoc on the heaviest draw passes.

## Optimizations already in place (for validation)

When validating captures, compare static vs moving camera:

- **Visible object cache** — reuses M2/WMO draw lists when the camera is still and tiles are clean.
- **Tile sort skip** — skips front-to-back sort when the visible tile set and camera are unchanged.
- **Occlusion query skip** — skips per-tile occlusion tests when the camera is still.
- **Shadow depth pass skip** — skips `drawSunShadowDepthPass` when texel-snapped sun shadow matrices are unchanged.
- **Point-light sort cache** — reuses closest-light ordering when the camera has not moved meaningfully.
- **Billboard scoping** — MCCV, texture-layer, and sound-emitter billboards iterate frustum-visible tiles only.

After profiling, prefer the next change that targets the confirmed top bottleneck (see the rendering optimization plan Phase 3).
