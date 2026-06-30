#!/usr/bin/env python3
"""
tbcompare — rigorous titlebar pixel comparison vs a reference screenshot.

The reference and our render can have different window WIDTHS, so a naive
overlay fails on right-anchored elements (min/max/close buttons).  This tool
detects the relevant feature edges (sysmenu icon on the left, the close
button on the right, the title text band) and reports EXACT pixel positions
in both images, plus a right-edge-aligned diff so streak↔button overlap is
unambiguous.

Usage:
  tbcompare.py REF.png REF_X0 REF_Y0 REF_X1 REF_Y1  CUR.png  [out.png]
Coordinates are the titlebar rect in the reference. CUR is composited over a
neutral desktop colour first (it has alpha).
"""
import sys
from PIL import Image

def load_rgb(path, compose=False):
    im = Image.open(path).convert("RGBA")
    if compose:
        bg = Image.new("RGBA", im.size, (212, 208, 200, 255))
        im = Image.alpha_composite(bg, im)
    return im.convert("RGB")

def bright(p): return (p[0]+p[1]+p[2])//3

def scan_row(im, y, x0, x1):
    """Return list of (x, brightness) across a row."""
    return [(x, bright(im.getpixel((x, y)))) for x in range(x0, x1)]

def find_dark_runs(im, y, x0, x1, thr=70):
    """x-ranges where the row is DARK (button gaps / button outlines / text)."""
    runs = []; start = None
    for x in range(x0, x1):
        d = bright(im.getpixel((x, y))) < thr
        if d and start is None: start = x
        elif not d and start is not None: runs.append((start, x-1)); start = None
    if start is not None: runs.append((start, x1-1))
    return runs

def find_streak_end_from_right(im, y, x_from, x_to_left, light_thr=120):
    """Walk leftward from x_from until we leave the buttons and re-enter the
    continuous light streak; return the streak's right end x."""
    # crude: the streak is a long light run; buttons are a dense dark/light mix
    # report the rightmost x that begins a >=8px continuous light run
    x = x_from
    while x > x_to_left:
        run = all(bright(im.getpixel((xx, y))) > light_thr for xx in range(x-8, x))
        if run: return x-1
        x -= 1
    return None

def main():
    ref_p = sys.argv[1]
    rx0, ry0, rx1, ry1 = map(int, sys.argv[2:6])
    cur_p = sys.argv[6]
    out_p = sys.argv[7] if len(sys.argv) > 7 else "/tmp/tbcompare_out.png"

    ref = load_rgb(ref_p)
    cur = load_rgb(cur_p, compose=True)
    rw, rh = rx1-rx0, ry1-ry0
    ymid = (ry0+ry1)//2

    print(f"=== titlebar compare: ref[{rx0},{ry0}..{rx1},{ry1}] ({rw}x{rh}) vs cur({cur.size[0]}x{cur.size[1]}) ===")
    print(f"sample row y={ymid} (ref) / y={ymid-ry0} (cur-local)")

    cy = ymid - ry0   # cur uses local coords (titlebar at top of its own render)
    # 1) right-edge-aligned button comparison: find the rightmost dark feature
    ref_darks = find_dark_runs(ref, ymid, rx0, rx1)
    cur_darks = find_dark_runs(cur, cy, 0, cur.size[0])
    print(f"\nREF dark runs (x, local x):", [(a-rx0, b-rx0) for a, b in ref_darks][-8:])
    print(f"CUR dark runs:", cur_darks[-8:])

    # 2) streak right-end: where the continuous light streak ends before buttons
    # find the right edge of the window first = last column with any non-desktop content
    def window_right(im, y, x_start, x_max):
        last = x_start
        for x in range(x_start, x_max):
            if im.getpixel((x, y)) != (212,208,200): last = x
        return last
    ref_right = window_right(ref, ymid, rx0, rx1)
    cur_right = cur.size[0]-1
    print(f"\nwindow right edge: ref(local)={ref_right-rx0}  cur={cur_right}")

    # 3) build a magnified, right-aligned side-by-side
    scale = 3
    band = ref.crop((rx0, ry0, rx1, ry1)).resize((rw*scale, rh*scale), Image.NEAREST)
    cband = cur.crop((0, 0, cur.size[0], rh)).resize((cur.size[0]*scale, rh*scale), Image.NEAREST)
    W = max(band.width, cband.width)
    out = Image.new("RGB", (W, rh*scale*2 + 6), (255, 0, 255))
    out.paste(band, (0, 0))                       # ref left-aligned
    out.paste(cband, (0, rh*scale + 6))           # cur left-aligned
    out.save(out_p)
    print(f"\nsaved {out_p} (ref top, cur bottom, both left-aligned, {scale}x)")

if __name__ == "__main__":
    main()
