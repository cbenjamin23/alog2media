# Changelog

## Unreleased

- Accept an explicit `.alog` in any argument position.
- With no input argument, discover the latest unambiguous scene-bearing `.alog`
  within the current mission tree without relying on log filename prefixes.
- Reject ambiguous latest-run sets and automatically selected logs that are
  still changing.
- Do not mistake shared pMarineViewer commands in vehicle logs for
  authoritative `REGION_INFO` scene evidence.
- Reproduce the original launch speed from a discovered mission's
  `MOOSTimeWarp`; retain explicit `--warp` precedence and warn when falling
  back to `1`.

## 0.3.3 - 2026-08-03

- Keep the coordinate grid off by default; use `--grid auto` to follow a
  mission's `hash_viewable` setting or `--grid on` to force it on.
- Apply the first logged vehicle type, color, and length to earlier navigation
  samples, matching alogview metadata discovery and preventing an oversized
  fallback hull before the first node report.
- Correct the documented full-log estimates to distinguish the 30 fps
  benchmark from the 15 fps product default, and add measured alogview
  playback comparisons.

## 0.3.2 - 2026-08-03

- Ignore invalid negative pre-start records when calculating the default media
  range, including malformed `uMAC` appcast requests from warped launches.
- Discover one unambiguous pMarineViewer mission even when `REGION_INFO` is
  present, allowing custom map paths to resolve relative to their `.moos` file.
- Search nearby MOOS-IvP data directories and `IVP_IMAGE_DIRS`, and install the
  standard MOOS-IvP maps with binary packages.
- Explain `--mission` and `--map` recovery directly when a custom map's mission
  is outside the bounded automatic discovery layout.

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
