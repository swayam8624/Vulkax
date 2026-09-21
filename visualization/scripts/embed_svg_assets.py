#!/usr/bin/env python3
from __future__ import annotations
import argparse, base64, mimetypes, re
from pathlib import Path

PATTERN = re.compile(r'(?P<attr>(?:xlink:)?href)="(?P<path>assets/[^"]+)"')

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--svg",type=Path,required=True)
    p.add_argument("--out",type=Path,required=True)
    a=p.parse_args()
    text=a.svg.read_text(encoding="utf-8")

    cache={}
    def repl(m):
        rel=m.group("path")
        if rel not in cache:
            path=a.svg.parent / rel
            if not path.is_file():
                raise SystemExit(f"missing linked SVG asset: {path}")
            mime=mimetypes.guess_type(path.name)[0] or "application/octet-stream"
            data=base64.b64encode(path.read_bytes()).decode("ascii")
            cache[rel]=f"data:{mime};base64,{data}"
        return f'{m.group("attr")}="{cache[rel]}"'

    out=PATTERN.sub(repl,text)
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(out,encoding="utf-8")
    print(a.out)
    print(f"embedded {len(cache)} linked assets")

if __name__=="__main__":
    main()
