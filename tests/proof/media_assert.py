#!/usr/bin/env python3
"""FFprobe metadata and decoded-frame pixel assertions for alog2media tests."""

from __future__ import annotations

import argparse
from collections import Counter
import json
import math
import subprocess
import sys
from dataclasses import asdict, dataclass
from fractions import Fraction
from pathlib import Path
from typing import Sequence


class MediaAssertionError(RuntimeError):
    """Raised when media does not satisfy an asserted contract."""


@dataclass(frozen=True)
class VideoInfo:
    path: Path
    codec: str
    pixel_format: str
    width: int
    height: int
    fps: Fraction
    frame_count: int
    duration: float


@dataclass(frozen=True)
class PixelDiff:
    width: int
    height: int
    changed_pixels: int
    changed_fraction: float
    max_channel_delta: int
    mean_absolute_error: float


@dataclass(frozen=True)
class ForegroundDiff:
    width: int
    height: int
    foreground_pixels: int
    changed_pixels: int
    changed_fraction: float
    max_channel_delta: int
    mean_absolute_error: float
    background_rgb: tuple[int, int, int]


def _run(command: Sequence[str], *, binary: bool = False) -> bytes | str:
    result = subprocess.run(
        list(command),
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=not binary,
    )
    if result.returncode != 0:
        stderr = (
            result.stderr.decode("utf-8", errors="replace")
            if binary
            else result.stderr
        )
        raise MediaAssertionError(
            f"command failed with exit code {result.returncode}: "
            f"{' '.join(command)}\n{stderr.strip()}"
        )
    return result.stdout


def _integer(value: object) -> int | None:
    if value in (None, "", "N/A"):
        return None
    try:
        return int(str(value))
    except ValueError:
        return None


def _number(value: object) -> float | None:
    if value in (None, "", "N/A"):
        return None
    try:
        parsed = float(str(value))
    except ValueError:
        return None
    return parsed if math.isfinite(parsed) else None


def _rate(value: object) -> Fraction | None:
    if value in (None, "", "N/A", "0/0"):
        return None
    try:
        parsed = Fraction(str(value))
    except (ValueError, ZeroDivisionError):
        return None
    return parsed if parsed > 0 else None


def probe_video(path: Path | str, ffprobe: str = "ffprobe") -> VideoInfo:
    """Return normalized metadata for the first video stream."""

    media = Path(path)
    output = _run(
        [
            ffprobe,
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-count_frames",
            "-show_entries",
            (
                "stream=codec_name,pix_fmt,width,height,r_frame_rate,"
                "avg_frame_rate,nb_frames,nb_read_frames,duration:"
                "format=duration"
            ),
            "-of",
            "json",
            str(media),
        ]
    )
    assert isinstance(output, str)
    try:
        document = json.loads(output)
        stream = document["streams"][0]
    except (json.JSONDecodeError, KeyError, IndexError, TypeError) as error:
        raise MediaAssertionError(
            f"ffprobe returned no readable video stream for {media}: {error}"
        ) from error

    fps = _rate(stream.get("avg_frame_rate")) or _rate(
        stream.get("r_frame_rate")
    )
    frame_count = _integer(stream.get("nb_read_frames")) or _integer(
        stream.get("nb_frames")
    )
    duration = _number(stream.get("duration")) or _number(
        document.get("format", {}).get("duration")
    )
    # Still-image demuxers commonly report one decoded frame and a nominal
    # frame rate, but no duration. Normalize that image to a one-frame stream
    # so decoded-pixel comparisons work for PNG proof artifacts too.
    if duration is None and frame_count == 1 and fps is not None:
        duration = float(Fraction(1, 1) / fps)
    if fps is None or frame_count is None or duration is None:
        raise MediaAssertionError(
            f"ffprobe returned incomplete timing metadata for {media}: {stream}"
        )

    return VideoInfo(
        path=media,
        codec=str(stream.get("codec_name", "")),
        pixel_format=str(stream.get("pix_fmt", "")),
        width=int(stream.get("width", 0)),
        height=int(stream.get("height", 0)),
        fps=fps,
        frame_count=frame_count,
        duration=duration,
    )


def assert_metadata(
    info: VideoInfo,
    *,
    codec: str | None = None,
    pixel_format: str | None = None,
    width: int | None = None,
    height: int | None = None,
    fps: Fraction | None = None,
    frame_count: int | None = None,
    duration: float | None = None,
    duration_tolerance: float = 0.001,
) -> None:
    """Assert selected ffprobe fields, reporting all mismatches together."""

    mismatches: list[str] = []
    expected = {
        "codec": codec,
        "pixel format": pixel_format,
        "width": width,
        "height": height,
        "fps": fps,
        "frame count": frame_count,
    }
    actual = {
        "codec": info.codec,
        "pixel format": info.pixel_format,
        "width": info.width,
        "height": info.height,
        "fps": info.fps,
        "frame count": info.frame_count,
    }
    for name, wanted in expected.items():
        if wanted is not None and actual[name] != wanted:
            mismatches.append(f"{name}: expected {wanted}, got {actual[name]}")
    if duration is not None and not math.isclose(
        info.duration, duration, rel_tol=0.0, abs_tol=duration_tolerance
    ):
        mismatches.append(
            f"duration: expected {duration} +/- {duration_tolerance}, "
            f"got {info.duration}"
        )
    if mismatches:
        raise MediaAssertionError(
            f"metadata assertion failed for {info.path}:\n  "
            + "\n  ".join(mismatches)
        )


def resolve_frame(frame: str | int, frame_count: int) -> int:
    """Resolve first/middle/last, a positive index, or a negative index."""

    if frame_count <= 0:
        raise MediaAssertionError("cannot select a frame from an empty stream")
    if isinstance(frame, str):
        names = {"first": 0, "middle": frame_count // 2, "last": frame_count - 1}
        if frame in names:
            return names[frame]
        try:
            index = int(frame)
        except ValueError as error:
            raise MediaAssertionError(
                f"frame must be first, middle, last, or an integer; got {frame!r}"
            ) from error
    else:
        index = frame
    if index < 0:
        index += frame_count
    if index < 0 or index >= frame_count:
        raise MediaAssertionError(
            f"frame index {index} is outside a {frame_count}-frame stream"
        )
    return index


def decode_rgb_frame(
    path: Path | str,
    frame: str | int,
    *,
    ffmpeg: str = "ffmpeg",
    ffprobe: str = "ffprobe",
) -> tuple[VideoInfo, int, bytes]:
    """Decode one selected frame to tightly packed RGB24 bytes."""

    info = probe_video(path, ffprobe)
    index = resolve_frame(frame, info.frame_count)
    output = _run(
        [
            ffmpeg,
            "-v",
            "error",
            "-nostdin",
            "-i",
            str(info.path),
            "-map",
            "0:v:0",
            "-vf",
            f"select=eq(n\\,{index})",
            "-frames:v",
            "1",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "pipe:1",
        ],
        binary=True,
    )
    assert isinstance(output, bytes)
    expected = info.width * info.height * 3
    if len(output) != expected:
        raise MediaAssertionError(
            f"decoded frame {index} from {info.path} has {len(output)} bytes; "
            f"expected {expected} RGB24 bytes"
        )
    return info, index, output


def decode_rgb_image(
    path: Path | str,
    width: int,
    height: int,
    *,
    ffmpeg: str = "ffmpeg",
) -> bytes:
    """Decode a single reference image to tightly packed RGB24 bytes."""

    output = _run(
        [
            ffmpeg,
            "-v",
            "error",
            "-nostdin",
            "-i",
            str(path),
            "-frames:v",
            "1",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "pipe:1",
        ],
        binary=True,
    )
    assert isinstance(output, bytes)
    expected = width * height * 3
    if len(output) != expected:
        raise MediaAssertionError(
            f"decoded reference {path} has {len(output)} bytes; expected "
            f"{expected} RGB24 bytes for {width}x{height}"
        )
    return output


def compare_pixels(
    left: bytes,
    right: bytes,
    width: int,
    height: int,
    *,
    channel_tolerance: int = 0,
) -> PixelDiff:
    """Measure RGB24 differences; tolerance is applied per channel."""

    expected = width * height * 3
    if len(left) != expected or len(right) != expected:
        raise MediaAssertionError(
            f"pixel buffers must both contain {expected} bytes for "
            f"{width}x{height} RGB24"
        )
    if channel_tolerance < 0 or channel_tolerance > 255:
        raise MediaAssertionError("channel tolerance must be between 0 and 255")

    changed_pixels = 0
    maximum = 0
    absolute_sum = 0
    for offset in range(0, expected, 3):
        pixel_changed = False
        for channel in range(3):
            delta = abs(left[offset + channel] - right[offset + channel])
            absolute_sum += delta
            maximum = max(maximum, delta)
            if delta > channel_tolerance:
                pixel_changed = True
        changed_pixels += int(pixel_changed)

    pixels = width * height
    return PixelDiff(
        width=width,
        height=height,
        changed_pixels=changed_pixels,
        changed_fraction=changed_pixels / pixels,
        max_channel_delta=maximum,
        mean_absolute_error=absolute_sum / expected,
    )


def compare_reference_foreground(
    reference: bytes,
    candidate: bytes,
    width: int,
    height: int,
    *,
    background_tolerance: int = 24,
    channel_tolerance: int = 8,
) -> ForegroundDiff:
    """Compare pixels that are visibly distinct from the reference background.

    The dominant RGB value in the reference defines the flat background. This
    makes the golden check sensitive to the comparatively small vehicle and
    geometry layer instead of letting the large unchanged background dominate
    a whole-frame average.
    """

    expected = width * height * 3
    if len(reference) != expected or len(candidate) != expected:
        raise MediaAssertionError(
            f"pixel buffers must both contain {expected} bytes for "
            f"{width}x{height} RGB24"
        )
    if not 0 <= background_tolerance <= 255:
        raise MediaAssertionError(
            "background tolerance must be between 0 and 255"
        )
    if not 0 <= channel_tolerance <= 255:
        raise MediaAssertionError("channel tolerance must be between 0 and 255")

    colors = Counter(
        tuple(reference[offset : offset + 3])
        for offset in range(0, expected, 3)
    )
    background = colors.most_common(1)[0][0]
    foreground_pixels = 0
    changed_pixels = 0
    maximum = 0
    absolute_sum = 0

    for offset in range(0, expected, 3):
        reference_pixel = reference[offset : offset + 3]
        if max(
            abs(reference_pixel[channel] - background[channel])
            for channel in range(3)
        ) <= background_tolerance:
            continue

        foreground_pixels += 1
        pixel_changed = False
        for channel in range(3):
            delta = abs(
                reference_pixel[channel] - candidate[offset + channel]
            )
            absolute_sum += delta
            maximum = max(maximum, delta)
            if delta > channel_tolerance:
                pixel_changed = True
        changed_pixels += int(pixel_changed)

    if foreground_pixels == 0:
        raise MediaAssertionError(
            "reference contains no foreground pixels at the requested tolerance"
        )

    return ForegroundDiff(
        width=width,
        height=height,
        foreground_pixels=foreground_pixels,
        changed_pixels=changed_pixels,
        changed_fraction=changed_pixels / foreground_pixels,
        max_channel_delta=maximum,
        mean_absolute_error=absolute_sum / (foreground_pixels * 3),
        background_rgb=background,
    )


def compare_frames(
    left_path: Path | str,
    right_path: Path | str,
    *,
    left_frame: str | int = "first",
    right_frame: str | int = "first",
    channel_tolerance: int = 0,
    ffmpeg: str = "ffmpeg",
    ffprobe: str = "ffprobe",
) -> tuple[PixelDiff, bytes, bytes]:
    """Decode and compare one frame from each media file."""

    left_info, _, left = decode_rgb_frame(
        left_path, left_frame, ffmpeg=ffmpeg, ffprobe=ffprobe
    )
    right_info, _, right = decode_rgb_frame(
        right_path, right_frame, ffmpeg=ffmpeg, ffprobe=ffprobe
    )
    if (left_info.width, left_info.height) != (
        right_info.width,
        right_info.height,
    ):
        raise MediaAssertionError(
            "cannot compare frames with different dimensions: "
            f"{left_info.width}x{left_info.height} and "
            f"{right_info.width}x{right_info.height}"
        )
    return (
        compare_pixels(
            left,
            right,
            left_info.width,
            left_info.height,
            channel_tolerance=channel_tolerance,
        ),
        left,
        right,
    )


def write_difference_ppm(
    path: Path | str, left: bytes, right: bytes, width: int, height: int
) -> None:
    """Write an RGB PPM heat map whose channels are absolute differences."""

    difference = bytes(abs(a - b) for a, b in zip(left, right))
    Path(path).write_bytes(f"P6\n{width} {height}\n255\n".encode() + difference)


def _fraction(value: str) -> Fraction:
    try:
        return Fraction(value)
    except (ValueError, ZeroDivisionError) as error:
        raise argparse.ArgumentTypeError(f"invalid rate {value!r}") from error


def _add_tools(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    metadata = subparsers.add_parser(
        "metadata", help="inspect and optionally assert ffprobe metadata"
    )
    metadata.add_argument("media", type=Path)
    metadata.add_argument("--codec")
    metadata.add_argument("--pixel-format")
    metadata.add_argument("--width", type=int)
    metadata.add_argument("--height", type=int)
    metadata.add_argument("--fps", type=_fraction)
    metadata.add_argument("--frames", type=int)
    metadata.add_argument("--duration", type=float)
    metadata.add_argument("--duration-tolerance", type=float, default=0.001)
    metadata.add_argument("--ffprobe", default="ffprobe")

    for name, help_text in (
        ("same", "assert that two selected decoded frames are close enough"),
        ("different", "assert that two selected decoded frames differ"),
    ):
        comparison = subparsers.add_parser(name, help=help_text)
        comparison.add_argument("left", type=Path)
        comparison.add_argument("right", type=Path)
        comparison.add_argument("--left-frame", default="first")
        comparison.add_argument("--right-frame", default="first")
        comparison.add_argument("--channel-tolerance", type=int, default=0)
        comparison.add_argument("--diff-ppm", type=Path)
        _add_tools(comparison)
        if name == "same":
            comparison.add_argument("--max-different-pixels", type=int, default=0)
            comparison.add_argument(
                "--max-different-fraction", type=float, default=0.0
            )
            comparison.add_argument("--max-mae", type=float, default=0.0)
        else:
            comparison.add_argument("--min-different-pixels", type=int, default=1)
            comparison.add_argument(
                "--min-different-fraction", type=float, default=0.0
            )
            comparison.add_argument("--min-mae", type=float, default=0.0)

    return parser


def _print_diff(diff: PixelDiff) -> None:
    print(
        f"changed_pixels={diff.changed_pixels}/{diff.width * diff.height} "
        f"changed_fraction={diff.changed_fraction:.8f} "
        f"max_channel_delta={diff.max_channel_delta} "
        f"mean_absolute_error={diff.mean_absolute_error:.8f}"
    )


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        if arguments.command == "metadata":
            info = probe_video(arguments.media, arguments.ffprobe)
            assert_metadata(
                info,
                codec=arguments.codec,
                pixel_format=arguments.pixel_format,
                width=arguments.width,
                height=arguments.height,
                fps=arguments.fps,
                frame_count=arguments.frames,
                duration=arguments.duration,
                duration_tolerance=arguments.duration_tolerance,
            )
            printable = asdict(info)
            printable["path"] = str(info.path)
            printable["fps"] = str(info.fps)
            print(json.dumps(printable, sort_keys=True))
            return 0

        diff, left, right = compare_frames(
            arguments.left,
            arguments.right,
            left_frame=arguments.left_frame,
            right_frame=arguments.right_frame,
            channel_tolerance=arguments.channel_tolerance,
            ffmpeg=arguments.ffmpeg,
            ffprobe=arguments.ffprobe,
        )
        _print_diff(diff)
        if arguments.diff_ppm:
            write_difference_ppm(
                arguments.diff_ppm, left, right, diff.width, diff.height
            )

        if arguments.command == "same":
            failures = []
            if diff.changed_pixels > arguments.max_different_pixels:
                failures.append(
                    f"changed pixels {diff.changed_pixels} exceed "
                    f"{arguments.max_different_pixels}"
                )
            if diff.changed_fraction > arguments.max_different_fraction:
                failures.append(
                    f"changed fraction {diff.changed_fraction:.8f} exceeds "
                    f"{arguments.max_different_fraction}"
                )
            if diff.mean_absolute_error > arguments.max_mae:
                failures.append(
                    f"mean absolute error {diff.mean_absolute_error:.8f} "
                    f"exceeds {arguments.max_mae}"
                )
        else:
            failures = []
            if diff.changed_pixels < arguments.min_different_pixels:
                failures.append(
                    f"changed pixels {diff.changed_pixels} are below "
                    f"{arguments.min_different_pixels}"
                )
            if diff.changed_fraction < arguments.min_different_fraction:
                failures.append(
                    f"changed fraction {diff.changed_fraction:.8f} is below "
                    f"{arguments.min_different_fraction}"
                )
            if diff.mean_absolute_error < arguments.min_mae:
                failures.append(
                    f"mean absolute error {diff.mean_absolute_error:.8f} "
                    f"is below {arguments.min_mae}"
                )
        if failures:
            raise MediaAssertionError(
                "pixel assertion failed:\n  " + "\n  ".join(failures)
            )
        return 0
    except MediaAssertionError as error:
        print(f"media_assert: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
