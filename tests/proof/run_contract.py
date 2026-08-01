#!/usr/bin/env python3
"""Run proof-oriented, end-to-end alog2media acceptance checks."""

from __future__ import annotations

import argparse
import hashlib
import os
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
from fractions import Fraction
from pathlib import Path
from typing import Sequence

from media_assert import (
    MediaAssertionError,
    PixelDiff,
    assert_metadata,
    compare_frames,
    compare_pixels,
    compare_reference_foreground,
    decode_rgb_frame,
    decode_rgb_image,
    probe_video,
    write_difference_ppm,
)


class ContractError(RuntimeError):
    """Raised when an end-to-end product contract is not satisfied."""


def _command(command: Sequence[str], *, cwd: Path | None = None) -> None:
    location = f" (cwd={cwd})" if cwd else ""
    print(f"$ {shlex.join(command)}{location}", flush=True)
    result = subprocess.run(
        list(command),
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=cwd,
    )
    if result.returncode != 0:
        raise ContractError(
            f"command failed with exit code {result.returncode}:\n"
            f"{shlex.join(command)}\n"
            f"--- stdout ---\n{result.stdout}"
            f"--- stderr ---\n{result.stderr}"
        )


def _require_executable(value: str | Path, label: str) -> str:
    text = str(value)
    candidate = Path(text)
    if candidate.parent != Path(".") or candidate.is_absolute():
        resolved = candidate.expanduser().resolve()
        if not resolved.is_file() or not os.access(resolved, os.X_OK):
            raise ContractError(f"{label} is not executable: {resolved}")
        return str(resolved)
    found = shutil.which(text)
    if not found:
        raise ContractError(f"{label} was not found on PATH: {text}")
    return found


def _copy(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise ContractError(f"fixture is missing: {source}")
    shutil.copyfile(source, destination)


def _digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _snapshot(root: Path) -> dict[str, tuple[str, int, int, str]]:
    """Capture names, types, modes, sizes, and file content (but not atime)."""

    snapshot: dict[str, tuple[str, int, int, str]] = {}
    paths = [root, *sorted(root.rglob("*"), key=lambda item: item.as_posix())]
    for path in paths:
        relative = "." if path == root else path.relative_to(root).as_posix()
        metadata = path.lstat()
        mode = stat.S_IMODE(metadata.st_mode)
        if stat.S_ISREG(metadata.st_mode):
            snapshot[relative] = ("file", mode, metadata.st_size, _digest(path))
        elif stat.S_ISDIR(metadata.st_mode):
            snapshot[relative] = ("directory", mode, 0, "")
        elif stat.S_ISLNK(metadata.st_mode):
            snapshot[relative] = ("symlink", mode, 0, os.readlink(path))
        else:
            snapshot[relative] = ("other", mode, metadata.st_size, "")
    return snapshot


def _assert_unchanged(
    before: dict[str, tuple[str, int, int, str]],
    after: dict[str, tuple[str, int, int, str]],
) -> None:
    if before == after:
        return
    added = sorted(after.keys() - before.keys())
    removed = sorted(before.keys() - after.keys())
    changed = sorted(
        key for key in before.keys() & after.keys() if before[key] != after[key]
    )
    details = []
    if added:
        details.append(f"added: {added}")
    if removed:
        details.append(f"removed: {removed}")
    if changed:
        details.append(f"changed: {changed}")
    raise ContractError("read-only input tree was mutated (" + "; ".join(details) + ")")


def _make_read_only(root: Path) -> None:
    for path in root.rglob("*"):
        if path.is_dir():
            path.chmod(0o555)
        elif path.is_file():
            path.chmod(0o444)
    root.chmod(0o555)


def _make_writable_for_cleanup(root: Path) -> None:
    if not root.exists():
        return
    root.chmod(0o755)
    for path in root.rglob("*"):
        if path.is_dir():
            path.chmod(0o755)
        elif path.is_file():
            path.chmod(0o644)


def _assert_read_only(snapshot: dict[str, tuple[str, int, int, str]]) -> None:
    writable = [name for name, value in snapshot.items() if value[1] & 0o222]
    if writable:
        raise ContractError(f"input fixture entries remain writable: {writable}")


def _assert_no_alvtmp(root: Path) -> None:
    caches = [
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.name.endswith("_alvtmp")
    ]
    if caches:
        raise ContractError(f"unexpected alogview cache paths were created: {caches}")


def _render(
    executable: str,
    alog: Path,
    map_path: Path | None,
    output: Path,
    *extra: str,
) -> None:
    command = [executable, str(alog)]
    if map_path is not None:
        command.extend(("--map", str(map_path)))
    command.extend(
        ("--output", str(output), "--size", "320x180", "--force", *extra)
    )
    _command(command)


def _assert_exact_frames(
    left: Path, right: Path, ffmpeg: str, ffprobe: str
) -> None:
    for frame in ("first", "middle", "last"):
        difference, _, _ = compare_frames(
            left,
            right,
            left_frame=frame,
            right_frame=frame,
            ffmpeg=ffmpeg,
            ffprobe=ffprobe,
        )
        if difference.changed_pixels or difference.mean_absolute_error:
            raise ContractError(
                f"decoded {frame} frames differ for {left.name} and "
                f"{right.name}: {difference}"
            )
    print(
        f"pixel proof: {left.name} and {right.name} have identical "
        "first/middle/last frames"
    )


def _assert_difference(
    difference: PixelDiff, minimum_pixels: int, description: str
) -> None:
    if difference.changed_pixels < minimum_pixels:
        raise ContractError(
            f"{description} changed only {difference.changed_pixels} pixels; "
            f"expected at least {minimum_pixels}"
        )
    print(
        f"pixel proof: {description} changed {difference.changed_pixels}/"
        f"{difference.width * difference.height} pixels "
        f"(MAE {difference.mean_absolute_error:.6f})"
    )


def _prepare_fixtures(
    fixture_map: str, fixtures: Path, input_root: Path
) -> dict[str, Path]:
    input_root.mkdir(parents=True)

    basic_alog = input_root / "raw mission α with spaces.alog"
    basic_map = input_root / "quadrant Ω map.tif"
    basic_tiff = basic_map.with_suffix(".tiff")
    basic_info = basic_map.with_suffix(".info")
    natural_map = input_root / "basic.tif"
    natural_info = natural_map.with_suffix(".info")
    _copy(fixtures / "basic.alog", basic_alog)
    _copy(fixtures / "basic.info", basic_info)
    _copy(fixtures / "basic.info", natural_info)
    _command([fixture_map, str(basic_map)])
    _command([fixture_map, str(natural_map)])
    shutil.copyfile(basic_map, basic_tiff)

    geometry_alog = input_root / "geometry mission β.alog"
    geometry_map = input_root / "geometry map δ.tif"
    geometry_info = geometry_map.with_suffix(".info")
    geometry_mission = input_root / "viewer settings γ.moos"
    _copy(fixtures / "geometry_visibility.alog", geometry_alog)
    _copy(fixtures / "geometry_visibility.info", geometry_info)
    _copy(fixtures / "geometry_visibility.moos", geometry_mission)
    _command([fixture_map, str(geometry_map)])

    mission_alog = input_root / "mission fallback η.alog"
    mission_file = input_root / "mission fallback θ.moos"
    mission_map = input_root / "mission_only.tif"
    mission_info = mission_map.with_suffix(".info")
    _copy(fixtures / "mission_fallback.alog", mission_alog)
    _copy(fixtures / "mission_fallback.moos", mission_file)
    _copy(fixtures / "basic.info", mission_info)
    _command([fixture_map, str(mission_map)])

    return {
        "basic_alog": basic_alog,
        "basic_map": basic_map,
        "basic_tiff": basic_tiff,
        "natural_map": natural_map,
        "geometry_alog": geometry_alog,
        "geometry_map": geometry_map,
        "geometry_mission": geometry_mission,
        "mission_alog": mission_alog,
        "mission_file": mission_file,
        "mission_map": mission_map,
    }


def run_contract(
    executable: str,
    fixture_map: str,
    fixtures: Path,
    work: Path,
    ffmpeg: str,
    ffprobe: str,
    require_headless_env: bool,
) -> None:
    if require_headless_env:
        present = [
            name
            for name in ("DISPLAY", "WAYLAND_DISPLAY")
            if name in os.environ
        ]
        if present:
            raise ContractError(
                "headless proof requires display variables to be absent, not empty: "
                + ", ".join(present)
            )

    input_root = work / "read-only input – naïve café"
    output_root = work / "media output ζ"
    output_root.mkdir(parents=True)
    paths = _prepare_fixtures(fixture_map, fixtures, input_root)

    _make_read_only(input_root)
    before = _snapshot(input_root)
    _assert_read_only(before)

    # The product's headline contract is a single positional argument. Keep a
    # same-directory basic.tif matching REGION_INFO and prove that the natural
    # defaults find it, select mission view, and produce the default MP4 name.
    default_output = output_root / paths["basic_alog"].with_suffix(".mp4").name
    _command([executable, str(paths["basic_alog"])], cwd=output_root)
    default_info = probe_video(default_output, ffprobe)
    assert_metadata(
        default_info,
        codec="h264",
        pixel_format="yuv420p",
        width=1280,
        height=720,
        fps=Fraction(15, 1),
    )
    if default_info.frame_count < 2:
        raise ContractError("zero-option default render did not animate")

    mp4_tif = output_root / "explicit tif.mp4"
    mp4_tiff = output_root / "explicit tiff.mp4"
    gif = output_root / "animation.gif"
    base_options = (
        "--view",
        "fit",
        "--start",
        "0",
        "--duration",
        "1.5",
        "--fps",
        "4",
        "--grid",
        "off",
        "--trails",
        "off",
    )
    _render(
        executable,
        paths["basic_alog"],
        paths["basic_map"],
        mp4_tif,
        *base_options,
    )
    _render(
        executable,
        paths["basic_alog"],
        paths["basic_tiff"],
        mp4_tiff,
        *base_options,
    )
    _render(
        executable,
        paths["basic_alog"],
        paths["basic_tiff"],
        gif,
        *base_options,
    )

    for media in (mp4_tif, mp4_tiff):
        assert_metadata(
            probe_video(media, ffprobe),
            codec="h264",
            pixel_format="yuv420p",
            width=320,
            height=180,
            fps=Fraction(4, 1),
            frame_count=6,
            duration=1.5,
        )
    assert_metadata(
        probe_video(gif, ffprobe),
        codec="gif",
        pixel_format="bgra",
        width=320,
        height=180,
        fps=Fraction(4, 1),
        frame_count=6,
        duration=1.5,
    )
    _assert_exact_frames(mp4_tif, mp4_tiff, ffmpeg, ffprobe)

    movement, movement_first, movement_last = compare_frames(
        mp4_tif,
        mp4_tif,
        left_frame="first",
        right_frame="last",
        ffmpeg=ffmpeg,
        ffprobe=ffprobe,
    )
    _assert_difference(movement, 32, "first-to-last movement")
    write_difference_ppm(
        output_root / "movement difference.ppm",
        movement_first,
        movement_last,
        movement.width,
        movement.height,
    )
    gif_movement, _, _ = compare_frames(
        gif,
        gif,
        left_frame="first",
        right_frame="last",
        ffmpeg=ffmpeg,
        ffprobe=ffprobe,
    )
    _assert_difference(gif_movement, 32, "GIF first-to-last animation")

    # Each user-facing scene switch must produce an observable, independently
    # controllable effect. Keep everything except the switch under test fixed.
    option_common = (
        "--view",
        "fit",
        "--start",
        "0",
        "--duration",
        "1.5",
        "--fps",
        "4",
        "--geometry",
        "on",
    )
    scene_control = output_root / "scene control.mp4"
    scene_labels = output_root / "scene labels on.mp4"
    scene_grid = output_root / "scene grid on.mp4"
    scene_trails = output_root / "scene full trails.mp4"
    scene_window = output_root / "scene window trails.mp4"
    scene_mapless = output_root / "scene mapless.mp4"
    _render(
        executable,
        paths["basic_alog"],
        paths["basic_map"],
        scene_control,
        *option_common,
        "--grid",
        "off",
        "--labels",
        "off",
        "--trails",
        "off",
    )
    _render(
        executable,
        paths["basic_alog"],
        paths["basic_map"],
        scene_labels,
        *option_common,
        "--grid",
        "off",
        "--labels",
        "on",
        "--trails",
        "off",
    )
    _render(
        executable,
        paths["basic_alog"],
        paths["basic_map"],
        scene_grid,
        *option_common,
        "--grid",
        "on",
        "--labels",
        "off",
        "--trails",
        "off",
    )
    _render(
        executable,
        paths["basic_alog"],
        paths["basic_map"],
        scene_trails,
        *option_common,
        "--grid",
        "off",
        "--labels",
        "off",
        "--trails",
        "full",
    )
    _render(
        executable,
        paths["basic_alog"],
        paths["basic_map"],
        scene_window,
        *option_common,
        "--grid",
        "off",
        "--labels",
        "off",
        "--trails",
        "0.1",
    )
    _render(
        executable,
        paths["basic_alog"],
        None,
        scene_mapless,
        *option_common,
        "--map",
        "none",
        "--grid",
        "off",
        "--labels",
        "off",
        "--trails",
        "off",
    )
    for candidate in (
        scene_control,
        scene_labels,
        scene_grid,
        scene_trails,
        scene_window,
        scene_mapless,
    ):
        assert_metadata(
            probe_video(candidate, ffprobe),
            codec="h264",
            pixel_format="yuv420p",
            width=320,
            height=180,
            fps=Fraction(4, 1),
            frame_count=6,
            duration=1.5,
        )
    for candidate, minimum, description in (
        (scene_labels, 1000, "labels on versus off"),
        (scene_grid, 1000, "coordinate grid on versus off"),
        (scene_trails, 100, "full trails versus off"),
        (scene_mapless, 30000, "mapless versus TIFF background"),
    ):
        difference, _, _ = compare_frames(
            scene_control,
            candidate,
            left_frame="last",
            right_frame="last",
            ffmpeg=ffmpeg,
            ffprobe=ffprobe,
        )
        _assert_difference(difference, minimum, description)
    trail_difference, _, _ = compare_frames(
        scene_trails,
        scene_window,
        left_frame="last",
        right_frame="last",
        ffmpeg=ffmpeg,
        ffprobe=ffprobe,
    )
    _assert_difference(
        trail_difference, 100, "full trails versus a 0.1-second window"
    )

    # Render the golden subject as a one-frame clip at the exact documented
    # timestamp. Do not reuse "last" from the animation above: that is t=1.25.
    golden_subject = output_root / "golden subject t0.5.mp4"
    _render(
        executable,
        paths["basic_alog"],
        None,
        golden_subject,
        "--map",
        "none",
        "--view",
        "fit",
        "--start",
        "0.5",
        "--duration",
        "0.25",
        "--fps",
        "4",
        "--geometry",
        "on",
        "--grid",
        "off",
        "--labels",
        "off",
        "--trails",
        "off",
    )
    assert_metadata(
        probe_video(golden_subject, ffprobe),
        codec="h264",
        pixel_format="yuv420p",
        width=320,
        height=180,
        fps=Fraction(4, 1),
        frame_count=1,
        duration=0.25,
    )
    golden_path = fixtures.parent / "golden" / "mapless_scene.png"
    mapless_info, _, mapless_pixels = decode_rgb_frame(
        golden_subject, "first", ffmpeg=ffmpeg, ffprobe=ffprobe
    )
    golden_pixels = decode_rgb_image(
        golden_path, mapless_info.width, mapless_info.height, ffmpeg=ffmpeg
    )
    golden_difference = compare_pixels(
        golden_pixels,
        mapless_pixels,
        mapless_info.width,
        mapless_info.height,
        channel_tolerance=8,
    )
    if (
        golden_difference.changed_fraction > 0.02
        or golden_difference.mean_absolute_error > 2.0
    ):
        raise ContractError(
            "mapless golden-frame drift exceeded tolerance: "
            f"{golden_difference}"
        )
    print(
        "golden proof: mapless frame drift "
        f"{golden_difference.changed_pixels}/{mapless_info.width * mapless_info.height} "
        f"pixels (MAE {golden_difference.mean_absolute_error:.6f})"
    )
    foreground_difference = compare_reference_foreground(
        golden_pixels,
        mapless_pixels,
        mapless_info.width,
        mapless_info.height,
        background_tolerance=24,
        channel_tolerance=8,
    )
    if (
        foreground_difference.foreground_pixels < 600
        or foreground_difference.changed_fraction > 0.30
        or foreground_difference.mean_absolute_error > 20.0
    ):
        raise ContractError(
            "mapless golden foreground drift exceeded tolerance: "
            f"{foreground_difference}"
        )
    print(
        "golden foreground proof: "
        f"{foreground_difference.foreground_pixels} vehicle/geometry pixels, "
        f"{foreground_difference.changed_pixels} changed "
        f"(MAE {foreground_difference.mean_absolute_error:.6f})"
    )

    geometry_auto = output_root / "mission geometry auto.mp4"
    geometry_off = output_root / "mission geometry forced off.mp4"
    geometry_forced = output_root / "mission geometry forced on.mp4"
    geometry_options = (
        "--mission",
        str(paths["geometry_mission"]),
        "--view",
        "fit",
        "--start",
        "0",
        "--duration",
        "1",
        "--fps",
        "2",
        "--grid",
        "off",
        "--labels",
        "off",
        "--trails",
        "off",
    )
    _render(
        executable,
        paths["geometry_alog"],
        paths["geometry_map"],
        geometry_auto,
        *geometry_options,
        "--geometry",
        "auto",
    )
    _render(
        executable,
        paths["geometry_alog"],
        paths["geometry_map"],
        geometry_off,
        *geometry_options,
        "--geometry",
        "off",
    )
    _render(
        executable,
        paths["geometry_alog"],
        paths["geometry_map"],
        geometry_forced,
        *geometry_options,
        "--geometry",
        "on",
    )
    for media in (geometry_auto, geometry_off, geometry_forced):
        assert_metadata(
            probe_video(media, ffprobe),
            codec="h264",
            pixel_format="yuv420p",
            width=320,
            height=180,
            fps=Fraction(2, 1),
            frame_count=2,
            duration=1.0,
        )
    _assert_exact_frames(geometry_auto, geometry_off, ffmpeg, ffprobe)
    geometry_difference, auto_frame, forced_frame = compare_frames(
        geometry_auto,
        geometry_forced,
        left_frame="last",
        right_frame="last",
        ffmpeg=ffmpeg,
        ffprobe=ffprobe,
    )
    _assert_difference(
        geometry_difference,
        1000,
        "mission-auto versus forced-on geometry",
    )
    write_difference_ppm(
        output_root / "geometry difference.ppm",
        auto_frame,
        forced_frame,
        geometry_difference.width,
        geometry_difference.height,
    )

    # With no REGION_INFO, the mission supplies map, datum, partial camera,
    # and natural visual defaults. Exercise each import and its CLI override.
    mission_auto = output_root / "mission fallback auto.mp4"
    mission_mapless = output_root / "mission fallback mapless.mp4"
    mission_grid_off = output_root / "mission fallback grid off.mp4"
    mission_labels_on = output_root / "mission fallback labels on.mp4"
    mission_trails_full = output_root / "mission fallback trails full.mp4"
    mission_fit = output_root / "mission fallback fit.mp4"
    mission_common = (
        "--mission",
        str(paths["mission_file"]),
        "--start",
        "0",
        "--duration",
        "1",
        "--fps",
        "4",
        "--geometry",
        "off",
    )
    _render(
        executable,
        paths["mission_alog"],
        None,
        mission_auto,
        *mission_common,
        "--view",
        "mission",
    )
    _render(
        executable,
        paths["mission_alog"],
        None,
        mission_mapless,
        *mission_common,
        "--view",
        "mission",
        "--map",
        "none",
    )
    _render(
        executable,
        paths["mission_alog"],
        None,
        mission_grid_off,
        *mission_common,
        "--view",
        "mission",
        "--grid",
        "off",
    )
    _render(
        executable,
        paths["mission_alog"],
        None,
        mission_labels_on,
        *mission_common,
        "--view",
        "mission",
        "--labels",
        "on",
    )
    _render(
        executable,
        paths["mission_alog"],
        None,
        mission_trails_full,
        *mission_common,
        "--view",
        "mission",
        "--trails",
        "full",
    )
    _render(
        executable,
        paths["mission_alog"],
        None,
        mission_fit,
        *mission_common,
        "--view",
        "fit",
    )
    for media in (
        mission_auto,
        mission_mapless,
        mission_grid_off,
        mission_labels_on,
        mission_trails_full,
        mission_fit,
    ):
        assert_metadata(
            probe_video(media, ffprobe),
            codec="h264",
            pixel_format="yuv420p",
            width=320,
            height=180,
            fps=Fraction(4, 1),
            frame_count=4,
            duration=1.0,
        )
    for candidate, minimum, description in (
        (mission_mapless, 30000, "mission TIFF_FILE_B map versus mapless"),
        (mission_grid_off, 1000, "mission grid auto versus CLI off"),
        (mission_labels_on, 500, "mission labels off versus CLI on"),
        (mission_trails_full, 50, "mission trails off versus CLI full"),
        (mission_fit, 1000, "partial mission camera versus fit view"),
    ):
        difference, _, _ = compare_frames(
            mission_auto,
            candidate,
            left_frame="last",
            right_frame="last",
            ffmpeg=ffmpeg,
            ffprobe=ffprobe,
        )
        _assert_difference(difference, minimum, description)

    after = _snapshot(input_root)
    _assert_unchanged(before, after)
    _assert_no_alvtmp(input_root)
    print(
        "PASS: Unicode/space path remained byte-for-byte and mode-for-mode "
        "unchanged; no _alvtmp cache was created."
    )
    print(
        "PASS: zero-option defaults, MP4/GIF metadata, .tif/.tiff frame identity, animation, and "
        "mission geometry precedence are proven."
    )
    print(
        "PASS: the exact-time whole-frame/foreground golden and grid, labels, full/seconds "
        "trails, geometry, and mapless options rendered independently."
    )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--alog2media", type=Path, required=True)
    parser.add_argument("--fixture-map", type=Path, required=True)
    parser.add_argument(
        "--fixtures",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "fixtures",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        help="fresh directory to retain; omitted uses and removes a temporary directory",
    )
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument(
        "--require-headless-env",
        action="store_true",
        help="fail unless DISPLAY and WAYLAND_DISPLAY are completely unset",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    temporary: tempfile.TemporaryDirectory[str] | None = None
    input_root: Path | None = None
    try:
        executable = _require_executable(arguments.alog2media, "alog2media")
        fixture_map = _require_executable(arguments.fixture_map, "fixture_map")
        ffmpeg = _require_executable(arguments.ffmpeg, "ffmpeg")
        ffprobe = _require_executable(arguments.ffprobe, "ffprobe")
        fixtures = arguments.fixtures.expanduser().resolve()
        if arguments.work_dir:
            work = arguments.work_dir.expanduser().resolve()
            if work.exists() and any(work.iterdir()):
                raise ContractError(f"--work-dir must be empty: {work}")
            work.mkdir(parents=True, exist_ok=True)
        else:
            temporary = tempfile.TemporaryDirectory(prefix="alog2media-proof-")
            work = Path(temporary.name)
        input_root = work / "read-only input – naïve café"
        print(f"proof work directory: {work}")
        run_contract(
            executable,
            fixture_map,
            fixtures,
            work,
            ffmpeg,
            ffprobe,
            arguments.require_headless_env,
        )
        return 0
    except (ContractError, MediaAssertionError, OSError) as error:
        print(f"run_contract: {error}", file=sys.stderr)
        return 1
    finally:
        if temporary:
            if input_root:
                _make_writable_for_cleanup(input_root)
            temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
