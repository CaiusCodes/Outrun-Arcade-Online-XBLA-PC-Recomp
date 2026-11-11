"""HD HUD art for Settings > HUD = HD (docs/HUD.md).

Draws new art for the race HUD's digits, labels, speedometer and gear strip at
the size the renderer enlarges each HUD texture to (src/native_gpu.cpp
hud::scale_for), and writes native/hud/<hash>.png (only the redrawn cells;
everything else transparent) plus native/hud/cells.txt, the cell list
src/native_hud_table.h is made from.

The game's own textures are read only to measure each cell: where its
lettering sits (alpha box), its colours (fill bands top to bottom, outline,
rim). No game pixels go into the output. Those textures are decoded dumps from
a background run with gpu_native_log_hud = true (native_textures beside the
exe, named <hash high 32 bits>_f<fmt>_<w>x<h>.bmp): game content, kept local.

    python tools/hud-art/make_hud_art.py <native_textures dir> [--preview <dir>]
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

FONTS = r'C:\Windows\Fonts'
ITALIC = os.path.join(FONTS, 'seguibli.ttf')   # Segoe UI Black Italic: the HUD's heavy italic
BOLD_ITALIC = os.path.join(FONTS, 'segoeuiz.ttf')  # Segoe UI Bold Italic: the gold lettering, nearer the game's weight
PLAIN = os.path.join(FONTS, 'bahnschrift.ttf')  # dial numerals, gear numbers
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'native', 'hud')

RACE, REDRAWN, LOGO = 2, 1, 4  # native_hud_table.h flags


def scale_for(w, h):  # = hud::scale_for
    k = 8
    while k > 1 and w * k * h * k * 4 * 1.34 > 30.0 * 1024 * 1024:
        k -= 1
    return k


def load_bmp(path):
    b = open(path, 'rb').read()
    w = int.from_bytes(b[18:22], 'little')
    h = -int.from_bytes(b[22:26], 'little', signed=True)
    return Image.frombytes('RGBA', (w, h), b[54:54 + w * h * 4], 'raw', 'BGRA')


# ---- measuring a cell of the game's texture (upright: the HUD samples it flipped) ----
def cell_image(tex, x, y, w, h):
    return tex.crop((x, y, x + w, y + h)).transpose(Image.FLIP_TOP_BOTTOM)


def alpha_box(img, threshold=60):
    a = img.getchannel('A').point(lambda v: 255 if v >= threshold else 0)
    return a.getbbox()


def luminance(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def measure(img):
    """Fill colours in 6 bands (top to bottom), the outline colour and whether a
    light rim lies outside it."""
    w, h = img.size
    px = img.load()
    opaque = [(x, y) for y in range(h) for x in range(w) if px[x, y][3] > 200]
    if not opaque:
        return None
    lum = sorted(luminance(px[p]) for p in opaque)
    dark_cut = lum[len(lum) // 5]
    dark = [px[p] for p in opaque if luminance(px[p]) <= dark_cut]
    outline = tuple(sum(c[i] for c in dark) // len(dark) for i in range(3))
    # Fill: opaque texels whose 4 neighbours are opaque and clearly lighter than the outline.
    def inner(x, y):
        return all(0 <= x + dx < w and 0 <= y + dy < h and px[x + dx, y + dy][3] > 200
                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
    fill = [(x, y) for (x, y) in opaque if inner(x, y) and luminance(px[x, y]) > dark_cut + 40]
    if not fill:
        fill = opaque
    ys = [y for _, y in fill]
    y0, y1 = min(ys), max(ys) + 1
    bands = []
    for b in range(6):
        lo, hi = y0 + (y1 - y0) * b / 6, y0 + (y1 - y0) * (b + 1) / 6
        sel = [px[x, y] for (x, y) in fill if lo <= y < hi] or [px[x, y] for (x, y) in fill]
        bands.append(tuple(sum(c[i] for c in sel) // len(sel) for i in range(3)))
    # Rim: semi-opaque or opaque light texels on the outer edge of the shape.
    edge = [(x, y) for y in range(h) for x in range(w) if 40 < px[x, y][3] and any(
        not (0 <= x + dx < w and 0 <= y + dy < h) or px[x + dx, y + dy][3] < 40
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    light = [px[p] for p in edge if luminance(px[p]) > luminance(outline) + 90]
    rim = tuple(sum(c[i] for c in light) // len(light) for i in range(3)) if len(light) > len(edge) * 0.4 else None
    return {'bands': bands, 'outline': outline, 'rim': rim}


# Fill colours top to bottom and outline, as the game's lettering sets have them
# (read off their cells; the band average alone came out duller).
NAVY = (22, 28, 74)
PALETTES = {
    'gold': {'bands': [(250, 186, 24), (255, 216, 60), (255, 246, 196), (255, 232, 120), (252, 200, 48),
                       (244, 164, 26)], 'outline': NAVY, 'rim': None},  # more yellow, less orange (user)
    'blue': {'bands': [(70, 150, 230), (150, 210, 255), (236, 248, 255), (180, 228, 255), (105, 180, 240),
                       (60, 130, 215)], 'outline': NAVY, 'rim': None},
    'sky': {'bands': [(120, 200, 250), (140, 210, 252), (160, 220, 255), (150, 214, 252), (120, 196, 246),
                      (100, 180, 240)], 'outline': NAVY, 'rim': None},
    'white': {'bands': [(255, 255, 255), (255, 255, 255), (250, 250, 252), (240, 242, 248), (228, 232, 242),
                        (215, 220, 235)], 'outline': NAVY, 'rim': None},
    'pink': {'bands': [(236, 130, 140), (248, 170, 176), (255, 206, 206), (250, 182, 186), (236, 136, 146),
                       (220, 112, 126)], 'outline': NAVY, 'rim': None},
    'kmh': {'bands': [(40, 52, 120)] * 6, 'outline': (236, 240, 250), 'rim': None},
    'label': {'bands': [(255, 236, 110), (255, 228, 90), (255, 220, 72), (252, 210, 60), (248, 198, 50),
                        (242, 186, 40)], 'outline': NAVY, 'rim': None},
}


# ---- drawing ----
def gradient(size, bands):
    w, h = size
    g = Image.new('RGBA', size)
    d = ImageDraw.Draw(g)
    n = len(bands)
    for y in range(h):
        t = y / max(1, h - 1) * (n - 1)
        i = min(int(t), n - 2)
        f = t - i
        c = tuple(int(bands[i][k] * (1 - f) + bands[i + 1][k] * f) for k in range(3))
        d.line([(0, y), (w, y)], fill=c + (255,))
    return g


def styled_text(text, font_path, px, style, outline_frac=0.065, rim_frac=0.05):
    """The text in its fill gradient with outline (and rim), tightly cropped."""
    font = ImageFont.truetype(font_path, px)
    o = max(1, int(px * outline_frac))
    r = int(px * rim_frac) if style.get('rim') else 0
    blur = max(1, int(px * 0.015))
    sx, sy = int(px * 0.025), int(px * 0.04)  # drop shadow offset
    pad = o + r + 3 * blur + max(sx, sy) + 4
    l, t, rr, b = font.getbbox(text)
    size = (rr - l + 2 * pad, b - t + 2 * pad)
    org = (pad - l, pad - t)
    img = Image.new('RGBA', size, (0, 0, 0, 0))
    # A soft drop shadow (design A1: no glow).
    shadow = Image.new('L', size, 0)
    ImageDraw.Draw(shadow).text((org[0] + sx, org[1] + sy), text, font=font, fill=255, stroke_width=o + r,
                                stroke_fill=255)
    shadow = shadow.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: v * 0.45)
    img.paste(Image.new('RGBA', size, (0, 0, 20, 255)), (0, 0), shadow)
    if r:
        rim = Image.new('L', size, 0)
        ImageDraw.Draw(rim).text(org, text, font=font, fill=255, stroke_width=o + r, stroke_fill=255)
        img.paste(Image.new('RGBA', size, style['rim'] + (255,)), (0, 0), rim)
    line = Image.new('L', size, 0)
    ImageDraw.Draw(line).text(org, text, font=font, fill=255, stroke_width=o, stroke_fill=255)
    img.paste(Image.new('RGBA', size, style['outline'] + (255,)), (0, 0), line)
    body = Image.new('L', size, 0)
    ImageDraw.Draw(body).text(org, text, font=font, fill=255)
    bb = body.getbbox()
    g = Image.new('RGBA', size, (0, 0, 0, 0))
    if bb:
        g.paste(gradient((bb[2] - bb[0], bb[3] - bb[1]), style['bands']), (bb[0], bb[1]))
    img.paste(g, (0, 0), body)
    full = img.getbbox()
    core = line.getbbox()  # the lettering with its outline, without the glow
    return img.crop(full), (core[0] - full[0], core[1] - full[1], core[2] - full[0], core[3] - full[1])


def fit_into(canvas, art, box, k, align='center', cell=None):
    """Scales `art` (image, core box) so its lettering fills the height of `box`
    (the game's lettering, cell texels, upright) and places it there (at k
    pixels per texel); the glow spills around it."""
    art, (cx0, cy0, cx1, cy1) = art
    x0, y0, x1, y1 = [v * k for v in box]
    f = (y1 - y0) / (cy1 - cy0)
    if (cx1 - cx0) * f > (x1 - x0) * 1.08:  # wider than the original: fit the width instead
        f = (x1 - x0) / (cx1 - cx0)
    if cell and align == 'left':  # whole, shadow included, inside its cell
        room = cell[2] * k - x0 - k
        if (art.width - cx0) * f > room:
            f = room / (art.width - cx0)
    a = art.resize((max(1, round(art.width * f)), max(1, round(art.height * f))), Image.LANCZOS)
    core_w, core_h = (cx1 - cx0) * f, (cy1 - cy0) * f
    left = x0 if align == 'left' else (x0 + x1) / 2 - core_w / 2
    top = (y0 + y1) / 2 - core_h / 2
    canvas.alpha_composite(a, (round(left - cx0 * f), round(top - cy0 * f)))


class Sheet:
    """One HUD texture: an upright canvas at k x its size, flipped when saved."""

    def __init__(self, dumps, hash_, w, h, name):
        self.hash, self.name = hash_, name
        self.w, self.h = w, h
        self.k = scale_for(w, h)
        path = [f for f in os.listdir(dumps) if f.upper().startswith(hash_[:8].upper())]
        self.tex = load_bmp(os.path.join(dumps, path[0])) if path else None
        self.canvas = Image.new('RGBA', (w * self.k, h * self.k), (0, 0, 0, 0))
        self.cells = []  # (x, y, w, h, flags) in stored texels

    def upright(self, x, y, w, h):
        return (x, self.h - y - h, x + w, self.h - y)

    def glyph(self, x, y, w, h, text, font=ITALIC, race=True, style=None, align='center', size=1.0, **kw):
        """Redraws one lettering cell (stored texel rect) fitted to the game's alpha box."""
        cell = cell_image(self.tex, x, y, w, h)
        m = PALETTES[style] if isinstance(style, str) else style or measure(cell)
        if not m:
            return
        bb = alpha_box(cell)
        ux0, uy0, ux1, uy1 = self.upright(x, y, w, h)
        box = (ux0 + bb[0], uy0 + bb[1], ux0 + bb[2], uy0 + bb[3])
        if size != 1.0:  # smaller than the game's lettering, about its centre
            cx, cy, hw, hh = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2, (box[2] - box[0]) / 2 * size, (box[3] - box[1]) / 2 * size
            box = (cx - hw, cy - hh, cx + hw, cy + hh)
        art = styled_text(text, font, 256, m, **kw)
        fit_into(self.canvas, art, box, self.k, align, (ux0, uy0, ux0 + w, uy0 + h))
        self.cells.append((x, y, w, h, REDRAWN | (RACE if race else 0)))

    def custom(self, x, y, w, h, draw, race=True):
        """A cell drawn by `draw(image)` at the enlarged cell size (upright)."""
        img = Image.new('RGBA', (w * self.k, h * self.k), (0, 0, 0, 0))
        draw(img, self.k)
        ux0, uy0, _, _ = self.upright(x, y, w, h)
        self.canvas.alpha_composite(img, (ux0 * self.k, uy0 * self.k))
        self.cells.append((x, y, w, h, REDRAWN | (RACE if race else 0)))

    def clear_only(self, x, y, w, h):
        """A race cell left to the enlargement in HD, cleared when the HUD is Off."""
        self.cells.append((x, y, w, h, RACE))

    def save(self, preview=None):
        os.makedirs(OUT, exist_ok=True)
        self.canvas.transpose(Image.FLIP_TOP_BOTTOM).save(os.path.join(OUT, f'{self.hash}.png'), optimize=True)
        if preview and self.tex:
            # The game's cells (enlarged, nearest) beside the new ones, upright: checking only.
            k = self.k
            base = Image.new('RGBA', self.canvas.size, (40, 90, 40, 255))
            base.alpha_composite(self.canvas)
            orig = Image.new('RGBA', self.canvas.size, (40, 90, 40, 255))
            orig.alpha_composite(self.tex.transpose(Image.FLIP_TOP_BOTTOM).resize(self.canvas.size, Image.BICUBIC))
            for i, (x, y, w, h, f) in enumerate(self.cells):
                if not f & REDRAWN:
                    continue
                ux0, uy0, ux1, uy1 = self.upright(x, y, w, h)
                b = (ux0 * k, uy0 * k, ux1 * k, uy1 * k)
                pair = Image.new('RGB', ((b[2] - b[0]) * 2 + 8, b[3] - b[1]), (20, 20, 20))
                pair.paste(orig.crop(b).convert('RGB'), (0, 0))
                pair.paste(base.crop(b).convert('RGB'), (b[2] - b[0] + 8, 0))
                os.makedirs(preview, exist_ok=True)
                pair.save(os.path.join(preview, f'{self.hash[:8]}_{x}_{y}.png'))


# ---- the speedometer, gear strip, timer bar ----
def dial(img, k):
    """The 0-100 dial (x100 rpm... as km/h x 10): tick ring, numerals, red zone.
    k = 8: 1024 x 1024 for the game's 128 x 128 (the logo is not redrawn)."""
    s = img.width / 128.0
    d = ImageDraw.Draw(img)
    c = 64 * s
    d.ellipse([c - 47 * s, c - 47 * s, c + 47 * s, c + 47 * s], fill=(0, 0, 0, 150))
    a0, a1, n = 108.0, 364.0, 42
    for i in range(n):
        ang = math.radians(a0 + (a1 - a0) * i / (n - 1))
        t = i / (n - 1)
        col = (255, 255, 255, 255) if t < 0.80 else (255, 170, 20, 255) if t < 0.86 else (230, 20, 40, 255)
        r, q = 47.5 * s, 2.3 * s
        cx, cy = c + r * math.cos(ang), c + r * math.sin(ang)
        pts = [(cx + px * math.cos(ang) - py * math.sin(ang), cy + px * math.sin(ang) + py * math.cos(ang))
               for px, py in ((-q, -q), (q, -q), (q, q), (-q, q))]
        d.polygon(pts, fill=(0, 0, 0, 90))
        d.line(pts + [pts[0]], fill=col, width=max(1, int(0.6 * s)), joint='curve')
    f = ImageFont.truetype(PLAIN, int(7.4 * s))
    f.set_variation_by_name('SemiBold')
    grey = (226, 228, 224, 255)
    for v in range(0, 101, 10):
        ang = math.radians(a0 + (a1 - a0) * v / 100.0)
        r = 36 * s - (2 * s if v == 100 else 0)
        d.text((c + r * math.cos(ang), c + r * math.sin(ang)), str(v), font=f, fill=grey, anchor='mm')
    d.text((c, c - 12.5 * s), '\u00d7100', font=ImageFont.truetype(PLAIN, int(6.6 * s)), fill=grey, anchor='mm')
    horse(img, s)


# The dial's emblem, redrawn (user, 9 Nov: "even if you need to redraw it"): a
# rearing horse built from tapered strokes (skeleton points, widths) in the
# game's 22 x 25 texel logo box, dial texels 53-75 x 73-98 upright.
HORSE = [
    ([(9.4, 10.6), (11.2, 12.6), (13.2, 14.8)], [3.6, 3.9, 3.4]),                       # body
    ([(9.6, 10.4), (10.4, 8.4), (10.9, 6.4)], [3.2, 2.7, 2.2]),                         # neck
    ([(11.5, 5.9), (11.7, 8.0), (11.2, 10.2)], [1.2, 1.5, 1.1]),                        # mane
    ([(11.0, 5.9), (9.6, 5.6), (8.2, 6.6)], [2.2, 1.9, 1.45]),                          # head
    ([(10.9, 5.6), (11.4, 4.2)], [0.9, 0.3]),                                           # ears
    ([(9.0, 10.9), (7.4, 10.2), (5.8, 10.4), (5.0, 12.0)], [1.7, 1.25, 1.05, 0.95]),     # raised foreleg
    ([(9.2, 12.3), (7.6, 12.6), (6.6, 13.4), (6.0, 14.8)], [1.6, 1.15, 1.0, 0.9]),       # other foreleg
    ([(13.0, 14.8), (12.8, 17.4), (12.0, 19.0), (11.8, 21.4)], [2.6, 1.6, 1.1, 1.0]),    # standing hind leg
    ([(12.2, 14.6), (11.2, 16.8), (10.6, 18.6), (9.9, 20.4)], [2.2, 1.35, 1.05, 0.95]),  # other hind leg
    ([(14.0, 13.6), (15.3, 13.6), (15.9, 12.2), (15.4, 10.6), (16.1, 9.4)], [1.1, 1.0, 0.95, 0.8, 0.45]),  # tail
]


def horse(img, s, ox=53, oy=73):
    """Draws the emblem onto the dial image (s pixels per dial texel), supersampled."""
    ss = 4
    box = (22 * s * ss, 25 * s * ss)
    m = Image.new('L', (int(box[0]), int(box[1])), 0)
    d = ImageDraw.Draw(m)
    f = s * ss
    for pts, widths in HORSE:
        for (a, wa), (b, wb) in zip(zip(pts, widths), zip(pts[1:], widths[1:])):
            n = max(2, int(math.dist(a, b) * 8))
            for i in range(n + 1):
                t = i / n
                x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
                r = (wa + (wb - wa) * t) / 2
                d.ellipse([(x - r) * f, (y - r) * f, (x + r) * f, (y + r) * f], fill=255)
    m = m.resize((int(22 * s), int(25 * s)), Image.LANCZOS)
    logo = Image.new('RGBA', m.size, (245, 245, 245, 255))
    logo.putalpha(m)
    img.alpha_composite(logo, (int(ox * s), int(oy * s)))


def needle(img, k):
    """White tapered needle; the game's quad is 16 x 128 with the pivot at its centre."""
    W, H = img.size
    big = Image.new('RGBA', (W * 4, H * 4), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    s = big.width / 16.0
    cx = big.width / 2
    # stored rows 19..71 (tip at row 19) -> upright rows 128-71 .. 128-19
    base, tip = (128 - 71) * s, (128 - 19.5) * s
    wb, wt = 1.9 * s, 1.25 * s
    d.polygon([(cx - wb, base), (cx + wb, base), (cx + wt, tip), (cx - wt, tip)], fill=(255, 255, 255, 255))
    d.ellipse([cx - wt, tip - wt, cx + wt, tip + wt], fill=(255, 255, 255, 255))
    img.alpha_composite(big.resize((W, H), Image.LANCZOS))


def gear_strip(img, k):
    """'123456' (the game's 82 x 18 strip): dark boxes, light numbers."""
    W, H = img.size
    d = ImageDraw.Draw(img)
    s = W / 82.0
    f = ImageFont.truetype(PLAIN, int(14 * s))
    f.set_variation_by_name('Bold')
    cw = W / 6
    for i in range(6):
        x0 = i * cw
        d.rounded_rectangle([x0 + 0.6 * s, 1 * s, x0 + cw - 0.6 * s, H - 1 * s], radius=int(1.6 * s),
                            fill=(30, 34, 40, 175))
        d.text((x0 + cw / 2, H / 2 + 0.2 * s), str(i + 1), font=f, fill=(196, 200, 206, 235), anchor='mm')


GEAR_GOLD = (255, 200, 40, 255)
GEAR_EDGE = (214, 128, 16, 255)


def gear_digit(n):
    """The current gear's number (14 x 10, drawn 21 x 15 inside the highlight
    frame): a solid gold box with the number dark, so the strip's grey number
    behind never shows."""
    def draw(img, k):
        W, H = img.size
        d = ImageDraw.Draw(img)
        d.rectangle([0, 0, W, H], fill=GEAR_GOLD)
        f = ImageFont.truetype(PLAIN, int(H * 1.05))
        f.set_variation_by_name('Bold')
        d.text((W / 2, H / 2 + H * 0.02), str(n), font=f, fill=(28, 24, 10, 255), anchor='mm')
    return draw


def gear_frame(img, k):
    """The highlight frame (18 x 22, drawn 27 x 33 around the gear number): a
    gold box with rounded corners whose middle (the number's 21 x 15 box, 2-16 x
    6-16 in texels, less a texel's overlap so no seam shows) is left open for
    the number drawn under it."""
    W, H = img.size
    s = W / 18.0
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0.4 * s, 0.4 * s, W - 0.4 * s, H - 0.4 * s], radius=int(2.4 * s), fill=GEAR_GOLD,
                        outline=GEAR_EDGE, width=max(1, int(0.8 * s)))
    hole = Image.new('L', img.size, 255)
    ImageDraw.Draw(hole).rectangle([3.5 * s, 7.5 * s, 14.5 * s, 14.5 * s], fill=0)  # overlaps the number's box
    img.putalpha(Image.composite(img.getchannel('A'), Image.new('L', img.size, 0), hole))


def course_icon(img, k):
    """The course map (62 x 50): a dark rounded tile and the 15 stages, one
    column per leg (1 to 5 stages), joined where the road forks. The red dot
    (a separate sprite) is drawn on these places, so the lattice is the game's
    own: first stage at (13.1, 23.8) upright texels, a leg on is (+9.06, -3.1),
    the right-hand fork (-3.0, +7.0)."""
    W, H = img.size
    s = W / 62.0
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([3.5 * s, 2.0 * s, 58.5 * s, 47.5 * s], radius=int(5 * s), fill=(18, 22, 30, 170))
    u, v, o = (9.06, -3.1), (-3.0, 7.0), (13.1, 23.8)

    def at(i, j):
        return ((o[0] + i * u[0] + j * v[0]) * s, (o[1] + i * u[1] + j * v[1]) * s)
    for i in range(4):
        for j in range(i + 1):
            for jj in (j, j + 1):
                d.line([at(i, j), at(i + 1, jj)], fill=(150, 155, 164, 220), width=max(1, int(0.8 * s)))
    for i in range(5):
        for j in range(i + 1):
            cx, cy = at(i, j)
            hw, hh, sk = 2.6 * s, 1.75 * s, 1.0 * s  # level top and bottom, sides leaning right, as the game's
            quad = [(cx - hw + sk, cy - hh), (cx + hw + sk, cy - hh), (cx + hw - sk, cy + hh), (cx - hw - sk, cy + hh)]
            d.polygon([(x + 0.6 * s, y + 0.8 * s) for x, y in quad], fill=(0, 0, 0, 150))
            d.polygon(quad, fill=(190, 194, 200, 255))


def course_dot(img, k):
    """Where the car is on the course map (14 x 14): a red dot with a light rim."""
    W, H = img.size
    d = ImageDraw.Draw(img)
    r = W * 0.30
    d.ellipse([W / 2 - r, H / 2 - r, W / 2 + r, H / 2 + r], fill=(235, 36, 36, 255), outline=(255, 255, 255, 230),
              width=max(1, int(W * 0.06)))


def timer_bar(img, k):
    """The dashed ring and the white bar fading out (158 x 34)."""
    W, H = img.size
    s = W / 158.0
    d = ImageDraw.Draw(img)
    cx, cy, r = 16.5 * s, H / 2, 12.5 * s
    for i in range(12):
        a0 = i * 30 + 6
        d.arc([cx - r, cy - r, cx + r, cy + r], a0, a0 + 16, fill=(255, 255, 255, 240), width=int(3.5 * s))
    bar = Image.new('RGBA', (int(124 * s), int(7 * s)))
    bd = ImageDraw.Draw(bar)
    for x in range(bar.width):
        a = int(175 * max(0.0, 1 - x / bar.width) ** 0.9)
        bd.line([(x, 0), (x, bar.height)], fill=(255, 255, 255, a))
    img.alpha_composite(bar, (int(32 * s), int(cy - bar.height / 2)))


def main():
    dumps = sys.argv[1]
    preview = sys.argv[sys.argv.index('--preview') + 1] if '--preview' in sys.argv else None
    sheets = []

    s = Sheet(dumps, '1EF05844465FA269', 128, 128, 'speedometer dial')
    s.custom(0, 0, 128, 128, dial)
    sheets.append(s)
    s = Sheet(dumps, '97FFAFBB9F525669', 16, 128, 'speedometer needle')
    s.custom(0, 0, 16, 128, needle)
    sheets.append(s)

    # HUD sheet: Time's big gold digits, speed digits (gold), blue digits, km/h, pink digits.
    s = Sheet(dumps, '92F50FBE36F99AC3', 512, 512, 'HUD sheet')
    big = [(64, 0, '9'), (122, 0, '8'), (180, 0, '7'), (238, 0, '6'), (296, 0, '5'), (354, 0, '4'), (412, 0, '3'),
           (0, 64, '2'), (58, 64, '1'), (116, 64, '0')]
    for x, y, t in big:
        s.glyph(x, y, 58, 60, t, font=BOLD_ITALIC, style='gold', outline_frac=0.045, size=0.9)
    for i, t in enumerate('9876543'):
        s.glyph(244 + 34 * i, 124, 34, 34, t, race=False, style='blue', outline_frac=0.06)  # blue: Extend Time bonus
    for i, t in enumerate('210'):
        s.glyph(34 * i, 166, 34, 34, t, race=False, style='blue', outline_frac=0.06)
    s.glyph(210, 124, 34, 34, 'km/h', race=False, style='kmh', outline_frac=0.09)
    s.glyph(136, 166, 34, 34, 'km/h', style='kmh', outline_frac=0.09)
    for i, t in enumerate('9876543210'):
        s.glyph(170 + 34 * i, 166, 34, 34, t, font=BOLD_ITALIC, style='gold', outline_frac=0.06)
    for i, t in enumerate('987654'):
        s.glyph(288 + 24 * i, 444, 24, 24, t, style='pink')
    s.custom(422, 64, 62, 50, course_icon)
    sheets.append(s)

    # Font sheet: Total Time and Score digits (white), blue and pink digits, the ' and ".
    s = Sheet(dumps, '4D23944303D91F91', 512, 512, 'font sheet')
    for i, t in enumerate('9876543210'):
        s.glyph(72 + 24 * i, 255, 24, 24, t, style='white')
    for i, t in enumerate('9876543'):
        s.glyph(336 + 24 * i, 231, 24, 24, t, race=False, style='sky')
    for i, t in enumerate('210'):
        s.glyph(24 * i, 255, 24, 24, t, race=False, style='sky')
    for i, t in enumerate('3210'):
        s.glyph(327 + 24 * i, 183, 24, 24, t, style='pink')
    s.glyph(48, 231, 24, 24, '\u201d', style='white', outline_frac=0.06)
    s.glyph(72, 231, 24, 24, '\u2019', style='white', outline_frac=0.06)
    s.custom(463, 279, 18, 22, gear_frame)
    s.custom(487, 301, 14, 14, course_dot)
    sheets.append(s)

    # Label sheet: Total Time / Score / Time labels, gear strip, gear numbers.
    s = Sheet(dumps, 'FBB0D9D26BAA003A', 1024, 512, 'label sheet')
    s.glyph(0, 332, 126, 26, 'Total Time', font=BOLD_ITALIC, align='left', style='label', outline_frac=0.055)
    s.glyph(330, 332, 72, 26, 'Score', font=BOLD_ITALIC, align='left', style='label', outline_frac=0.055)
    s.glyph(402, 332, 62, 26, 'Time', font=BOLD_ITALIC, align='left', style='label', outline_frac=0.055)
    s.custom(940, 380, 82, 18, gear_strip)
    # Gold a texel past the numbers' cells too: the sampler's bilinear edge then
    # stays opaque, so no seam of the grey strip shows around the current gear.
    s.custom(894, 497, 72, 12, lambda img, k: ImageDraw.Draw(img).rectangle([0, 0, img.width, img.height],
                                                                             fill=GEAR_GOLD))
    for i, n in enumerate((5, 4, 3, 2, 1)):
        s.custom(895 + 14 * i, 498, 14, 10, gear_digit(n))
    sheets.append(s)

    # Timer bars sheet: the bar under Time / Total Time.
    s = Sheet(dumps, '21D36C6DC75459AC', 512, 512, 'timer bars')
    s.custom(252, 378, 158, 34, timer_bar)
    sheets.append(s)

    os.makedirs(OUT, exist_ok=True)
    # The cells for src/native_hud_table.h: redrawn (HD) and race-only (Off).
    lines = ['// Generated by tools/hud-art/make_hud_art.py: the HUD cells it redraws (kRedrawn)',
             '// and those only races draw (kRace). Stored texels.', '#pragma once', '',
             'const std::vector<std::pair<uint64_t, std::vector<Rect>>> kCells = {']
    for s in sheets:
        s.save(preview)
        lines.append(f'    {{0x{s.hash}ull, {{  // {s.name}')
        for (x, y, w, h, f) in s.cells:
            lines.append(f'        {{{x}, {y}, {w}, {h}, {f}}},')
        lines.append('    }},')
    lines.append('};')
    with open(os.path.join(ROOT, 'src', 'native_hud_cells.h'), 'w', newline='\r\n') as out:
        out.write('\n'.join(lines) + '\n')
    print('wrote', len(sheets), 'sheets to', OUT)


if __name__ == '__main__':
    main()
