# Changelog

## Unreleased

- Automatically discover `targ_shoreside.moos` for launch-time visual settings
  while retaining logged camera precedence. When a log has no `REGION_INFO`,
  also accept one unambiguous pMarineViewer mission; retain `--mission` as the
  explicit override.

## 0.1.0 - 2026-07-31

Initial public release.

- Render a read-only MOOS `.alog` directly to H.264 MP4 or animated GIF.
- Reproduce the supported pMarineViewer map, camera, vehicle, trail, label,
  grid, and logged `VIEW_*` scene without a visible window.
- Import supported `ProcessConfig = pMarineViewer` settings with explicit CLI
  override precedence.
- Support `.tif`, `.tiff`, mapless, mission-camera, and content-fit views.
- Render with CGL on macOS and surfaceless EGL on Linux.
- Validate the output contract on macOS and displayless Linux against pinned
  official MOOS-IvP source.
- Compare exact frames with a test-only offscreen build of upstream
  `PMV_Viewer.cpp`.
