#!/usr/bin/env python3
"""Public launcher for the Vulkax interactive viewer MVP.

This wraps the core generator with presentation/runtime hardening that is easier to
iterate independently from the scene-data compiler: Retina-safe point sizing,
explicit DOM bindings, production browser-side PLY/OBJ/glTF/GLB/image importers,
and Vulkax-compatible splat export.
"""
from __future__ import annotations

import argparse
from pathlib import Path

import build_interactive_viewer as core


def _source(name: str) -> str:
    path = Path(__file__).with_name(name)
    if not path.exists():
        raise RuntimeError(f"interactive viewer runtime module missing: {path}")
    return path.read_text(encoding="utf-8")


def harden_html(path: Path) -> None:
    text = path.read_text(encoding="utf-8")

    # The shader already performs perspective scaling by viewport height/q.w. Keep
    # this multiplier in world-space scale territory; large values saturate the
    # implementation point-size limit on Retina displays and turn splats into blobs.
    text = text.replace("g?cam.splat*880:Math.max(5,cam.splat*16)", "g?cam.splat*.62:cam.splat*.72")

    # Multiple selection lets .gltf travel with an external .bin sidecar.
    text = text.replace(
        'Drop/click ASCII .PLY or .OBJ<input id="file" type="file" accept=".ply,.obj">',
        'Drop/click 3D or image asset<input id="file" type="file" multiple accept=".ply,.obj,.glb,.gltf,.bin,.png,.jpg,.jpeg,.webp">',
    )

    marker = "if(!gl){document.body.innerHTML='<div style=\"padding:40px\">WebGL2 is required.</div>';throw new Error('WebGL2 unavailable')}"
    bindings = """const beforeBtn=document.getElementById('beforeBtn'),afterBtn=document.getElementById('afterBtn'),rewriteToggle=document.getElementById('rewriteToggle'),gridToggle=document.getElementById('gridToggle'),orbitToggle=document.getElementById('orbitToggle'),splatScale=document.getElementById('splatScale'),splatValue=document.getElementById('splatValue'),opacity=document.getElementById('opacity'),opacityValue=document.getElementById('opacityValue'),exposure=document.getElementById('exposure'),exposureValue=document.getElementById('exposureValue'),gaussianCount=document.getElementById('gaussianCount'),particleCount=document.getElementById('particleCount'),rewriteCount=document.getElementById('rewriteCount'),surfaceKind=document.getElementById('surfaceKind'),disp=document.getElementById('disp'),file=document.getElementById('file'),importStatus=document.getElementById('importStatus'),resetScene=document.getElementById('resetScene'),shot=document.getElementById('shot');"""
    if bindings not in text:
        if marker not in text:
            raise RuntimeError("interactive viewer template marker changed; runtime hardening refused")
        text = text.replace(marker, marker + bindings, 1)

    # The core template retains its tiny MVP parser for backwards compatibility.
    # Override it with robust, independently-tested modules. Everything is embedded
    # so the generated viewer remains a single local HTML file.
    importer = _source("viewer_importers.js")
    gltf_importer = _source("viewer_gltf_importer.js")
    image_importer = _source("viewer_image_importer.js")
    exporter = _source("viewer_splat_export.js")
    import_runtime = r"""
const VULKAX_IMPORT_POINT_BUDGET=250000;
const VULKAX_IMAGE_POINT_BUDGET=80000;
let currentAssetName='vulkax_scene';
function importedSceneFrom(result){
    const pts=result.points;
    return {before:pts,after:structuredClone(pts),particles:[],rewriteIds:[],surface:{positions:[],normals:[],colors:[],indices:[],kind:'none'},meta:{gaussians:pts.length,particles:0,rewriteParticles:0,maxGaussianDisplacement:0,surfaceKind:'imported_'+result.kind}};
}
function importDescription(name,result){
    let detail=`Loaded ${name}: ${result.points.length.toLocaleString()} displayed splats`;
    if(result.pixelWidth&&result.pixelHeight)detail+=` · ${result.pixelWidth}×${result.pixelHeight} 2.5D image card`;
    else if(result.sourceCount!==undefined&&result.sourceCount!==result.points.length)detail+=` from ${Number(result.sourceCount).toLocaleString()} source vertices`;
    if(result.meshCount)detail+=`, ${Number(result.meshCount).toLocaleString()} meshes`;
    if(result.primitiveCount)detail+=`, ${Number(result.primitiveCount).toLocaleString()} primitives`;
    if(result.triangleCount)detail+=`, ${Number(result.triangleCount).toLocaleString()} triangles sampled`;
    if(result.format)detail+=` · ${result.format}`;
    if(result.downsampled)detail+=` · downsampled to viewer budget`;
    if(result.presentationOnly)detail+=` · presentation-only`;
    return detail+'.';
}
function extension(name){const m=String(name||'').toLowerCase().match(/(\.[^.]+)$/);return m?m[1]:''}
function stem(name){return String(name||'vulkax_scene').replace(/\.[^.]+$/,'').replace(/[^a-zA-Z0-9._-]+/g,'_')||'vulkax_scene'}
function importPrimary(files){
    // Prefer actual 3D containers over texture/image sidecars when several files are
    // dropped together. This prevents a PNG texture from becoming the primary asset
    // of a glTF package.
    for(const ext of ['.glb','.gltf','.ply','.obj','.png','.jpg','.jpeg','.webp']){
        const found=files.find(f=>extension(f.name)===ext);if(found)return found;
    }
    throw new Error('No supported primary asset found. Use .ply, .obj, .glb, .gltf, .png, .jpg, or .webp.');
}
async function handleImportFiles(fileList){
    const files=Array.from(fileList||[]);
    if(!files.length)return;
    importStatus.classList.remove('warn');
    try{
        const primary=importPrimary(files),ext=extension(primary.name);
        importStatus.textContent=`Reading ${primary.name} (${(primary.size/1048576).toFixed(1)} MiB)…`;
        const hardLimit=Math.max(VulkaxImport.MAX_FILE_BYTES,VulkaxGltfImport.MAX_FILE_BYTES,VulkaxImageImport.MAX_FILE_BYTES);
        let totalBytes=0;
        for(const f of files){
            totalBytes+=f.size;
            if(f.size>hardLimit)throw new Error(`${f.name} is ${(f.size/1048576).toFixed(1)} MiB; per-file browser import limit is ${(hardLimit/1048576).toFixed(0)} MiB`);
        }
        if(totalBytes>hardLimit*1.5)throw new Error(`Selected package is ${(totalBytes/1048576).toFixed(1)} MiB; choose a smaller browser-import package`);

        let result;
        if(['.png','.jpg','.jpeg','.webp'].includes(ext)){
            result=await VulkaxImageImport.parseFile(primary,{maxPoints:VULKAX_IMAGE_POINT_BUDGET});
        }else{
            const resources={};
            let primaryBuffer=null;
            for(const f of files){
                const data=await f.arrayBuffer();
                if(f===primary)primaryBuffer=data;
                else{
                    resources[f.name]=data;
                    if(f.webkitRelativePath)resources[f.webkitRelativePath]=data;
                }
            }
            if(!primaryBuffer)primaryBuffer=await primary.arrayBuffer();
            result=(ext==='.glb'||ext==='.gltf')
                ? VulkaxGltfImport.parseAsset(primary.name,primaryBuffer,{maxPoints:VULKAX_IMPORT_POINT_BUDGET,resources})
                : VulkaxImport.parseAsset(primary.name,primaryBuffer,{maxPoints:VULKAX_IMPORT_POINT_BUDGET});
        }
        scene=importedSceneFrom(result);
        currentAssetName=stem(primary.name);
        rebuild();
        cam.mode='gaussian';
        document.querySelectorAll('[data-mode]').forEach(x=>x.classList.toggle('active',x.dataset.mode==='gaussian'));
        importStatus.textContent=importDescription(primary.name,result);
    }catch(err){
        console.error('Vulkax import failed',err);
        importStatus.textContent='Import failed — '+(err&&err.message?err.message:String(err));
        importStatus.classList.add('warn');
    }finally{
        file.value='';
    }
}
file.onchange=e=>handleImportFiles(e.target.files);
const importDrop=document.querySelector('.drop');
['dragenter','dragover'].forEach(name=>importDrop.addEventListener(name,e=>{e.preventDefault();e.stopPropagation();importDrop.style.borderColor='#79a8ff';importDrop.style.background='#13213a'}));
['dragleave','drop'].forEach(name=>importDrop.addEventListener(name,e=>{e.preventDefault();e.stopPropagation();importDrop.style.borderColor='';importDrop.style.background=''}));
importDrop.addEventListener('drop',e=>handleImportFiles(e.dataTransfer&&e.dataTransfer.files));
window.addEventListener('dragover',e=>e.preventDefault());
window.addEventListener('drop',e=>e.preventDefault());

const exportPlyBtn=document.createElement('button');
exportPlyBtn.className='btn';
exportPlyBtn.id='exportSplatPly';
exportPlyBtn.textContent='Export splat PLY';
shot.parentElement.appendChild(exportPlyBtn);
exportPlyBtn.onclick=()=>{
    try{
        const pts=cam.state==='before'?scene.before:scene.after;
        VulkaxSplatExport.download(pts,currentAssetName+'_vulkax_splats.ply');
        importStatus.classList.remove('warn');
        importStatus.textContent=`Exported ${pts.length.toLocaleString()} splats as Vulkax-compatible PLY. Coordinates are the current viewer/import coordinates.`;
    }catch(err){
        importStatus.textContent='Export failed — '+(err&&err.message?err.message:String(err));
        importStatus.classList.add('warn');
    }
};
const originalResetScene=resetScene.onclick;
resetScene.onclick=()=>{if(originalResetScene)originalResetScene();currentAssetName='vulkax_scene'};
importStatus.textContent='Import PLY/OBJ/GLB/GLTF or PNG/JPEG/WebP. For GLTF + external BIN, select/drop both. A single image is shown as a 2.5D splat card, not reconstructed 3D. Imported/current splats can be exported to Vulkax-compatible PLY.';
"""

    anchor = "rebuild();requestAnimationFrame(frame);"
    injected = importer + "\n" + gltf_importer + "\n" + image_importer + "\n" + exporter + "\n" + import_runtime
    if "VulkaxSplatExport.download" not in text:
        if anchor not in text:
            raise RuntimeError("interactive viewer frame anchor changed; importer/export injection refused")
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
        assert "VulkaxGltfImport.parseAsset" in text
        assert "VulkaxImageImport.parseFile" in text
        assert "VulkaxSplatExport.download" in text
        assert "Export splat PLY" in text
        assert "handleImportFiles" in text
        assert "e.dataTransfer&&e.dataTransfer.files" in text
        assert 'multiple accept=".ply,.obj,.glb,.gltf,.bin,.png,.jpg,.jpeg,.webp"' in text
        assert "VULKAX_IMAGE_POINT_BUDGET=80000" in text
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
