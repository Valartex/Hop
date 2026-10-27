"""Прозрачный знак с мятной окантовкой вместо фоновой плашки."""
from pathlib import Path
import copy
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / ".tools" / "preview-runtime"))
from PySide6.QtCore import QByteArray
from PySide6.QtGui import QGuiApplication, QImage, QPainter, QColor, QFont, QFontDatabase
from PySide6.QtSvg import QSvgRenderer

APP = QGuiApplication([])
for font_path in ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/segoeuib.ttf"):
    QFontDatabase.addApplicationFont(font_path)
NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", NS)
original = ET.parse(OUT.parent / "hop-options-extended-v1/03-hop-mint.svg").getroot()
svg = copy.deepcopy(original)
for tile in list(svg.findall(f"{{{NS}}}rect")):
    svg.remove(tile)
svg.find(f"{{{NS}}}title").text = "Hop — прозрачный знак с мятной окантовкой"
svg.find(f"{{{NS}}}desc").text = "H и оранжевая траектория с мятной окантовкой и мятным зазором между ними. Плашки и внешней рамки нет."

# Единый внешний контур H не создаёт лишних линий на стыке стоек и перекладины.
letter = (
    "M13.35 10.8H24.05A1.35 1.35 0 0 1 25.4 12.15V28.85H38.6V12.15"
    "A1.35 1.35 0 0 1 39.95 10.8H50.7A1.35 1.35 0 0 1 52.05 12.15V51.95"
    "A1.35 1.35 0 0 1 50.7 53.3H39.95A1.35 1.35 0 0 1 38.6 51.95V36.05H25.4V51.95"
    "A1.35 1.35 0 0 1 24.05 53.3H13.35A1.35 1.35 0 0 1 12 51.95V12.15"
    "A1.35 1.35 0 0 1 13.35 10.8Z"
)
group = svg.find(f"{{{NS}}}g")
outline = ET.Element(f"{{{NS}}}path", {
    "d": letter, "fill": "url(#tile)", "stroke": "url(#tile)",
    "stroke-width": "1.2", "stroke-linejoin": "round",
})
group.insert(0, outline)

# Существующие H, траектория и её мятный зазор остаются без изменений.
assert ET.tostring(group.find(f"{{{NS}}}g")) == ET.tostring(original.find(f"{{{NS}}}g/{{{NS}}}g"))
assert [ET.tostring(node) for node in group.findall(f"{{{NS}}}path")][1:] == [ET.tostring(node) for node in original.findall(f"{{{NS}}}g/{{{NS}}}path")]
data = ET.tostring(svg, encoding="utf-8")
(OUT / "hop-mint-transparent.svg").write_bytes(data)


def render(size):
    renderer = QSvgRenderer(QByteArray(data))
    if not renderer.isValid():
        raise RuntimeError("Некорректный SVG")
    image = QImage(size, size, QImage.Format_ARGB32)
    image.fill(0)
    painter = QPainter(image)
    renderer.render(painter)
    painter.end()
    for x, y in ((0, 0), (size // 2, size // 8), (size // 16, size // 2), (size * 15 // 16, size // 2)):
        if image.pixelColor(x, y).alpha() != 0:
            raise RuntimeError("Внешний фон или пространство над перекладиной не прозрачны")
    return image


for size in (256, 512, 1024):
    render(size).save(str(OUT / f"hop-mint-transparent-{size}.png"))

board = QImage(900, 352, QImage.Format_ARGB32)
board.fill(QColor("#EBEFF1"))
painter = QPainter(board)
font = QFont("Segoe UI")
font.setPixelSize(17)
painter.setFont(font)
for index, (label, background, foreground) in enumerate((
    ("Светлый фон", "#FFFFFF", "#173640"),
    ("Тёмный фон", "#181E24", "#F1F5F6"),
    ("Цветной фон", "#594D78", "#FFFFFF"),
)):
    x = 12 + index * 296
    painter.fillRect(x, 12, 284, 328, QColor(background))
    painter.drawImage(x + 14, 24, render(256))
    painter.setPen(QColor(foreground))
    painter.drawText(x + 20, 316, label)
painter.end()
board.save(str(OUT / "background-preview.png"))
print("SVG и PNG: фон прозрачный, мятная окантовка и зазор сохранены")
