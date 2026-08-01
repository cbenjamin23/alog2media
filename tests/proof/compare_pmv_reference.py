#!/usr/bin/env python3
"""Compare alog2media frames with an offscreen upstream PMV_Viewer render."""

from __future__ import annotations

import argparse
from pathlib import Path

from media_assert import compare_pixels, decode_rgb_frame, decode_rgb_image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("media", type=Path)
    parser.add_argument("references", nargs="+", type=Path)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--channel-tolerance", type=int, default=8)
    parser.add_argument("--max-different-fraction", type=float, default=0.025)
    parser.add_argument("--max-mae", type=float, default=2.5)
    arguments = parser.parse_args()

    for index, reference_path in enumerate(arguments.references):
        _, _, actual = decode_rgb_frame(arguments.media, index)
        reference = decode_rgb_image(
            reference_path, arguments.width, arguments.height
        )
        difference = compare_pixels(
            reference,
            actual,
            arguments.width,
            arguments.height,
            channel_tolerance=arguments.channel_tolerance,
        )
        print(
            f"frame={index} changed_pixels={difference.changed_pixels}/"
            f"{arguments.width * arguments.height} "
            f"changed_fraction={difference.changed_fraction:.8f} "
            f"max_channel_delta={difference.max_channel_delta} "
            f"mean_absolute_error={difference.mean_absolute_error:.8f}"
        )
        if difference.changed_fraction > arguments.max_different_fraction:
            raise RuntimeError(
                f"frame {index} changed fraction "
                f"{difference.changed_fraction:.8f} exceeds "
                f"{arguments.max_different_fraction:.8f}"
            )
        if difference.mean_absolute_error > arguments.max_mae:
            raise RuntimeError(
                f"frame {index} mean absolute error "
                f"{difference.mean_absolute_error:.8f} exceeds "
                f"{arguments.max_mae:.8f}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
