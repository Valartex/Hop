"""Векторный знак Hop и предпросмотр в фактических размерах трея."""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / ".tools" / "preview-runtime"))
from PySide6.QtCore import QByteArray, QRectF, Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter, QColor, QFont, QFontDatabase
from PySide6.QtSvg import QSvgRenderer

APP = QGuiApplication([])
for font_path in ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/segoeuib.ttf"):
    if QFontDatabase.addApplicationFont(font_path) < 0:
        raise RuntimeError(f"Не удалось загрузить шрифт: {font_path}")
OUT = Path(__file__).resolve().parent
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
COLORS = {"teal": "#007F79", "light-bg": "#073641", "dark-bg": "#F1F7F6"}


def svg(color, micro=False):
    # Для малых размеров увеличены точки, толщина дуги и зазоры.
    arc = "M4.8 8.1C7.2 3 9.3 1.9 11.6 3.9" if micro else "M4.6 8.5C7 3 9.5 2.2 11.7 4.5"
    radius = 2 if micro else 1.9
    stroke = 2.5 if micro else 2.15
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 16 16">
  <g fill="{color}">
    <circle cx="3" cy="12" r="{radius}"/>
    <circle cx="13" cy="8" r="{radius}"/>
    <path d="{arc}" fill="none" stroke="{color}" stroke-width="{stroke}" stroke-linecap="butt"/>
  </g>
</svg>
'''.encode("utf-8")


def render(source, size):
    image = QImage(size, size, QImage.Format_ARGB32)
    image.fill(0)
    painter = QPainter(image)
    QSvgRenderer(QByteArray(source)).render(painter)
    painter.end()
    return image


def png_bytes(image):
    from PySide6.QtCore import QBuffer, QIODevice
    data = QByteArray()
    buffer = QBuffer(data)
    buffer.open(QIODevice.WriteOnly)
    if not image.save(buffer, "PNG"):
        raise RuntimeError("Не удалось сохранить PNG")
    return bytes(data)


def write_ico(path, images):
    offset = 6 + len(SIZES) * 16
    entries, payloads = [], []
    for size, image in zip(SIZES, images):
        data = png_bytes(image)
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset))
        payloads.append(data)
        offset += len(data)
    path.write_bytes(struct.pack("<HHH", 0, 1, len(SIZES)) + b"".join(entries) + b"".join(payloads))


def component_count(image):
    # Проверка того, что после растеризации точки не сливаются с дугой.
    pixels = {(x, y) for y in range(image.height()) for x in range(image.width())
              if image.pixelColor(x, y).alpha() >= 128}
    count = 0
    while pixels:
        count += 1
        pending = [pixels.pop()]
        while pending:
            x, y = pending.pop()
            for dx, dy in ((-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1)):
                point = (x + dx, y + dy)
                if point in pixels:
                    pixels.remove(point)
                    pending.append(point)
    return count


images = {}
for name, color in COLORS.items():
    (OUT / f"hop-{name}.svg").write_bytes(svg(color))
    (OUT / f"hop-{name}-micro.svg").write_bytes(svg(color, True))
    variants = []
    for size in SIZES:
        image = render(svg(color, size <= 24), size)
        image.save(str(OUT / f"hop-{name}-{size}.png"))
        variants.append(image)
        images[name, size] = image
    write_ico(OUT / f"hop-{name}.ico", variants)

report = []
for size in (16, 20, 24, 32):
    count = component_count(images["light-bg", size])
    report.append(f"{size}x{size}: {count} отдельных элемента при пороге alpha >= 128")
    if count != 3:
        raise RuntimeError(f"Элементы слились или распались: {size}px, {count}")
(OUT / "pixel-check.txt").write_text("\n".join(report) + "\n", encoding="utf-8")

canvas = QImage(1000, 790, QImage.Format_ARGB32)
canvas.fill(QColor("#F3F6F7"))
painter = QPainter(canvas)
painter.setRenderHint(QPainter.Antialiasing)


def text(x, y, value, size=16, color="#073641", bold=False):
    painter.setPen(QColor(color))
    font = QFont("Segoe UI")
    font.setPixelSize(size)
    font.setBold(bold)
    painter.setFont(font)
    painter.drawText(x, y, value)


text(44, 55, "Hop / прыжок между локациями", 27, bold=True)
text(44, 86, "Знак и версия для маленьких размеров", 16, "#576C75")
painter.drawImage(75, 105, render(svg(COLORS["teal"]), 240))
text(350, 258, "Hop", 100, bold=True)
text(44, 383, "ФАКТИЧЕСКИЙ РАЗМЕР — 100%", 14, "#576C75", True)
for x, size in zip((340, 475, 610, 745), (16, 20, 24, 32)):
    text(x - 16, 412, f"{size} × {size}", 14, "#576C75")

for top, background, theme, foreground, title in (
    (430, "#FFFFFF", "light-bg", "#073641", "Светлый фон"),
    (523, "#202124", "dark-bg", "#F1F7F6", "Тёмный фон"),
):
    painter.fillRect(44, top, 912, 78, QColor(background))
    text(64, top + 46, title, 16, foreground)
    for x, size in zip((340, 475, 610, 745), (16, 20, 24, 32)):
        painter.drawImage(x - size // 2, top + 39 - size // 2, images[theme, size])

text(44, 647, "16 × 16 / пиксели увеличены в 6 раз", 15, "#576C75")
painter.setRenderHint(QPainter.SmoothPixmapTransform, False)
for x, theme, background in ((400, "light-bg", "#FFFFFF"), (550, "dark-bg", "#202124")):
    painter.fillRect(x - 12, 627, 120, 120, QColor(background))
    painter.drawImage(QRectF(x, 639, 96, 96), images[theme, 16])
painter.end()
canvas.save(str(OUT / "tray-size-preview.png"))
print("\n".join(report))
print(f"Сохранено в {OUT}")
