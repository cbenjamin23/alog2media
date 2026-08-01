# Validation record

This file records reproducible validation runs. Results are tied to the
dependency revision and date below; they are not evergreen claims.

## Expected local command

```bash
./scripts/build.sh -DMOOS_IVP_ROOT=../moos-ivp
./build/alog2media --help
./build/alog2media /path/to/mission.alog \
  --start 0 --duration 10 --warp 5 --fps 10 \
  --output out/smoke.mp4 --force
ffprobe -v error -show_streams -show_format out/smoke.mp4
```

## 2026-07-31 — macOS Apple Silicon

Environment:

- MOOS-IvP revision `b4a6162b018cde48279659c8b595594990a29086`
- AppleClang 21.0.0
- CMake 4.3.2
- FLTK 1.4.4
- FreeType 2.14.3
- libtiff 4.7.1
- FFmpeg from `/opt/homebrew/bin/ffmpeg`
- renderer backend reported by `--version`: `cgl-fbo`

Build and automated tests:

```text
cmake -S . -B build -DMOOS_IVP_ROOT=../moos-ivp
cmake --build build --parallel
ctest --test-dir build --output-on-failure

100% tests passed, 0 tests failed out of 4
```

The four tests cover option parsing, complete help startup, version startup,
and an end-to-end synthetic render. The integration test:

- generates an original 128×128 TIFF;
- renders the same bytes through `.tif` and `.tiff` paths;
- confirms their decoded MP4 frame hashes are identical;
- renders an animated GIF;
- checks GIF codec, 320×180 size, 4 fps, and six frames;
- keeps the alogview cache under the build directory.

Real-log acceptance used a temporary copy of the compact `s8_hotel` log from
the local MOOS-IvP checkout. The source log was not added to this repository.

```text
./build/alog2media /tmp/.../mission.alog \
  --start 14 --duration 20 --warp 4 --fps 10 --size 640x360 \
  --output out/smoke.mp4 --force
```

`ffprobe` reported:

```text
codec_name=h264
width=640
height=360
pix_fmt=yuv420p
r_frame_rate=10/1
duration=5.000000
nb_frames=50
```

Decoded first, middle, and last frames had distinct SHA-256 hashes. Visual
inspection confirmed the MIT map crop, timed survey seglists, a range pulse,
vehicle/point movement, no coordinate grid before logged survey geometry
appeared, and no alogview yellow pan/zoom footer.

A separate synthetic fit-view frame confirmed that the headless FreeType shim
draws the yellow kayak and white `alpha` vehicle label in the CGL framebuffer.

GIF acceptance reported:

```text
codec_name=gif
width=320
height=180
pix_fmt=bgra
r_frame_rate=5/1
duration=1.000000
nb_frames=5
```

Decoded first and last GIF frames had different hashes, confirming animation.

### Findings fixed during validation

- Stock FLTK `gl_draw` requires an FLTK window driver and crashes in a bare CGL
  context. The executable now interposes the two MarineViewer text entry points
  with a FreeType implementation.
- An upstream offscreen-vehicle label path returns before popping its legacy
  modelview matrix. The adapter restores its caller-owned matrix-stack boundary
  after every frame, preventing overflow during longer renders.
- Explicit framebuffer dimensions avoid the 2× Retina-size mismatch seen in
  the original FLTK-window scratch capture.

### Gates not yet passed

- Linux OSMesa fallback validation
- read-only parser with no cache beside arbitrary input logs
- paths containing whitespace and Unicode
- full pMarineViewer artifact coverage beyond alogview's current replay set
- `--mission` configuration import and geometry-complete fit bounds
- atomic temporary output followed by rename

## 2026-07-31 — Ubuntu 22.04 ARM64, display-less Docker VM

Environment:

- MOOS-IvP revision `798ef1d41c26010c9ce7d9140c2368cdd79e40a3`
- GCC 11.4.0
- Mesa surfaceless EGL with `LIBGL_ALWAYS_SOFTWARE=1`
- `DISPLAY` and `WAYLAND_DISPLAY` both unset
- no Xvfb or visible window
- renderer backend: `egl-surfaceless-fbo`

The repository was mounted into the Linux container, built against the image's
prebuilt MOOS-IvP tree, and tested with:

```text
env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  cmake -S . -B /tmp/alog2media-build -DMOOS_IVP_ROOT=/git/moos-ivp
env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  cmake --build /tmp/alog2media-build --parallel 4
env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  ctest --test-dir /tmp/alog2media-build --output-on-failure

100% tests passed, 0 tests failed out of 4
```

The same synthetic integration test used on macOS rendered MP4 and GIF through
the EGL framebuffer. It also proved identical decoded frames for explicit
`.tif` and `.tiff` inputs. This older MOOS-IvP revision exposed a suffix
compatibility gap; the renderer now supplies its own temporary lowercase
`.tif`/`.info` aliases, so the public `.tiff` contract does not depend on the
linked MOOS-IvP version.
