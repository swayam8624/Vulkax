#!/usr/bin/env python3
from __future__ import annotations
import argparse
import xml.etree.ElementTree as ET
from pathlib import Path

def walk(node, transformed=False):
    here = transformed or bool(node.attrib.get("transform"))
    tag = node.tag.split("}")[-1]
    if tag == "image":
        if here:
            raise SystemExit("Illustrator-unsafe SVG: <image> is nested under a transformed element")
        for key in ("x","y","width","height","href"):
            if key not in node.attrib:
                raise SystemExit(f"Illustrator-unsafe SVG: <image> missing {key}")
        if "preserveAspectRatio" in node.attrib:
            raise SystemExit("Illustrator-unsafe SVG: raster placement relies on preserveAspectRatio")
        if "{http://www.w3.org/1999/xlink}href" in node.attrib:
            raise SystemExit("Illustrator-unsafe SVG: duplicate xlink:href present")
    for child in node:
        walk(child, here)

def main():
    p=argparse.ArgumentParser()
    p.add_argument("svg",type=Path,nargs="+")
    a=p.parse_args()
    for path in a.svg:
        root=ET.parse(path).getroot()
        walk(root,False)
        print(f"ILLUSTRATOR_SVG PASS {path}")

if __name__=="__main__":
    main()
