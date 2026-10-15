#!/usr/bin/env python3
"""Turn the host renderer's raw L8 frame dumps into previews (Spare original).

  <out>/raw/stills/<name>.pgm      -> <out>/<name>.png (2x, green) + <name>.l8.png (1x L8)
  <out>/raw/anim/<name>/NNNN.pgm   -> <out>/<name>.gif (2x, 20 ms/frame = 50 fps)
  all stills                       -> <out>/contact_sheet.png
"""
import glob, os, sys
from PIL import Image, ImageDraw

def green(im, s=2):
    im = im.resize((im.width * s, im.height * s), Image.NEAREST)
    z = Image.new("L", im.size, 0)
    return Image.merge("RGB", (z, im, z))

def main(out):
    raw = os.path.join(out, "raw")
    stills = sorted(glob.glob(os.path.join(raw, "stills", "*.pgm")))
    sheet = []
    for f in stills:
        n = os.path.splitext(os.path.basename(f))[0]
        im = Image.open(f).convert("L")
        im.save(os.path.join(out, n + ".l8.png"))
        g = green(im); g.save(os.path.join(out, n + ".png")); sheet.append((n, g))
    for d in sorted(glob.glob(os.path.join(raw, "anim", "*"))):
        n = os.path.basename(d)
        fr = [green(Image.open(f).convert("L")) for f in sorted(glob.glob(os.path.join(d, "*.pgm")))]
        if not fr: continue
        pal = Image.new("P", (1, 1)); pal.putpalette([c for i in range(256) for c in (0, i, 0)])
        q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in fr]
        q[0].save(os.path.join(out, n + ".gif"), save_all=True, append_images=q[1:], duration=20, loop=0, optimize=False)
        print("gif", n, len(fr), "frames")
    if sheet:
        w, h = sheet[0][1].size
        rows = (len(sheet) + 1) // 2
        o = Image.new("RGB", (w * 2, rows * (h + 22)), (8, 8, 10)); dr = ImageDraw.Draw(o)
        for i, (n, g) in enumerate(sheet):
            x, y = (i % 2) * w, (i // 2) * (h + 22)
            dr.text((x + 10, y + 5), n, fill=(150, 150, 150)); o.paste(g, (x, y + 22))
        o.save(os.path.join(out, "contact_sheet.png"))
    print("stills", len(stills))

if __name__ == "__main__":
    main(sys.argv[1])
