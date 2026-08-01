#!/usr/bin/env python3

from __future__ import annotations

import unittest
from fractions import Fraction
from pathlib import Path

from media_assert import (
    MediaAssertionError,
    VideoInfo,
    assert_metadata,
    compare_pixels,
    compare_reference_foreground,
    decode_rgb_image,
    resolve_frame,
)


class PixelComparisonTest(unittest.TestCase):
    def test_counts_pixels_once_when_multiple_channels_change(self) -> None:
        left = bytes([0, 0, 0, 10, 20, 30])
        right = bytes([1, 2, 3, 10, 20, 31])

        difference = compare_pixels(left, right, 2, 1)

        self.assertEqual(difference.changed_pixels, 2)
        self.assertEqual(difference.changed_fraction, 1.0)
        self.assertEqual(difference.max_channel_delta, 3)
        self.assertAlmostEqual(difference.mean_absolute_error, 7 / 6)

    def test_channel_tolerance_is_inclusive(self) -> None:
        difference = compare_pixels(
            bytes([0, 0, 0]),
            bytes([2, 2, 3]),
            1,
            1,
            channel_tolerance=2,
        )
        self.assertEqual(difference.changed_pixels, 1)

    def test_rejects_wrong_buffer_size(self) -> None:
        with self.assertRaises(MediaAssertionError):
            compare_pixels(b"", b"", 1, 1)

    def test_foreground_check_rejects_flat_background_replacement(self) -> None:
        background = [20, 30, 40]
        reference = bytes(background + [200, 100, 50])
        candidate = bytes(background + background)

        difference = compare_reference_foreground(
            reference,
            candidate,
            2,
            1,
            background_tolerance=10,
            channel_tolerance=2,
        )

        self.assertEqual(difference.foreground_pixels, 1)
        self.assertEqual(difference.changed_pixels, 1)
        self.assertEqual(difference.changed_fraction, 1.0)
        self.assertEqual(difference.background_rgb, (20, 30, 40))

    def test_foreground_check_accepts_small_edge_deltas(self) -> None:
        reference = bytes([20, 30, 40, 200, 100, 50])
        candidate = bytes([20, 30, 40, 204, 104, 54])

        difference = compare_reference_foreground(
            reference,
            candidate,
            2,
            1,
            background_tolerance=10,
            channel_tolerance=4,
        )

        self.assertEqual(difference.changed_pixels, 0)


class FrameSelectionTest(unittest.TestCase):
    def test_named_and_negative_frames(self) -> None:
        self.assertEqual(resolve_frame("first", 6), 0)
        self.assertEqual(resolve_frame("middle", 6), 3)
        self.assertEqual(resolve_frame("last", 6), 5)
        self.assertEqual(resolve_frame("-2", 6), 4)

    def test_rejects_out_of_range_frame(self) -> None:
        with self.assertRaises(MediaAssertionError):
            resolve_frame("6", 6)


class ReferenceImageTest(unittest.TestCase):
    def test_missing_image_is_reported(self) -> None:
        with self.assertRaises(MediaAssertionError):
            decode_rgb_image(Path("does-not-exist.png"), 1, 1)


class MetadataTest(unittest.TestCase):
    def setUp(self) -> None:
        self.info = VideoInfo(
            path=Path("proof.mp4"),
            codec="h264",
            pixel_format="yuv420p",
            width=320,
            height=180,
            fps=Fraction(4, 1),
            frame_count=6,
            duration=1.5,
        )

    def test_accepts_matching_contract(self) -> None:
        assert_metadata(
            self.info,
            codec="h264",
            pixel_format="yuv420p",
            width=320,
            height=180,
            fps=Fraction(4, 1),
            frame_count=6,
            duration=1.5,
        )

    def test_reports_mismatch(self) -> None:
        with self.assertRaisesRegex(MediaAssertionError, "width"):
            assert_metadata(self.info, width=640)


if __name__ == "__main__":
    unittest.main()
