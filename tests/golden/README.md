# Golden frame

`mapless_scene.png` is the reviewed 320x180 RGB reference for the synthetic
`basic.alog` scene at log time 0.5, rendered with:

```text
--map none --view fit --geometry on --grid off --labels off --trails off
```

The reference intentionally excludes TIFF sampling and font rasterization. It
therefore locks the local-coordinate camera, background, vehicle body, point,
and seglist composition while remaining useful on both CGL and Mesa EGL.

The proof runner renders a dedicated one-frame clip beginning at exactly
`t=0.5`, then decodes that frame and this PNG to RGB24. It checks both the whole
frame and a foreground mask derived from pixels that differ visibly from the
dominant mapless background. The mask prevents a flat-background renderer from
passing merely because vehicles and geometry cover a small part of the image.

Small driver/codec edge differences are allowed. Whole-frame drift is limited
to 2% of pixels and mean absolute error 2.0; the foreground check separately
requires at least 600 reference object pixels and limits foreground drift to
30% of those pixels and mean absolute error 20.0.

The reference was generated on macOS Apple Silicon and visually reviewed.
macOS and Linux CI check it against the official MOOS-IvP revision pinned in
`.github/workflows/ci.yml`.
