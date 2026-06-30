#!/usr/bin/env python3
"""
titlemeasure — measure a titlebar's title-text extent/center/cap-height.

The title text carries a 1px black drop-shadow (shadowx/y=1, shadowcolor 0,0,0),
so its glyph columns contain near-black pixels in the text band, while the streak
bars never go near-black there.  That lets us isolate the title precisely and
report: left edge, right edge, width, center, and the glyph vertical span
(cap-height) — independent of window width or right-anchored buttons.

Usage:
  titlemeasure.py IMG.png X0 Y0 X1 Y1 [--compose] [--label NAME]
Coordinates = the titlebar rect.  --compose flattens alpha over dark desktop.
"""
import sys
from PIL import Image

def load(path, compose):
    im = Image.open(path).convert("RGBA")
    if compose:
        bg = Image.new("RGBA", im.size, (40, 40, 40, 255))
        im = Image.alpha_composite(bg, im)
    return im.convert("RGB")

def main():
    p = sys.argv[1]
    x0, y0, x1, y1 = map(int, sys.argv[2:6])
    compose = "--compose" in sys.argv
    label = "img"
    if "--label" in sys.argv:
        label = sys.argv[sys.argv.index("--label")+1]
    im = load(p, compose); px = im.load()

    def b(x, y):
        r, g, bb = px[x, y]; return (r+g+bb)//3

    # text band: skip the top/bottom 1px streak bevel
    ty0, ty1 = y0+1, y1-1
    SHADOW = 45           # near-black shadow threshold
    # per-column: does it contain a shadow pixel in the text band?
    cols = []
    for x in range(x0, x1):
        has = any(b(x, y) < SHADOW for y in range(ty0, ty1))
        cols.append(has)
    # find the title cluster = longest run of columns with shadow density,
    # allowing small gaps (inter-glyph spaces up to 4px)
    runs = []; start = None; gap = 0
    for i, h in enumerate(cols):
        if h:
            if start is None: start = i
            gap = 0
        else:
            if start is not None:
                gap += 1
                if gap > 5:
                    runs.append((start, i-gap)); start = None; gap = 0
    if start is not None: runs.append((start, len(cols)-1-gap))
    if not runs:
        print(f"[{label}] no title detected"); return
    # the title is the widest run near the center
    cx = (x1-x0)/2
    runs.sort(key=lambda r: (r[1]-r[0]), reverse=True)
    L, R = runs[0]
    left = x0+L; right = x0+R
    width = right-left+1
    center_local = (left+right)/2 - x0
    win_w = x1-x0
    # cap-height: vertical span of glyph (non-shadow bright) pixels within the run
    ys = []
    for x in range(left, right+1):
        for y in range(ty0, ty1):
            if b(x, y) > 150:   # bright glyph body
                ys.append(y)
    cap = (max(ys)-min(ys)+1) if ys else 0
    print(f"[{label}] title L={left} R={right} W={width} "
          f"center_local={center_local:.1f} win_w={win_w} win_center={win_w/2:.1f} "
          f"off_from_center={center_local-win_w/2:+.1f} cap_h={cap} "
          f"(glyph y {min(ys) if ys else '-'}..{max(ys) if ys else '-'})")

if __name__ == "__main__":
    main()
