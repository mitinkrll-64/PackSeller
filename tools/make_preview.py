# -*- coding: utf-8 -*-
# Builds the Workshop preview images (1920x1080 JPEG), RU and EN.
# Usage: python tools\make_preview.py [screenshot]
# Needs Pillow and Windows fonts (segoeui*.ttf). Output: assets\workshop_preview.jpg, assets\workshop_preview_en.jpg
# The screenshot (Towns tab) defaults to assets\screenshots\screenshot_3_towns_tab.jpg.
import os
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageEnhance

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = r"C:\Windows\Fonts"
W, H = 1920, 1080
CREAM = (240, 222, 170)
ORANGE = (222, 122, 66)
WHITE = (255, 255, 255)
GREY = (205, 200, 190)

# crop of the screenshot shown in the frame (fractions of width/height)
CROP = (0.405, 0.255, 0.985, 0.745)
FRAME = (785, 380, 1875, 948)  # x0, y0, x1, y1 of the framed picture

TEXTS = {
    "ru": {
        "out": "workshop_preview.jpg",
        "version": "0.2",
        "sub": u"Автопродажа на животном: торговцы и города",
        "sub2": u"Auto-sell on your animal: traders and towns",
        "label": u"\u25BC вкладка «Города»",
        "bullets": [
            u"Кнопка «Торговцы» в инвентаре животного",
            u"Вкладка «Города»: поход в другой город, продажа, возврат",
            u"Настройки в меню, RU / EN",
        ],
        "footer": u"Требуется RE_Kenshi 0.3.5  \u2022  Kenshi 1.0.65  \u2022  GPLv3  \u2022  github.com/mitinkrll-64/PackSeller",
    },
    "en": {
        "out": "workshop_preview_en.jpg",
        "version": "0.2",
        "sub": u"Auto-sell on your animal: traders and towns",
        "sub2": u"RE_Kenshi plugin for Kenshi",
        "label": u"\u25BC the \"Towns\" tab",
        "bullets": [
            u"\"Traders\" button in the animal inventory window",
            u"\"Towns\" tab: trip to another town, sell, return",
            u"Settings in the menu, RU / EN",
        ],
        "footer": u"Requires RE_Kenshi 0.3.5  \u2022  Kenshi 1.0.65  \u2022  GPLv3  \u2022  github.com/mitinkrll-64/PackSeller",
    },
}


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


def shadow_text(draw, xy, text, fnt, fill, off=4):
    x, y = xy
    draw.text((x + off, y + off), text, font=fnt, fill=(0, 0, 0))
    draw.text((x, y), text, font=fnt, fill=fill)


def wrap(text, fnt, width):
    words = text.split(" ")
    lines, cur = [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if fnt.getlength(t) <= width or not cur:
            cur = t
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    return lines


def background(shot):
    bg = shot.copy()
    scale = max(W / bg.width, H / bg.height)
    bg = bg.resize((int(bg.width * scale + 1), int(bg.height * scale + 1)), Image.LANCZOS)
    left = (bg.width - W) // 2
    top = (bg.height - H) // 2
    bg = bg.crop((left, top, left + W, top + H)).filter(ImageFilter.GaussianBlur(14))
    return ImageEnhance.Brightness(bg).enhance(0.33)


def build(lang, shot):
    t = TEXTS[lang]
    img = background(shot).convert("RGB")
    d = ImageDraw.Draw(img)

    # title: "PackSeller" + version in orange
    f_title = font("segoeuib.ttf", 170)
    shadow_text(d, (80, 80), "PackSeller", f_title, CREAM, 6)
    x_ver = 80 + f_title.getlength("PackSeller") + 28
    shadow_text(d, (x_ver, 80), t["version"], f_title, ORANGE, 6)

    shadow_text(d, (88, 262), t["sub"], font("segoeuib.ttf", 46), WHITE, 3)
    d.text((88, 330), t["sub2"], font=font("segoeuil.ttf", 38), fill=GREY)
    d.rectangle((88, 384, 688, 388), fill=ORANGE)

    # bullets
    f_b = font("segoeuisl.ttf", 38)
    y = 440
    for b in t["bullets"]:
        lines = wrap(b, f_b, 600)
        d.rectangle((92, y + 14, 106, y + 28), fill=ORANGE)
        for ln in lines:
            shadow_text(d, (130, y), ln, f_b, WHITE, 2)
            y += 48
        y += 24

    # framed screenshot crop
    sw, sh = shot.size
    c = shot.crop((int(sw * CROP[0]), int(sh * CROP[1]), int(sw * CROP[2]), int(sh * CROP[3]))).convert("RGB")
    fx0, fy0, fx1, fy1 = FRAME
    c = c.resize((fx1 - fx0, fy1 - fy0), Image.LANCZOS)
    d.rectangle((fx0 - 5, fy0 - 5, fx1 + 5, fy1 + 5), fill=(250, 245, 235))
    d.rectangle((fx0 - 2, fy0 - 2, fx1 + 2, fy1 + 2), fill=(30, 20, 15))
    img.paste(c, (fx0, fy0))
    shadow_text(d, (fx0 + 40, fy0 - 62), t["label"], font("segoeuib.ttf", 34), ORANGE, 2)

    # footer
    d.rectangle((0, 1000, W, H), fill=(24, 18, 14))
    f_f = font("segoeui.ttf", 34)
    d.text((80, 1022), t["footer"], font=f_f, fill=GREY)

    out = os.path.join(ROOT, "assets", t["out"])
    img.save(out, "JPEG", quality=90, optimize=True)
    print("wrote", out, os.path.getsize(out))


def main():
    shot_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "assets", "screenshots", "screenshot_3_towns_tab.jpg")
    shot = Image.open(shot_path).convert("RGB")
    for lang in ("ru", "en"):
        build(lang, shot)


if __name__ == "__main__":
    main()
