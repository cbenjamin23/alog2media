# Decoded-media contract

This suite tests externally visible behavior with FFprobe and decoded RGB
frames. It requires a built `alog2media`, `fixture_map`, FFmpeg, FFprobe, and
Python 3; it has no Python package dependencies.

```bash
env -u DISPLAY -u WAYLAND_DISPLAY \
  LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless \
  python3 tests/proof/run_contract.py \
    --alog2media build/alog2media \
    --fixture-map build/fixture_map \
    --require-headless-env
```

Use `--work-dir /path` to retain generated media and difference images.

The contract verifies:

- default output name, dimensions, rate, map lookup, and animation;
- H.264/yuv420p MP4, animated GIF, and one-frame PNG metadata;
- exact PNG timestamp selection and tolerant mapless golden fidelity;
- identical decoded frames for `.tif` and `.tiff` aliases;
- independent grid, label, geometry, trail, and map effects;
- mission discovery, partial camera and datum recovery, and CLI precedence;
- grid-off default versus `--grid auto` mission import;
- unchanged read-only inputs containing spaces and Unicode, with no `_alvtmp`
  cache.

The reusable helper can inspect arbitrary output:

```bash
python3 tests/proof/media_assert.py metadata clip.mp4 \
  --codec h264 --pixel-format yuv420p \
  --width 320 --height 180 --fps 4 --frames 6 --duration 1.5
```

Run its unit tests with:

```bash
python3 -m unittest discover -s tests/proof -p 'test_*.py'
```
