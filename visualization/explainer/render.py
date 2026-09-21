#!/usr/bin/env python3
"""Deterministic vector film. Read-only consumer of frozen results and replay.

Every drawing primitive has a raster and editable SVG implementation. No solver,
statistic, label, or force is modified by this presentation renderer.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
import shutil
import statistics
import subprocess
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = next(p for p in HERE.parents if (p / 'visualization/manifest.json').exists())
CFG = json.loads((HERE / "reality_probe.json").read_text())
BG, INK, MUTED, RULE, TRUTH, REPAIR, RED = [CFG[k] for k in
    ("background", "ink", "muted", "rule", "truth", "repair", "refusal")]
W, H = 1600, 900
DIRECTIONS = ("px", "nx", "py", "pz")
DIR_LABEL = ("+X", "−X", "+Y", "+Z")
CHAPTERS = [(0, "A repair that lies"), (20, "The deception map"),
            (31, "Ask a physical question"), (49, "A response signature"),
            (64, "Isolate the disagreement"), (77, "Mechanism-selective evidence"),
            (85, "Evidence has a scale"), (96, "The information limit"), (109, "Reality Probe")]


def unit(x):
    return max(0., min(1., x))


def ease(x):
    x = unit(x)
    return x * x * (3. - 2. * x)


def mix(a, b, t):
    return a + (b - a) * t


def color(a, b, t):
    av = tuple(int(a[i:i+2], 16) for i in (1, 3, 5))
    bv = tuple(int(b[i:i+2], 16) for i in (1, 3, 5))
    return "#" + "".join(f"{round(mix(x,y,unit(t))):02x}" for x,y in zip(av,bv))


@lru_cache(maxsize=160)
def font(size, serif=False, bold=False):
    folder = Path('/System/Library/Fonts/Supplemental')
    name = ('Georgia' if serif else 'Arial') + (' Bold' if bold else '') + '.ttf'
    path = folder / name
    if not path.exists():
        # System-installed fonts only; no downloaded or vendored dependency.
        path = Path('/usr/share/fonts/truetype/dejavu') / (
            'DejaVuSerif.ttf' if serif else 'DejaVuSans.ttf')
    return ImageFont.truetype(str(path), size)


class Canvas:
    def __init__(self, width=1600, vector=False):
        self.scale = width / W
        self.vector = vector
        self.parts = []
        self.im = None if vector else Image.new('RGB', (width, round(H*self.scale)), BG)
        self.draw = None if vector else ImageDraw.Draw(self.im)
        self.rect(0, 0, W, H, BG)

    def text(self, x, y, value, size=26, fill=INK, anchor='start', serif=False, bold=False):
        value = str(value)
        if self.vector:
            family = 'Georgia' if serif else 'Arial'
            self.parts.append(f'<text x="{x:.3f}" y="{y:.3f}" font-family="{family}" '
                f'font-size="{size}" font-weight="{700 if bold else 400}" '
                f'text-anchor="{anchor}" fill="{fill}">{html.escape(value)}</text>')
        else:
            f = font(round(size*self.scale), serif, bold)
            self.draw.text((x*self.scale,y*self.scale), value, font=f, fill=fill,
                anchor={'start':'ls','middle':'ms','end':'rs'}[anchor])

    def line(self, pts, fill=RULE, width=1.5, close=False):
        pts = [tuple(map(float,p)) for p in pts]
        if not pts:
            return
        if close:
            pts += [pts[0]]
        if self.vector:
            p = ' '.join(f'{x:.3f},{y:.3f}' for x,y in pts)
            self.parts.append(f'<polyline points="{p}" stroke="{fill}" '
                              f'stroke-width="{width}" fill="none" stroke-linejoin="round"/>')
        else:
            self.draw.line([(x*self.scale,y*self.scale) for x,y in pts], fill=fill,
                           width=max(1,round(width*self.scale)), joint='curve')

    def poly(self, pts, fill):
        if self.vector:
            p = ' '.join(f'{x:.3f},{y:.3f}' for x,y in pts)
            self.parts.append(f'<polygon points="{p}" fill="{fill}"/>')
        else:
            self.draw.polygon([(x*self.scale,y*self.scale) for x,y in pts],fill=fill)

    def rect(self, x, y, w, h, fill):
        if w <= 0 or h <= 0:
            return
        if self.vector:
            self.parts.append(f'<rect x="{x:.3f}" y="{y:.3f}" width="{w:.3f}" '
                              f'height="{h:.3f}" fill="{fill}"/>')
        else:
            self.draw.rectangle((x*self.scale,y*self.scale,(x+w)*self.scale,
                                 (y+h)*self.scale), fill=fill)

    def dot(self, x, y, r=5, fill=INK, outline=None, width=2):
        if self.vector:
            self.parts.append(f'<circle cx="{x:.3f}" cy="{y:.3f}" r="{r:.3f}" '
                f'fill="{fill or "none"}" stroke="{outline or "none"}" stroke-width="{width}"/>')
        else:
            self.draw.ellipse(((x-r)*self.scale,(y-r)*self.scale,(x+r)*self.scale,
                               (y+r)*self.scale),fill=fill,outline=outline,
                               width=max(1,round(width*self.scale)))

    def arrow(self, start, end, fill=INK, width=2.5, head=10):
        self.line([start,end],fill,width)
        a = math.atan2(end[1]-start[1],end[0]-start[0])
        self.poly([end,(end[0]-head*math.cos(a-.4),end[1]-head*math.sin(a-.4)),
                   (end[0]-head*math.cos(a+.4),end[1]-head*math.sin(a+.4))],fill)

    def svg(self):
        return ('<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="900" '
                'viewBox="0 0 1600 900"><title>Reality Probe scientific explainer</title>'
                + ''.join(self.parts) + '</svg>')


def csv_rows(path):
    with path.open(newline="", encoding="utf-8") as stream:
        yield from csv.DictReader(stream)


class Evidence:
    def __init__(self, replay):
        self.replay = replay
        self.hero_doc = json.loads((ROOT/'visualization/data/hero_case_ofc_2026-09-21.json').read_text())
        self.hero = self.hero_doc['row']
        self.rows = list(csv_rows(ROOT/'visualization/data/ofc_proposals_visualization_2026-09-21.csv'))
        for r in self.rows:
            for k in ('ordinary_improvement_pct','target_change_pct','dcs_progress_z','force_progress_z'):
                r[k] = float(r[k])
        ledger = json.loads((ROOT/'research/results/VULKAX_FINAL_RESULTS_2026-09-21.json').read_text())
        self.ofc = ledger['orthogonal_force_compliance']
        self.manifest = json.loads((replay/'trajectory_manifest.json').read_text())
        self.summary = {r['direction']:r for r in csv_rows(replay/'direction_summary.csv')}
        self.rest = np.zeros((64,3))
        self.state = np.full((4,3,61,64,3),np.nan)
        models = {'truth':0,'baseline_apic':1,'repair_pic':2}
        for r in csv_rows(replay/'particle_trajectories.csv'):
            i = int(r['particle_id'])-1
            self.rest[i] = [float(r['rest_'+k]) for k in 'xyz']
            self.state[DIRECTIONS.index(r['direction']),models[r['model']],int(r['frame']),i] = [float(r[k]) for k in 'xyz']
        self.disp = self.state - self.rest
        self.residual = np.linalg.norm(self.disp[:,2]-self.disp[:,0],axis=-1)
        self.residual_max = float(self.residual[:,-1].max())
        self.improve = 100*(self.hero['baseline_holdout_m']-self.hero['repair_holdout_m'])/self.hero['baseline_holdout_m']
        self.worsen = 100*(self.hero['repair_target_m']-self.hero['baseline_target_m'])/self.hero['baseline_target_m']
        self.validate()

    def validate(self):
        assert len(self.rows)==36 and sum(r['label']=='deceptive' for r in self.rows)==12
        assert np.isfinite(self.state).all()
        assert self.manifest['replay_guard']=='matched_frozen_hero_row'
        assert self.manifest['force_per_top_particle_N']==40
        assert self.manifest['directions']==list(DIRECTIONS)
        assert np.allclose(self.disp[:,:,0],0,atol=1e-14)
        bottom = self.rest[:,1]<-.17
        assert np.max(np.abs(self.disp[:,:,:,bottom]))<1e-12
        for key, channel in [('dcs_progress_z','dcs'),('force_progress_z','force_compliance')]:
            vals = [abs(r[key]) for r in self.rows]
            assert math.isclose(statistics.median(vals),self.ofc[channel]['median_abs_z'],rel_tol=1e-12)
            assert math.isclose(max(vals),self.ofc[channel]['max_abs_z'],rel_tol=1e-12)
            assert max(vals)<2
        selected = min((r for r in self.rows if r['label']=='deceptive'),key=lambda r:r['force_progress_z'])
        assert selected['truth_id']=='5' and selected['baseline']=='apic' and selected['repair']=='pic'
        # Independently recompute six-component raw RMS and centroid response.
        for d, name in enumerate(DIRECTIONS):
            bundle=[]
            for m, model in enumerate(('truth','baseline','repair')):
                top=self.disp[d,m,-1,self.rest[:,1]>.17].mean(axis=0)
                interior=self.disp[d,m,-1,np.abs(self.rest[:,1])<.17].mean(axis=0)
                v=np.r_[top,interior]
                expected=np.array([float(self.summary[name][f'{model}_{layer}_d{axis}'])
                                   for layer in ('top','interior') for axis in 'xyz'])
                assert np.allclose(v,expected,rtol=1e-9,atol=1e-13)
                bundle.append(v)
            for m,model in ((1,'baseline'),(2,'repair')):
                rms=float(np.sqrt(np.mean((bundle[m]-bundle[0])**2)))
                assert math.isclose(rms,float(self.summary[name][f'{model}_raw_rms_to_truth_m']),rel_tol=1e-9)
        return True

    def displacement(self,d,m,f):
        f=max(0.,min(60.,f)); lo=int(f); hi=min(lo+1,60)
        return mix(self.disp[d,m,lo],self.disp[d,m,hi],f-lo)

    def field(self,d,m,f,points):
        """Trilinear rest-coordinate interpolation, x-fastest particle IDs."""
        a=(np.asarray(points)+.18)/.12
        lo=np.clip(np.floor(a).astype(int),0,2); q=np.clip(a-lo,0,1)
        u=self.displacement(d,m,f)
        out=np.zeros_like(a)
        for z in (0,1):
            for y in (0,1):
                for x in (0,1):
                    idx=(lo[:,2]+z)*16+(lo[:,1]+y)*4+lo[:,0]+x
                    wt=np.prod(np.where(np.array([x,y,z]),q,1-q),axis=1)
                    out+=u[idx]*wt[:,None]
        return out


def projection(points,cx,cy,scale):
    a=np.asarray(points)
    return np.column_stack((cx+scale*(a[:,0]+.32*a[:,2]),
                            cy-scale*(a[:,1]+.16*a[:,2])))


def body(c,e,cx,cy,scale,d=0,m=0,frame=0,fill=TRUTH,opacity=1.,grid=True):
    """A material-coordinate grid on the real solver's front face, plus depth edges."""
    tint=color(BG,fill,opacity)
    for z in (.18,):
        pts=np.array([[-.18,-.18,z],[.18,-.18,z],[.18,.18,z],[-.18,.18,z],[-.18,-.18,z]])
        out=pts+CFG['display_magnification']*e.field(d,m,frame,pts)
        c.line(projection(out,cx,cy,scale),color(BG,tint,.3),1.4)
    for x in (-.18,.18):
        for y in (-.18,.18):
            pts=np.array([[x,y,-.18],[x,y,.18]])
            c.line(projection(pts+400*e.field(d,m,frame,pts),cx,cy,scale),color(BG,tint,.4),1.4)
    if grid:
        for axis in (0,1):
            for v in np.linspace(-.18,.18,13):
                pts=np.zeros((19,3)); pts[:,2]=-.18
                pts[:,axis]=v; pts[:,1-axis]=np.linspace(-.18,.18,19)
                p=projection(pts+400*e.field(d,m,frame,pts),cx,cy,scale)
                major=abs((v+.18)/.12-round((v+.18)/.12))<1e-6
                c.line(p,tint if major else color(BG,tint,.44),2.2 if major else 1.)
    pts=e.rest[:16]+400*e.displacement(d,m,frame)[:16]
    for x,y in projection(pts,cx,cy,scale):
        c.dot(x,y,3.5,tint)


def header(c,t,k,title,sub=''):
    c.text(76,51,CFG['title'],19,bold=True)
    c.text(1524,51,f'{k:02d}  /  {len(CHAPTERS):02d}',18,MUTED,anchor='end')
    c.line([(76,72),(1524,72)],RULE,1)
    c.text(76,145,title,48,serif=True)
    if sub:
        c.text(78,190,sub,25,MUTED)
    c.rect(76,859,1448,2,RULE)
    c.rect(76,859,1448*unit(t/CFG['duration_s']),2,INK)


def footer(c,caption,note='',fill=INK):
    c.text(800,790,caption,30,fill=fill,anchor='middle')
    if note:
        c.text(800,827,note,18,MUTED,anchor='middle')


def title_scene(c,e,t):
    header(c,t,1,'Can a better fit be less true?')
    p=ease((t-3)/4)
    body(c,e,1100,450,850,frame=0,fill=TRUTH)
    c.text(1050,700,'EXECUTABLE WORLD',21,MUTED,anchor='middle')
    if t<7:
        c.text(78,315,'A captured world.',47,serif=True)
        c.text(78,380,'A plausible repair.',47,serif=True)
        c.text(78,468,'What did the ordinary test miss?',28,MUTED)
        # Schematic observation rays terminate at real rest-grid particles.
        for i in (4,7,8,11):
            end=projection(e.rest[i:i+1],1100,450,850)[0]
            start=np.array([760.,end[1]])
            c.line([start,mix(start,end,p)],color(BG,TRUTH,.45),1.5)
            c.dot(*end,6,TRUTH)
        footer(c,'Matching observations is only one physical question.',
               'Observation links are schematic • geometry is the 4 × 4 × 4 solver body')
    else:
        q=ease((t-7)/1.4)
        # Two errors, same physical units and visual scale.
        body(c,e,1100,450,850,frame=0,fill=color(TRUTH,REPAIR,q),opacity=q)
        for row,(label,b,r,delta,col) in enumerate([
            ('Ordinary held-out error',e.hero['baseline_holdout_m'],e.hero['repair_holdout_m'],-e.improve,TRUTH),
            ('Untouched physical-target error',e.hero['baseline_target_m'],e.hero['repair_target_m'],e.worsen,REPAIR)]):
            y=300+row*205
            c.text(78,y,label,25)
            progress=ease((t-8-row*3)/2)
            value=mix(b,r,progress)*1e6
            c.rect(78,y+28,b*1e6*38,12,RULE)
            c.rect(78,y+59,value*38,24,col)
            c.text(78+value*38+16,y+81,f'{value:.3f} µm',25,col)
            if progress>.99:
                c.text(78,y+131,f'{delta:+.2f}%  '+('BETTER' if row==0 else 'WORSE'),28,col,bold=True)
        c.text(1060,315,'APIC → PIC',28,anchor='middle')
        c.text(1060,652,'One selected proposal',23,MUTED,anchor='middle')
        footer(c,'A deceptive repair improves ordinary fit and worsens the untouched target.',
               'Truth world 5 • post-hoc visualization selection • baseline error shown in grey')


def scatter_scene(c,e,t):
    header(c,t,2,'When “better” becomes deceptive.','36 real proposals from the fresh synthetic force/compliance experiment')
    left,right,top,bottom=185,1120,260,687
    xs=[r['ordinary_improvement_pct'] for r in e.rows]
    ys=[r['target_change_pct'] for r in e.rows]
    xmax=math.ceil(max(xs)/10)*10; ymin=math.floor(min(ys)/10)*10; ymax=math.ceil(max(ys)/10)*10
    sx=lambda x:left+(right-left-20)*x/xmax+10
    sy=lambda y:bottom-10-(bottom-top-20)*(y-ymin)/(ymax-ymin)
    zero=sy(0)
    c.rect(left,top,right-left,zero-top,color(BG,REPAIR,.065))
    c.rect(left,zero,right-left,bottom-zero,color(BG,TRUTH,.045))
    c.line([(left,top),(right,top),(right,bottom),(left,bottom),(left,top)],RULE,1)
    for x in np.linspace(0,xmax,5):
        xx=sx(x);c.line([(xx,top),(xx,bottom)],RULE,1)
        c.text(xx,720,f'{x:g}',21,MUTED,'middle')
    for y in range(ymin,ymax+1,20):
        yy=sy(y);c.line([(left,yy),(right,yy)],RULE,1)
        c.text(left-20,yy+7,f'{y:+d}',21,MUTED,'end')
    c.line([(left,zero),(right,zero)],MUTED,1.8)
    c.text(left,238,'Untouched target error change (%) ↑ worse',23)
    c.text((left+right)/2,757,'Ordinary held-out error improvement (%) → better',24,anchor='middle')
    hero=next(r for r in e.rows if r['truth_id']=='5' and r['baseline']=='apic' and r['repair']=='pic')
    for i,r in enumerate(e.rows):
        p=ease((t-21-i*.065)/1.5)
        x,y=sx(r['ordinary_improvement_pct']),mix(zero,sy(r['target_change_pct']),p)
        if p==0:continue
        if r['label']=='deceptive':c.poly([(x,y-7),(x+7,y),(x,y+7),(x-7,y)],REPAIR)
        else:c.dot(x,y,5.5,TRUTH)
    hx,hy=sx(hero['ordinary_improvement_pct']),sy(hero['target_change_pct'])
    if t>24:
        c.dot(hx,hy,14,None,INK,2)
        c.line([(hx+12,hy-8),(hx+58,hy-55),(hx+236,hy-55)],INK,1.5)
        c.text(hx+61,hy-69,'Selected example',23)
    c.text(1190,307,'12',70,REPAIR,serif=True)
    c.text(1190,346,'deceptive',26,REPAIR)
    c.text(1190,406,'Looks better.',24)
    c.text(1190,443,'Physics worse.',24)
    c.text(1190,547,'24',70,TRUTH,serif=True)
    c.text(1190,586,'beneficial',26,TRUTH)
    footer(c,'The ordinary criterion proposes every repair. The untouched target labels it.',
           'Equal-size marks • every proposal retained • upward means worse, not better')


def force_scene(c,e,t):
    header(c,t,3,'Ask the same world a different question.','A known load reveals response that the ordinary fit did not constrain.')
    frame=60*unit((t-34)/7)
    join=ease((t-42)/3)
    shrink=ease((t-46)/3)
    cx1=mix(mix(485,800,join),260,shrink);cx2=mix(mix(1135,800,join),260,shrink)
    cy=mix(505,369,shrink)
    scale=mix(900,370,shrink)
    if join<.99:
        c.text(cx1,260,'TRUTH RESPONSE',25,TRUTH,anchor='middle')
        c.text(cx2,260,'REPAIR RESPONSE',25,REPAIR,anchor='middle')
    else:
        c.text(mix(800,260,shrink),263,'SAME PROBE · DIFFERENT RESPONSE' if shrink<.5 else '+X',26,anchor='middle')
    for cx in (cx1,cx2) if join<.99 else (cx1,):
        body(c,e,cx,cy,scale,frame=0,fill=RULE,opacity=.65)
        # A small force arrow denotes the top layer's per-particle loading.
        origin=projection(np.array([[0,.18,-.18]]),cx,cy,scale)[0]
        origin[1]-=47
        grow=ease((t-32)/1.2)
        c.arrow(origin,origin+np.array([95*grow,0]),INK,2.5)
        c.line([(cx-220,713),(cx+170,713)],MUTED,2)
        for x in range(-210,170,20):c.line([(cx+x,713),(cx+x-10,725)],RULE,1.5)
    body(c,e,cx1,cy,scale,frame=frame,m=0,fill=TRUTH)
    body(c,e,cx2,cy,scale,frame=frame,m=2,fill=REPAIR)
    c.text(1500,280,'+X',26,anchor='end')
    c.text(1500,320,'40 N',39,anchor='end',serif=True)
    c.text(1500,355,'per top particle',21,MUTED,'end')
    c.text(1500,432,f'{frame*.1:.2f} ms',28,anchor='end')
    c.text(1500,465,'solver time',21,MUTED,'end')
    if join>.5 and shrink<.01:
        c.line([(970,568),(1130,602)],REPAIR,1.5)
        c.text(1144,612,'Residual response',23,REPAIR)
    footer(c,'The grid is attached to the material. Its motion exposes the mismatch.',
           'RAW SOLVER REPLAY • displacement ×400 for display • numbers unscaled • bottom layer fixed')


def fingerprint_scene(c,e,t):
    header(c,t,4,'Four probes. One response signature.','The response bundle records top-layer and interior displacement in x, y and z.')
    for d,name in enumerate(DIR_LABEL):
        cx=260+365*d
        frame=60 if d==0 else 60*unit((t-49.5-d*1.5)/2)
        expand=ease((t-62)/2)
        cy=mix(369,467,expand); size=mix(370,690,expand)
        c.text(cx,246,name,35,anchor='middle')
        body(c,e,cx,cy,size,d=d,m=0,frame=frame,fill=TRUTH)
        body(c,e,cx,cy,size,d=d,m=2,frame=frame,fill=REPAIR)
        c.arrow((cx-22,483),(cx-22,520),color(MUTED,BG,expand),1.5,8)
        # Each response column shows signed components on one shared µm scale.
        vals=[]
        for model in (0,2):
            disp=e.displacement(d,model,frame)
            vals.append(np.r_[disp[e.rest[:,1]>.17].mean(axis=0),
                              disp[np.abs(e.rest[:,1])<.17].mean(axis=0)]*1e6)
        for row in range(6):
            y=550+row*25
            c.line([(cx-80,y),(cx+80,y)],color(RULE,BG,expand),1)
            c.line([(cx,y-6),(cx,y+8)],color(RULE,BG,expand),1)
            for j,col in enumerate((TRUTH,REPAIR)):
                value=vals[j][row]
                c.line([(cx,y-3+j*7),(cx+value*.21,y-3+j*7)],color(col,BG,expand),3)
        if d==0:
            for row,label in enumerate(('top x','top y','top z','inner x','inner y','inner z')):
                c.text(107,556+25*row,label,18,color(MUTED,BG,expand),'end')
        c.text(cx,710,'−350     0     +350 µm',18,color(MUTED,BG,expand),'middle')
    c.text(550,753,'TRUTH',20,TRUTH,'end');c.text(600,753,'REPAIR',20,REPAIR)
    c.text(910,753,'Φ = [ r(+X)  r(−X)  r(+Y)  r(+Z) ]',26,serif=True)
    footer(c,'A new physical channel adds information; opposite X probes test both signs.',
           'Raw six-component response • common component scale • ±X are not independent basis axes')


def residual_scene(c,e,t):
    header(c,t,5,'Subtract the responses. Reveal the mechanism.','D(x, p) = ‖u repair(x, p) − u truth(x, p)‖')
    progress=ease((t-65)/3)
    n=19
    face=np.array([[x,y,-.18] for y in np.linspace(-.18,.18,n) for x in np.linspace(-.18,.18,n)])
    for d,name in enumerate(DIR_LABEL):
        cx=260+365*d
        c.text(cx,263,name,34,anchor='middle')
        ut=e.field(d,0,60,face);ur=e.field(d,2,60,face)
        residual=np.linalg.norm(ur-ut,axis=1)
        p=projection(face,cx,467,690)
        if progress<1:
            body(c,e,cx,467,690,d=d,m=0,frame=60,fill=TRUTH,opacity=1-progress)
            body(c,e,cx,467,690,d=d,m=2,frame=60,fill=REPAIR,opacity=1-progress)
        for iy in range(n-1):
            for ix in range(n-1):
                ids=[iy*n+ix,iy*n+ix+1,(iy+1)*n+ix+1,(iy+1)*n+ix]
                mag=float(residual[ids].mean())/e.residual_max
                tint=color('#dce9e9',REPAIR,mag)
                c.poly(p[ids],color(BG,tint,progress))
        for iy in range(0,n,6):c.line(p[iy*n:(iy+1)*n],color(BG,INK,.28),1)
        for ix in range(0,n,6):c.line(p[ix::n],color(BG,INK,.28),1)
        b=float(e.summary[DIRECTIONS[d]]['baseline_raw_rms_to_truth_m'])*1e6
        r=float(e.summary[DIRECTIONS[d]]['repair_raw_rms_to_truth_m'])*1e6
        c.text(cx,659,f'{b:.2f} → {r:.2f} µm',29,anchor='middle')
        c.text(cx,692,'baseline → repair response RMS',18,MUTED,'middle')
    for i in range(240):c.rect(160+i*2,722,2.2,12,color('#dce9e9',REPAIR,i/239))
    c.text(160,759,'0',19,MUTED)
    c.text(640,759,f'{e.residual_max*1e6:.1f} µm',19,MUTED,'end')
    c.text(702,734,'ONE SHARED RESIDUAL SCALE',20)
    footer(c,'Hidden disagreement becomes a field you can inspect.',
           'Front-face trilinear field at 6 ms • colors unscaled • RMS uses all six compliance components')


def dcs_scene(c,e,t):
    header(c,t,6,'Select a mechanism. Cancel the shared trend.',
           'DCS: dark-field counterfactual spectroscopy uses weighted intervention responses.')
    phase=ease((t-78)/3)
    c.text(390,274,'RESPONSE TO INTERVENTION',21,MUTED,'middle')
    xs=[240,390,540];base=[515,445,375];bend=[-35,0,-35]
    c.line([(180,624),(620,624)],RULE,2)
    c.arrow((180,624),(180,316),RULE,1.5)
    c.line(list(zip(xs,base)),TRUTH,2.5)
    warped=[base[i]+bend[i]*phase for i in range(3)]
    c.line(list(zip(xs,warped)),REPAIR,3)
    for i,x in enumerate(xs):
        c.dot(x,base[i],6,TRUTH);c.dot(x,warped[i],5,REPAIR)
        c.text(x,665,('−h','0','+h')[i],26,anchor='middle')
        c.text(x,724,('+1','−2','+1')[i],30,anchor='middle',serif=True)
    c.text(640,724,'weights',20,MUTED)
    c.arrow((692,467),(810,467),INK,2,12)
    c.text(1160,318,'r(−h) − 2r(0) + r(+h)',40,serif=True,anchor='middle')
    c.text(1160,391,'constant + linear terms cancel',26,MUTED,'middle')
    c.line([(950,538),(1380,538)],RULE,2)
    c.dot(980,538,7,TRUTH)
    c.text(980,581,'0',25,TRUTH,'middle')
    c.arrow((980,538),(980+260*phase,538),REPAIR,4,13)
    c.text(1170,640,'nonlinear response remains',27,REPAIR,'middle')
    footer(c,'Mechanism-selective contrasts ask what ordinary agreement leaves hidden.',
           'SCHEMATIC second difference • actual comparator: optimized order-2 stencil on a 3 × 3 intervention lattice')


def evidence_scene(c,e,t):
    header(c,t,7,'A visible difference is not yet credible evidence.')
    if t<90:
        c.text(800,310,'progress',46,serif=True,anchor='middle')
        c.text(800,398,'baseline error − repair error',45,serif=True,anchor='middle')
        c.line([(430,425),(1170,425)],INK,2)
        c.text(800,488,'combined uncertainty',45,serif=True,anchor='middle')
        c.text(800,580,'measurement  +  repeat  +  numerical refinement',27,MUTED,'middle')
        c.text(800,650,'Variances add; the denominator is their square root.',25,MUTED,'middle')
        footer(c,'Standardize the improvement by the uncertainty that remains.',
               'Exact force statistic: z = [e(B,Y) − e(R,Y)] / √(σ²meas + σ²repeat + σ²num,B + σ²num,R)')
    else:
        gain=e.ofc['force_to_dcs_median_abs_z_ratio']
        c.text(800,321,f'{gain:.2f}×',104,serif=True,anchor='middle')
        c.text(800,375,'stronger median standardized signal',30,anchor='middle')
        lo=e.ofc['dcs']['median_abs_z'];hi=e.ofc['force_compliance']['median_abs_z']
        q=ease((t-90)/3)
        left,right=280,1320
        sx=lambda z:left+z/.65*(right-left)
        c.line([(left,570),(right,570)],RULE,2)
        for v in (0,.2,.4,.6):
            x=sx(v);c.line([(x,565),(x,579)],MUTED,1.5);c.text(x,614,f'{v:.1f}',22,MUTED,'middle')
        c.dot(sx(lo),570,8,TRUTH)
        x=sx(mix(lo,hi,q));c.dot(x,570,10,REPAIR)
        c.text(sx(lo),515,'DCS',26,TRUTH,'middle')
        c.text(sx(lo),480,f'{lo:.4f}',28,TRUTH,'middle')
        if q>.5:
            c.text(x,515,'FORCE',26,REPAIR,'middle');c.text(x,480,f'{mix(lo,hi,q):.4f}',28,REPAIR,'middle')
        c.text(800,676,'Median |z| across the same 36 proposals',26,anchor='middle')
        footer(c,'A genuinely different physical question reveals much stronger evidence.',
               'Ratio of channel medians • fresh synthetic OFC • not the selected hero ratio')


def threshold_scene(c,e,t):
    header(c,t,8,'Better evidence. Still insufficient information.')
    zoom=ease((t-96)/2.8)
    upper=mix(.65,2.2,zoom)
    left,right=225,1390
    sx=lambda z:left+z/upper*(right-left)
    medD=e.ofc['dcs']['median_abs_z'];medF=e.ofc['force_compliance']['median_abs_z']
    for row,(key,col,median,label) in enumerate([
        ('dcs_progress_z',TRUTH,medD,'DCS'),('force_progress_z',REPAIR,medF,'FORCE')]):
        y=380+row*190
        c.text(78,y+7,label,23,col)
        c.line([(left,y),(right,y)],RULE,2)
        if t>99:
            for i,r in enumerate(e.rows):
                v=abs(r[key])
                c.dot(sx(v),y+23+(i%3)*12,3.6,color(BG,col,.55))
        c.dot(sx(median),y,10,col)
        c.text(sx(median),y-24,f'{median:.4f}',28,col,'middle')
    for val in (0,.5,1,1.5,2):
        if val<=upper:
            x=sx(val);c.line([(x,657),(x,667)],MUTED,1.5)
            c.text(x,704,f'{val:g}',23,MUTED,'middle')
    if upper>=2:
        x=sx(2);c.line([(x,279),(x,656)],INK,2.5)
        c.text(x,245,'|z| = 2',29,anchor='middle')
        c.text(x,213,'Frozen threshold',22,MUTED,'middle')
    if t>101:
        mx=e.ofc['force_compliance']['max_abs_z']
        c.dot(sx(mx),570,8,None,REPAIR,2)
        c.text(sx(mx),527,f'max {mx:.4f}',24,REPAIR,'middle')
    if t>103:
        c.text(830,334,'0 / 36 resolved',39,serif=True,anchor='middle')
        c.text(830,387,'0 support · 0 veto',25,MUTED,'middle')
        c.text(830,450,f"Selected example: z = {e.hero['force_progress_z']:.4f}",25,anchor='middle')
        c.text(830,488,'also unresolved',23,MUTED,'middle')
    c.text(800,749,'Absolute standardized progress |z|',24,anchor='middle')
    footer(c,'REFUSE CERTIFICATION' if t>104 else 'Even the strongest force evidence stays below the frozen threshold.',
           'Signed decisions: z ≥ +2 support · z ≤ −2 veto · otherwise unresolved',RED if t>104 else INK)


def closing_scene(c,e,t):
    header(c,t,9,'Knowing when not to certify is part of verification.')
    c.text(800,350,'Looks right',82,serif=True,anchor='middle')
    c.text(800,455,'≠',85,REPAIR,anchor='middle')
    c.text(800,560,'physically verified.',82,serif=True,anchor='middle')
    c.text(800,672,CFG['title'],27,bold=True,anchor='middle')
    c.text(800,713,'Ask the repaired world physical questions.',29,anchor='middle')
    footer(c,'Stronger evidence. Honest uncertainty.',
           'Frozen synthetic experiment • 21 September 2026 • prospective certification not established')


SCENES=(title_scene,scatter_scene,force_scene,fingerprint_scene,residual_scene,
        dcs_scene,evidence_scene,threshold_scene,closing_scene)


def render_frame(e,t,width=1600,vector=False):
    idx=max(i for i,(start,_) in enumerate(CHAPTERS) if t>=start)
    c=Canvas(width,vector);SCENES[idx](c,e,t)
    # Brief dissolves only at changes of representation; causal motion remains within scenes.
    if not vector and idx>0 and idx not in (3,4) and t<CHAPTERS[idx][0]+.45:
        old=Canvas(width);SCENES[idx-1](old,e,CHAPTERS[idx][0]-.001)
        c.im=Image.blend(old.im,c.im,ease((t-CHAPTERS[idx][0])/.45))
    return c


def write_stills(e,out):
    folder=out/'figures';folder.mkdir(parents=True,exist_ok=True)
    moments=[(6,'01_question'),(18,'02_deceptive_repair'),(28,'03_deception_map'),
             (39,'04_same_probe'),(47,'05_response_overlay'),(62,'06_fingerprint'),
             (74,'07_residual_field'),(83,'08_dcs_intuition'),(88,'09_standardization'),(95,'10_signal_gain'),
             (107,'11_information_limit'),(114,'12_conclusion')]
    thumbs=[]
    for t,name in moments:
        c=render_frame(e,t,1920);c.im.save(folder/(name+'.png'))
        (folder/(name+'.svg')).write_text(render_frame(e,t,vector=True).svg())
        small=c.im.resize((640,360),Image.Resampling.LANCZOS)
        thumbs.append(small)
    sheet=Image.new('RGB',(1920,360*4),BG)
    for i,im in enumerate(thumbs):sheet.paste(im,((i%3)*640,(i//3)*360))
    sheet.save(out/'storyboard.png')


def render_video(e,out,width,fps,aa,start,end,name):
    render_width=round(width*aa)
    if render_width%2:render_width+=1
    height=round(width*H/W)
    log=out/(name+'.ffmpeg.log')
    command=['ffmpeg','-hide_banner','-loglevel','warning','-y','-f','rawvideo',
             '-pixel_format','rgb24','-video_size',f'{width}x{height}','-framerate',str(fps),
             '-i','-','-an','-c:v','libx264','-preset','fast','-crf','17','-pix_fmt','yuv420p',
             '-force_key_frames',','.join(str(s-start) for s,_ in CHAPTERS if start<=s<end),
             '-movflags','+faststart',str(out/(name+'.mp4'))]
    frames=round((end-start)*fps)
    with log.open('w') as errors:
        p=subprocess.Popen(command,stdin=subprocess.PIPE,stderr=errors)
        try:
            for i in range(frames):
                im=render_frame(e,start+i/fps,render_width).im
                if im.size!=(width,height):im=im.resize((width,height),Image.Resampling.LANCZOS)
                p.stdin.write(im.tobytes())
                if i%max(1,round(fps*6))==0:print(f'{name}: {i}/{frames} frames',flush=True)
            p.stdin.close()
            if p.wait()!=0:raise RuntimeError(log.read_text())
        except BaseException:
            p.kill();p.wait();raise
    print(f'VIDEO PASS {out/(name+".mp4")}',flush=True)


def package(e,out):
    sources=[ROOT/'visualization/data/hero_case_ofc_2026-09-21.json',
             ROOT/'visualization/data/ofc_proposals_visualization_2026-09-21.csv',
             ROOT/'research/results/VULKAX_FINAL_RESULTS_2026-09-21.json',
             e.replay/'trajectory_manifest.json',e.replay/'direction_summary.csv',
             e.replay/'particle_trajectories.csv']
    data_dir=out/'source-data';data_dir.mkdir(exist_ok=True)
    provenance=[]
    for src in sources:
        dst=data_dir/src.name;shutil.copy2(src,dst)
        provenance.append({'source':str(src.relative_to(ROOT)),
                           'sha256':hashlib.sha256(src.read_bytes()).hexdigest()})
    (out/'provenance.json').write_text(json.dumps({
        'frozen_commit':'a9da8c0aa8689ebeea0d84baf95a74907659a837',
        'geometry':'4x4x4 MPM solver body; no bunny or substitute benchmark geometry',
        'display_displacement_magnification':400,
        'residual_normalization_m':e.residual_max,
        'field':'norm of trilinearly interpolated repair displacement minus truth displacement',
        'evidence_class':'frozen synthetic experiment; raw deterministic replay',
        'files':provenance},indent=2)+'\n')
    (out/'chapters.json').write_text(json.dumps(CHAPTERS,indent=2)+'\n')
    source=out/'editable-source';source.mkdir(exist_ok=True)
    for src in HERE.iterdir():
        if src.is_file():shutil.copy2(src,source/src.name)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--replay',type=Path,default=ROOT/'build/reality-probe-explainer/replay')
    p.add_argument('--out',type=Path,default=ROOT/'build/reality-probe-explainer')
    p.add_argument('--mode',choices=['stills','preview','final','check'],default='stills')
    p.add_argument('--width',type=int);p.add_argument('--fps',type=int)
    p.add_argument('--start',type=float,default=0);p.add_argument('--end',type=float,default=CFG['duration_s'])
    args=p.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    subprocess.run(['python3',str(ROOT/'visualization/scripts/verify_frozen_evidence.py')],check=True)
    e=Evidence(args.replay)
    print('EXPLAINER_DATA_CHECK PASS: 36 proposals, all medians/maxima, hero selection, 64 particles, fixed boundary, four compliance bundles',flush=True)
    if args.mode=='check':return
    package(e,args.out)
    if args.mode=='stills':write_stills(e,args.out)
    elif args.mode=='preview':render_video(e,args.out,args.width or 960,args.fps or 15,1,args.start,args.end,'reality_probe_preview')
    else:render_video(e,args.out,args.width or 2560,args.fps or 30,1.25,args.start,args.end,'reality_probe_explainer')


if __name__=='__main__':main()
