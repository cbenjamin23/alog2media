# Proof-oriented acceptance tests

These tests exercise externally observable product contracts rather than C++
implementation details. They require a built `alog2media`, the test
`fixture_map` helper, FFmpeg, FFprobe, and Python 3. No Python packages are
required.

Run the complete proof suite with display variables absent:

```bash
env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  python3 tests/proof/run_contract.py \
    --alog2media build/alog2media \
    --fixture-map build/fixture_map \
    --require-headless-env
```

Pass `--work-dir /fresh/path` to retain rendered media and the two generated
PPM difference maps. Without it, the runner uses and removes a temporary
directory.

## Contracts proved

The runner creates an input directory and filenames containing both Unicode
and spaces. It generates maps, copies the raw `.alog`/`.moos` fixtures, removes
all write bits from every input, and snapshots names, modes, sizes, and SHA-256
content. After all renders it proves that snapshot is unchanged and that no
`*_alvtmp` path exists.

Media checks use FFprobe JSON plus decoded RGB24 frames. They assert:

- the zero-option `alog2media INPUT.alog` path resolves logged `basic.tif`,
  uses the documented output name/default dimensions/rate, and animates;
- H.264/yuv420p MP4 and animated GIF codec, pixel format, size, rate,
  duration, and frame count;
- a lossless PNG signature, odd-dimension support, and exact `--at 0.5`
  snapshot fidelity against the mapless golden frame;
- exact first/middle/last decoded-frame identity for `.tif` and `.tiff` map
  aliases;
- visible first-to-last animation;
- an exact-`t=0.5` mapless golden frame, checked both globally and through a
  foreground mask that remains sensitive to vehicle and geometry deletion;
- independent pixel differences for grid, labels, full/recent trails, and
  TIFF-versus-mapless rendering;
- under `--view fit`, mission `point_viewable_all=false` makes
  `--geometry auto` pixel-identical to forced-off geometry (including camera
  bounds), while `--geometry on` overrides it and produces a large pixel
  difference;
- without `REGION_INFO` or `--mission`, the normal
  `mission/XLOG.../LOG....alog` layout discovers its parent
  `targ_shoreside.moos`; its mission-only datum, `TIFF_FILE_B` map, partial
  zoom-only camera, grid, labels, and trails all affect the render, and their
  corresponding CLI overrides win independently.

The media helper is also usable on arbitrary outputs:

```bash
python3 tests/proof/media_assert.py metadata clip.mp4 \
  --codec h264 --pixel-format yuv420p \
  --width 320 --height 180 --fps 4 --frames 6 --duration 1.5

python3 tests/proof/media_assert.py same a.mp4 b.mp4 \
  --left-frame middle --right-frame middle

python3 tests/proof/media_assert.py different clip.mp4 clip.mp4 \
  --left-frame first --right-frame last \
  --min-different-pixels 32 --diff-ppm movement.ppm
```

Run the helper's dependency-free unit tests with:

```bash
python3 -m unittest discover -s tests/proof -p 'test_*.py'
```

The GitHub Actions workflow invokes both CTest and this suite directly. If the
proof suite is later registered with CTest, use `find_package(Python3 REQUIRED
COMPONENTS Interpreter)` and pass the two target paths with generator
expressions; the runner itself needs no CMake-generated configuration.
