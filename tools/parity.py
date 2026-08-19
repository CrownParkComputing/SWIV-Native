#!/usr/bin/env python3
"""parity.py HOST_OBJLOG NATIVE_OBJLOG --names NAMES [--gfx HEX] [--window N]

Align the two object logs by SCROLL position (3530) and compare, per graphic:
  * how many objects of that graphic are alive at each scroll position
  * the first divergence (scroll, host count, native count)
  * for --gfx: the per-object trajectories (x, y-scroll, z) sampled every 8 px of scroll
Everything in map units of the original (scroll counts down)."""
import argparse, collections, sys

def s16(v): return v - 65536 if v > 32767 else v
def load(path):
    scroll = {}; frames = collections.defaultdict(list)
    for line in open(path):
        p = line.split()
        if p[1] == "SCROLL": scroll[int(p[0])] = int(p[2], 16); continue
        frames[int(p[0])].append((p[1], int(p[2], 16), int(p[3]), int(p[4]), int(p[5]), p[6]))
    # index by scroll: first frame at which scroll == s
    by_scroll = {}
    for f in sorted(scroll):
        s = scroll[f]
        if s not in by_scroll: by_scroll[s] = f
    return scroll, frames, by_scroll

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host"); ap.add_argument("native"); ap.add_argument("--names", required=True)
    ap.add_argument("--gfx"); ap.add_argument("--window", type=int, default=4)
    ap.add_argument("--filter", help="only gfx with this name substring")
    a = ap.parse_args()
    names = [l.strip() for l in open(a.names)]
    def nm(g): return "%s#%d" % (names[g & 0x1ff] if (g & 0x1ff) < len(names) else "?", g >> 9)
    hs, hf, hb = load(a.host); ns, nf, nb = load(a.native)
    common = sorted(set(hb) & set(nb), reverse=True)      # scroll counts down
    print("common scroll range: %04x..%04x (%d positions)" % (common[0], common[-1], len(common)))
    # per gfx counts
    allg = set()
    def counts(frames, f):
        c = collections.Counter()
        for o in frames.get(f, []): 
            if o[1] & 0x1ff < len(names) and not (o[1] == 0): c[o[1]] += 1
        return c
    first_div = {}; worst = collections.Counter()
    for s in common[::a.window]:
        ch = counts(hf, hb[s]); cn = counts(nf, nb[s])
        for gfx in set(ch) | set(cn):
            allg.add(gfx)
            if ch[gfx] != cn[gfx]:
                worst[gfx] += abs(ch[gfx] - cn[gfx])
                if gfx not in first_div: first_div[gfx] = (s, ch[gfx], cn[gfx])
    print("%-18s %-8s %s" % ("graphic", "divergence", "first divergence (scroll: host vs native)"))
    for gfx, w in worst.most_common(40):
        if a.filter and a.filter.lower() not in nm(gfx).lower(): continue
        fd = first_div[gfx]; print("%-18s %6d     %04x: %d vs %d" % (nm(gfx), w, fd[0], fd[1], fd[2]))
    if a.gfx:
        gfx = int(a.gfx, 16)
        print("\ntrajectories for", nm(gfx), "(x, y-scroll, z) every 8 px of scroll; host | native")
        for s in common[::8]:
            h = [(o[2], s16(o[3]) - s16(hs[hb[s]]), o[4]) for o in hf.get(hb[s], []) if o[1] == gfx]
            n = [(o[2], s16(o[3]) - s16(ns[nb[s]]), o[4]) for o in nf.get(nb[s], []) if o[1] == gfx]
            if h or n: print("%04x  H:%s\n      N:%s" % (s, h[:6], n[:6]))

if __name__ == "__main__":
    sys.exit(main())
