#!/usr/bin/env python3
"""Build a procedural Blender animation driven by exported VULKAX solver trajectories.

The display-space deformation may be magnified, but every particle displacement
direction and magnitude comes from the replayed solver state. The magnification is
printed in-scene and defaults to 400x.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

import bpy
from mathutils import Vector


def parse_args():
    argv=sys.argv
    args=argv[argv.index("--")+1:] if "--" in argv else []
    p=argparse.ArgumentParser()
    p.add_argument("--trajectory-dir",type=Path,required=True)
    p.add_argument("--direction",choices=["px","nx","py","pz"],default="px")
    p.add_argument("--motion-scale",type=float,default=400.0)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--render",action="store_true")
    return p.parse_args(args)


def mat(name,color,metallic=.1,rough=.35,emission=0.0):
    m=bpy.data.materials.new(name)
    m.use_nodes=True
    bsdf=m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value=(*color,1.0)
    bsdf.inputs["Metallic"].default_value=metallic
    bsdf.inputs["Roughness"].default_value=rough
    if emission>0:
        bsdf.inputs["Emission Color"].default_value=(*color,1.0)
        bsdf.inputs["Emission Strength"].default_value=emission
    return m


def add_text(value,loc,size,material,align="CENTER"):
    bpy.ops.object.text_add(location=loc,rotation=(math.radians(90),0,0))
    obj=bpy.context.object
    obj.data.body=value
    obj.data.size=size
    obj.data.align_x=align
    obj.data.extrude=.008
    obj.data.bevel_depth=.002
    obj.data.materials.append(material)
    return obj


def add_box(loc,scale,material,bevel=.08):
    bpy.ops.mesh.primitive_cube_add(location=loc)
    obj=bpy.context.object
    obj.scale=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    mod=obj.modifiers.new("bevel","BEVEL"); mod.width=bevel; mod.segments=3
    obj.data.materials.append(material)
    return obj


def cylinder_between(a,b,radius,material):
    a,b=Vector(a),Vector(b)
    d=b-a
    bpy.ops.mesh.primitive_cylinder_add(vertices=20,radius=radius,depth=d.length,location=(a+b)*.5)
    obj=bpy.context.object
    obj.rotation_mode="QUATERNION"
    obj.rotation_quaternion=d.to_track_quat("Z","Y")
    obj.data.materials.append(material)
    return obj


def add_arrow(start,end,material):
    a,b=Vector(start),Vector(end)
    d=(b-a)
    unit=d.normalized()
    cylinder_between(a,b-unit*.22,.025,material)
    bpy.ops.mesh.primitive_cone_add(vertices=24,radius1=.09,radius2=0,depth=.25,location=b-unit*.11)
    cone=bpy.context.object
    cone.rotation_mode="QUATERNION"
    cone.rotation_quaternion=d.to_track_quat("Z","Y")
    cone.data.materials.append(material)


def load(path,direction):
    groups=defaultdict(dict)
    max_frame=0
    with path.open(newline="",encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["direction"]!=direction: continue
            frame=int(r["frame"]); pid=int(r["particle_id"])
            groups[(r["model"],frame)][pid]={
                "rest":Vector((float(r["rest_x"]),float(r["rest_y"]),float(r["rest_z"]))),
                "pos":Vector((float(r["x"]),float(r["y"]),float(r["z"]))),
                "top":r["is_top"]=="1",
                "bottom":r["is_bottom"]=="1",
            }
            max_frame=max(max_frame,frame)
    return groups,max_frame


def main():
    args=parse_args()
    if not math.isfinite(args.motion_scale) or args.motion_scale<=0:
        raise SystemExit("motion scale must be finite and positive")

    manifest=json.loads((args.trajectory_dir/"trajectory_manifest.json").read_text(encoding="utf-8"))
    groups,last=load(args.trajectory_dir/"particle_trajectories.csv",args.direction)
    if last+1!=manifest["frames_per_direction"]:
        raise SystemExit("trajectory frame count does not match manifest")

    bpy.ops.object.select_all(action="SELECT"); bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene
    try: scene.render.engine="BLENDER_EEVEE_NEXT"
    except Exception: scene.render.engine="BLENDER_EEVEE"
    scene.render.resolution_x=1920; scene.render.resolution_y=1080; scene.render.resolution_percentage=100
    scene.frame_start=1; scene.frame_end=1+last*3
    scene.world.color=(.008,.014,.028)

    white=mat("white",(.88,.93,1.0),.15,.30)
    truth_mat=mat("truth",(.82,.88,.96),.25,.25,.25)
    base_mat=mat("baseline",(.12,.38,.95),.2,.25,.55)
    repair_mat=mat("repair",(1.0,.28,.05),.2,.25,.65)
    amber=mat("force",(1.0,.60,.08),.2,.2,.75)
    navy=mat("stage",(.025,.055,.10),.35,.25)
    muted=mat("muted",(.35,.43,.56),.1,.4)

    models=[
        ("truth","TRUTH  APIC",truth_mat,-3.7),
        ("baseline_apic","BASELINE  APIC",base_mat,0.0),
        ("repair_pic","REPAIR  PIC",repair_mat,3.7),
    ]

    add_box((0,0,-1.05),(6.8,3.4,.08),navy,.03)
    for _,label,material,offset in models:
        add_box((offset,0,-.58),(1.55,1.55,.12),navy,.12)
        add_text(label,(offset,1.55,2.9),.28,material)
        # One sphere per real MPM particle. Position keyframes are raw displacement
        # multiplied by the declared display-space motion scale.
        initial=groups[(_,0)] if False else None

    for model,label,material,offset in models:
        frame0=groups[(model,0)]
        for pid in sorted(frame0):
            p0=frame0[pid]
            bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,radius=.055,location=(0,0,0))
            obj=bpy.context.object
            obj.name=f"{model}_p{pid:03d}"
            obj.data.materials.append(material)
            for f in range(last+1):
                state=groups[(model,f)][pid]
                display=state["rest"] + (state["pos"]-state["rest"])*args.motion_scale
                obj.location=(display.x+offset,display.y,display.z+.55)
                obj.keyframe_insert(data_path="location",frame=1+f*3)

    force_vec={
        "px":Vector((1,0,0)),"nx":Vector((-1,0,0)),
        "py":Vector((0,1,0)),"pz":Vector((0,0,1))
    }[args.direction]
    center=Vector((0,-1.7,2.2))
    add_arrow(center-force_vec*.65,center+force_vec*.65,amber)

    add_text("VULKAX  |  SOLVER-STATE REALITY INSPECTOR",(0,2.25,4.25),.42,white)
    add_text(
        f"RAW MPM TRAJECTORY • 40 N / TOP PARTICLE • DIRECTION {args.direction.upper()}",
        (0,2.25,3.72),.20,muted
    )
    add_text(
        f"MOTION MAGNIFIED ×{args.motion_scale:g} FOR VISIBILITY — NUMERICAL DATA UNSCALED",
        (0,2.25,3.38),.19,amber
    )
    add_text(
        "truth world 5 • frozen APIC → PIC deceptive proposal",
        (0,2.25,-1.18),.20,muted
    )

    bpy.ops.object.camera_add(location=(0,-15.8,6.4))
    cam=bpy.context.object
    cam.data.lens=48
    target=Vector((0,0,1.0))
    cam.rotation_euler=(target-cam.location).to_track_quat("-Z","Y").to_euler()
    scene.camera=cam

    bpy.ops.object.light_add(type="AREA",location=(0,-3,8))
    key=bpy.context.object; key.data.energy=1800; key.data.size=10
    bpy.ops.object.light_add(type="AREA",location=(-6,2,4))
    fill=bpy.context.object; fill.data.energy=900; fill.data.color=(.12,.35,1.0); fill.data.size=5
    bpy.ops.object.light_add(type="AREA",location=(6,2,4))
    rim=bpy.context.object; rim.data.energy=950; rim.data.color=(1.0,.20,.04); rim.data.size=5

    args.output.parent.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output.with_suffix(".blend")))

    if args.render:
        settings=scene.render.image_settings
        if hasattr(settings,"media_type"):
            settings.media_type="VIDEO"
        else:
            settings.file_format="FFMPEG"
        scene.render.ffmpeg.format="MPEG4"
        scene.render.ffmpeg.codec="H264"
        try:
            scene.render.ffmpeg.constant_rate_factor="HIGH"
        except Exception:
            pass
        scene.render.fps=30
        scene.render.filepath=str(args.output.with_suffix(".mp4"))
        bpy.ops.render.render(animation=True)

    print(f"saved {args.output.with_suffix('.blend')}")
    print(f"display motion scale: {args.motion_scale:g}x; all source displacements are raw solver state")


if __name__=="__main__":
    main()
