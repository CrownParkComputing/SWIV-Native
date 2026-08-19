#!/bin/sh
# Native renderer must be pixel-identical to the reference Python renderer
# (~/swiv-amiga-re/tools/map.py) for every level, with and without objects.
set -e
RE=/home/jon/swiv-amiga-re
mkdir -p build/ref build/native
python3 - <<PY
import sys; sys.path.insert(0,'$RE/tools'); import map as m, os
d=m.Disk('$RE/SWIVFIX.ADF')
for lv in range(7):
    m.render(d,lv,'build/ref/l%d_obj.png'%lv)
    m.render(d,lv,'build/ref/l%d_ter.png'%lv,with_objects=False)
PY
fail=0
for lv in 0 1 2 3 4 5 6; do
  ./build/dumpmap $lv build/native/l${lv}_ter.ppm --bake >/dev/null
  ./build/dumpmap $lv build/native/l${lv}_obj.ppm --objects --bake >/dev/null
  for k in ter obj; do
    r=$(python3 -c "
import numpy as np; from PIL import Image
a=np.asarray(Image.open('build/ref/l${lv}_$k.png').convert('RGB')); b=np.asarray(Image.open('build/native/l${lv}_$k.ppm').convert('RGB'))
print('IDENTICAL' if a.shape==b.shape and (a==b).all() else 'DIFF shape %s vs %s mismatch=%d'%(a.shape,b.shape,int((a!=b).any(axis=2).sum()) if a.shape==b.shape else -1))")
    echo "level $lv $k: $r"; case "$r" in IDENTICAL) ;; *) fail=1;; esac
  done
done
[ $fail = 0 ] && echo "native == python: ALL 14 IMAGES IDENTICAL" || { echo "MISMATCH"; exit 1; }
