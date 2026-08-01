# alog2media

`alog2media` renders the pMarineViewer navigation scene from a MOOS-IvP
`.alog` directly to MP4 or GIF. It does not record the desktop, create a
visible window, or include alogview controls in the output.

```bash
alog2media mission.alog
alog2media mission.alog --mission mission.moos
alog2media mission.alog -o clip.gif --start 20 --duration 30 --warp 4
alog2media mission.alog --map harbor.tiff --view fit --trails full
```

The `.alog` is always the first argument. With no other arguments, output is
`./mission.mp4`. Run `alog2media -h` for the complete option reference.

The current release is `v0.1.0`. It is a native C++/CMake source package, not
a Python package or prebuilt application bundle. The supported dependency
baseline is official MOOS-IvP commit
`174bd7340c33b43e96e1b7eb1ef57aae4df385c9`; macOS and displayless Linux are
validated against that exact revision in CI.

## What it reproduces

The renderer uses MOOS-IvP's map, vehicle, and geometry drawing primitives and
composes them in pMarineViewer order:

1. TIFF map (or a mapless coordinate plane)
2. optional coordinate hash/grid
3. active logged `VIEW_*` geometry, operation area, datum, and drop points
4. vehicle trails
5. vehicle bodies and names

The default is the normal mission viewport from logged `REGION_INFO`, with the
coordinate grid off, labels and logged geometry on, and the normal recent
trail. When `REGION_INFO` is absent, alog2media automatically looks beside the
log and in its parent mission directory for `targ_shoreside.moos` or one
unambiguous pMarineViewer mission. Supplying `--mission FILE.moos` overrides
discovery. Mission import includes map visibility, pan/zoom fallback, vehicle
styling, trails, and per-family geometry visibility.

Configuration precedence is:

1. explicit CLI overrides;
2. the logged `REGION_INFO` map, datum, pan, and zoom;
3. supported visual settings from an automatically discovered or explicit
   mission, with its map, datum, and launch camera used when log context is
   missing;
4. pMarineViewer-compatible defaults.

The output intentionally excludes menus, controls, cursors, log plots, window
chrome, and alogview's yellow pan/zoom footer.

## Scene options

```text
--mission FILE.moos        Override automatic pMarineViewer mission discovery.
--map FILE.tif|FILE.tiff   Override the logged/configured map.
--map none                 Render a mapless local-coordinate scene.
--view mission|fit         Use the configured viewport or fit tracks/geometry.
--grid auto|on|off         Follow mission config or override the hash grid.
--labels auto|on|off       Follow mission config or override scene labels.
--geometry auto|on|off     Follow mission config or override logged geometry.
--trails auto|off|full|S   Configured recent trail, none, full, or S seconds.
```

Both `.tif` and `.tiff` maps are accepted and require a same-basename `.info`
file. `--trails full` includes history only through the current frame—never
future positions. `--view fit` includes vehicle tracks and supported geometry
active during the requested output interval.

## Architecture and input safety

The executable parses the original `.alog` through a read-only `std::ifstream`.
It does not invoke alogview's `SplitHandler`, write indexes beside the log, or
create `<input>_alvtmp`. Paths with spaces, Unicode, and shell punctuation are
passed as process arguments, never interpolated into shell commands.

macOS renders into a CGL framebuffer. Linux uses a surfaceless EGL framebuffer
and runs with `DISPLAY` and `WAYLAND_DISPLAY` absent. FFmpeg receives RGB frames
through stdin and encodes H.264/yuv420p MP4 or an animated GIF.

## Dependencies

- CMake 3.20 or newer and a C++17 compiler
- a configured and built MOOS-IvP source checkout
- FLTK, OpenGL/EGL or macOS OpenGL, libtiff, and FreeType
- a bold sans-serif font (DejaVu Sans Bold is used in Linux CI)
- FFmpeg on `PATH`, including `libx264` for MP4

## Build

Clone the release and the supported MOOS-IvP revision:

```bash
git clone --branch v0.1.0 https://github.com/cbenjamin23/alog2media.git
git clone https://github.com/moos-ivp/moos-ivp.git
git -C moos-ivp checkout 174bd7340c33b43e96e1b7eb1ef57aae4df385c9
cd moos-ivp
./build.sh
cd ../alog2media
./scripts/build.sh
```

If the checkouts are not siblings, provide the dependency path explicitly:

```bash
./scripts/build.sh -DMOOS_IVP_ROOT=/path/to/moos-ivp
```

Run `./build/alog2media`, or install it on your `PATH`:

```bash
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
```

The install tree includes the linked MOOSGeodesy shared library and uses a
relative runtime search path; CTest verifies that the installed executable
starts outside the build tree. `v0.1.0` is distributed as source plus example
media on GitHub Releases. A one-command Homebrew formula is planned next but
is not part of this release.

## Reproducible proof suite

The C++ tests cover option parsing, raw timeline semantics, same-time ordering,
LAT/LON conversion, geometry lifecycle/replacement/expiry, mission parsing,
end-to-end media generation, and a direct scene comparison against upstream
`PMV_Viewer.cpp` rendered in a separate offscreen reference path.

The product contract then renders from a read-only directory whose filenames
contain spaces and Unicode. It verifies SHA-256/mode/tree identity, absence of
`_alvtmp`, MP4/GIF metadata, animation, exact `.tif`/`.tiff` decoded-frame
identity, a tolerant golden frame, mission/CLI precedence, and independent
grid, labels, geometry, trail, and mapless effects. It also runs the exact
zero-option command and a no-`REGION_INFO` case that discovers
`mission/XLOG.../LOG....alog`'s parent `targ_shoreside.moos` without an
explicit mission option.

```bash
./scripts/build.sh -DMOOS_IVP_ROOT=../moos-ivp

env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  python3 tests/proof/run_contract.py \
    --alog2media build/alog2media \
    --fixture-map build/fixture_map \
    --require-headless-env
```

GitHub Actions repeats this on macOS 14/CGL and Ubuntu 24.04/surfaceless EGL
against the pinned official MOOS-IvP revision. See
[validation](docs/VALIDATION.md) for exact recorded results and
[the implementation plan](docs/IMPLEMENTATION_PLAN.md) for post-release
hardening work.

## Fidelity boundaries

An `.alog` cannot reconstruct an interactive pan, zoom, or visibility change
that was never logged. `REGION_INFO` normally captures the startup viewport;
provide the original `.moos` and map pair for the closest launch-equivalent
scene. One input log is currently rendered at a time, and message community is
inferred where the alog format no longer retains it. Font antialiasing may vary
slightly across platforms, so cross-platform golden checks use tolerances.

The current headless compositor still derives from `MarineViewer`'s
`Fl_Gl_Window` state holder, although it constructs no native window and never
runs the FLTK event loop. Removing that inheritance and adding an OSMesa
fallback remain hardening work; neither is required for the tested CGL/EGL
headless paths.

## License

`alog2media` is GPL-3.0-or-later. It links and adapts GPL-covered MOOS-IvP
viewer code. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
