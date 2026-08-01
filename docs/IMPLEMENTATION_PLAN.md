# alog2media implementation plan

## Current status

The first three product milestones are implemented:

| Area | Status | Current evidence |
| --- | --- | --- |
| Fidelity proofs | Implemented | C++ unit/integration tests, decoded-media contracts, a tolerant golden, and direct offscreen parity against upstream `PMV_Viewer.cpp` |
| Read-only raw-log input | Implemented | Direct `.alog` parsing through `std::ifstream`; no `SplitHandler`, alogview cache, or `<input>_alvtmp` |
| Mission-aware scene | Implemented | automatic XLOG-parent discovery, `--mission` override, tri-state visual options, geometry replay/lifetimes, mapless mode, and geometry-aware fit |
| macOS headless rendering | Implemented | CGL framebuffer, no shown window, no FLTK event loop |
| Linux headless rendering | Implemented for EGL | Surfaceless EGL succeeds with `DISPLAY` and `WAYLAND_DISPLAY` unset |
| Renderer extraction | Partially complete | Rendering is isolated behind `HeadlessSceneViewer`, but it still derives from `MarineViewer` as a state holder |
| Software fallback | Pending | OSMesa fallback and deterministic software-only goldens remain hardening work |

The detailed, dated results are in [VALIDATION.md](VALIDATION.md). This plan
separates what the product does now from remaining release hardening.

## 1. Product outcome

The product command is:

```bash
alog2media INPUT.alog [OPTIONS]
```

It emits an MP4 or animated GIF containing only the pMarineViewer navigation
viewport. It does not require screen recording, user interaction, a visible
window, `DISPLAY`, or `WAYLAND_DISPLAY`.

The `.alog` is the first positional argument so humans, scripts, and a future
demo skill can use the same command. `alog2media -h` is the complete option
reference. Both `--option value` and `--option=value` forms are accepted.

## 2. Fidelity definition

At a given timestamp, output should be visually equivalent to pMarineViewer
when all of the following are equal:

- viewport dimensions;
- TIFF map and `.info` metadata, or the same mapless coordinate plane;
- local-coordinate datum;
- pan and zoom;
- current vehicle states and trail policy;
- active logged `VIEW_*` artifacts;
- pMarineViewer visibility, style, and label settings that alog2media supports.

The output intentionally excludes the menu bar, controls, cursor, window
chrome, log plots, and alogview's yellow pan/zoom footer.

“Exact” means the same positions, shapes, colors, map crop, layering, and text
content for the supported scene. Same-platform golden images should be very
close. Cross-platform tests allow a small per-channel and aggregate tolerance
because OpenGL drivers and font rasterizers differ. Byte-for-byte identity
between macOS and Linux is not a requirement.

An `.alog` cannot reconstruct an interactive pan, zoom, or visibility change
that was never logged. The renderer applies configuration in this order:

1. explicit CLI overrides;
2. logged `REGION_INFO` map, datum, pan, and zoom;
3. supported `ProcessConfig = pMarineViewer` settings from an automatically
   discovered or explicit mission when log context does not supply the
   corresponding value;
4. pMarineViewer-compatible alog2media defaults.

Natural defaults are the mission viewport, grid off, labels on, logged
geometry on, and the normal recent trail. Mission settings can alter visual
families when the matching CLI option remains `auto`.

## 3. Implemented CLI contract

The principal scene options are:

```text
--mission FILE.moos        Override automatic pMarineViewer mission discovery.
--map FILE.tif|FILE.tiff   Override the logged/configured map.
--map none                 Render a mapless local-coordinate scene.
--view mission|fit         Use configured pan/zoom or fit tracks and geometry.
--grid auto|on|off         Follow mission config or override the hash grid.
--trails auto|off|full|S   Configured recent trail, none, full, or S seconds.
--labels auto|on|off       Follow mission config or override scene labels.
--geometry auto|on|off     Follow mission config or override logged geometry.
```

The existing timing and encoding options include output, size, FPS, start,
end, duration, warp, force, verbose, help, and version. `.tif` and `.tiff` are
both accepted and require a same-basename `.info` file. MP4 uses
H.264/yuv420p; GIF output uses a generated palette. FFmpeg is started with an
argument vector and receives RGB frames over stdin, never through a shell
command string.

## 4. Current architecture

```text
.alog (read-only) + optional .moos
          |
          v
raw timeline parser ----------> scene state at timestamp
                                      |
REGION_INFO / map / mission settings -+
                                      v
                          HeadlessSceneViewer compositor
                                      |
                         CGL or surfaceless EGL FBO
                                      |
                                      v
                                RGB frame stream
                                      |
                                      v
                                  FFmpeg
                                      |
                                  MP4 or GIF
```

### Raw timeline and scene state

`ALogTimeline` reads the source through a read-only `std::ifstream`. It parses
`LOGSTART`, `DB_TIME`, `REGION_INFO`, `NAV_X/Y/HEADING`, local and geodetic
`NODE_REPORT` variants, and supported logged geometry. Sample-and-hold and
same-timestamp ordering are deterministic. LAT/LON-only reports are converted
with the log or mission datum.

`GeometryReplay` preserves event order and models activation, deactivation,
same-label replacement, duration, expiry, and backward seeks for the supported
pMarineViewer geometry families. Fit bounds include vehicle tracks and
supported geometry active in the requested output interval.

`MissionConfig` reads the pMarineViewer block with the MOOS configuration
reader and imports supported map, datum, pan/zoom, vehicle, trail, grid,
label, and per-geometry-family settings. Explicit CLI values override imported
settings.

### Rendering and encoding

`HeadlessSceneViewer` composes the scene in pMarineViewer order:

1. TIFF map, or mapless coordinate plane;
2. optional coordinate hash/grid;
3. active geometry, operation area, datum, and drop points;
4. vehicle trails;
5. vehicle bodies and names.

It reuses MOOS-IvP map, vehicle, and geometry drawing primitives. On macOS it
targets a CGL framebuffer; on Linux it targets surfaceless EGL. It constructs
no native window and never starts the FLTK event loop. The current class still
inherits `MarineViewer`/`Fl_Gl_Window` to reuse state and drawing behavior; that
inheritance is an implementation dependency, not a runtime screen capture.

## 5. Completed milestones

### Milestone 1 — fidelity and regression proofs

Implemented:

- C++ tests for option parsing, raw timeline semantics, same-time ordering,
  LAT/LON conversion, geometry lifecycle, mission parsing, and render smoke;
- decoded RGB checks for MP4/GIF codec, pixel format, dimensions, FPS,
  duration, frame count, and animation;
- exact decoded-frame equivalence for `.tif` and `.tiff` aliases;
- independent visual-effect checks for grid, labels, geometry, trails, and
  TIFF-versus-mapless rendering;
- mission/CLI precedence checks;
- a committed synthetic mapless golden frame with cross-platform tolerances;
- a test-only upstream `PMV_Viewer.cpp` compositor rendered through its own
  offscreen context and compared at exact timestamps;
- SHA-256, mode, filename, and directory-tree snapshots around read-only
  Unicode/space-path inputs.

The committed golden is a regression oracle for a deliberately small scene.
The upstream PMV comparison separately protects compositor fidelity without a
visible window. It proves close parity for the tested scene families; it does
not justify a universal “pixel identical” claim across every upstream geometry
family, font stack, and OpenGL driver.

### Milestone 2 — raw, non-mutating `.alog` parser

Implemented:

- removed the runtime dependency on `ALogDataBroker`, `SplitHandler`, and
  alogview-generated cache directories;
- reads arbitrary filenames directly without rejecting whitespace or Unicode;
- preserves deterministic event order and state-at-time behavior;
- resolves `.tif` and `.tiff` maps without modifying the map or log directory;
- proves the read-only input tree is unchanged after a complete render suite.

### Milestone 3 — mission-aware scene completeness

Implemented:

- automatic `targ_shoreside.moos` discovery for normal XLOG layouts, with
  logged map/camera precedence and `--mission FILE.moos` as an explicit
  override;
- tri-state `auto|on|off` controls for grid, labels, and geometry;
- `auto|off|full|SECONDS` trail policies;
- CLI override precedence over mission visibility settings;
- mapless scenes and explicit map overrides;
- mission viewport fallback and geometry-aware `--view fit`;
- broad logged geometry replay with lifecycle and replacement behavior;
- pMarineViewer-compatible draw ordering for map, geometry, trails, vehicles,
  and labels.

## 6. Platform and CI plan

| Platform | Context | Status / required coverage |
| --- | --- | --- |
| macOS Apple Silicon | CGL + FBO | Local build, tests, contract proof, MP4/GIF, and real-mission examples validated |
| Ubuntu displayless | surfaceless EGL | Local Docker/VM build and tests validated with display variables unset |
| macOS 14 Actions | CGL + FBO | Workflow pins official MOOS-IvP and runs CTest plus the product contract |
| Ubuntu 24.04 Actions | surfaceless EGL | Workflow pins official MOOS-IvP and runs CTest plus the product contract |
| Ubuntu | OSMesa | Pending fallback and deterministic software rendering |
| macOS/Linux | ASan/UBSan | Pending parser/render hardening |

The Linux command must continue to work without Xvfb:

```bash
env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  ctest --test-dir build --output-on-failure
```

## 7. Test-fixture policy

Only original synthetic fixtures and reference frames belong in this
repository. Private mission logs and MIT background map imagery may be used
for local acceptance, but are not committed.

The suite has these layers:

1. option and parser unit tests;
2. scene-state and geometry-lifecycle tests;
3. mission configuration and viewport tests;
4. decoded RGB golden/tolerance checks;
5. FFmpeg MP4/GIF integration checks with FFprobe;
6. direct upstream pMarineViewer compositor parity;
7. real-mission manual acceptance;
8. no-display platform checks.

## 8. Post-0.2 hardening

Core headless generation and the `0.2.0` release gates are complete. The next
hardening work should be:

1. broaden fixtures for any upstream `VIEW_*` family not yet represented and
   for multi-community logs whose source community is no longer recoverable;
2. remove `Fl_Gl_Window` inheritance from the state holder and use a bundled,
   versioned font for stronger determinism;
3. add OSMesa as a software fallback and run sanitizer jobs;
4. encode to a temporary destination and atomically promote successful output,
   so a failed FFmpeg process cannot leave a partial final file;
5. keep the official MOOS-IvP revision pinned in required CI and test newer
   upstream revisions separately before advancing the pin.

The `0.2.0` release requires:

- natural-default MP4 and GIF output with correct metadata and animation;
- mission and fit view coverage, including geometry-aware bounds;
- grid absent by default and no alogview UI/footer;
- macOS rendering without a visible window;
- Linux rendering with display variables unset;
- no mutation of source logs, maps, mission files, or their directory tree;
- help, README, and implemented behavior in agreement;
- GPL and third-party attribution complete;
- required CI green against the pinned official MOOS-IvP revision;
- a documented fidelity comparison to the upstream pMarineViewer compositor.

## 9. Updating the validation record

`docs/VALIDATION.md` records exact dependency revisions, commands, artifact
metadata, representative examples, known deviations, and current gate status.
Results are evidence tied to that revision and date, not evergreen claims.
Update the record whenever parser behavior, scene composition, OpenGL context,
font selection, or the pinned MOOS-IvP revision changes.
