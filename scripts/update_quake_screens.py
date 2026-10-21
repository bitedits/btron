#!/usr/bin/env python3
"""
update_quake_screens.py - Copy Quake_*.png from b-system/img/screens/
into b-system/apps/img/ for use by Quake.html.

raw_to_png.py already produces b-system/img/screens/Quake_*.png from
/tmp/btron_raw_screens/Quake_*.raw.  This script just makes sure the
Quake.html image directory is also populated.
"""
import os, shutil, glob

BASE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC  = os.path.join(BASE, "b-system", "img", "screens")
DST  = os.path.join(BASE, "b-system", "apps", "img")

os.makedirs(DST, exist_ok=True)

count = 0
for src_path in sorted(glob.glob(os.path.join(SRC, "Quake_*.png"))):
    name = os.path.basename(src_path)
    dst_path = os.path.join(DST, name)
    shutil.copy2(src_path, dst_path)
    print(f"  [QUAKE IMG] {name}")
    count += 1

print(f"=== {count} Quake PNG(s) installed to {DST} ===")
