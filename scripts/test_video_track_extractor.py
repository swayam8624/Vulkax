#!/usr/bin/env python3
"""Sanity test for the dependency-light video tracker using a synthetic moving square."""

from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    extractor = root / "scripts" / "extract_video_track.py"
    with tempfile.TemporaryDirectory(prefix="vulkax-video-selftest-") as temporary:
        temporary = Path(temporary)
        frames = temporary / "frames"
        frames.mkdir()
        width, height = 160, 120
        for frame in range(24):
            pixels = bytearray(width * height)
            x0 = 10 + 3 * frame
            y0 = 50
            for y in range(y0, y0 + 14):
                for x in range(x0, min(x0 + 14, width)):
                    pixels[y * width + x] = 255
            (frames / f"frame_{frame:03d}.pgm").write_bytes(
                f"P5\n{width} {height}\n255\n".encode() + pixels
            )
        video = temporary / "synthetic.mp4"
        track = temporary / "track.csv"
        subprocess.run([
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
            "-framerate", "12", "-i", str(frames / "frame_%03d.pgm"),
            "-c:v", "libx264", "-pix_fmt", "yuv420p", str(video),
        ], check=True)
        subprocess.run([
            "python3", str(extractor), str(video), str(track),
            "--fps", "12", "--scale-width", "160", "--threshold", "20",
            "--min-component-pixels", "20", "--source", "synthetic:selftest",
        ], check=True)
        rows = [line for line in track.read_text().splitlines() if line and not line.startswith("#")][1:]
        if len(rows) != 24:
            raise SystemExit(f"expected 24 tracked rows, got {len(rows)}")
        xs = [float(row.split(",")[2]) for row in rows]
        if xs[-1] - xs[0] < 60.0:
            raise SystemExit("tracker did not follow the moving synthetic object")
        if sum(row.endswith(",validation") for row in rows) < 4:
            raise SystemExit("tracker did not emit held-out validation rows")
    print("VALID synthetic video tracking self-test")


if __name__ == "__main__":
    main()
