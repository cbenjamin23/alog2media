# Third-party notices

`alog2media` is designed to build against an existing MOOS-IvP checkout and
invoke an existing FFmpeg executable. Those dependencies are not vendored in
this source repository. The install target copies the linked MOOSGeodesy
shared library next to the executable's installation tree so the installed
binary retains its coordinate-conversion dependency.

## MOOS-IvP

The renderer links MOOS-IvP libraries and reuses the `MarineViewer` map,
vehicle, and geometry drawing primitives. Its own `HeadlessSceneViewer`
composes those primitives into an offscreen framebuffer and intentionally
omits alogview's controls and pan/zoom footer. It does not compile or run
`NavPlotViewer`, `ALogDataBroker`, or `SplitHandler`.

The test suite separately compiles upstream `PMV_Viewer.cpp` into a test-only
offscreen reference executable. This validates scene-compositor parity without
shipping that executable or opening a pMarineViewer window.

- Project: <https://github.com/moos-ivp/moos-ivp>
- License: GNU GPL version 3 or later for the relevant viewer/logging code;
  some component libraries use the GNU LGPL.
- Copyright and author notices remain in the dependency source files.

The first validated local dependency revision was
`b4a6162b018cde48279659c8b595594990a29086`. That revision records development
provenance. The supported `v0.3.0` build and CI baseline is official MOOS-IvP
commit `174bd7340c33b43e96e1b7eb1ef57aae4df385c9`.

## FLTK

FLTK supplies the state-holder base class and compatibility OpenGL declarations
used by the current MOOS-IvP viewer implementation. alog2media constructs no
native FLTK window and does not run the FLTK event loop.

- Project: <https://www.fltk.org/>
- License: FLTK License (LGPL-2.0 with exceptions)

## libtiff

libtiff is used through MOOS-IvP to read map imagery.

- Project: <https://libtiff.gitlab.io/libtiff/>
- License: libtiff license

## FreeType and the system font

FreeType rasterizes labels in the native headless OpenGL context. The renderer
uses a user-selected `ALOG2MEDIA_FONT` when provided; otherwise it selects an
installed system bold sans-serif font. No font file is bundled or redistributed.

- Project: <https://freetype.org/>
- License: FreeType License or GPLv2, at the user's option

## FFmpeg

FFmpeg is launched as a separate executable and is not linked or redistributed
by this repository. Users are responsible for installing a build with the
encoders they intend to use.

- Project: <https://ffmpeg.org/>
- License: depends on the installed build and enabled components
