#!/usr/bin/env python3
"""Public launcher for the Vulkax interactive viewer MVP.

This wraps the core generator with presentation/runtime hardening that is easier to
iterate independently from the scene-data compiler: Retina-safe point sizing,
explicit DOM bindings, and the production browser-side PLY/OBJ importer.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import build_interactive_viewer as core


def _importer_source() -> str:
    path = Path(__file__).with_name("viewer_importers.js")
    if not path.exists():
        raise RuntimeError(f"interactive viewer importer module missing: {path}")
    return path.read_text(encoding="utf-8")


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

    # The core template retains its tiny MVP parser for backwards compatibility.
    # Override that handler late in the same script with the robust importer. This
    # keeps the generated viewer single-file while allowing the parser to be tested
    # independently under Node.
    importer = _importer_source()
    import_runtime = r"""
const VULKAX_IMPORT_POINT_BUDGET=250000;
function importedSceneFrom(result){
    const pts=result.points;
    return {before:pts,after:structuredClone(pts),particles:[],rewriteIds:[],surface:{positions:[],normals:[],colors:[],indices:[],kind:'none'},meta:{gaussians:pts.length,particles:0,rewriteParticles:0,maxGaussianDisplacement:0,surfaceKind:'imported_'+result.kind}};
}
function importDescription(name,result){
    let detail=`Loaded ${name}: ${result.points.length.toLocaleString()} displayed splats`;
    if(result.sourceCount!==undefined&&result.sourceCount!==result.points.length)detail+=` from ${Number(result.sourceCount).toLocaleString()} source vertices`;
    if(result.triangleCount)detail+=`, ${Number(result.triangleCount).toLocaleString()} triangles sampled`;
    if(result.format)detail+=` · ${result.format}`;
    if(result.downsampled)detail+=` · downsampled to viewer budget`;
    return detail+'.';
}
async function handleImportFile(f){
    if(!f)return;
    importStatus.classList.remove('warn');
    importStatus.textContent=`Reading ${f.name} (${(f.size/1048576).toFixed(1)} MiB)…`;
    try{
        if(f.size>VulkaxImport.MAX_FILE_BYTES)throw new Error(`File is ${(f.size/1048576).toFixed(1)} MiB; browser import limit is ${(VulkaxImport.MAX_FILE_BYTES/1048576).toFixed(0)} MiB`);
        const buffer=await f.arrayBuffer();
        const result=VulkaxImport.parseAsset(f.name,buffer,{maxPoints:VULKAX_IMPORT_POINT_BUDGET});
        scene=importedSceneFrom(result);
        rebuild();
        cam.mode='gaussian';
        document.querySelectorAll('[data-mode]').forEach(x=>x.classList.toggle('active',x.dataset.mode==='gaussian'));
        importStatus.textContent=importDescription(f.name,result);
    }catch(err){
        console.error('Vulkax import failed',err);
        importStatus.textContent='Import failed — '+(err&&err.message?err.message:String(err));
        importStatus.classList.add('warn');
    }finally{
        file.value='';
    }
}
file.onchange=e=>handleImportFile(e.target.files&&e.target.files[0]);
const importDrop=document.querySelector('.drop');
['dragenter','dragover'].forEach(name=>importDrop.addEventListener(name,e=>{e.preventDefault();e.stopPropagation();importDrop.style.borderColor='#79a8ff';importDrop.style.background='#13213a'}));
['dragleave','drop'].forEach(name=>importDrop.addEventListener(name,e=>{e.preventDefault();e.stopPropagation();importDrop.style.borderColor='';importDrop.style.background=''}));
importDrop.addEventListener('drop',e=>handleImportFile(e.dataTransfer&&e.dataTransfer.files&&e.dataTransfer.files[0]));
window.addEventListener('dragover',e=>e.preventDefault());
window.addEventListener('drop',e=>e.preventDefault());
"""

    anchor = "rebuild();requestAnimationFrame(frame);"
    injected = importer + "\n" + import_runtime
    if "VULKAX_IMPORT_POINT_BUDGET" not in text:
        if anchor not in text:
            raise RuntimeError("interactive viewer frame anchor changed; importer injection refused")
        text = text.replace(anchor, injected + "\n" + anchor, 1)

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
        path.write_text(
            core.HTML.replace(
                "__SCENE_JSON__",
                '{"before":[],"after":[],"particles":[],"rewriteIds":[],"surface":{"positions":[],"normals":[],"colors":[],"indices":[],"kind":"none"},"meta":{}}',
            ),
            encoding="utf-8",
        )
        harden_html(path)
        text = path.read_text(encoding="utf-8")
        assert "cam.splat*.62:cam.splat*.72" in text
        assert "const beforeBtn=document.getElementById('beforeBtn')" in text
        assert "VulkaxImport.parseAsset" in text
        assert "f.arrayBuffer()" in text
        assert "importDrop.addEventListener('drop'" in text
        assert "DataTransfer()" not in text
        assert "VULKAX_IMPORT_POINT_BUDGET=250000" in text
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
