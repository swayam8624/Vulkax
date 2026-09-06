#!/usr/bin/env python3
"""Vulkax presentation-only surface proxy renderer (stdlib only)."""
from __future__ import annotations
import argparse, json, math, struct, tempfile, zlib
from pathlib import Path

SH0=0.28209479177387814

def vadd(a,b): return (a[0]+b[0],a[1]+b[1],a[2]+b[2])
def vsub(a,b): return (a[0]-b[0],a[1]-b[1],a[2]-b[2])
def vmul(a,s): return (a[0]*s,a[1]*s,a[2]*s)
def dot(a,b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def cross(a,b): return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def norm(a): return math.sqrt(max(dot(a,a),0.0))
def unit(a):
    n=norm(a); return (0,0,1) if n<1e-12 else vmul(a,1/n)
def cl(x): return max(0.0,min(1.0,x))

def read_ply(path):
    props=[]; count=None; rows=[]; inv=False
    with open(path,encoding='utf-8') as f:
        for raw in f:
            s=raw.strip()
            if s.startswith('element vertex '): count=int(s.split()[-1]); inv=True; continue
            if s.startswith('element ') and not s.startswith('element vertex '): inv=False
            if inv and s.startswith('property '): props.append(s.split()[-1])
            if s=='end_header': break
        rows=[r.split() for r in f if r.strip()]
    if count is None or len(rows)<count: raise ValueError(f'bad PLY: {path}')
    ix={p:i for i,p in enumerate(props)}; out=[]
    for n,r in enumerate(rows[:count],1):
        p=(float(r[ix['x']]),float(r[ix['y']]),float(r[ix['z']]))
        if all(f'f_dc_{c}' in ix for c in range(3)):
            rgb=tuple(cl(0.5+SH0*float(r[ix[f'f_dc_{c}']])) for c in range(3))
        else: rgb=(.68,.72,.82)
        lid=int(r[ix['vulkax_id_local']]) if 'vulkax_id_local' in ix else n
        out.append((lid,p,rgb))
    return sorted(out)

def grid(points):
    s=round(math.sqrt(len(points)))
    if s<2 or s*s!=len(points): raise ValueError(f'surface proxy needs square lattice; got {len(points)} points')
    return [points[y*s:(y+1)*s] for y in range(s)]

def rot(p,yaw,pitch):
    y=math.radians(yaw); x=math.radians(pitch); cy,sy=math.cos(y),math.sin(y); cx,sx=math.cos(x),math.sin(x)
    q=(cy*p[0]+sy*p[2],p[1],-sy*p[0]+cy*p[2])
    return (q[0],cx*q[1]-sx*q[2],sx*q[1]+cx*q[2])

def prep(g,yaw,pitch=-7):
    ps=[x[1] for row in g for x in row]
    mn=tuple(min(p[i] for p in ps) for i in range(3)); mx=tuple(max(p[i] for p in ps) for i in range(3)); c=tuple((mn[i]+mx[i])*.5 for i in range(3))
    return [[(lid,rot(vsub(p,c),yaw,pitch),rgb) for lid,p,rgb in row] for row in g]

def normals(g):
    h,w=len(g),len(g[0]); out=[]
    for y in range(h):
        row=[]
        for x in range(w):
            L=g[y][max(0,x-1)][1]; R=g[y][min(w-1,x+1)][1]; U=g[max(0,y-1)][x][1]; D=g[min(h-1,y+1)][x][1]
            n=unit(cross(vsub(R,L),vsub(D,U))); row.append(vmul(n,-1) if n[2]<0 else n)
        out.append(row)
    return out

class Img:
    def __init__(self,w,h): self.w=w; self.h=h; self.p=bytearray(w*h*3); self.z=[1e99]*(w*h)
    def bg(self):
        for y in range(self.h):
            v=y/max(1,self.h-1)
            for x in range(self.w):
                u=x/max(1,self.w-1); d=math.hypot(u-.52,v-.45); glow=max(0,1-d/.75)
                a=(.025*(1-v)+.010*v+.025*glow,.040*(1-v)+.015*v+.032*glow,.075*(1-v)+.030*v+.060*glow)
                i=(y*self.w+x)*3
                self.p[i:i+3]=bytes(int(cl(c)*255) for c in a)
    def put(self,x,y,z,c):
        if x<0 or y<0 or x>=self.w or y>=self.h: return
        k=y*self.w+x
        if z>=self.z[k]: return
        self.z[k]=z; i=k*3; self.p[i:i+3]=bytes(int(cl(q)*255) for q in c)
    def blend(self,x,y,c,a):
        if x<0 or y<0 or x>=self.w or y>=self.h:return
        i=(y*self.w+x)*3
        for j in range(3): self.p[i+j]=int((self.p[i+j]/255*(1-a)+c[j]*a)*255)

def basis(cam,target=(0,0,0)):
    f=unit(vsub(target,cam)); r=unit(cross(f,(0,1,0))); u=unit(cross(r,f)); return r,u,f

def proj(p,cam,W,H,fov=34):
    r,u,f=basis(cam); q=vsub(p,cam); z=dot(q,f)
    if z<=1e-6:return None
    k=1/math.tan(math.radians(fov)/2); a=W/H
    X=(dot(q,r)/z)*k/a; Y=(dot(q,u)/z)*k
    return ((X*.5+.5)*(W-1),(.5-Y*.5)*(H-1),z)

def shade(rgb,n,p):
    key=unit((-.52,.62,.92)); fill=unit((.72,.12,.68)); rim=unit((.18,-.88,.45)); view=(0,0,1); h=unit(vadd(key,view))
    kd=max(0,dot(n,key)); fd=max(0,dot(n,fill)); rd=max(0,dot(n,rim)); sp=max(0,dot(n,h))**44
    r,g,b=rgb
    if max(rgb)-min(rgb)<.06: r=.48*r+.52*.38; g=.48*g+.52*.52; b=.48*b+.52*.78
    L=.31+.72*kd+.22*fd+.15*rd; z=1+.08*math.tanh(p[2]*18)
    return (cl(r*L*z+.15*sp),cl(g*L*z+.16*sp),cl(b*L*z+.19*sp))

def edge(a,b,p): return (p[0]-a[0])*(b[1]-a[1])-(p[1]-a[1])*(b[0]-a[0])
def tri(im,A,B,C):
    area=edge(A[:2],B[:2],C[:2])
    if abs(area)<1e-8:return
    x0=max(0,int(min(A[0],B[0],C[0]))); x1=min(im.w-1,int(math.ceil(max(A[0],B[0],C[0])))); y0=max(0,int(min(A[1],B[1],C[1]))); y1=min(im.h-1,int(math.ceil(max(A[1],B[1],C[1]))))
    for y in range(y0,y1+1):
      for x in range(x0,x1+1):
        P=(x+.5,y+.5); a=edge(B[:2],C[:2],P)/area; b=edge(C[:2],A[:2],P)/area; c=1-a-b
        if min(a,b,c)<-1e-6: continue
        z=a*A[2]+b*B[2]+c*C[2]; n=unit(tuple(a*A[4][i]+b*B[4][i]+c*C[4][i] for i in range(3))); p=tuple(a*A[3][i]+b*B[3][i]+c*C[3][i] for i in range(3)); rgb=tuple(a*A[5][i]+b*B[5][i]+c*C[5][i] for i in range(3)); im.put(x,y,z,shade(rgb,n,p))

def shadow(im,cx,cy,rx,ry):
    for y in range(max(0,int(cy-ry)),min(im.h,int(cy+ry)+1)):
      for x in range(max(0,int(cx-rx)),min(im.w,int(cx+rx)+1)):
        q=((x-cx)/max(rx,1))**2+((y-cy)/max(ry,1))**2
        if q<1: im.blend(x,y,(0,0,0),.22*(1-q)**2)

def circle(im,cx,cy,r):
    for y in range(int(cy-r),int(cy+r)+1):
      for x in range(int(cx-r),int(cx+r)+1):
        d=math.hypot(x-cx,y-cy)
        if d<=r: im.blend(x,y,(.94,.97,1),.45+.55*cl((r-d)/max(1,r*.4)))

def render(g,W,H,yaw):
    g=prep(g,yaw); ns=normals(g); pts=[q[1] for row in g for q in row]; span=max(max(p[i] for p in pts)-min(p[i] for p in pts) for i in range(3)); cam=(0,0,2.45*max(span,1e-4)); im=Img(W,H); im.bg(); pv=[]
    for y,row in enumerate(g):
        rr=[]
        for x,(_,p,rgb) in enumerate(row):
            q=proj(p,cam,W,H); rr.append((q[0],q[1],q[2],p,ns[y][x],rgb))
        pv.append(rr)
    xs=[q[0] for r in pv for q in r]; ys=[q[1] for r in pv for q in r]; cw=max(xs)-min(xs); ch=max(ys)-min(ys); shadow(im,(min(xs)+max(xs))/2+.03*cw,max(ys)+.08*ch,.42*cw,max(7,.11*ch))
    for y in range(len(pv)-1):
      for x in range(len(pv[0])-1):
        A,B,C,D=pv[y][x],pv[y][x+1],pv[y+1][x+1],pv[y+1][x]; tri(im,A,B,C); tri(im,A,C,D)
    for q in (pv[0][0],pv[0][-1],pv[-1][0],pv[-1][-1]): circle(im,q[0],q[1],max(3,min(W,H)*.0045))
    return im

def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
def png(im,path):
    raw=bytearray(); s=im.w*3
    for y in range(im.h): raw+=b'\0'+im.p[y*s:(y+1)*s]
    data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',im.w,im.h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(raw),6))+chunk(b'IEND',b''); path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(data)

def displacement(a,b):
    if len(a)!=len(b):return float('inf')
    return max((norm(vsub(p[1],q[1])) for p,q in zip(a,b)),default=0)

def gallery(path,frames,d):
    stat='No committed geometric change' if d<=1e-12 else 'Committed geometric rewrite visible'; detail='The authoritative before/rewritten Gaussian centers are identical in this run.' if d<=1e-12 else f'Maximum Gaussian-center displacement: {d:.6g} world units.'; js=json.dumps(frames)
    path.write_text(f'''<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Vulkax Surface Proxy</title><style>*{{box-sizing:border-box}}body{{margin:0;background:radial-gradient(circle at 50% 0,#142244,#070b16 60%);color:#edf4ff;font:15px/1.5 system-ui,-apple-system,sans-serif}}main{{max-width:1320px;margin:auto;padding:52px 24px 80px}}h1{{font-size:clamp(38px,6vw,72px);line-height:.98;letter-spacing:-.045em;margin:8px 0 18px}}p{{color:#9fb0ca;max-width:880px}}.pill{{display:inline-block;border:1px solid #31466f;border-radius:999px;padding:7px 12px;margin:16px 0 32px}}.g{{display:grid;grid-template-columns:1fr 1fr;gap:18px}}.c{{background:#0f1729;border:1px solid #253553;border-radius:22px;padding:14px;box-shadow:0 18px 60px #0005}}img{{width:100%;display:block;border-radius:14px;background:#050912}}b{{font-size:17px;display:block;margin:12px 3px 3px}}span{{color:#9fb0ca}}.turn{{margin-top:20px}}input{{width:100%;accent-color:#7aa2ff}}@media(max-width:800px){{.g{{grid-template-columns:1fr}}}}</style><main><div style="color:#8db0ff;text-transform:uppercase;letter-spacing:.15em;font-size:12px">Vulkax 1.0.x · presentation surface</div><h1>Verified reality,<br>without the cloud-blob look.</h1><p>Raw Gaussian evidence remains authoritative. This view derives a surface proxy only for legibility: continuous triangles, estimated normals, depth, studio shading and a contact shadow.</p><div class="pill">{stat}</div><div class="g"><div class="c"><img src="surface_before.png"><b>Before · surface proxy</b><span>{detail}</span></div><div class="c"><img src="surface_after.png"><b>After · surface proxy</b><span>Beauty representation only; raw PLY/PPM evidence is untouched.</span></div></div><div class="c turn"><b>Turntable</b><img id="im" src="{frames[0] if frames else 'surface_after.png'}"><input id="sl" type="range" min="0" max="{max(0,len(frames)-1)}" value="0"><span id="lb"></span></div><script>const f={js},im=document.getElementById('im'),sl=document.getElementById('sl'),lb=document.getElementById('lb');function u(){{let i=+sl.value||0;if(f.length)im.src=f[i];lb.textContent=f.length?`frame ${{i+1}} / ${{f.length}}`:''}}sl.oninput=u;u()</script></main>''',encoding='utf-8')

def run(root,W,H,N):
    ap=root/'appearance'; out=root/'render'/'surface_proxy'; A=read_ply(ap/'before.ply'); B=read_ply(ap/'rewritten.ply'); GA,GB=grid(A),grid(B); out.mkdir(parents=True,exist_ok=True); png(render(GA,W,H,-18),out/'surface_before.png'); png(render(GB,W,H,18),out/'surface_after.png'); td=out/'turntable'; frames=[]
    if N:
        td.mkdir(exist_ok=True)
        for i in range(N):
            name=f'frame_{i:03d}.png'; yaw=-42+84*i/max(1,N-1); png(render(GB,W,H,yaw),td/name); frames.append('turntable/'+name)
    d=displacement(A,B); gallery(out/'surface_gallery.html',frames,d); (out/'manifest.json').write_text(json.dumps({'schema':'vulkax.surface_proxy.presentation.v1','presentation_only':True,'turntable_frames':N,'max_gaussian_center_displacement':d},indent=2)+'\n'); print('surface_proxy_status: completed'); print(f'surface_proxy_gallery: {out/"surface_gallery.html"}'); print(f'surface_proxy_max_displacement: {d:.12g}')

def selftest():
    with tempfile.TemporaryDirectory() as t:
        root=Path(t); ap=root/'appearance'; ap.mkdir();
        def w(p,off):
            lines=['ply','format ascii 1.0','element vertex 16','property double x','property double y','property double z','property double f_dc_0','property double f_dc_1','property double f_dc_2','property uint vulkax_id_local','end_header']; k=1
            for y in range(4):
              for x in range(4): lines.append(f'{(x-1.5)*.02} {(y-1.5)*.02} {math.sin(x+y)*.003+off*x/3} 0 0 0 {k}'); k+=1
            p.write_text('\n'.join(lines)+'\n')
        w(ap/'before.ply',0); w(ap/'rewritten.ply',.003); run(root,320,180,3); assert (root/'render/surface_proxy/surface_before.png').read_bytes()[:8]==b'\x89PNG\r\n\x1a\n'; print('surface_proxy_self_test: passed')

def main():
    p=argparse.ArgumentParser(); p.add_argument('run_dir',nargs='?',type=Path); p.add_argument('--width',type=int,default=1280); p.add_argument('--height',type=int,default=720); p.add_argument('--turntable',type=int,default=12); p.add_argument('--self-test',action='store_true'); a=p.parse_args()
    if a.self_test:return selftest()
    if not a.run_dir:p.error('run_dir is required unless --self-test is used')
    run(a.run_dir,a.width,a.height,a.turntable)
if __name__=='__main__':main()
