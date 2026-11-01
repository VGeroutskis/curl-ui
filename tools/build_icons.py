#!/usr/bin/env python3
"""Draw the app icon: packaging/curl-ui.png, .ico and .icns. Requires Pillow."""
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "packaging")


def draw(size=1024):
    ss = 4
    s = size * ss
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([s * 0.06, s * 0.06, s * 0.94, s * 0.94], radius=s * 0.2, fill=(20, 24, 31, 255))
    accent = (108, 182, 255, 255)
    # the "://" of a URL
    w = int(s * 0.075)
    for cy in (0.38, 0.62):
        r = s * 0.058
        cx = s * 0.27
        d.ellipse([cx - r, s * cy - r, cx + r, s * cy + r], fill=accent)
    for x in (0.42, 0.60):
        d.line([(s * (x + 0.13), s * 0.24), (s * (x - 0.03), s * 0.76)], fill=accent, width=w)
        for (px, py) in ((x + 0.13, 0.24), (x - 0.03, 0.76)):
            d.ellipse([s * px - w / 2, s * py - w / 2, s * px + w / 2, s * py + w / 2], fill=accent)
    return img.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    big = draw()
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, "curl-ui.png"))
    big.save(os.path.join(OUT, "curl-ui.ico"), sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    big.save(os.path.join(OUT, "curl-ui.icns"))


if __name__ == "__main__":
    main()
