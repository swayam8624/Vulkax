#!/usr/bin/env python3
"""Extract a reproducible single-point motion track from a static-camera video.

This bootstrap observation producer uses ffmpeg for grayscale frame extraction,
a temporal-median background, motion-supported foreground components, geometric
component filtering, and continuity-aware tracking. Output rows are derived image
evidence, not 3D or material ground truth.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import math
import shutil
import subprocess
import tempfile


def fail(message: str) -> "None":
    raise SystemExit(f"video track extraction FAILED: {message}")


def read_pgm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P5"):
        fail(f"expected binary PGM frame: {path}")
    index = 2

    def token() -> bytes:
        nonlocal index
        while index < len(data):
            if data[index:index + 1] == b"#":
                newline = data.find(b"\n", index)
                if newline < 0:
                    fail(f"unterminated PGM comment: {path}")
                index = newline + 1
            elif chr(data[index]).isspace():
                index += 1
            else:
                break
        begin = index
        while index < len(data) and not chr(data[index]).isspace():
            index += 1
        return data[begin:index]

    try:
        width = int(token())
        height = int(token())
        maximum = int(token())
    except ValueError as error:
        fail(f"invalid PGM header in {path}: {error}")

    # Raw PGM has a whitespace separator between maxval and the binary raster.
    # Consume that separator exactly once (or CRLF as one line ending). Never run
    # an arbitrary whitespace-skip loop here: pixel bytes are unrestricted and a
    # perfectly valid first pixel may itself be 0x09, 0x0A, 0x0D or 0x20.
    if index >= len(data) or not chr(data[index]).isspace():
        fail(f"missing PGM raster separator: {path}")
    if data[index:index + 2] == b"\r\n":
        index += 2
    else:
        index += 1

    pixels = data[index:]
    if width <= 0 or height <= 0 or maximum != 255 or len(pixels) != width * height:
        fail(f"unsupported or truncated PGM frame: {path}")
    return width, height, pixels


def temporal_background(frames: list[bytes], max_samples: int = 21) -> bytes:
    if not frames:
        fail("no frames available for background estimation")
    stride = max(1, len(frames) // max_samples)
    selected = frames[::stride][:max_samples]
    if any(len(frame) != len(selected[0]) for frame in selected):
        fail("frame dimensions changed during extraction")
    middle = len(selected) // 2
    background = bytearray(len(selected[0]))
    for index in range(len(background)):
        values = sorted(frame[index] for frame in selected)
        background[index] = values[middle]
    return bytes(background)


def components(
    frame: bytes,
    background: bytes,
    motion_reference: bytes,
    width: int,
    height: int,
    threshold: int,
    motion_threshold: int,
    min_pixels: int,
    max_component_fraction: float,
    max_aspect_ratio: float,
    min_motion_fraction: float,
) -> list[tuple[int, float, float, float, float, float]]:
    foreground = bytearray(len(frame))
    motion = bytearray(len(frame))
    delta = bytearray(len(frame))
    for index, (value, reference, prior) in enumerate(zip(frame, background, motion_reference)):
        difference = abs(value - reference)
        delta[index] = min(difference, 255)
        if difference >= threshold:
            foreground[index] = 1
        if abs(value - prior) >= motion_threshold:
            motion[index] = 1

    result: list[tuple[int, float, float, float, float, float]] = []
    stack: list[int] = []
    maximum_area = max(min_pixels, int(width * height * max_component_fraction))
    for seed in range(len(foreground)):
        if foreground[seed] == 0:
            continue
        foreground[seed] = 0
        stack.append(seed)
        area = 0
        sum_x = 0.0
        sum_y = 0.0
        sum_delta = 0.0
        motion_hits = 0
        min_x = width
        min_y = height
        max_x = 0
        max_y = 0
        while stack:
            current = stack.pop()
            y, x = divmod(current, width)
            area += 1
            sum_x += x + 0.5
            sum_y += y + 0.5
            sum_delta += delta[current]
            motion_hits += int(motion[current] != 0)
            min_x = min(min_x, x)
            min_y = min(min_y, y)
            max_x = max(max_x, x)
            max_y = max(max_y, y)
            if x > 0:
                neighbor = current - 1
                if foreground[neighbor]:
                    foreground[neighbor] = 0
                    stack.append(neighbor)
            if x + 1 < width:
                neighbor = current + 1
                if foreground[neighbor]:
                    foreground[neighbor] = 0
                    stack.append(neighbor)
            if y > 0:
                neighbor = current - width
                if foreground[neighbor]:
                    foreground[neighbor] = 0
                    stack.append(neighbor)
            if y + 1 < height:
                neighbor = current + width
                if foreground[neighbor]:
                    foreground[neighbor] = 0
                    stack.append(neighbor)

        if area < min_pixels or area > maximum_area:
            continue
        box_width = max_x - min_x + 1
        box_height = max_y - min_y + 1
        short_side = max(1, min(box_width, box_height))
        long_side = max(box_width, box_height)
        aspect_ratio = long_side / short_side
        if aspect_ratio > max_aspect_ratio:
            continue
        box_area = box_width * box_height
        compactness = area / max(float(box_area), 1.0)
        motion_fraction = motion_hits / float(area)
        required_motion_hits = max(2, min_pixels // 8)
        if motion_hits < required_motion_hits or motion_fraction < min_motion_fraction:
            continue
        result.append((
            area,
            sum_x / area,
            sum_y / area,
            sum_delta / area,
            compactness,
            motion_fraction,
        ))
    return result


def select_component(
    candidates: list[tuple[int, float, float, float, float, float]],
    previous: tuple[float, float] | None,
    threshold: int,
    min_pixels: int,
    maximum_displacement: float,
) -> tuple[float, float, float] | None:
    if not candidates:
        return None

    def quality(item: tuple[int, float, float, float, float, float]) -> float:
        area, _, _, mean_delta, compactness, motion_fraction = item
        area_term = min(1.0, area / max(float(min_pixels * 8), 1.0))
        contrast_term = min(1.0, mean_delta / max(float(threshold * 2), 1.0))
        return area_term * contrast_term * math.sqrt(max(compactness, 0.0)) * math.sqrt(max(motion_fraction, 0.0))

    usable = candidates
    if previous is not None:
        px, py = previous
        usable = [
            item for item in candidates
            if math.hypot(item[1] - px, item[2] - py) <= maximum_displacement
        ]
        if not usable:
            return None

        def score(item: tuple[int, float, float, float, float, float]) -> float:
            _, x, y, _, _, _ = item
            distance = math.hypot(x - px, y - py)
            proximity = 1.0 / (1.0 + (distance / max(maximum_displacement * 0.35, 1.0)) ** 2)
            return quality(item) * proximity

        selected = max(usable, key=score)
    else:
        selected = max(usable, key=quality)

    area, x, y, mean_delta, compactness, motion_fraction = selected
    area_term = min(1.0, area / max(float(min_pixels * 8), 1.0))
    contrast_term = min(1.0, mean_delta / max(float(threshold * 2), 1.0))
    confidence = area_term * contrast_term * math.sqrt(max(compactness, 0.0)) * math.sqrt(max(motion_fraction, 0.0))
    confidence = min(1.0, max(0.05, confidence))
    return x, y, confidence


def main() -> None:
    parser = argparse.ArgumentParser(description="Extract a Vulkax single-point video motion track")
    parser.add_argument("input_video", type=Path)
    parser.add_argument("output_csv", type=Path)
    parser.add_argument("--source", default=None, help="stable source/provenance URI stored in the track")
    parser.add_argument("--fps", type=float, default=12.0)
    parser.add_argument("--scale-width", type=int, default=320)
    parser.add_argument("--threshold", type=int, default=24)
    parser.add_argument("--motion-threshold", type=int, default=10)
    parser.add_argument("--min-component-pixels", type=int, default=18)
    parser.add_argument("--max-component-fraction", type=float, default=0.02)
    parser.add_argument("--max-aspect-ratio", type=float, default=4.0)
    parser.add_argument("--min-motion-fraction", type=float, default=0.04)
    parser.add_argument("--max-displacement-fraction", type=float, default=0.12)
    parser.add_argument("--reset-after-misses", type=int, default=4)
    parser.add_argument("--validation-stride", type=int, default=5)
    parser.add_argument("--max-frames", type=int, default=0, help="0 means no explicit limit")
    args = parser.parse_args()

    if shutil.which("ffmpeg") is None:
        fail("ffmpeg is required on PATH")
    if not args.input_video.is_file():
        fail(f"input video does not exist: {args.input_video}")
    if not math.isfinite(args.fps) or args.fps <= 0:
        fail("--fps must be positive")
    if args.scale_width < 32 or args.threshold < 1 or args.threshold > 255:
        fail("invalid scale width or threshold")
    if args.motion_threshold < 1 or args.motion_threshold > 255:
        fail("invalid motion threshold")
    if args.min_component_pixels < 1 or args.validation_stride < 2 or args.max_frames < 0:
        fail("invalid tracking/split settings")
    if not (0.0 < args.max_component_fraction <= 0.25):
        fail("--max-component-fraction must lie in (0, 0.25]")
    if args.max_aspect_ratio < 1.0 or not (0.0 <= args.min_motion_fraction <= 1.0):
        fail("invalid component geometry/motion settings")
    if not (0.0 < args.max_displacement_fraction <= 1.0) or args.reset_after_misses < 1:
        fail("invalid continuity settings")

    with tempfile.TemporaryDirectory(prefix="vulkax-video-track-") as temporary:
        root = Path(temporary)
        frame_pattern = root / "frame_%06d.pgm"
        command = [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
            "-i", str(args.input_video),
            "-vf", f"fps={args.fps:.17g},scale={args.scale_width}:-2,format=gray",
        ]
        if args.max_frames:
            command.extend(["-frames:v", str(args.max_frames)])
        command.append(str(frame_pattern))
        completed = subprocess.run(command, check=False)
        if completed.returncode != 0:
            fail(f"ffmpeg frame extraction returned {completed.returncode}")

        paths = sorted(root.glob("frame_*.pgm"))
        if len(paths) < 3:
            fail("need at least three extracted frames")
        decoded = [read_pgm(path) for path in paths]
        width, height = decoded[0][0], decoded[0][1]
        frames = [item[2] for item in decoded]
        if any(item[0] != width or item[1] != height for item in decoded):
            fail("extracted video dimensions changed between frames")

        background = temporal_background(frames)
        samples: list[tuple[int, float, float, float, float, str]] = []
        previous: tuple[float, float] | None = None
        misses = 0
        maximum_displacement = math.hypot(width, height) * args.max_displacement_fraction
        for frame_index, frame in enumerate(frames):
            reference = frames[frame_index - 1] if frame_index > 0 else frames[1]
            found = select_component(
                components(
                    frame, background, reference, width, height,
                    args.threshold, args.motion_threshold, args.min_component_pixels,
                    args.max_component_fraction, args.max_aspect_ratio, args.min_motion_fraction,
                ),
                previous, args.threshold, args.min_component_pixels, maximum_displacement,
            )
            if found is None:
                misses += 1
                if misses >= args.reset_after_misses:
                    previous = None
                continue
            misses = 0
            x, y, confidence = found
            previous = (x, y)
            split = "validation" if len(samples) % args.validation_stride == args.validation_stride - 1 else "fit"
            samples.append((frame_index, frame_index / args.fps, x, y, confidence, split))

    if len(samples) < 3:
        fail("motion tracker produced fewer than three usable observations")

    args.output_csv.parent.mkdir(parents=True, exist_ok=True)
    source = args.source or args.input_video.resolve().as_uri()
    with args.output_csv.open("w", encoding="utf-8", newline="\n") as output:
        output.write("# vulkax_video_point_track_v1\n")
        output.write(f"# source={source}\n")
        output.write(f"# width_pixels={width}\n")
        output.write(f"# height_pixels={height}\n")
        output.write(f"# nominal_fps={args.fps:.17g}\n")
        output.write("frame_index,time_seconds,x_pixels,y_pixels,confidence,split\n")
        for frame_index, time_seconds, x, y, confidence, split in samples:
            output.write(f"{frame_index},{time_seconds:.17g},{x:.17g},{y:.17g},{confidence:.17g},{split}\n")
    print(f"VALID video point track: {len(samples)} samples, {width}x{height} @ {args.fps:g} fps -> {args.output_csv}")


if __name__ == "__main__":
    main()
