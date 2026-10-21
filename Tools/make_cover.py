#!/usr/bin/env python3
# "NEW PLUGIN" borítókép a weboldal tetejére (és a link-előnézethez): a legújabb plugin nagy panelképe, jelvény, név, alcím.
#   python3 -I Tools/make_cover.py <panel.png (ui_snapshot, 2x)> <NÉV> "<alcím>" "<rövid mondat>" docs/assets/new-plugin.jpg [glow hex]
#   (a jelvény szövege a BADGE környezeti változóval állítható, alapból "NEW PLUGIN")
# A panelkép: build/ui_snapshot aufx <subtype> Ekid panel.png (zárt fiókkal).
import os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

panel_path, name, subtitle, line, out = sys.argv[1:6]
glow = tuple(int(sys.argv[6][i:i + 2], 16) for i in (0, 2, 4)) if len(sys.argv) > 6 else (242, 163, 58)

W, H = 2000, 1080
FUT = "/System/Library/Fonts/Supplemental/Futura.ttc"
AVN = "/System/Library/Fonts/Avenir Next.ttc"
def font(path, size, index=0):
    try: return ImageFont.truetype(path, size, index=index)
    except OSError: return ImageFont.load_default()

# háttér: mély fekete, a panel mögött meleg fényudvar
bg = Image.new("RGB", (W, H), (11, 11, 13))
halo = Image.new("L", (W, H), 0)
ImageDraw.Draw(halo).ellipse((W * 0.12, H * 0.38, W * 0.88, H * 1.08), fill=255)
halo = halo.filter(ImageFilter.GaussianBlur(160))
bg = Image.composite(Image.new("RGB", (W, H), tuple(int(c * 0.42) for c in glow)), bg, halo)

d = ImageDraw.Draw(bg)
# jelvény
badge_font = font(AVN, 30, 2)
bt = os.environ.get("BADGE", "NEW PLUGIN")
bw = d.textlength(bt, font=badge_font) + 56
d.rounded_rectangle(((W - bw) / 2, 64, (W + bw) / 2, 64 + 58), 29, fill=glow)
d.text((W / 2, 64 + 29), bt, font=badge_font, fill=(26, 16, 6), anchor="mm")
# név, alcím, mondat
d.text((W / 2, 196), name, font=font(FUT, 118, 2), fill=(246, 240, 228), anchor="mm")
d.text((W / 2, 286), subtitle.upper(), font=font(AVN, 30, 2), fill=glow, anchor="mm")
d.text((W / 2, 340), line, font=font(AVN, 30, 0), fill=(200, 202, 212), anchor="mm")

# panel árnyékkal és halvány tükröződéssel
panel = Image.open(panel_path).convert("RGBA")
pw = 1640
ph = round(panel.height * pw / panel.width)
panel = panel.resize((pw, ph), Image.LANCZOS)
mask = Image.new("L", (pw, ph), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, pw - 1, ph - 1), 22, fill=255)
px, py = (W - pw) // 2, 410
shadow = Image.new("L", (W, H), 0)
ImageDraw.Draw(shadow).rounded_rectangle((px + 10, py + 30, px + pw - 10, py + ph + 30), 30, fill=200)
shadow = shadow.filter(ImageFilter.GaussianBlur(36))
bg = Image.composite(Image.new("RGB", (W, H), (0, 0, 0)), bg, shadow)
bg.paste(panel, (px, py), mask)
refl = panel.transpose(Image.FLIP_TOP_BOTTOM).crop((0, 0, pw, min(110, H - (py + ph + 8))))
fade = Image.linear_gradient("L").resize(refl.size).point(lambda v: int((255 - v) * 0.16))
rm = Image.new("L", refl.size, 0); rm.paste(fade, (0, 0), mask.transpose(Image.FLIP_TOP_BOTTOM).crop((0, 0) + refl.size))
bg.paste(refl.convert("RGB"), (px, py + ph + 8), rm)
bg.save(out, quality=88)
print(out, bg.size)
