# alog2media implementation and validation plan

## Current implementation status

| Phase | Status | Evidence |
| --- | --- | --- |
| A — repository and CLI | First slice complete | Build, help/version, option tests, MP4/GIF pipeline |
| B — macOS offscreen | First slice complete | CGL/FBO, unshown viewer, FreeType labels, real-log render |
| C — parser/map isolation | Pending | Current broker still writes `_alvtmp` cache data |
| D — renderer extraction | In progress | Explicit framebuffer draw adapter exists; FLTK inheritance remains |
| E — Linux headless | First slice complete | EGL passes with display variables unset; OSMesa remains pending |
| F — mission/full fit | Pending | REGION_INFO works; `--mission` and geometry-complete bounds remain |

## 1. Product outcome

The product command is:

```bash
alog2media INPUT.alog [OPTIONS]
```

It emits an MP4 or animated GIF containing only the pMarineViewer navigation
viewport. It must not require screen recording, user interaction, a visible
window, `DISPLAY`, or `WAYLAND_DISPLAY`.

The first positional argument remains the `.alog` path so humans and automation
can use the same simple command. `alog2media -h` is the canonical, complete
option reference. Both `--option value` and `--option=value` forms are accepted.

## 2. Fidelity definition

At a given timestamp, output should be visually equivalent to pMarineViewer
when all of the following are equal:

- output viewport dimensions;
- TIFF map and `.info` metadata;
- local-coordinate datum;
- pan and zoom;
- current vehicle states and trails;
- active logged `VIEW_*` artifacts;
- visibility settings and labels.

The output excludes the menu bar, controls, cursor, window chrome, log plots,
and alogview's yellow `--zoom/--panx/--pany` footer.

“Exact” means the same positions, shapes, colors, map crop, layering, and text
content. Same-platform golden images should be extremely close. macOS/Linux
goldens may allow a small per-pixel tolerance for driver and font rasterization
differences. Byte-for-byte identity across OpenGL implementations is not a
release requirement.

An `.alog` alone cannot reproduce unlogged interactive pan, zoom, or visibility
changes. The renderer therefore applies configuration in this precedence order:

1. explicit CLI overrides;
2. supported settings from an optional mission file;
3. logged `REGION_INFO` map, datum, pan, and zoom;
4. documented alog2media defaults.

The initial defaults are grid off, labels on, normal recent trails, all logged
geometry on, and mission viewport mode.

## 3. Target CLI

The implemented first slice includes output, size, FPS, start/end/duration,
warp, map, view, grid, trails, force, verbose, help, and version options.

The stable target adds:

```text
--mission FILE.moos        Import supported pMarineViewer launch settings.
--map FILE.tif|FILE.tiff   Override TIFF; same-basename .info is required.
--map none                 Render a mapless local-coordinate scene.
--view mission|fit         Use configured viewport or fit all scene content.
--grid auto|on|off         auto follows mission config, otherwise off.
--trails auto|off|full|S   auto follows mission config; S is a time window.
--labels auto|on|off       auto follows mission config, otherwise on.
--diagnostics              Report selected context, renderer, and encoder.
```

Output suffix selects the encoder. MP4 uses H.264/yuv420p and rejects odd
dimensions coherently. GIF uses a generated palette. FFmpeg is invoked through
an argument vector and stdin pipe, never a shell string.

## 4. Architecture

The final code is divided into four boundaries:

```text
.alog + optional .moos
        |
        v
read-only timeline parser ----> scene state at timestamp
                                      |
REGION_INFO / map / settings ---------+
                                      v
                              shared scene renderer
                                      |
                       CGL / EGL / OSMesa framebuffer
                                      |
                                      v
                                RGB frame stream
                                      |
                                      v
                                FFmpeg process
                                      |
                                  MP4 or GIF
```

The scene renderer owns pMarineViewer-compatible drawing. Context creation is a
small platform interface and must never leak window-system assumptions into the
scene model. Encoding is independent from parsing and rendering.

The current first slice intentionally reuses `ALogDataBroker`, `NavPlotViewer`,
and the MOOS-IvP static libraries to establish end-to-end fidelity quickly. It
already replaces the window back buffer with a macOS CGL or Linux surfaceless
EGL context and framebuffer object. It does not use `LogViewLauncher` or
`REPLAY_GUI`.

## 5. Implementation phases

### Phase A — repository and executable contract

- Establish GPL licensing and third-party attribution.
- Create a standard C++17/CMake build with explicit `MOOS_IVP_ROOT` discovery.
- Implement the first-argument CLI, generated default output name, coherent
  diagnostics, and complete `-h` output.
- Accept both lowercase `.tif` and `.tiff`; require the matching `.info` file.
- Stream frames to an argv-based FFmpeg child process.

Acceptance:

- clean configure/build against the pinned checkout;
- unit tests cover both TIFF suffixes and core option conflicts;
- `-h` and `--version` work without OpenGL, FFmpeg, or an input log;
- MP4 and GIF metadata match requested dimensions, FPS, and frame count.

### Phase B — offscreen macOS vertical slice

- Create a legacy CGL compatibility context with an RGBA/depth framebuffer.
- Construct the viewer as an unshown state/dimension holder only.
- Reproduce the shared `MarineViewer` draw sequence against explicit framebuffer
  dimensions.
- Remove alogview-only overlays.
- Read RGB directly from the framebuffer and flip rows for FFmpeg.

Acceptance:

- rendering creates no `NSWindow` and never calls `Fl::run()`;
- a real mission log renders to valid MP4 and GIF;
- first/middle/last frames contain a nonblank map and changing vehicle state;
- grid is absent by default;
- map crop matches `REGION_INFO` pan/zoom.

### Phase C — parser and map isolation

- Replace `ALogDataBroker`/`SplitHandler` with a read-only streaming parser.
- Support paths containing spaces, Unicode, quotes, and shell metacharacters.
- Keep any optional index under an explicit cache directory, never beside the
  input unless requested.
- Parse NAV state, `REGION_INFO`, `NODE_REPORT`, and every supported `VIEW_*`
  family with activation, deactivation, duration, and expiry semantics.
- Resolve maps deterministically: explicit path, log/mission directory, current
  directory, `IVP_IMAGE_DIRS`, then installed MOOS-IvP data.
- Validate TIFF contents and `.info` metadata before initializing OpenGL.

Acceptance:

- input logs are opened read-only and their directories remain unchanged;
- synthetic parser fixtures cover state transitions and geometry lifetimes;
- map errors list every attempted location and suggest `--map`.

### Phase D — renderer extraction and deterministic text

- Move reusable map, vehicle, trail, and geometry drawing behind a renderer that
  accepts scene, viewport, dimensions, and framebuffer.
- Remove inheritance from `Fl_Gl_Window`.
- Replace FLTK `gl_font`/`gl_draw` with an explicit, redistributable bundled font
  and deterministic glyph atlas; record its license.
- Keep a compatibility test that compares the extracted renderer to the current
  pMarineViewer/MarineViewer implementation at exact timestamps.

Acceptance:

- final renderer links no FLTK windowing code;
- label tests are deterministic on each platform;
- reference-scene positions and colors match within agreed tolerances.

### Phase E — Linux headless backends

- Implement surfaceless EGL as the preferred Linux backend.
- Implement OSMesa as a software fallback and deterministic CI backend.
- Add `--diagnostics` so tests can assert which backend was selected.
- Never silently fall back to X11/Wayland or Xvfb in headless mode.

Acceptance:

```bash
env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  ctest --test-dir build --output-on-failure
```

must build, render, encode, and pass without `xvfb-run`.

### Phase F — mission configuration and complete fit mode

- Parse the relevant `ProcessConfig = pMarineViewer` block supplied with
  `--mission`.
- Import map, grid, names, trails, pan/zoom, and supported per-geometry settings.
- Make `auto` CLI values follow mission configuration.
- Extend fit bounds from vehicle tracks to all visible/active `VIEW_*` content,
  with stable padding and degenerate-scene behavior.

Acceptance:

- a mission-file render matches a normal initial pMarineViewer launch;
- CLI settings override mission values one by one;
- missing `REGION_INFO` falls back to fit with an explicit warning and manifest.

## 6. Test fixtures

Commit only original, synthetic fixtures:

- a tiny quadrant/checkerboard TIFF plus `.info` metadata;
- the identical TIFF bytes exposed as `.tif` and `.tiff` during tests;
- a small `.alog` containing LOGSTART, REGION_INFO, NAV_X/Y/HEADING,
  NODE_REPORT_LOCAL, points, seglists, polygons, circles, and pulses;
- variants with no REGION_INFO, missing map, inactive geometry, and expiry.

Do not commit private mission data or redistribute MIT map imagery. A compact
local MOOS-IvP mission log may be used for manual acceptance only.

Test layers:

1. option/parser unit tests;
2. scene-state timestamp tests;
3. map-validation and viewport tests;
4. uncompressed RGB golden/tolerance tests;
5. FFmpeg MP4/GIF integration tests inspected with `ffprobe`;
6. pMarineViewer/al​ogview side-by-side fidelity references;
7. no-display platform tests.

For media tests, verify codec, pixel format, dimensions, FPS, duration, frame
count, animation, and movement between decoded first/last frames. Failed
encoding must not leave a corrupt final output; encode to a temporary path and
atomically rename in the stable implementation.

## 7. Platform matrix

Local development and CI use the same fixtures:

| Platform | Context | Required coverage |
| --- | --- | --- |
| macOS Apple Silicon | CGL + FBO | build, unit, render, MP4/GIF, fidelity |
| Ubuntu 24.04 | surfaceless EGL | build, unit, DISPLAY-unset integration |
| Ubuntu 24.04 | OSMesa | software fallback and golden rendering |
| Ubuntu 24.04 | EGL/OSMesa + sanitizers | ASan/UBSan parser and render tests |

Linux can be exercised locally in Docker/VM and on GitHub Actions. macOS is
tested on Apple hardware locally and with a macOS Actions runner. VM/container
success supplements but does not replace native macOS validation.

## 8. Release gates

No `0.1.0` release is tagged until all of the following are true:

- `alog2media INPUT.alog` produces a valid MP4 with natural defaults;
- MP4 and GIF pass metadata and decoded-frame validation;
- `.tif` and `.tiff` render the same fixture identically;
- mission viewport and fit viewport are both covered;
- the output has no grid by default and no alogview footer;
- macOS creates no visible window;
- Linux passes with `DISPLAY` and `WAYLAND_DISPLAY` unset;
- the parser does not mutate the input directory;
- help and README match implemented behavior;
- GPL and third-party attribution are complete;
- CI pins a known upstream MOOS-IvP revision and a nightly job checks upstream
  compatibility separately.

## 9. Immediate validation record

Development began against:

- MOOS-IvP checkout: a built sibling checkout supplied through `MOOS_IVP_ROOT`
- initial dependency revision:
  `b4a6162b018cde48279659c8b595594990a29086`
- macOS Apple Silicon toolchain
- FLTK 1.4.4
- FFmpeg available at `/opt/homebrew/bin/ffmpeg`

Validation results belong in `docs/VALIDATION.md` with exact commands, artifact
metadata, representative frames, known deviations, and the current release-gate
status. Results are evidence, not evergreen claims; update them whenever the
renderer or dependency revision changes.
