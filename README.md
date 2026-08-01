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

The current release is `v0.3.1`. It is a native C++/CMake package. The
supported dependency baseline is official MOOS-IvP commit
`174bd7340c33b43e96e1b7eb1ef57aae4df385c9`; CI validates macOS and
displayless Linux against that revision.

## Natural mission rendering

By default, alog2media reproduces the mission's configured startup scene:

1. TIFF map, or a mapless coordinate plane
2. optional coordinate grid
3. active logged `VIEW_*` geometry, operation area, datum, and drop points
4. vehicle trails
5. vehicle bodies and names

It uses the map and startup camera recorded in `REGION_INFO`. It also searches
beside the log and in its parent mission directory for the ordinary
`targ_shoreside.moos`, importing launch-time visibility, vehicle, trail, and
geometry settings. This makes the normal layout work without `--mission`:

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
--grid auto|on|off        Follow mission config or override the grid.
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
git clone --branch v0.3.1 https://github.com/cbenjamin23/alog2media.git
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
its linked MOOSGeodesy shared library with a relative runtime search path.
Runtime still needs its platform graphics libraries, FLTK, FreeType, libtiff,
a bold sans-serif system font, FFmpeg, and any map files referenced by a log.

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
