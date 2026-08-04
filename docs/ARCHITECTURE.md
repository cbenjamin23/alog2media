# Architecture and roadmap

This document covers implementation boundaries and unfinished work. User
installation and CLI guidance belong in the root [README](../README.md);
test results belong in [VALIDATION.md](VALIDATION.md).

## Product contract

`alog2media [OPTIONS] [INPUT.alog]` emits only the pMarineViewer navigation
viewport as H.264 MP4, animated GIF, or one lossless PNG frame. It requires no
visible window, desktop recording, FLTK event loop, `DISPLAY`, or
`WAYLAND_DISPLAY`.

At a given timestamp, supported scene elements should match pMarineViewer when
the map, datum, viewport, vehicle state, trail policy, logged geometry, and
visibility settings match. Window chrome, controls, plots, cursor, and
alogview's camera footer are intentionally excluded. Cross-platform pixels may
differ slightly because OpenGL and font rasterization differ.

Configuration precedence is:

1. explicit CLI options;
2. logged `REGION_INFO` map, datum, pan, and zoom;
3. an automatically discovered or explicit pMarineViewer mission;
4. alog2media defaults.

The natural defaults are the mission viewport, grid off, labels on, logged
geometry on, and configured recent trails. `--grid auto` opts into the mission
grid setting.

## Pipeline

```text
.alog (read-only) + optional .moos
                  |
                  v
       ALogTimeline / GeometryReplay
                  |
       map, camera, mission settings
                  |
                  v
          HeadlessSceneViewer
                  |
          CGL or EGL framebuffer
                  |
                  v
             RGB -> FFmpeg
                  |
             MP4 / GIF / PNG
```

- `ALogTimeline` parses timestamps, navigation, node reports, datum and
  `REGION_INFO` directly from the original log.
- `GeometryReplay` applies supported `VIEW_*` activation, replacement,
  duration, expiry, and seek behavior.
- `MissionConfig` imports pMarineViewer map, camera, vehicle, trail, label,
  grid, and geometry settings.
- `HeadlessSceneViewer` composes map, optional grid, geometry, trails,
  vehicles, and labels using MOOS-IvP drawing primitives.
- macOS renders through CGL; Linux uses surfaceless EGL. RGB frames are passed
  to FFmpeg over stdin with an argument vector, not a shell command.

The renderer still inherits `MarineViewer`/`Fl_Gl_Window` as a state holder,
but creates no native window and never enters the event loop.

## Verification boundaries

The suite combines C++ tests, encoded-media inspection, a tolerant mapless
golden, read-only input-tree checks, and exact-time comparison with a test-only
offscreen build of upstream `PMV_Viewer.cpp`. It proves representative map,
camera, vehicle, label, polygon, trail, and visibility behavior; it does not
claim byte-identical output across every driver or exhaustive support for
every upstream geometry family.

Only synthetic fixtures and reference frames are committed. Private mission
logs and third-party map imagery remain local acceptance inputs.

## Supported platforms

| Platform | Backend | Required coverage |
| --- | --- | --- |
| macOS Apple Silicon | CGL framebuffer | CTest and decoded-media contract |
| Ubuntu 24.04 | surfaceless EGL | Same tests with display variables unset |
| Homebrew macOS/Linux | platform backend | Source install and package smoke test |
| Ubuntu 22.04/24.04 | EGL | Signed APT install and headless PNG render |

CI builds against official MOOS-IvP commit
`174bd7340c33b43e96e1b7eb1ef57aae4df385c9`.

## Roadmap

1. Write encoded output to a temporary file and atomically promote it on
   success.
2. Add OSMesa fallback and sanitizer jobs.
3. Remove `Fl_Gl_Window` inheritance and bundle a versioned font for stronger
   determinism.
4. Expand coverage for unsupported `VIEW_*` families and ambiguous
   multi-community logs.
5. Test newer MOOS-IvP revisions separately before advancing the supported
   pin.
