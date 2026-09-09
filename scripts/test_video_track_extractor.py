#!/usr/bin/env python3
"""Sanity tests for the dependency-light video tracker and raw PGM parser."""

from __future__ import annotations

from pathlib import Path
import importlib.util
import subprocess
import tempfile


def load_extractor(path: Path):
    spec = importlib.util.spec_from_file_location("vulkax_extract_video_track", path)
    if spec is None or spec.loader is None:
        raise SystemExit("could not import video extractor for parser regression")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    extractor = root / "scripts" / "extract_video_track.py"
    module = load_extractor(extractor)

    with tempfile.TemporaryDirectory(prefix="vulkax-video-selftest-") as temporary:
        temporary = Path(temporary)

        # Regression for the raw-binary boundary: these first raster bytes are all
        # classified as whitespace by Python. A PGM reader must preserve them as
        # pixels rather than greedily skipping into the binary image payload.
        pgm = temporary / "whitespace_first_pixels.pgm"
        expected_pixels = bytes([0x0A, 0x20, 0x09, 0x0D, 0x7F, 0xFF])
        pgm.write_bytes(b"P5\n3 2\n255\n" + expected_pixels)
        width, height, decoded = module.read_pgm(pgm)
        if (width, height) != (3, 2) or decoded != expected_pixels:
            raise SystemExit("raw PGM parser consumed binary whitespace-valued pixel bytes")

        crlf = temporary / "crlf_separator.pgm"
        crlf_pixels = bytes([0x20, 0x01, 0x02, 0x03])
        crlf.write_bytes(b"P5\r\n2 2\r\n255\r\n" + crlf_pixels)
        width, height, decoded = module.read_pgm(crlf)
        if (width, height) != (2, 2) or decoded != crlf_pixels:
            raise SystemExit("raw PGM parser mishandled CRLF raster separator")

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
    print("VALID PGM binary-boundary and synthetic video tracking self-tests")


if __name__ == "__main__":
    main()
