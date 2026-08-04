# alog2media

`alog2media` renders the pMarineViewer scene from a MOOS-IvP `.alog` as an
H.264 MP4, animated GIF, or lossless PNG. It runs offscreen, so the output has
no desktop, window chrome, controls, plots, or alogview footer.

```bash
alog2media mission.alog
alog2media -o latest.mp4
alog2media mission.alog -o mission.gif
alog2media mission.alog --at 120 -o scene.png
alog2media mission.alog --start 20 --duration 30 --warp 4 -o excerpt.mp4
```

The `.alog` may appear before or after options. When omitted, alog2media finds
the latest unambiguous scene log in the current directory or up to two levels
below it, prints the selected path, and refuses ambiguous or changing logs.
An explicit path is always used directly. With no output option, the command
writes `INPUT_BASENAME.mp4`; the output suffix selects the format.

## Install

### Homebrew (macOS or Linux)

```bash
brew install cbenjamin23/tap/alog2media
```

### APT (Ubuntu 22.04 or 24.04, amd64 or arm64)

```bash
curl -fsSL https://cbenjamin23.github.io/alog2media/apt/alog2media-archive-keyring.gpg \
  | sudo tee /usr/share/keyrings/alog2media-archive-keyring.gpg >/dev/null
. /etc/os-release
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/alog2media-archive-keyring.gpg] https://cbenjamin23.github.io/alog2media/apt ${VERSION_CODENAME} main" \
  | sudo tee /etc/apt/sources.list.d/alog2media.list
sudo apt update
sudo apt install alog2media
```

Both packages install FFmpeg and the required runtime libraries. Neither
requires a MOOS-IvP source checkout at runtime.

### Build from source

Source builds require CMake 3.20+, C++17, FFmpeg with `libx264`, FLTK,
FreeType, libtiff, OpenGL, and a configured MOOS-IvP checkout. Linux also
requires EGL. The supported MOOS-IvP baseline is commit
`174bd7340c33b43e96e1b7eb1ef57aae4df385c9`.

```bash
git clone https://github.com/cbenjamin23/alog2media.git
git clone https://github.com/moos-ivp/moos-ivp.git
git -C moos-ivp checkout 174bd7340c33b43e96e1b7eb1ef57aae4df385c9
(cd moos-ivp && ./build.sh)
(cd alog2media && ./scripts/build.sh)
cmake --install alog2media/build --prefix "$HOME/.local"
```

The build script finds a sibling `moos-ivp` checkout. Otherwise pass
`-DMOOS_IVP_ROOT=/path/to/moos-ivp`. Ensure `$HOME/.local/bin` is on `PATH`
after installation.

## Scene recovery

Automatic input discovery considers regular `.alog` files regardless of their
directory or filename prefix. It groups communities by their logged
`MISSION_HASH`, ranks runs by the hash's UTC start, and uses the newest run's
unique `REGION_INFO` log as the authoritative viewport. Shared messages merely
sourced from pMarineViewer do not promote a vehicle log. Older logs without a
usable `MISSION_HASH` use a warned modification-time fallback. Symlinks are not
followed, and ambiguous choices require an explicit `.alog` path.

By default, alog2media uses the logged map, datum, startup pan/zoom, vehicles,
trails, and supported `VIEW_*` geometry. The coordinate grid is deliberately
off; use `--grid auto` to follow the mission's `hash_viewable` setting or
`--grid on` to force it.

The tool automatically looks beside the log and one directory above it for
`targ_shoreside.moos`, or for one unambiguous `.moos` file containing a
pMarineViewer block. This supports the usual layout without `--mission`:

```text
mission/
├── targ_shoreside.moos
└── XLOG_SHORESIDE.../
    └── LOG_SHORESIDE....alog
```

For custom maps, keep the TIFF and same-basename `.info` file together. A
relative path such as `tiff_file = maps/harbor.tif` is resolved relative to
the discovered mission. Standard maps are installed with the package.

Explicit CLI values override logged `REGION_INFO`, which overrides mission
settings and built-in defaults. An `.alog` cannot reproduce an interactive
pan, zoom, or visibility change that was never logged.

Video playback defaults to `MOOSTimeWarp` from the discovered mission, matching
the original launch's wall-clock speed. `--warp FACTOR` overrides it. If no
valid mission warp is available, alog2media warns and uses `1`.

## Common options

```text
-o, --output FILE         Output .mp4, .gif, or .png path.
--at SECONDS              Exact log time for a PNG snapshot.
--start SECONDS           First log time to render.
--end SECONDS             Last log time to render.
--duration SECONDS        Log-time duration after --start.
--warp FACTOR             Override log seconds per output second; otherwise
                          use mission MOOSTimeWarp, or 1 with a warning.
--fps FPS                 Output rate; default 15.
--size WIDTHxHEIGHT       Output dimensions; default 1280x720.
--mission FILE.moos       Override automatic mission discovery.
--map FILE.tif|FILE.tiff  Override the logged/configured map.
--map none                Render without a TIFF map.
--view mission|fit        Use startup view or fit visible content.
--grid auto|on|off        Default off; auto follows mission config.
--labels auto|on|off      Follow mission config or override labels.
--geometry auto|on|off    Follow mission config or override geometry.
--trails auto|off|full|S  Configured, none, full, or recent seconds.
--force                   Replace an existing output file.
```

Run `alog2media -h` for the complete reference. PNG accepts exactly one
timestamp through `--at`; video interval, FPS, and warp options do not apply.

## Rendering time

The main cost is the number of output frames:

```text
frames = ceil((end - start) / warp * fps)
```

Measured on a 10-core Apple M1 Max with FFmpeg 8.1.2, using the 284.45-second
Alpha tutorial log at 1280×720:

| Output | Media | Frames | Render time | Throughput |
| --- | ---: | ---: | ---: | ---: |
| H.264 MP4, `--warp 1` | 284.47 s | 4,267 | 14.53 s | 294 frames/s |
| H.264 MP4, `--warp 10` | 28.47 s | 427 | 1.61 s | 265 frames/s |
| Animated GIF excerpt | 10 s | 150 | 6.63 s | 23 frames/s |
| Lossless PNG | one frame | 1 | 0.23 s | n/a |

alogview draws the current interactive frame rather than exporting a file, so
watching the same log takes about 284.45 seconds at 1× or 28.45 seconds at
10×. The corresponding MP4 exports were about 18–20× faster than playback.
When a nearby mission declares `MOOSTimeWarp = 10`, the second row is now the
automatic playback default.
This compares workflows, not renderer internals: alogview does not encode
media and may draw fewer frames at the GUI refresh rate.

Hardware, resolution, scene complexity, and encoder build affect results. GIF
encoding is substantially slower than H.264 MP4.

## How it works

alog2media parses the original log directly, reconstructs scene state for each
timestamp, renders through the MOOS-IvP viewer primitives into a CGL framebuffer
on macOS or surfaceless EGL framebuffer on Linux, and streams RGB frames to
FFmpeg. It never screen-records, opens a native window, runs alogview, or
creates an `_alvtmp` cache beside the input.

See [the architecture and roadmap](docs/ARCHITECTURE.md) and
[current validation evidence](docs/VALIDATION.md) for implementation details.

## Development

```bash
./scripts/build.sh -DMOOS_IVP_ROOT=../moos-ivp
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s tests/proof -p 'test_*.py'
```

CI runs the full decoded-media contract on macOS 14/CGL and displayless Ubuntu
24.04/surfaceless EGL. Release packaging additionally verifies Homebrew on
macOS/Linux and signed APT installation on Ubuntu 22.04/24.04.

## Current limits

- One `.alog` is rendered at a time.
- Unlogged interactive camera and visibility changes are unrecoverable.
- Supported outputs are H.264 MP4, animated GIF, and lossless PNG.
- Font rasterization may differ slightly across platforms.
- Surfaceless EGL is the only Linux rendering backend; there is no OSMesa
  fallback.
- A failed FFmpeg process may leave a partial output file.

## License

`alog2media` is GPL-3.0-or-later. It links and adapts GPL-covered MOOS-IvP
viewer code. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
