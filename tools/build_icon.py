"""Rebuild the original vector icon and Windows size-specific raster assets.

Requires Pillow only when regenerating assets; normal C++ builds use app.ico.
"""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1] / "assets"
NAVY, MINT = "#121b24", "#77d7c0"
SVG = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128">
  <title>EyeBreak — eye and clock</title>
  <rect x="4" y="4" width="120" height="120" rx="26" fill="#121b24"/>
  <path d="M18 62 C40 31 88 31 110 62 C88 93 40 93 18 62Z"
        fill="none" stroke="#77d7c0" stroke-width="6" stroke-linejoin="round"/>
  <circle cx="64" cy="62" r="18" fill="#77d7c0"/>
  <path d="M64 49 V63 L74 69" fill="none" stroke="#121b24"
        stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>
  <g fill="#77d7c0"><circle cx="46" cy="104" r="3"/>
    <circle cx="64" cy="104" r="3"/><circle cx="82" cy="104" r="3"/></g>
</svg>
'''

def render(size):
    # Supersampling preserves the curves and transparent corners at tray sizes.
    scale = size * 4 / 128
    image = Image.new("RGBA", (size * 4, size * 4))
    draw = ImageDraw.Draw(image)
    def coords(values): return tuple(round(v * scale) for v in values)
    draw.rounded_rectangle(coords((4, 4, 124, 124)), round(26 * scale), fill=NAVY)
    def curve(a, b, c, d):
        return [tuple((1-t)**3*a[j]+3*(1-t)**2*t*b[j]+3*(1-t)*t*t*c[j]+t**3*d[j]
                      for j in (0, 1)) for t in (i/96 for i in range(97))]
    points = curve((18,62),(40,31),(88,31),(110,62))
    points += curve((110,62),(88,93),(40,93),(18,62))
    draw.line([coords(p) for p in points], fill=MINT, width=round(6*scale), joint="curve")
    for x in (18,110): draw.ellipse(coords((x-3,59,x+3,65)),fill=MINT)
    draw.ellipse(coords((46,44,82,80)), fill=MINT)
    hand = [(64,49),(64,63),(74,69)]
    draw.line([coords(p) for p in hand], fill=NAVY, width=max(1,round(4*scale)), joint="curve")
    for x,y in hand: draw.ellipse(coords((x-2,y-2,x+2,y+2)),fill=NAVY)
    # The three 20/20/20 marks are omitted below 48 px for a cleaner tray icon.
    if size >= 48:
        for x in (46,64,82): draw.ellipse(coords((x-3,101,x+3,107)),fill=MINT)
    return image.resize((size,size),Image.Resampling.LANCZOS)

if __name__ == "__main__":
    ROOT.mkdir(exist_ok=True)
    (ROOT / "app-icon.svg").write_text(SVG, encoding="utf-8")
    render(512).save(ROOT / "app-icon.png")
    sizes = (16,20,24,32,40,48,64,128,256)
    icons = [render(size) for size in sizes]
    icons[-1].save(ROOT / "app.ico", sizes=[(s,s) for s in sizes], append_images=icons[:-1])
    print("Created SVG, 512 px PNG, and nine-size ICO in", ROOT)
