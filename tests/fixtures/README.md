# Synthetic fixtures

`basic.alog` and `basic.info` are original test data created for alog2media.
They contain no recorded mission or third-party map content.

The `fixture_map` test helper generates a deterministic 128×128 TIFF during the
test. The integration test exposes the same bytes under `.tif` and `.tiff`,
renders both, and compares decoded frames. Generated maps, alogview caches, and
media remain under the CMake build directory and are not source artifacts.
