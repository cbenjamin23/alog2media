# alog2media

`alog2media` renders the pMarineViewer-style map scene from a MOOS-IvP
`.alog` file directly to MP4 or GIF. It does not record the desktop or include
the alogview controls in the output.

```bash
alog2media mission.alog
alog2media mission.alog -o clip.gif --start 20 --duration 30 --warp 4
alog2media mission.alog --map harbor.tiff --view fit
```

The input log is the first argument. With no other arguments, the output is
`./mission.mp4`. Run `alog2media -h` for the complete option reference.

## Current status

This repository is under active development. The first implementation uses a
native CGL framebuffer on macOS and a surfaceless EGL framebuffer on Linux,
constructs no visible FLTK window, loads mission state with MOOS-IvP's alogview
data broker, draws the shared `MarineViewer` scene, and streams RGB frames to
the `ffmpeg` executable.

The OSMesa fallback, read-only streaming `.alog` parser, mission-file
configuration import, and full cross-platform fidelity suite remain release
gates, not completed claims. See the
[implementation plan](docs/IMPLEMENTATION_PLAN.md) for the exact phases and
acceptance criteria.

## Dependencies

- CMake 3.20 or newer and a C++17 compiler
- a configured and built MOOS-IvP source checkout
- FLTK, OpenGL, and libtiff development files compatible with that checkout
- FreeType and an installed bold sans-serif font for headless labels
- FFmpeg on `PATH`, with `libx264` available for MP4 output

The map named by `REGION_INFO` must be discoverable by MOOS-IvP. An explicit
`--map` may name either a `.tif` or `.tiff` file. In both cases, the matching
same-basename `.info` file is required.

## Build

If `alog2media` and `moos-ivp` are sibling directories:

```bash
./scripts/build.sh
```

Otherwise provide the checkout explicitly:

```bash
./scripts/build.sh -DMOOS_IVP_ROOT=/path/to/moos-ivp
```

The script configures, builds, and runs the current test suite. It does not edit
your shell profile. Run the binary as `./build/alog2media`, install it with
`cmake --install build --prefix /your/prefix`, or add the build directory to
your own `PATH`.

## Fidelity contract

The default scene is the navigation viewport only: background map, vehicles,
trails, and logged `VIEW_*` geometry. The coordinate grid defaults off. Mission
view uses `REGION_INFO` map, pan, and zoom. The renderer intentionally omits
alogview's yellow debug footer and every surrounding GUI widget.

An `.alog` does not necessarily contain interactive viewer changes or all
pMarineViewer launch settings. A future `--mission FILE.moos` input will close
that gap where the original mission configuration is available. Exact fidelity
also requires the original TIFF/INFO map pair.

## Important v0.1 limitations

- macOS CGL and Linux surfaceless EGL are implemented; the planned OSMesa
  software fallback is not.
- The current alogview broker creates `<input>_alvtmp` cache data beside a log
  and inherits its restriction against whitespace in `.alog` paths.
- `--view fit` currently fits vehicle tracks; extending bounds to every
  `VIEW_*` object is planned.
- Cross-platform output is expected to be visually equivalent, but tiny text
  antialiasing differences will be compared with tolerances rather than byte
  equality.

## License

`alog2media` is licensed under GPL-3.0-or-later. It links and adapts GPL-covered
MOOS-IvP viewer code. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
