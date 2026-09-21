#!/usr/bin/env python3
"""Build a shot-based VULKAX verification animation."""
from __future__ import annotations
import argparse, math, sys
from pathlib import Path
import bpy
from mathutils import Vector

HERE=Path(__file__).resolve().parent
if str(HERE) not in sys.path: sys.path.insert(0,str(HERE))
import aesthetic_bunny_scene as hero

def parse_args():
    argv=sys.argv; args=argv[argv.index("--")+1:] if "--" in argv else []
    p=argparse.ArgumentParser(); p.add_argument("--trajectory-dir",type=Path,required=True); p.add_argument("--bunny",type=Path,required=True)
    p.add_argument("--direction",choices=["px","nx","py","pz"],default="px"); p.add_argument("--motion-scale",type=float,default=400.0)
    p.add_argument("--output",type=Path,required=True); p.add_argument("--render",action="store_true"); return p.parse_args(args)

def created_since(before): return [o for o in bpy.data.objects if o not in before]
def set_visible(objects,frame,visible):
    for obj in objects:
        obj.hide_render=not visible; obj.hide_viewport=not visible
        obj.keyframe_insert(data_path="hide_render",frame=frame); obj.keyframe_insert(data_path="hide_viewport",frame=frame)
def shot_visibility(objects,start,end):
    set_visible(objects,max(1,start-1),False); set_visible(objects,start,True); set_visible(objects,end,True); set_visible(objects,end+1,False)
def mats():
    return {"stage":hero.material("anim_stage",hero.DARK_METAL,metallic=.9,roughness=.17),
    "cyan":hero.material("anim_cyan",hero.CYAN,metallic=.1,roughness=.18,emission=.8),
    "cyan_glow":hero.material("anim_cyan_glow",hero.CYAN_SOFT,metallic=0,roughness=.12,emission=5),
    "orange_glow":hero.material("anim_orange",hero.AMBER,metallic=0,roughness=.12,emission=5),
    "pearl":hero.material("anim_pearl",(.8,.86,.94,1),metallic=.3,roughness=.1,emission=.035),
    "ghost":hero.material("anim_ghost",hero.CYAN_SOFT,metallic=0,roughness=.08,emission=1.2,alpha=.16),
    "repair_trans":hero.material("anim_repair_trans",hero.ORANGE,metallic=0,roughness=.12,emission=.6,alpha=.28)}

def main():
    args=parse_args(); groups,last=hero.load_states(args.trajectory_dir/"particle_trajectories.csv",args.direction)
    scene=hero.configure_scene("eevee",64); scene.render.resolution_x=1920; scene.render.resolution_y=1080; scene.render.resolution_percentage=100
    scene.frame_start=1; scene.frame_end=450; scene.render.fps=30; scene.world.color=(.004,.010,.025)
    mm=mats(); hero.add_pedestal(0,mm["stage"],mm["cyan_glow"]); hero.add_torus("anim_portal",(0,.6,1.48),1.88,.018,mm["cyan_glow"],rotation=(math.radians(90),0,0))
    source=hero.import_ply(args.bunny); params,rest_display=hero.orient_and_normalize_vertices(source.data); source.hide_render=True; source.hide_viewport=True
    before=set(bpy.data.objects); hero.create_deformed_bunny(source,"Anim_Observe_Wire",params,rest_display,groups,"baseline_apic",last,0,args.motion_scale,mm["cyan"],animate=False,wireframe=True)
    hero.create_deformed_bunny(source,"Anim_Observe_Ghost",params,rest_display,groups,"baseline_apic",last,0,args.motion_scale,mm["ghost"],animate=False)
    hero.add_solver_lattice(groups,"baseline_apic",last,0,args.motion_scale,mm["cyan_glow"],mm["cyan"]); observe=created_since(before)
    before=set(bpy.data.objects); hero.create_deformed_bunny(source,"Anim_Repair",params,rest_display,groups,"repair_pic",last,0,args.motion_scale,mm["pearl"],animate=False); repair=created_since(before)
    before=set(bpy.data.objects); hero.create_deformed_bunny(source,"Anim_Interrogate_Repair",params,rest_display,groups,"repair_pic",last,0,args.motion_scale,mm["repair_trans"],animate=False)
    hero.create_deformed_bunny(source,"Anim_Interrogate_Truth",params,rest_display,groups,"truth",last,0,args.motion_scale,mm["ghost"],animate=False,wireframe=True,ghost=True)
    hero.add_residual_vectors(groups,last,0,args.motion_scale,mm["cyan_glow"],mm["orange_glow"]); pre_probe=set(bpy.data.objects); hero.add_probe(0,args.direction,mm["stage"],mm["orange_glow"])
    probe=created_since(pre_probe); interrogate=[o for o in created_since(before) if o not in probe]
    shot_visibility(observe,1,145); shot_visibility(repair,146,285); shot_visibility(interrogate,286,450); shot_visibility(probe,330,450)
    scan_mat=hero.material("scanner",hero.CYAN_SOFT,metallic=0,roughness=.05,emission=6,alpha=.18); scanner=hero.add_box("scanner_plane",(-1.8,-.15,1.45),(.018,1.45,1.75),scan_mat,bevel=.005)
    scanner.keyframe_insert(data_path="location",frame=18); scanner.location.x=1.8; scanner.keyframe_insert(data_path="location",frame=125); shot_visibility([scanner],18,130)
    force={"px":Vector((1,0,0)),"nx":Vector((-1,0,0)),"py":Vector((0,0,1)),"pz":Vector((0,1,0))}[args.direction]
    for obj in probe:
        final=obj.location.copy(); obj.location=final-force*1.15; obj.keyframe_insert(data_path="location",frame=330); obj.location=final; obj.keyframe_insert(data_path="location",frame=382)
    bpy.ops.object.camera_add(location=(0,-9.6,3.65)); cam=bpy.context.object; cam.data.lens=56; target=Vector((0,0,1.35))
    for frame,loc in [(1,(-.55,-9.8,3.75)),(145,(.25,-9.2,3.45)),(146,(.2,-9.1,3.5)),(285,(-.25,-8.8,3.4)),(286,(-.35,-9.7,3.7)),(450,(.25,-9.2,3.5))]:
        cam.location=loc; cam.rotation_euler=(target-cam.location).to_track_quat("-Z","Y").to_euler(); cam.keyframe_insert(data_path="location",frame=frame); cam.keyframe_insert(data_path="rotation_euler",frame=frame)
    scene.camera=cam
    bpy.ops.object.light_add(type="AREA",location=(0,-2.5,7)); key=bpy.context.object; key.data.energy=1700; key.data.size=6
    bpy.ops.object.light_add(type="AREA",location=(-4,0,4.5)); fill=bpy.context.object; fill.data.energy=850; fill.data.color=(.04,.38,1); fill.data.size=4
    bpy.ops.object.light_add(type="AREA",location=(4,.6,4.7)); rim=bpy.context.object; rim.data.energy=1050; rim.data.color=(1,.18,.03); rim.data.size=4
    args.output.parent.mkdir(parents=True,exist_ok=True); bpy.ops.wm.save_as_mainfile(filepath=str(args.output.with_suffix(".blend")))
    if args.render:
        settings=scene.render.image_settings
        if hasattr(settings,"media_type"): settings.media_type="VIDEO"
        else: settings.file_format="FFMPEG"
        scene.render.ffmpeg.format="MPEG4"; scene.render.ffmpeg.codec="H264"; scene.render.filepath=str(args.output.with_suffix(".mp4")); bpy.ops.render.render(animation=True)
    print(f"saved {args.output.with_suffix('.blend')}"); print("VERIFICATION_ANIMATION PASS")
if __name__=="__main__": main()
