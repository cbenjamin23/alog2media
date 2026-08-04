# Validation

This file summarizes current reproducible evidence. Exact historical output
is retained in git history and GitHub Actions rather than duplicated here.

## Supported baseline

- MOOS-IvP: `174bd7340c33b43e96e1b7eb1ef57aae4df385c9`
- macOS: CGL framebuffer, no visible window
- Linux: surfaceless EGL with `DISPLAY` and `WAYLAND_DISPLAY` unset
- Release packages: Homebrew on macOS/Linux; signed APT for Ubuntu 22.04 and
  24.04 on amd64/arm64

## Local checks

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

## What is proved

- CLI parsing, help/version startup, and timeline ordering
- rejection of invalid negative startup timestamps
- navigation and node-report replay, including metadata before the first
  node report and LAT/LON conversion
- geometry activation, replacement, duration, expiry, and backward seeking
- mission/map discovery, custom relative maps, and CLI precedence
- default grid-off behavior and explicit grid, label, geometry, and trail
  controls
- H.264/yuv420p MP4, animated GIF, and exact-time lossless PNG metadata
- animation and `.tif`/`.tiff` decoded-frame equivalence
- tolerant golden-frame and upstream pMarineViewer compositor comparisons
- byte-for-byte preservation of read-only Unicode/space-path inputs with no
  `_alvtmp` cache creation
- installation-tree, Homebrew, Debian-package, and signed-APT smoke renders

See [the proof-suite README](../tests/proof/README.md) for the external media
contract and [the golden README](../tests/golden/README.md) for image
tolerances.

## Real-mission acceptance

Private logs and third-party maps are not committed.

| Mission | Zero-override result |
| --- | --- |
| Shadow harness `turn_north_shadow_pass` | Both vehicles and the north-turn sequence rendered; mission evaluation passed |
| Charlie concentric figure eight | Both vehicles and both figure-eight seglists rendered with the logged startup camera |
| Alpha tutorial | Mission and `forrest19.tif` were discovered; malformed negative startup records were ignored; the grid remained off and the 4 m kayak was correctly scaled before its first node report |

Representative frames are also checked against a separately compiled
offscreen `PMV_Viewer.cpp` compositor. The figure-eight comparison was
byte-identical after H.264 encode/decode; other tested scenes remained within
the documented cross-platform tolerance.

The current Alpha rendering benchmark and alogview workflow comparison live in
the root [README](../README.md#rendering-time), keeping the user-facing numbers
in one place.

## Known validation boundaries

- Font antialiasing and OpenGL edge pixels can differ across platforms.
- The upstream compositor comparison covers representative scene families,
  not every pMarineViewer `VIEW_*` type.
- Unlogged interactive camera or visibility changes cannot be reconstructed.
- Surfaceless EGL is tested; OSMesa is not implemented.
