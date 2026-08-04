# alog2media

`alog2media` turns a MOOS-IvP `.alog` into the pMarineViewer scene as an MP4,
animated GIF, or lossless PNG snapshot. It renders offscreen: no desktop
recording, visible window, alogview controls, or yellow camera footer.

```bash
alog2media mission.alog
alog2media mission.alog -o mission.gif
alog2media mission.alog --at 120 -o scene.png
alog2media mission.alog -o excerpt.mp4 --start 20 --duration 30 --warp 4
```

The log is always the first argument. The zero-option form writes
`./mission.mp4`; the output suffix selects MP4, GIF, or PNG. Run
`alog2media -h` for the complete, coherent option reference.

The current release is `v0.3.2`. It is a native C++/CMake package. The
supported dependency baseline is official MOOS-IvP commit
`174bd7340c33b43e96e1b7eb1ef57aae4df385c9`; CI validates macOS and
displayless Linux against that revision.

## Natural mission rendering

By default, alog2media reproduces the mission's configured startup scene with
the coordinate grid deliberately kept off:

1. TIFF map, or a mapless coordinate plane
2. optional coordinate grid when requested
3. active logged `VIEW_*` geometry, operation area, datum, and drop points
4. vehicle trails
5. vehicle bodies and names

Use `--grid auto` to honor `hash_viewable` from the mission or `--grid on` to
force the grid on. alog2media uses the map identity and startup camera recorded
in `REGION_INFO`. It also
searches beside the log and in its parent mission directory for the ordinary
`targ_shoreside.moos`, or one unambiguous `.moos` file containing a
pMarineViewer block. It imports launch-time vehicle, trail, label, and geometry
settings. This makes the normal layout work without `--mission`:

```text
mission/
├── targ_shoreside.moos
└── XLOG_SHORESIDE.../
    └── LOG_SHORESIDE....alog
```

Configuration precedence is explicit CLI options, logged `REGION_INFO`,
discovered or explicit mission settings, then pMarineViewer-compatible
defaults. Use `--mission FILE.moos` only to override discovery or supply a
mission stored elsewhere.

`REGION_INFO` stores only the active map's basename. For a repository-local
custom map such as `tiff_file = maps/harbor.tif`, alog2media recovers that
reference from the discovered mission and resolves it relative to the `.moos`
file. It also searches beside the log, nearby `ivp/data` directories,
`IVP_IMAGE_DIRS`, and the standard maps installed with alog2media. The TIFF and
its same-basename `.info` file must remain together.

An `.alog` cannot recover a camera or visibility change made interactively
after launch unless that change was logged. `--view mission` is therefore the
default; `--view fit` deliberately computes a new camera around tracks and
supported geometry.

## Useful options

```text
-o, --output FILE         Output .mp4, .gif, or .png path.
--at SECONDS              Exact log time for a PNG snapshot.
--start SECONDS           First log time to render.
--duration SECONDS        Log-time duration to render.
--warp FACTOR             Log seconds per output second.
--fps FPS                 Output frames per second.
--size WIDTHxHEIGHT       Output dimensions; default 1280x720.
--mission FILE.moos       Override automatic mission discovery.
--map FILE.tif|FILE.tiff  Override the logged/configured map.
--map none                Render without a TIFF map.
--view mission|fit        Use the startup view or fit scene content.
--grid auto|on|off        Grid is off by default; auto follows mission config.
--labels auto|on|off      Follow mission config or override labels.
--geometry auto|on|off    Follow mission config or override geometry.
--trails auto|off|full|S  Configured, none, full, or S recent seconds.
--force                   Replace an existing output file.
```

Both `.tif` and `.tiff` maps are accepted and require a same-basename `.info`
file. `--trails full` includes history only through the current frame, never
future positions. `--view fit` considers vehicles and supported geometry
active during the requested interval. PNG output contains exactly one
lossless frame; it uses `--at` rather than video interval, FPS, or warp options.

## Performance

Animation work scales mainly with the number of rendered frames:

```text
frames = ceil((end - start) / warp * fps)
```

On an Apple M1 Max, the 284.45-second Alpha tutorial log at 1280×720 produced
these measured wall-clock results with the corrected default scene:

| Output | Media | Frames | Render time | Throughput | Size |
| --- | ---: | ---: | ---: | ---: | ---: |
| H.264 MP4, default 1× | 284.47 s | 4,267 | 14.53 s | 294 frames/s | 10.13 MB |
| H.264 MP4, `--warp 10` | 28.47 s | 427 | 1.61 s | 265 frames/s | 1.29 MB |
| Animated GIF excerpt | 10 s | 150 | 6.63 s | 23 frames/s | 1.00 MB |
| Lossless PNG | one frame | 1 | 0.23 s | n/a | 1.80 MB |

alogview is interactive rather than an exporter: it draws the current screen
on demand and playback takes approximately `log duration / warp`. Watching
this Alpha log in alogview therefore takes about 284.45 seconds at 1× or 28.45
seconds at 10×. alog2media completed the corresponding MP4 exports in 14.53
and 1.61 seconds, about 19.6× and 17.7× faster than watching them. This is a
workflow comparison, not a renderer-only benchmark: alogview does not encode
media and may draw fewer frames at its GUI refresh rate.

Scene complexity, resolution, output format, encoder build, and hardware
affect throughput. GIF encoding is substantially slower than H.264 MP4.

## Install

Install with Homebrew on macOS or Linux:

```bash
brew install cbenjamin23/tap/alog2media
```

Ubuntu 22.04 and 24.04 packages are published for amd64 and arm64 through a
signed APT repository:

```bash
curl -fsSL https://cbenjamin23.github.io/alog2media/apt/alog2media-archive-keyring.gpg \
  | sudo tee /usr/share/keyrings/alog2media-archive-keyring.gpg >/dev/null
. /etc/os-release
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/alog2media-archive-keyring.gpg] https://cbenjamin23.github.io/alog2media/apt ${VERSION_CODENAME} main" \
  | sudo tee /etc/apt/sources.list.d/alog2media.list
sudo apt update
sudo apt install alog2media
```

The Homebrew formula currently builds from source. APT installs a native
binary package. Both install FFmpeg and the required runtime libraries through
their package manager; neither requires a MOOS-IvP checkout at runtime.

### Build from source

Build requirements are CMake 3.20+, a C++17 compiler, and a configured, built
MOOS-IvP checkout. FLTK, FreeType, libtiff, and OpenGL are also required;
Linux additionally uses EGL. FFmpeg with `libx264` support is required to
produce MP4.

Clone the release and supported MOOS-IvP revision:

```bash
git clone --branch v0.3.2 https://github.com/cbenjamin23/alog2media.git
git clone https://github.com/moos-ivp/moos-ivp.git
git -C moos-ivp checkout 174bd7340c33b43e96e1b7eb1ef57aae4df385c9
cd moos-ivp
./build.sh
cd ../alog2media
./scripts/build.sh
```

If the repositories are not siblings, specify the dependency:

```bash
./scripts/build.sh -DMOOS_IVP_ROOT=/path/to/moos-ivp
```

Run `./build/alog2media`, or install it on your `PATH`:

```bash
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
```

MOOS-IvP is needed while building, not as a source checkout at runtime. The
installed executable contains the statically linked IvP viewer code and ships
its linked MOOSGeodesy shared library and the standard MOOS-IvP maps. Runtime
still needs its platform graphics libraries, FLTK, FreeType, libtiff, a bold
sans-serif system font, FFmpeg, and any custom map referenced by a mission.

## Safety and verification

The input log is read directly through `std::ifstream`. alog2media does not run
alogview's splitting/indexing path, write beside the log, or create an
`_alvtmp` directory. Input paths with spaces, Unicode, and shell punctuation
are passed as process arguments rather than interpolated into shell commands.

macOS renders into a CGL framebuffer. Linux uses surfaceless EGL and is tested
with `DISPLAY` and `WAYLAND_DISPLAY` unset. To run the same proof suite:

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

Tests cover parsing and timeline order, map and mission resolution, geometry
lifecycle, option precedence, input immutability, MP4/GIF metadata, exact-time
PNG snapshots, and direct frame comparison with an independently built
upstream `PMV_Viewer.cpp` reference. GitHub Actions repeats the suite on macOS
14/CGL and Ubuntu 24.04/surfaceless EGL.

See [the validation record](docs/VALIDATION.md) for exact results and real
mission examples, and [the implementation plan](docs/IMPLEMENTATION_PLAN.md)
for remaining hardening work.

## Current limits

- One `.alog` is rendered at a time.
- Unlogged interactive pan, zoom, and visibility changes are unrecoverable.
- Output is H.264 MP4, animated GIF, or lossless PNG; additional
  containers/codecs are not yet a supported interface.
- Source community is inferred where the alog format does not retain it.
- Fonts are system-provided, so antialiasing may differ slightly by platform.
- The current state holder still inherits `Fl_Gl_Window`, but constructs no
  native window and never enters the FLTK event loop.

## License

`alog2media` is GPL-3.0-or-later. It links and adapts GPL-covered MOOS-IvP
viewer code. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
