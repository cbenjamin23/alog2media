# Third-party notices

`alog2media` is designed to build against an existing MOOS-IvP checkout and
invoke an existing FFmpeg executable. Those dependencies are not vendored in
this source repository.

## MOOS-IvP

The current renderer links MOOS-IvP libraries and compiles the checkout's
`app_alogview/NavPlotViewer.cpp` as a dependency source. `MediaRenderer.cpp`
adapts its protected drawing sequence to omit the alogview-only debug footer
and target an offscreen framebuffer.

- Project: <https://github.com/moos-ivp/moos-ivp>
- License: GNU GPL version 3 or later for the relevant viewer/logging code;
  some component libraries use the GNU LGPL.
- Copyright and author notices remain in the dependency source files.

The first validated local dependency revision was
`b4a6162b018cde48279659c8b595594990a29086`. That revision records development
provenance; it is not yet the portable minimum-version declaration.

## FLTK

FLTK supplies compatibility OpenGL declarations and legacy text drawing used
by the current MOOS-IvP viewer implementation.

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
