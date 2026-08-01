# Synthetic fixtures

`basic.alog` and `basic.info` are original test data created for alog2media.
They contain no recorded mission or third-party map content.

`geometry_visibility.alog`, `.info`, and `.moos` isolate mission/CLI
precedence. The mission hides logged points with `point_viewable_all=false`.
The proof test renders with `--geometry auto`, `off`, and `on`; auto must match
off exactly, and the large magenta point must only appear in the forced-on
render.

`mission.moos` is a synthetic pMarineViewer configuration used to verify
ordered parameter import, duplicate-key precedence, and global datum parsing.

`mission_fallback.alog` and `.moos` exercise end-to-end mission fallback when
the log has no `REGION_INFO`: a `TIFF_FILE_B`-only map, mission datum for a
LAT/LON report, a partial zoom-only camera, and mission-controlled grid,
labels, and trails.

The `fixture_map` test helper generates a deterministic 128×128 TIFF during the
test. The integration test exposes the same bytes under `.tif` and `.tiff`,
renders both, and compares decoded frames. Generated maps and media remain
under the CMake build directory and are not source artifacts. The proof runner
also rejects creation of any alogview cache.
