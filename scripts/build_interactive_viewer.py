#!/usr/bin/env python3
"""Build a self-contained interactive Vulkax WebGL2 viewer from a captured-world run.

The generated HTML has no external runtime dependencies. It embeds authoritative
Gaussian appearance evidence, optional physical particles, selected rewrite-region
metadata, and a presentation surface shell. The scientific artifacts are never
modified.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import tempfile
from pathlib import Path

SH0 = 0.28209479177387814


def _clamp(x: float) -> float:
    return max(0.0, min(1.0, x))


def read_gaussian_ply(path: Path) -> list[dict]:
    props: list[str] = []
    count: int | None = None
    in_vertices = False
    with path.open("r", encoding="utf-8") as stream:
        for raw in stream:
            line = raw.strip()
            if line.startswith("element vertex "):
                count = int(line.split()[-1])
                in_vertices = True
                continue
            if line.startswith("element ") and not line.startswith("element vertex "):
                in_vertices = False
            if in_vertices and line.startswith("property "):
                props.append(line.split()[-1])
            if line == "end_header":
                break
        rows = [line.split() for line in stream if line.strip()]
    if count is None or len(rows) < count:
        raise ValueError(f"invalid or truncated PLY: {path}")
    index = {name: i for i, name in enumerate(props)}
    for required in ("x", "y", "z"):
        if required not in index:
            raise ValueError(f"PLY missing {required}: {path}")
    out: list[dict] = []
    for n, row in enumerate(rows[:count], 1):
        pos = [float(row[index[k]]) for k in ("x", "y", "z")]
        if all(f"f_dc_{c}" in index for c in range(3)):
            color = [_clamp(0.5 + SH0 * float(row[index[f"f_dc_{c}"]])) for c in range(3)]
        else:
            color = [0.62, 0.72, 0.92]
        if all(f"scale_{c}" in index for c in range(3)):
            scale = math.exp(sum(float(row[index[f"scale_{c}"]]) for c in range(3)) / 3.0)
        else:
            scale = 0.025
        opacity = float(row[index["opacity"]]) if "opacity" in index else 3.5
        if opacity < 0.0 or opacity > 1.0:
            opacity = 1.0 / (1.0 + math.exp(-opacity))
        local_id = int(row[index["vulkax_id_local"]]) if "vulkax_id_local" in index else n
        out.append({"id": local_id, "p": pos, "c": color, "s": scale, "a": _clamp(opacity)})
    out.sort(key=lambda item: item["id"])
    return out


def read_particles(path: Path | None) -> list[dict]:
    if path is None or not path.exists():
        return []
    out: list[dict] = []
    with path.open("r", encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream):
            out.append({"id": int(row["particle_id"]), "p": [float(row["rest_x"]), float(row["rest_y"]), float(row["rest_z"])]})
    out.sort(key=lambda item: item["id"])
    return out


def read_rewrite_ids(path: Path) -> list[int]:
    if not path.exists():
        return []
    ids: list[int] = []
    with path.open("r", encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream):
            value = row.get("particle_id")
            if value:
                ids.append(int(value))
    return sorted(set(ids))


def _unique_axis(values: list[float], eps: float = 1.0e-8) -> list[float]:
    out: list[float] = []
    for value in sorted(values):
        if not out or abs(value - out[-1]) > eps:
            out.append(value)
    return out


def physical_surface(particles: list[dict], rewrite_ids: set[int]) -> dict:
    """Derive the outer shell for a regular Cartesian particle lattice.

    If the particles are not a complete regular lattice, return an empty shell rather
    than inventing topology. The viewer can still display particle and Gaussian modes.
    """
    if len(particles) < 8:
        return {"positions": [], "normals": [], "colors": [], "indices": [], "kind": "none"}
    xs = _unique_axis([p["p"][0] for p in particles])
    ys = _unique_axis([p["p"][1] for p in particles])
    zs = _unique_axis([p["p"][2] for p in particles])
    if len(xs) * len(ys) * len(zs) != len(particles):
        return {"positions": [], "normals": [], "colors": [], "indices": [], "kind": "none"}

    lookup: dict[tuple[int, int, int], dict] = {}

    def nearest_index(v: float, axis: list[float]) -> int:
        return min(range(len(axis)), key=lambda i: abs(axis[i] - v))

    for item in particles:
        p = item["p"]
        lookup[(nearest_index(p[0], xs), nearest_index(p[1], ys), nearest_index(p[2], zs))] = item
    if len(lookup) != len(particles):
        return {"positions": [], "normals": [], "colors": [], "indices": [], "kind": "none"}

    positions: list[float] = []
    normals: list[float] = []
    colors: list[float] = []
    indices: list[int] = []

    def emit_quad(keys: list[tuple[int, int, int]], normal: tuple[float, float, float]) -> None:
        base = len(positions) // 3
        for key in keys:
            item = lookup[key]
            positions.extend(item["p"])
            normals.extend(normal)
            colors.extend([1.0, 0.42, 0.10] if item["id"] in rewrite_ids else [0.28, 0.53, 0.95])
        indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])

    nx, ny, nz = len(xs), len(ys), len(zs)
    for iy in range(ny - 1):
        for iz in range(nz - 1):
            emit_quad([(0, iy, iz), (0, iy, iz + 1), (0, iy + 1, iz + 1), (0, iy + 1, iz)], (-1, 0, 0))
            emit_quad([(nx - 1, iy, iz), (nx - 1, iy + 1, iz), (nx - 1, iy + 1, iz + 1), (nx - 1, iy, iz + 1)], (1, 0, 0))
    for ix in range(nx - 1):
        for iz in range(nz - 1):
            emit_quad([(ix, 0, iz), (ix + 1, 0, iz), (ix + 1, 0, iz + 1), (ix, 0, iz + 1)], (0, -1, 0))
            emit_quad([(ix, ny - 1, iz), (ix, ny - 1, iz + 1), (ix + 1, ny - 1, iz + 1), (ix + 1, ny - 1, iz)], (0, 1, 0))
    for ix in range(nx - 1):
        for iy in range(ny - 1):
            emit_quad([(ix, iy, 0), (ix, iy + 1, 0), (ix + 1, iy + 1, 0), (ix + 1, iy, 0)], (0, 0, -1))
            emit_quad([(ix, iy, nz - 1), (ix + 1, iy, nz - 1), (ix + 1, iy + 1, nz - 1), (ix, iy + 1, nz - 1)], (0, 0, 1))

    return {"positions": positions, "normals": normals, "colors": colors, "indices": indices, "kind": f"physical_lattice_{nx}x{ny}x{nz}"}


def build_scene(run_dir: Path, particles_csv: Path | None) -> dict:
    appearance = run_dir / "appearance"
    before = read_gaussian_ply(appearance / "before.ply")
    after = read_gaussian_ply(appearance / "rewritten.ply")
    if len(before) != len(after):
        raise ValueError("before/rewritten Gaussian counts differ")
    particles = read_particles(particles_csv)
    rewrite_ids = read_rewrite_ids(run_dir / "influence" / "selected_rewrite_region.csv")
    shell = physical_surface(particles, set(rewrite_ids))
    max_disp = max((math.dist(a["p"], b["p"]) for a, b in zip(before, after)), default=0.0)
    return {"before": before, "after": after, "particles": particles, "rewriteIds": rewrite_ids, "surface": shell, "meta": {"gaussians": len(before), "particles": len(particles), "rewriteParticles": len(rewrite_ids), "maxGaussianDisplacement": max_disp, "surfaceKind": shell["kind"]}}


HTML = r'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Vulkax Interactive Viewer</title><style>
:root{color-scheme:dark;--bg:#050812;--panel:#0d1424e8;--line:#223250;--text:#eff5ff;--muted:#8fa4c3;--blue:#72a7ff;--orange:#ff7a24}*{box-sizing:border-box}html,body{margin:0;width:100%;height:100%;overflow:hidden;background:var(--bg);font:14px/1.45 Inter,ui-sans-serif,system-ui,-apple-system,sans-serif;color:var(--text)}#gl{position:fixed;inset:0;width:100%;height:100%;display:block;background:radial-gradient(circle at 50% 20%,#142448 0,#070c19 46%,#03050b 100%)}.top{position:fixed;left:24px;top:20px;pointer-events:none;text-shadow:0 2px 16px #000}.brand{font-size:12px;letter-spacing:.18em;text-transform:uppercase;color:#9cbbff}.title{font-size:28px;font-weight:750;letter-spacing:-.035em}.subtitle{color:var(--muted);font-size:12px;margin-top:2px}.panel{position:fixed;top:18px;right:18px;width:310px;max-height:calc(100vh - 36px);overflow:auto;padding:16px;border:1px solid var(--line);border-radius:20px;background:linear-gradient(180deg,#111a2dda,#0a101ee8);backdrop-filter:blur(18px);box-shadow:0 20px 80px #0008}.section{padding:12px 0;border-top:1px solid #1e2b45}.section:first-child{border-top:0;padding-top:0}.label{color:#8096b7;text-transform:uppercase;letter-spacing:.12em;font-size:10px;margin-bottom:8px}.row{display:flex;gap:8px;align-items:center;margin:8px 0}.btn{appearance:none;border:1px solid #2c4067;background:#101a2c;color:#dce9ff;border-radius:10px;padding:8px 10px;cursor:pointer;flex:1}.btn:hover{border-color:#5576ae}.btn.active{background:#18345d;border-color:#6a9eff;color:white}.btn.orange.active{background:#4a2415;border-color:#ff8742}input[type=range]{width:100%;accent-color:#7ba8ff}.stat{display:flex;justify-content:space-between;color:#9db0ca;margin:4px 0}.stat b{color:#edf5ff;font-weight:600}.toggle{display:flex;align-items:center;justify-content:space-between;margin:8px 0;color:#b8c8df}.toggle input{accent-color:#7ba8ff}.hint{position:fixed;left:24px;bottom:22px;color:#7590b6;font-size:12px;pointer-events:none}.drop{border:1px dashed #385077;border-radius:12px;padding:11px;text-align:center;color:#8fa4c3;cursor:pointer}.drop:hover{border-color:#6b93d3;color:#cbdcff}.drop input{display:none}.status{margin-top:8px;color:#90a9cc;font-size:12px}.warn{color:#ffaf7a}.footer{color:#667d9d;font-size:11px;margin-top:8px}@media(max-width:700px){.panel{width:270px}.title{font-size:22px}.top{left:14px}.hint{display:none}}</style></head><body>
<canvas id="gl"></canvas><div class="top"><div class="brand">Vulkax 1.0.x · Interactive World Viewer</div><div class="title">Verified Rewritable Reality</div><div class="subtitle" id="subtitle">Gaussian appearance + physical world + presentation surface</div></div>
<div class="panel"><div class="section"><div class="label">View mode</div><div class="row"><button class="btn active" data-mode="hybrid">Hybrid</button><button class="btn" data-mode="gaussian">Splats</button></div><div class="row"><button class="btn" data-mode="surface">Surface</button><button class="btn" data-mode="particles">Particles</button></div></div><div class="section"><div class="label">World state</div><div class="row"><button id="beforeBtn" class="btn">Before</button><button id="afterBtn" class="btn active orange">After / Verified</button></div><label class="toggle">Rewrite highlight <input id="rewriteToggle" type="checkbox" checked></label><label class="toggle">Ground grid <input id="gridToggle" type="checkbox" checked></label><label class="toggle">Auto orbit <input id="orbitToggle" type="checkbox"></label></div><div class="section"><div class="label">Gaussian presentation</div><div class="stat"><span>Splat scale</span><b id="splatValue">1.00×</b></div><input id="splatScale" type="range" min="0.2" max="4" step="0.05" value="1"><div class="stat"><span>Opacity</span><b id="opacityValue">0.86</b></div><input id="opacity" type="range" min="0.05" max="1" step="0.01" value="0.86"><div class="stat"><span>Exposure</span><b id="exposureValue">1.00</b></div><input id="exposure" type="range" min="0.4" max="2.4" step="0.05" value="1"></div><div class="section"><div class="label">Scene</div><div class="stat"><span>Gaussians</span><b id="gaussianCount"></b></div><div class="stat"><span>Physical particles</span><b id="particleCount"></b></div><div class="stat"><span>Rewrite region</span><b id="rewriteCount"></b></div><div class="stat"><span>Surface</span><b id="surfaceKind"></b></div><div class="stat"><span>Gaussian Δ max</span><b id="disp"></b></div></div><div class="section"><div class="label">Import local asset</div><label class="drop">Drop/click ASCII .PLY or .OBJ<input id="file" type="file" accept=".ply,.obj"></label><div class="status" id="importStatus">Imported assets are viewer-only and do not alter Vulkax evidence.</div><div class="row"><button class="btn" id="resetScene">Reload Vulkax scene</button><button class="btn" id="shot">Capture PNG</button></div></div><div class="footer">LMB orbit · RMB pan · wheel zoom · double-click focus · R reset camera</div></div><div class="hint">Interactive presentation layer — authoritative Gaussian/physics evidence remains unchanged.</div>
<script>'use strict';const ORIGINAL=__SCENE_JSON__;let scene=structuredClone(ORIGINAL);const canvas=document.getElementById('gl'),gl=canvas.getContext('webgl2',{antialias:true,alpha:true,preserveDrawingBuffer:true});if(!gl){document.body.innerHTML='<div style="padding:40px">WebGL2 is required.</div>';throw new Error('WebGL2 unavailable')}
const VS_POINTS=`#version 300 es\nprecision highp float;layout(location=0)in vec3 aPos;layout(location=1)in vec3 aColor;layout(location=2)in float aScale;layout(location=3)in float aMark;uniform mat4 uVP;uniform float uPointScale;uniform vec2 uViewport;out vec3 vColor;out float vMark;void main(){vec4 q=uVP*vec4(aPos,1.0);gl_Position=q;float px=max(2.0,uPointScale*aScale*uViewport.y/max(q.w,0.001));gl_PointSize=min(px,180.0);vColor=aColor;vMark=aMark;}`;
const FS_GAUSS=`#version 300 es\nprecision highp float;in vec3 vColor;in float vMark;uniform float uOpacity;uniform float uExposure;uniform float uHighlight;out vec4 outColor;void main(){vec2 p=gl_PointCoord*2.0-1.0;float r2=dot(p,p);if(r2>1.0)discard;float a=exp(-3.2*r2)*uOpacity;vec3 c=vColor;if(vMark>.5&&uHighlight>.5)c=mix(c,vec3(1.0,.35,.06),.82);c=vec3(1.0)-exp(-c*uExposure*1.35);outColor=vec4(c,a);}`;
const FS_POINT=`#version 300 es\nprecision highp float;in vec3 vColor;in float vMark;uniform float uOpacity;uniform float uExposure;uniform float uHighlight;out vec4 outColor;void main(){vec2 p=gl_PointCoord*2.0-1.0;if(dot(p,p)>1.0)discard;vec3 c=vColor;if(vMark>.5&&uHighlight>.5)c=vec3(1.0,.32,.05);c=vec3(1.0)-exp(-c*uExposure);outColor=vec4(c,1.0);}`;
const VS_SURF=`#version 300 es\nprecision highp float;layout(location=0)in vec3 aPos;layout(location=1)in vec3 aNormal;layout(location=2)in vec3 aColor;uniform mat4 uVP;out vec3 vN;out vec3 vC;void main(){vN=aNormal;vC=aColor;gl_Position=uVP*vec4(aPos,1.0);}`;
const FS_SURF=`#version 300 es\nprecision highp float;in vec3 vN;in vec3 vC;uniform float uExposure;uniform float uHighlight;out vec4 outColor;void main(){vec3 n=normalize(vN),k=normalize(vec3(-.45,.72,.85)),f=normalize(vec3(.72,.12,.55));float kd=max(dot(n,k),0.0),fd=max(dot(n,f),0.0),rim=pow(1.0-max(n.z,0.0),2.0);vec3 base=vC;if(uHighlight<.5&&base.r>.8&&base.g<.6)base=vec3(.28,.53,.95);vec3 c=base*(.25+.72*kd+.18*fd)+vec3(.20,.28,.55)*rim*.20;c=vec3(1.0)-exp(-c*uExposure*1.2);outColor=vec4(c,1.0);}`;
const VS_LINE=`#version 300 es\nprecision highp float;layout(location=0)in vec3 aPos;uniform mat4 uVP;void main(){gl_Position=uVP*vec4(aPos,1.0);}`,FS_LINE=`#version 300 es\nprecision highp float;out vec4 outColor;void main(){outColor=vec4(.20,.32,.52,.32);}`;
function shader(t,s){const x=gl.createShader(t);gl.shaderSource(x,s);gl.compileShader(x);if(!gl.getShaderParameter(x,gl.COMPILE_STATUS))throw new Error(gl.getShaderInfoLog(x));return x}function program(v,f){const p=gl.createProgram();gl.attachShader(p,shader(gl.VERTEX_SHADER,v));gl.attachShader(p,shader(gl.FRAGMENT_SHADER,f));gl.linkProgram(p);if(!gl.getProgramParameter(p,gl.LINK_STATUS))throw new Error(gl.getProgramInfoLog(p));return p}const pointProg=program(VS_POINTS,FS_GAUSS),particleProg=program(VS_POINTS,FS_POINT),surfProg=program(VS_SURF,FS_SURF),lineProg=program(VS_LINE,FS_LINE);function buf(d,t=gl.ARRAY_BUFFER){const b=gl.createBuffer();gl.bindBuffer(t,b);gl.bufferData(t,d,gl.STATIC_DRAW);return b}let gpu={};function flatten(a,k,n){const o=new Float32Array(a.length*n);a.forEach((x,i)=>{for(let j=0;j<n;j++)o[i*n+j]=x[k][j]});return o}function buildPointSet(a,particle=false,rewrite=[]){const m=new Set(rewrite),c=new Float32Array(a.length*3),s=new Float32Array(a.length),mark=new Float32Array(a.length);a.forEach((x,i)=>{c.set(x.c||[.32,.62,1],i*3);s[i]=particle?.008:(x.s||.02);mark[i]=m.has(x.id)?1:0});return{count:a.length,pos:buf(flatten(a,'p',3)),col:buf(c),scale:buf(s),mark:buf(mark)}}function buildSurface(s){if(!s||!s.positions.length)return null;return{count:s.indices.length,pos:buf(new Float32Array(s.positions)),normal:buf(new Float32Array(s.normals)),color:buf(new Float32Array(s.colors)),idx:buf(new Uint32Array(s.indices),gl.ELEMENT_ARRAY_BUFFER)}}function rebuild(){gpu.before=buildPointSet(scene.before,false,scene.rewriteIds);gpu.after=buildPointSet(scene.after,false,scene.rewriteIds);gpu.particles=buildPointSet(scene.particles,true,scene.rewriteIds);gpu.surface=buildSurface(scene.surface);fitScene();updateStats()}function attr(l,b,n){gl.bindBuffer(gl.ARRAY_BUFFER,b);gl.enableVertexAttribArray(l);gl.vertexAttribPointer(l,n,gl.FLOAT,false,0,0)}
function mul(a,b){let o=new Float32Array(16);for(let c=0;c<4;c++)for(let r=0;r<4;r++)o[c*4+r]=a[r]*b[c*4]+a[4+r]*b[c*4+1]+a[8+r]*b[c*4+2]+a[12+r]*b[c*4+3];return o}function perspective(f,a,n,z){let t=1/Math.tan(f/2),o=new Float32Array(16);o[0]=t/a;o[5]=t;o[10]=(z+n)/(n-z);o[11]=-1;o[14]=2*z*n/(n-z);return o}function norm(a){let n=Math.hypot(...a)||1;return a.map(v=>v/n)}function cross(a,b){return[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]}function sub(a,b){return a.map((v,i)=>v-b[i])}function lookAt(e,t,u){let z=norm(sub(e,t)),x=norm(cross(u,z)),y=cross(z,x),o=new Float32Array(16);o[0]=x[0];o[1]=y[0];o[2]=z[0];o[4]=x[1];o[5]=y[1];o[6]=z[1];o[8]=x[2];o[9]=y[2];o[10]=z[2];o[12]=-(x[0]*e[0]+x[1]*e[1]+x[2]*e[2]);o[13]=-(y[0]*e[0]+y[1]*e[1]+y[2]*e[2]);o[14]=-(z[0]*e[0]+z[1]*e[1]+z[2]*e[2]);o[15]=1;return o}
let cam={yaw:.65,pitch:.32,dist:2.4,target:[0,0,0],mode:'hybrid',state:'after',highlight:true,grid:true,auto:false,splat:1,opacity:.86,exposure:1},bounds={center:[0,0,0],radius:1};function allPoints(){return[...scene.before,...scene.after,...scene.particles].map(x=>x.p)}function fitScene(){let p=allPoints();if(!p.length)return;let mn=[Infinity,Infinity,Infinity],mx=[-Infinity,-Infinity,-Infinity];p.forEach(q=>q.forEach((v,i)=>{mn[i]=Math.min(mn[i],v);mx[i]=Math.max(mx[i],v)}));bounds.center=mn.map((v,i)=>(v+mx[i])/2);bounds.radius=Math.max(...mn.map((v,i)=>mx[i]-v),.1)*.72;resetCamera()}function resetCamera(){cam.target=[...bounds.center];cam.yaw=.65;cam.pitch=.28;cam.dist=bounds.radius*3.4}function cameraPos(){let cp=Math.cos(cam.pitch),sp=Math.sin(cam.pitch),cy=Math.cos(cam.yaw),sy=Math.sin(cam.yaw);return[cam.target[0]+cam.dist*cp*sy,cam.target[1]+cam.dist*sp,cam.target[2]+cam.dist*cp*cy]}function vp(){return mul(perspective(Math.PI/4,canvas.width/canvas.height,Math.max(.001,bounds.radius*.01),Math.max(50,bounds.radius*30)),lookAt(cameraPos(),cam.target,[0,1,0]))}
function drawPoints(s,p,g){if(!s||!s.count)return;gl.useProgram(p);attr(0,s.pos,3);attr(1,s.col,3);attr(2,s.scale,1);attr(3,s.mark,1);gl.uniformMatrix4fv(gl.getUniformLocation(p,'uVP'),false,vp());gl.uniform1f(gl.getUniformLocation(p,'uPointScale'),g?cam.splat*880:Math.max(5,cam.splat*16));gl.uniform2f(gl.getUniformLocation(p,'uViewport'),canvas.width,canvas.height);gl.uniform1f(gl.getUniformLocation(p,'uOpacity'),cam.opacity);gl.uniform1f(gl.getUniformLocation(p,'uExposure'),cam.exposure);gl.uniform1f(gl.getUniformLocation(p,'uHighlight'),cam.highlight?1:0);gl.drawArrays(gl.POINTS,0,s.count)}function drawSurface(){let s=gpu.surface;if(!s)return;gl.useProgram(surfProg);attr(0,s.pos,3);attr(1,s.normal,3);attr(2,s.color,3);gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,s.idx);gl.uniformMatrix4fv(gl.getUniformLocation(surfProg,'uVP'),false,vp());gl.uniform1f(gl.getUniformLocation(surfProg,'uExposure'),cam.exposure);gl.uniform1f(gl.getUniformLocation(surfProg,'uHighlight'),cam.highlight?1:0);gl.drawElements(gl.TRIANGLES,s.count,gl.UNSIGNED_INT,0)}let gridBuf=null,gridCount=0;(()=>{let a=[];for(let i=-10;i<=10;i++){let t=i/10*2;a.push(-2,0,t,2,0,t,t,0,-2,t,0,2)}gridBuf=buf(new Float32Array(a));gridCount=a.length/3})();function drawGrid(){let a=[],y=bounds.center[1]-bounds.radius*.72,r=bounds.radius*1.6;for(let i=-10;i<=10;i++){let t=i/10*r;a.push(bounds.center[0]-r,y,bounds.center[2]+t,bounds.center[0]+r,y,bounds.center[2]+t,bounds.center[0]+t,y,bounds.center[2]-r,bounds.center[0]+t,y,bounds.center[2]+r)}let b=buf(new Float32Array(a));gl.useProgram(lineProg);attr(0,b,3);gl.uniformMatrix4fv(gl.getUniformLocation(lineProg,'uVP'),false,vp());gl.drawArrays(gl.LINES,0,a.length/3);gl.deleteBuffer(b)}function resize(){let d=Math.min(devicePixelRatio||1,2),w=Math.floor(innerWidth*d),h=Math.floor(innerHeight*d);if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;gl.viewport(0,0,w,h)}}let last=performance.now();function frame(now){resize();let dt=(now-last)/1000;last=now;if(cam.auto)cam.yaw+=dt*.25;gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.enable(gl.DEPTH_TEST);gl.depthFunc(gl.LEQUAL);if(cam.grid)drawGrid();if(cam.mode==='surface'||cam.mode==='hybrid')drawSurface();if(cam.mode==='gaussian'||cam.mode==='hybrid'){gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);gl.depthMask(false);drawPoints(cam.state==='before'?gpu.before:gpu.after,pointProg,true);gl.depthMask(true);gl.disable(gl.BLEND)}if(cam.mode==='particles'||cam.mode==='hybrid')drawPoints(gpu.particles,particleProg,false);requestAnimationFrame(frame)}
let dragging=false,button=0,lx=0,ly=0;canvas.addEventListener('contextmenu',e=>e.preventDefault());canvas.addEventListener('pointerdown',e=>{dragging=true;button=e.button;lx=e.clientX;ly=e.clientY;canvas.setPointerCapture(e.pointerId)});canvas.addEventListener('pointerup',()=>dragging=false);canvas.addEventListener('pointermove',e=>{if(!dragging)return;let dx=e.clientX-lx,dy=e.clientY-ly;lx=e.clientX;ly=e.clientY;if(button===0){cam.yaw-=dx*.006;cam.pitch=Math.max(-1.45,Math.min(1.45,cam.pitch-dy*.006))}else{let pos=cameraPos(),f=norm(sub(cam.target,pos)),r=norm(cross(f,[0,1,0])),u=norm(cross(r,f)),s=cam.dist*.0016;for(let i=0;i<3;i++)cam.target[i]+=(-dx*r[i]+dy*u[i])*s}});canvas.addEventListener('wheel',e=>{e.preventDefault();cam.dist*=Math.exp(e.deltaY*.001);cam.dist=Math.max(bounds.radius*.08,Math.min(bounds.radius*40,cam.dist))},{passive:false});canvas.addEventListener('dblclick',resetCamera);addEventListener('keydown',e=>{if(e.key==='r'||e.key==='R')resetCamera()});
function updateStats(){let m=scene.meta||{};gaussianCount.textContent=m.gaussians??scene.before.length;particleCount.textContent=m.particles??scene.particles.length;rewriteCount.textContent=m.rewriteParticles??scene.rewriteIds.length;surfaceKind.textContent=m.surfaceKind||scene.surface?.kind||'none';disp.textContent=(m.maxGaussianDisplacement||0).toExponential(2)}document.querySelectorAll('[data-mode]').forEach(b=>b.onclick=()=>{document.querySelectorAll('[data-mode]').forEach(x=>x.classList.remove('active'));b.classList.add('active');cam.mode=b.dataset.mode});function state(s){cam.state=s;beforeBtn.classList.toggle('active',s==='before');afterBtn.classList.toggle('active',s==='after')}beforeBtn.onclick=()=>state('before');afterBtn.onclick=()=>state('after');rewriteToggle.onchange=e=>cam.highlight=e.target.checked;gridToggle.onchange=e=>cam.grid=e.target.checked;orbitToggle.onchange=e=>cam.auto=e.target.checked;splatScale.oninput=e=>{cam.splat=+e.target.value;splatValue.textContent=cam.splat.toFixed(2)+'×'};opacity.oninput=e=>{cam.opacity=+e.target.value;opacityValue.textContent=cam.opacity.toFixed(2)};exposure.oninput=e=>{cam.exposure=+e.target.value;exposureValue.textContent=cam.exposure.toFixed(2)};resetScene.onclick=()=>{scene=structuredClone(ORIGINAL);rebuild();importStatus.textContent='Reloaded embedded Vulkax scene.'};shot.onclick=()=>canvas.toBlob(blob=>{let a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='vulkax_viewer.png';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000)});
function centerScale(points){if(!points.length)return points;let mn=[Infinity,Infinity,Infinity],mx=[-Infinity,-Infinity,-Infinity];points.forEach(p=>p.p.forEach((v,i)=>{mn[i]=Math.min(mn[i],v);mx[i]=Math.max(mx[i],v)}));let c=mn.map((v,i)=>(v+mx[i])/2),span=Math.max(...mn.map((v,i)=>mx[i]-v),1e-6);points.forEach(p=>p.p=p.p.map((v,i)=>(v-c[i])/span));return points}function parsePLY(text){let lines=text.split(/\r?\n/),end=lines.indexOf('end_header');if(end<0)throw new Error('PLY end_header missing');let props=[],count=0,inV=false;for(let i=0;i<end;i++){let s=lines[i].trim();if(s.startsWith('element vertex ')){count=+s.split(/\s+/).pop();inV=true}else if(s.startsWith('element '))inV=false;else if(inV&&s.startsWith('property '))props.push(s.split(/\s+/).pop())}let ix=Object.fromEntries(props.map((p,i)=>[p,i])),out=[];for(let i=0;i<count;i++){let r=lines[end+1+i].trim().split(/\s+/),c=[.38,.62,.98];if(ix.f_dc_0!==undefined)c=[0,1,2].map(k=>Math.max(0,Math.min(1,.5+.28209479*+r[ix['f_dc_'+k]])));let s=.018;if(ix.scale_0!==undefined)s=Math.exp((+r[ix.scale_0]+ +r[ix.scale_1]+ +r[ix.scale_2])/3);out.push({id:i+1,p:[+r[ix.x],+r[ix.y],+r[ix.z]],c,s,a:.9})}return centerScale(out)}function parseOBJ(text){let out=[];for(let line of text.split(/\r?\n/)){let r=line.trim().split(/\s+/);if(r[0]==='v'&&r.length>=4)out.push({id:out.length+1,p:[+r[1],+r[2],+r[3]],c:[.42,.66,1],s:.014,a:.9})}if(!out.length)throw new Error('OBJ has no vertices');return centerScale(out)}file.onchange=async e=>{let f=e.target.files[0];if(!f)return;try{let text=await f.text(),pts=f.name.toLowerCase().endsWith('.obj')?parseOBJ(text):parsePLY(text);scene={before:pts,after:structuredClone(pts),particles:[],rewriteIds:[],surface:{positions:[],normals:[],colors:[],indices:[],kind:'none'},meta:{gaussians:pts.length,particles:0,rewriteParticles:0,maxGaussianDisplacement:0,surfaceKind:'imported_splats'}};rebuild();cam.mode='gaussian';document.querySelectorAll('[data-mode]').forEach(x=>x.classList.toggle('active',x.dataset.mode==='gaussian'));importStatus.textContent=`Loaded ${f.name}: ${pts.length} splats/vertices.`}catch(err){importStatus.textContent='Import failed: '+err.message;importStatus.classList.add('warn')}};rebuild();requestAnimationFrame(frame);</script></body></html>'''


def write_viewer(scene: dict, output: Path) -> None:
    payload = json.dumps(scene, separators=(",", ":"), ensure_ascii=False)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(HTML.replace("__SCENE_JSON__", payload), encoding="utf-8")


def self_test() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        appearance = root / "appearance"
        influence = root / "influence"
        appearance.mkdir()
        influence.mkdir()
        ply = """ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\nproperty float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\nproperty float opacity\nproperty float scale_0\nproperty float scale_1\nproperty float scale_2\nend_header\n-1 -1 0 0 0 0 4 -3 -3 -3\n1 -1 0 0 0 0 4 -3 -3 -3\n1 1 0 0 0 0 4 -3 -3 -3\n-1 1 0 0 0 0 4 -3 -3 -3\n"""
        (appearance / "before.ply").write_text(ply)
        (appearance / "rewritten.ply").write_text(ply)
        particles = root / "particles.csv"
        particles.write_text("particle_id,rest_x,rest_y,rest_z,mass,rest_volume\n" + "\n".join(f"{1 + x + 2*y + 4*z},{x},{y},{z},1,1" for z in range(2) for y in range(2) for x in range(2)) + "\n")
        (influence / "selected_rewrite_region.csv").write_text("region_id,particle_id\nr,1\n")
        scene = build_scene(root, particles)
        assert scene["meta"]["gaussians"] == 4
        assert scene["meta"]["particles"] == 8
        assert scene["surface"]["indices"]
        out = root / "viewer.html"
        write_viewer(scene, out)
        text = out.read_text()
        assert "Vulkax Interactive Viewer" in text and "__SCENE_JSON__" not in text
    print("interactive_viewer_self_test: passed")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the self-contained Vulkax interactive WebGL2 viewer.")
    parser.add_argument("run_dir", type=Path, nargs="?", help="captured-world-run directory")
    parser.add_argument("--particles-csv", type=Path, default=None, help="physical particle CSV used for surface/particle modes")
    parser.add_argument("--output", type=Path, default=None, help="output HTML path")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.run_dir is None:
        parser.error("run_dir is required unless --self-test is used")
    scene = build_scene(args.run_dir, args.particles_csv)
    output = args.output or (args.run_dir / "render" / "interactive" / "viewer.html")
    write_viewer(scene, output)
    print("interactive_viewer_status: completed")
    print(f"interactive_viewer_output: {output}")
    print(f"interactive_viewer_gaussians: {scene['meta']['gaussians']}")
    print(f"interactive_viewer_particles: {scene['meta']['particles']}")
    print(f"interactive_viewer_surface: {scene['meta']['surfaceKind']}")


if __name__ == "__main__":
    main()
