# Changelog

## 0.3.1 - 2026-08-01

- Add source installation through `cbenjamin23/tap/alog2media`.
- Publish native Ubuntu 22.04 and 24.04 packages for amd64 and arm64 through a
  signed APT repository.
- Verify every Debian package by installing it and rendering a PNG without a
  display server, then verify installation again from the public repository.

## 0.3.0 - 2026-08-01

- Export a lossless single-frame PNG snapshot with `--at SECONDS`.
- Reject video interval and rate options for PNG instead of silently ignoring
  them, while allowing PNG dimensions that do not meet H.264's even-size rule.
- Prove PNG signatures, dimensions, timestamp selection, input immutability,
  and golden-frame fidelity in the cross-platform product contract.

## 0.2.0 - 2026-08-01

- Automatically discover `targ_shoreside.moos` for launch-time visual settings
  while retaining logged camera precedence. When a log has no `REGION_INFO`,
  also accept one unambiguous pMarineViewer mission; retain `--mission` as the
  explicit override.
- Validate zero-override exports from a Shadow harness case and a two-vehicle
  concentric figure-eight mission against the upstream pMarineViewer
  compositor.

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
