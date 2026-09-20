#!/usr/bin/env python3
import importlib.util,json,pathlib,tempfile
path=pathlib.Path(__file__).with_name("gauge_deformable_to_vulkax.py")
spec=importlib.util.spec_from_file_location("gauge_adapter",path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
metadata={"assets":{"foam":{"material":{"soft":{"mass":0.0066,"density":13.2,"young":86410,"poisson":0.23,"friction":0,"restitution":0}}}},
"tasks":{"default":{"record":{"fps":30,"dim":["x","y","z"]},"fixed":{"base-1":True,"base-2":False,"foam":False},"kinematic":{"base-1":False,"base-2":True,"foam":False}}}}
trial={"FPS":30,"translation unit":"mm","base":{"x":[0,10],"y":[0,0],"z":[0,20]},
"foam":{"marker-01":{"x":[1000,1010],"y":[2000,2000],"z":[3000,3020]},"marker-02":{"x":[500,510],"y":[600,610],"z":[700,710]}}}
with tempfile.TemporaryDirectory() as td:
    d=pathlib.Path(td);mp=d/"meta.json";tp=d/"trial.json";o=d/"out";mp.write_text(json.dumps(metadata));tp.write_text(json.dumps(trial))
    manifest=m.convert(mp,tp,"soft",o)
    assert manifest["provenance"]=="measured" and manifest["marker_count"]==2 and manifest["frame_count"]==2
    assert manifest["material"]["young_modulus_pa"]==86410 and manifest["material"]["poisson_ratio"]==0.23
    lines=(o/"markers.csv").read_text().splitlines()
    assert "1.0,2.0,3.0" in lines[1]
    assert (o/"driver.csv").exists()
print("VALID GAUGE deformable adapter fixture")
