# Validation record

This file records reproducible evidence for the current implementation. Results
are tied to the dependency revisions and date below; they are not evergreen
claims.

## What the current proof suite checks

The repository has two complementary test layers:

- C++/CTest exercises option parsing, help/version startup, raw timeline
  semantics, same-time event ordering, LAT/LON conversion, geometry lifecycle,
  mission parsing, an end-to-end render smoke test, and direct frame parity
  against upstream `PMV_Viewer.cpp` in an independent offscreen compositor.
- `tests/proof/run_contract.py` inspects externally visible behavior using
  FFprobe and decoded RGB frames. It checks input immutability, MP4/GIF
  metadata, lossless PNG snapshots, animation, `.tif`/`.tiff` equivalence, a
  mapless golden frame, mission/CLI precedence, and independent grid, label,
  geometry, trail, and map effects.

The contract deliberately creates read-only source files and directories with
spaces and Unicode in their names. It snapshots filenames, modes, sizes, and
SHA-256 values before rendering, then verifies that the complete tree is
unchanged and that no `_alvtmp` path was created.

The standard commands are:

```bash
./scripts/build.sh -DMOOS_IVP_ROOT=../moos-ivp
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tests/proof -p 'test_*.py'

env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  python3 tests/proof/run_contract.py \
    --alog2media build/alog2media \
    --fixture-map build/fixture_map \
    --require-headless-env
```

## 2026-08-01 — PNG snapshot release gate

The CLI accepts `.png` output with one exact log timestamp:

```bash
alog2media mission.alog --at 120 -o scene.png
```

Option tests prove that `--at` requires PNG, that PNG rejects video interval,
FPS, and warp options, and that PNG permits odd dimensions while MP4 retains
the H.264 even-size constraint. The render smoke test produced a 319×179
`rgb24` PNG and FFprobe identified the `png` codec.

The complete read-only contract rendered `--at 0.5` at 320×180, verified the
PNG signature, decoded exactly one RGB image, and compared it with the
committed mapless golden. At an 8-level per-channel tolerance, 457 of 57,600
pixels differed and mean absolute error was 0.535995/255, within the same
cross-platform limits used for scene fidelity. The Unicode/space input tree
remained byte-for-byte and mode-for-mode unchanged with no `_alvtmp` path.

## 2026-08-01 — automatic mission discovery

The nine CTest targets and the complete decoded-media product contract pass
after adding bounded mission discovery. The no-`REGION_INFO` contract fixture
uses this ordinary launch layout:

```text
mission/
├── targ_shoreside.moos
└── XLOG_SHORESIDE_01/
    └── LOG_SHORESIDE_01.alog
```

The render command omits `--mission`. It discovers the parent mission, resolves
its `TIFF_FILE_B` map and datum, preserves its partial launch camera, and
honors its grid, label, and trail settings. Unit coverage also proves relative
input paths, import of conventional `targ_shoreside.moos` even when generic
fallback is disabled, a single generically named pMarineViewer mission when
fallback is needed, and rejection of ambiguous generic candidates.

## 2026-08-03 — launch outliers and automatic map lookup

The Alpha tutorial log reproduced a warped-launch `uMAC` defect: valid records
spanned `0.15148` through `284.59822`, while malformed `APPCAST_REQ` records
used timestamps near `-16071974355`. The rebuilt tool now ignores negative
pre-start records and selects the valid range without `--start`.

The same log was rendered with no `--mission`, `--map`, or `--start`. It
discovered `alpha.moos`, selected the logged `forrest19.tif` from the nearby
MOOS-IvP data directory, and rendered from log time `0.15148`.

The render smoke test also constructs a repository-local
`maps/custom.tif`/`maps/custom.info` pair. Its `.alog` contains only
`img_file=custom.tif`; its discovered mission retains
`tiff_file=maps/custom.tif`. A zero-map-override PNG render proves the path is
resolved relative to the mission file. The install smoke test confirms the
standard `forrest19` TIFF/INFO pair is included in the install tree.

## 2026-08-01 — real-mission v0.2.0 release gate

Two existing missions were launched normally, logged, and rendered with the
same short command shape. Neither export supplied `--mission`, `--map`,
`--view`, camera, trail, grid, label, or geometry overrides:

```bash
alog2media SHADOW_SHORESIDE.alog --warp 12 \
  -o shadow-turn-north-natural.mp4

alog2media FIGURE8_SHORESIDE.alog --warp 12 \
  -o charlie-concentric-natural.mp4
```

The logs and MIT map imagery were used as local acceptance data and are not
committed. The resulting evidence was:

| Mission | Logged evidence | Output |
| --- | --- | --- |
| Shadow harness `turn_north_shadow_pass` | `MISSION_EVALUATED=true`; Abe and Ben reports show the north-turn sequence | H.264, 1280×720, 15 fps, 163 frames, 10.867 s |
| Charlie two-boat concentric figure eight | both `alpha_figure8` and `bravo_figure8` seglists plus continuing reports from both vehicles | H.264, 1280×720, 15 fps, 290 frames, 19.333 s |

The figure-eight mission's launch camera was adjusted to `zoom=1.5`,
`pan_x=88`, and `pan_y=-470` before the run. Its logged `REGION_INFO` contains
those exact values, so alog2media reproduces the improved startup scene from
the log rather than applying an exporter-only crop. Both figure eights and
both vehicles are visible in the natural output.

Representative frames were also rendered through the independently compiled
upstream `PMV_Viewer.cpp` reference. Trails and the grid were disabled in both
comparison paths only to isolate the instantaneous compositor; the natural
videos above retained mission defaults.

| Mission / log time | Changed pixels | Changed fraction | Mean absolute error |
| --- | ---: | ---: | ---: |
| Shadow / 80 s | 3,300 / 921,600 | 0.358% | 0.548 / 255 |
| Figure eight / 120 s | 0 / 921,600 | 0.000% | 0.000 / 255 |

For the figure-eight comparison, both reference and alog2media frames passed
through the same H.264/yuv420p encoder before decoding. The decoded frames
were byte-identical. Comparing the raw reference directly with the decoded
video changed 5.681% of pixels with mean absolute error 2.295, demonstrating
that the larger raw comparison delta came from lossy video encoding rather
than a scene-compositor discrepancy.

## 2026-07-31 — macOS Apple Silicon

Environment:

- MOOS-IvP revision `b4a6162b018cde48279659c8b595594990a29086`
- AppleClang 21.0.0
- CMake 4.3.2
- FLTK 1.4.4
- FreeType 2.14.3
- libtiff 4.7.1
- FFmpeg from `/opt/homebrew/bin/ffmpeg`
- renderer backend: `cgl-fbo`

### Automated results

The current C++ suite completed with:

```text
100% tests passed, 0 tests failed out of 9
```

The nine tests are option parsing, complete help startup, version startup,
timeline parsing, geometry replay, mission configuration, end-to-end render
smoke, upstream pMarineViewer frame parity, and an install-tree executable
smoke test. The proof-helper unit suite completed with ten tests passing.

The current full contract passed all of the following:

- the headline zero-option `alog2media INPUT.alog` command resolved the logged
  map, used the documented default filename, 1280×720 dimensions, and 15 fps,
  and produced multiple frames;
- source `.alog`, `.moos`, `.tif`, and `.tiff` paths contained spaces and
  Unicode, were read-only, and were byte-for-byte/mode-for-mode unchanged;
- no `_alvtmp` cache path appeared;
- MP4 was H.264/yuv420p, 320×180, 4 fps, six frames, and 1.5 seconds;
- GIF was animated, 320×180, 4 fps, six frames, and 1.5 seconds;
- `.tif` and `.tiff` first/middle/last decoded frames were identical;
- the first and last frames differed, proving time advancement;
- grid on/off differed in 22,346 of 57,600 pixels;
- labels on/off differed in 21,256 of 57,600 pixels;
- full trails versus trails off differed in 870 of 57,600 pixels;
- full trails versus a 0.1-second trail differed in 870 of 57,600 pixels;
- TIFF versus mapless differed in 57,599 of 57,600 pixels;
- in fit view, mission-hidden geometry made `auto` exactly equal to forced off
  (including camera bounds), while `--geometry on` changed 55,320 of 57,600
  pixels;
- mission fallback with no `REGION_INFO` resolved a `TIFF_FILE_B`-only map,
  used the mission datum, retained a zoom-only partial camera, and honored
  mission grid/label/trail settings; individual CLI overrides changed 11,193,
  2,896, and 2,736 pixels respectively.

The committed mapless reference is
`tests/golden/mapless_scene.png`, SHA-256
`9fe04a7db070a76cf8f1c99b3dad856f6a59c3bd8a62e8cb1b3accd44622ded8`.
At the exact `t=0.5` reference timestamp, 302 of 57,600 whole-frame pixels
exceeded the 8-level per-channel tolerance and mean absolute error was
0.242876. A separate foreground mask found 845 vehicle/geometry pixels, of
which 50 varied, with foreground mean absolute error 2.522288. The enforced
whole-frame limits are 2% changed pixels and mean absolute error 2.0; the
foreground limits are 30% and 20.0. A flat-background replacement changes all
845 masked object pixels and fails even though the scene foreground is small.

### Real-mission scene example

Manual acceptance used the local `m2_berta` shoreside log and its
`meta_shoreside.moos` configuration with `forrest19.tif`. The input and map are
not committed. The command rendered 16 mission seconds at warp 4 and 8 fps to
a 640×360 MP4 and GIF:

```text
--start 100 --duration 16 --warp 4 --fps 8 --size 640x360
```

The resulting video contained 32 frames over 4 seconds. The natural render
showed the two kayaks, names, waypoint/loiter geometry, dotted trails, and the
mission map crop with no coordinate grid. The mission declares
`circle_viewable_all=false`; leaving geometry on `auto` hid the large contact
circle, while `--geometry on` restored it. At the representative middle frame,
the natural and forced-geometry renders differed in 141,150 of 230,400 pixels
(61.263%), with maximum channel delta 172 and mean absolute error 1.431.

This demonstrates that the headless output is recognizably the mission scene
and that mission visibility plus CLI override behavior is active.

### Upstream pMarineViewer reference comparison

The release adds a test-only `pmv_reference` executable that compiles the
official MOOS-IvP `PMV_Viewer.cpp` implementation directly. It feeds the
viewer a timestamped scene, renders through a separate CGL/EGL framebuffer,
and reads the resulting RGB pixels without constructing a native window or
running the FLTK event loop. This is the pMarineViewer scene compositor, not a
screen capture and not `HeadlessSceneViewer`.

The committed automated test compares two exact synthetic timestamps after
normal MP4 encoding. At an 8-level per-channel tolerance, the macOS results
were:

| Log time | Changed pixels | Changed fraction | Mean absolute error |
| --- | ---: | ---: | ---: |
| 0.5 | 1,621 / 230,400 | 0.704% | 0.533 / 255 |
| 1.5 | 1,852 / 230,400 | 0.804% | 0.557 / 255 |

Local acceptance then used the real `m2_berta` shoreside log, its
`meta_shoreside.moos`, and `forrest19.tif`. Trails were disabled in both paths
to isolate the same instantaneous scene. The upstream reference and decoded
alog2media frames measured:

| Log time | Changed pixels | Changed fraction | Mean absolute error |
| --- | ---: | ---: | ---: |
| 100 | 3,219 / 230,400 | 1.397% | 1.342 / 255 |
| 104 | 3,355 / 230,400 | 1.456% | 1.388 / 255 |
| 112 | 3,227 / 230,400 | 1.401% | 1.387 / 255 |

The small residual includes H.264 loss and rasterization differences. This
proves close parity for the tested map, camera, polygons, labels, and vehicles.
The reference driver deliberately shares `ALogTimeline` state extraction, so
parser correctness remains covered by the separate raw-log tests rather than
being claimed from this visual comparison.

### Real padded-pLogger parser example

A second acceptance render used the real `m2_berta` vehicle log whose first
numeric fields include pLogger padding (`NAV_X` is `180.00000` followed by four
spaces, with similarly padded `NAV_Y` and `NAV_HEADING`). It rendered mapless
to isolate raw timeline handling:

```text
--map none --view fit --start 40 --duration 20 --warp 4 --fps 8
--size 640x360 --trails full --grid off --geometry off
```

FFprobe reported H.264 High, 640×360, yuv420p, 8 fps, 5.0 seconds, and 40
frames. The output SHA-256 was
`821e196713ff860674de1fadb858a777b42ec309a6f7c6a01ff1a61def6a6ced`.
First-to-middle and middle-to-last comparisons changed 5,307 and 5,509 of
230,400 pixels, proving that padded real NAV samples were parsed and animated.

### Rendering fixes confirmed on macOS

- MarineViewer's FLTK text entry points are redirected to FreeType because
  stock `gl_draw` expects an FLTK window driver and crashes in a bare context.
- The adapter restores the modelview matrix-stack boundary after each frame,
  preventing overflow from an upstream offscreen-label early return.
- Explicit framebuffer dimensions avoid the 2× Retina mismatch seen in the
  initial window-backed scratch capture.
- The raw parser reads the source directly and no longer creates alogview cache
  data beside the log.

## 2026-07-31 — Ubuntu 22.04 ARM64, displayless Docker/VM

Environment:

- MOOS-IvP revision `798ef1d41c26010c9ce7d9140c2368cdd79e40a3`
- GCC 11.4.0
- Mesa surfaceless EGL with `LIBGL_ALWAYS_SOFTWARE=1`
- `DISPLAY` and `WAYLAND_DISPLAY` unset
- no Xvfb and no visible window
- renderer backend: `egl-surfaceless-fbo`

The repository was mounted read-only into the Linux container and built against
the image's prebuilt MOOS-IvP tree. The current integrated source completed:

```text
100% tests passed, 0 tests failed out of 9
10 proof-helper unit tests passed
```

The final displayless contract validated the zero-option command, read-only
Unicode/space-path inputs, absence of `_alvtmp`, installed-binary startup,
MP4/GIF rendering, exact `.tif`/`.tiff` decoded-frame equivalence, all visual
option effects, visibility-aware fit, and mission map/datum/partial-camera
fallback plus CLI overrides. The ninth CTest case also rendered and compared
the upstream pMarineViewer reference through surfaceless EGL. A previous
MOOS-IvP suffix incompatibility is
handled by temporary lowercase aliases outside the source tree, so the public
`.tiff` contract does not depend on that upstream revision.

Representative Linux decoded-frame differences were:

- labels: 20,457 / 57,600 pixels;
- grid: 21,473 / 57,600;
- full trails: 1,558 / 57,600;
- mapless versus TIFF: 57,600 / 57,600;
- mission-hidden versus forced-on fit geometry: 55,788 / 57,600;
- partial mission camera versus fit: 51,914 / 57,600.

The macOS golden produced 479 whole-frame changed pixels on Mesa EGL, mean
absolute error 1.214282. Its foreground mask retained all 845 reference object
pixels; 126 differed within the allowed cross-platform bound, with mean
absolute error 5.421696. This passed without a display server or Xvfb.

The command shape was:

```bash
env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  cmake -S /src -B /tmp/alog2media-build \
    -DMOOS_IVP_ROOT=/git/moos-ivp

env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  cmake --build /tmp/alog2media-build --parallel 4

env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  ctest --test-dir /tmp/alog2media-build --output-on-failure
```

## GitHub Actions targets

`.github/workflows/ci.yml` defines macOS 14 CGL and displayless Ubuntu 24.04
EGL jobs against the supported official MOOS-IvP commit
`174bd7340c33b43e96e1b7eb1ef57aae4df385c9`. It installs Mesa EGL and FFmpeg,
builds MOOS-IvP and alog2media without display variables, then runs CTest,
Python helpers, the pMarineViewer parity check, and the complete product
contract on both platforms. Release tags are created only from a `main` commit
whose required workflow is green; the corresponding run remains available in
the repository's Actions history.

## Current limitations and open gates

- `HeadlessSceneViewer` still derives from `MarineViewer`/`Fl_Gl_Window` as a
  state holder, although it creates no native window or event loop.
- Surfaceless EGL is the only Linux backend; OSMesa fallback is not implemented.
- One `.alog` is rendered at a time, and source community must sometimes be
  inferred because the log format no longer retains it on every message.
- Unlogged interactive pan, zoom, and visibility changes cannot be recovered.
- Font antialiasing differs slightly by platform; tolerant cross-platform
  comparisons are intentional.
- Failed encoding can still leave a partial target; atomic temporary output and
  promotion remain release hardening.
- The direct pMarineViewer comparison covers representative map, polygon,
  label, and vehicle scenes. Additional upstream `VIEW_*` families remain
  necessary before claiming exhaustive visual equivalence.
