"""Подготовка Android-ресурсов из принятого логотипа Hop без изменения оригинала."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "shared/client/images/hop/logo-preview.png"
RESOURCES = ROOT / "platforms/android/app/res"
logo = Image.open(SOURCE).convert("RGBA")

def image_on_canvas(size, logo_size, color, monochrome=False, round_background=False):
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0) if round_background else color)
    if round_background:
        ImageDraw.Draw(canvas).ellipse((0, 0, size - 1, size - 1), fill=color)
    symbol = logo.resize((logo_size, logo_size), Image.Resampling.LANCZOS)
    if monochrome:
        white = Image.new("RGBA", symbol.size, "white")
        white.putalpha(symbol.getchannel("A")); symbol = white
    canvas.alpha_composite(symbol, ((size - logo_size) // 2, (size - logo_size) // 2))
    return canvas

for density, scale in {"ldpi": .75, "mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}.items():
    mipmap = RESOURCES / ("mipmap-" + density)
    drawable = RESOURCES / ("drawable-" + density)
    mipmap.mkdir(parents=True, exist_ok=True); drawable.mkdir(parents=True, exist_ok=True)
    image_on_canvas(round(48 * scale), round(37 * scale), "#101418").save(mipmap / "icon.png")
    image_on_canvas(round(48 * scale), round(37 * scale), "#101418", round_background=True).save(mipmap / "icon_round.png")
    image_on_canvas(round(108 * scale), round(56 * scale), (0, 0, 0, 0)).save(mipmap / "ic_launcher_foreground.png")
    image_on_canvas(round(24 * scale), round(21 * scale), (0, 0, 0, 0), monochrome=True).save(drawable / "ic_hop_notification.png")

print("Android icons generated from", SOURCE)
