#!/usr/bin/env python3
"""Public launcher for the Vulkax interactive viewer MVP.

This wraps the core generator with presentation/runtime hardening that is easier to
iterate independently from the scene-data compiler: Retina-safe point sizing and
explicit DOM bindings for consistent Chrome/Safari behavior.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import build_interactive_viewer as core


def harden_html(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    # The shader already performs perspective scaling by viewport height/q.w. Keep
    # this multiplier in world-space scale territory; large values saturate the
    # implementation point-size limit on Retina displays and turn splats into blobs.
    text = text.replace("g?cam.splat*880:Math.max(5,cam.splat*16)", "g?cam.splat*.62:cam.splat*.72")

    marker = "if(!gl){document.body.innerHTML='<div style=\"padding:40px\">WebGL2 is required.</div>';throw new Error('WebGL2 unavailable')}"
    bindings = """const beforeBtn=document.getElementById('beforeBtn'),afterBtn=document.getElementById('afterBtn'),rewriteToggle=document.getElementById('rewriteToggle'),gridToggle=document.getElementById('gridToggle'),orbitToggle=document.getElementById('orbitToggle'),splatScale=document.getElementById('splatScale'),splatValue=document.getElementById('splatValue'),opacity=document.getElementById('opacity'),opacityValue=document.getElementById('opacityValue'),exposure=document.getElementById('exposure'),exposureValue=document.getElementById('exposureValue'),gaussianCount=document.getElementById('gaussianCount'),particleCount=document.getElementById('particleCount'),rewriteCount=document.getElementById('rewriteCount'),surfaceKind=document.getElementById('surfaceKind'),disp=document.getElementById('disp'),file=document.getElementById('file'),importStatus=document.getElementById('importStatus'),resetScene=document.getElementById('resetScene'),shot=document.getElementById('shot');"""
    if bindings not in text:
        if marker not in text:
            raise RuntimeError("interactive viewer template marker changed; runtime hardening refused")
        text = text.replace(marker, marker + bindings, 1)

    # The UI says drop/click, so implement actual drag/drop rather than relying only
    # on the hidden file input.
    drag_patch = """const drop=document.querySelector('.drop');['dragenter','dragover'].forEach(n=>drop.addEventListener(n,e=>{e.preventDefault();drop.style.borderColor='#79a8ff'}));['dragleave','drop'].forEach(n=>drop.addEventListener(n,e=>{e.preventDefault();drop.style.borderColor=''}));drop.addEventListener('drop',e=>{const f=e.dataTransfer.files&&e.dataTransfer.files[0];if(!f)return;const dt=new DataTransfer();dt.items.add(f);file.files=dt.files;file.dispatchEvent(new Event('change'));});"""
    anchor = "rebuild();requestAnimationFrame(frame);"
    if drag_patch not in text:
        if anchor not in text:
            raise RuntimeError("interactive viewer frame anchor changed; drag/drop patch refused")
        text = text.replace(anchor, drag_patch + anchor, 1)

    path.write_text(text, encoding="utf-8")


def build(run_dir: Path, particles_csv: Path | None, output: Path | None) -> Path:
    scene = core.build_scene(run_dir, particles_csv)
    target = output or (run_dir / "render" / "interactive" / "viewer.html")
    core.write_viewer(scene, target)
    harden_html(target)
    print("interactive_viewer_status: completed")
    print(f"interactive_viewer_output: {target}")
    print(f"interactive_viewer_gaussians: {scene['meta']['gaussians']}")
    print(f"interactive_viewer_particles: {scene['meta']['particles']}")
    print(f"interactive_viewer_surface: {scene['meta']['surfaceKind']}")
    return target


def self_test() -> None:
    core.self_test()
    import tempfile
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "viewer.html"
        path.write_text(core.HTML.replace("__SCENE_JSON__", '{"before":[],"after":[],"particles":[],"rewriteIds":[],"surface":{"positions":[],"normals":[],"colors":[],"indices":[],"kind":"none"},"meta":{}}'), encoding="utf-8")
        harden_html(path)
        text = path.read_text(encoding="utf-8")
        assert "cam.splat*.62:cam.splat*.72" in text
        assert "const beforeBtn=document.getElementById('beforeBtn')" in text
        assert "drop.addEventListener('drop'" in text
    print("interactive_viewer_app_self_test: passed")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the hardened self-contained Vulkax interactive viewer.")
    parser.add_argument("run_dir", type=Path, nargs="?")
    parser.add_argument("--particles-csv", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.run_dir is None:
        parser.error("run_dir is required unless --self-test is used")
    build(args.run_dir, args.particles_csv, args.output)


if __name__ == "__main__":
    main()
