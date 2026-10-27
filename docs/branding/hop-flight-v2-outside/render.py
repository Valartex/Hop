"""Начало и конец оранжевой траектории вынесены за боковые края H."""
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
BASE = ET.parse(OUT.parent / "hop-final-options-v1/03-hop-mint.svg").getroot()
OPTIONS = (
    ("01-outside", "1. Вынос за H", "Оба конца заметно выступают за букву",
     "M7 38C14 13 39 2 57 25L55.8 26.2C38 7 17 17 9.5 39Z"),
    ("02-more-air", "2. Больше воздуха", "Более высокий и свободный прыжок",
     "M6 40C14 10 39 -1 58 26L57 27C38 4 16 14 8.3 41Z"),
)


def source(title, arc):
    svg = copy.deepcopy(BASE)
    svg.find(f"{{{NS}}}title").text = "Hop — " + title
    svg.find(f"{{{NS}}}desc").text = "Траектория начинается слева за буквой H и заканчивается справа за её пределами."
    group = svg.find(f"{{{NS}}}g")
    for node in list(group.findall(f"{{{NS}}}path")):
        group.remove(node)
    ET.SubElement(group, f"{{{NS}}}path", dict(d=arc, fill="url(#tile)", stroke="url(#tile)", **{"stroke-width": "0.9", "stroke-linejoin": "round"}))
    ET.SubElement(group, f"{{{NS}}}path", dict(d=arc, fill="#EF862F"))
    return ET.tostring(svg, encoding="utf-8")


def render(svg, size):
    renderer = QSvgRenderer(QByteArray(svg))
    if not renderer.isValid():
        raise RuntimeError("Некорректный SVG")
    image = QImage(size, size, QImage.Format_ARGB32)
    image.fill(0)
    painter = QPainter(image)
    renderer.render(painter)
    painter.end()
    return image


def text(painter, x, y, value, size=15, bold=False):
    font = QFont("Segoe UI")
    font.setPixelSize(size)
    font.setBold(bold)
    painter.setFont(font)
    painter.setPen(QColor("#213E49"))
    painter.drawText(x, y, value)


board = QImage(800, 518, QImage.Format_ARGB32)
board.fill(QColor("#F0F3F4"))
painter = QPainter(board)
text(painter, 24, 36, "Hop / траектория выходит за пределы H", 24, True)
for index, (name, title, note, arc) in enumerate(OPTIONS):
    svg = source(title, arc)
    (OUT / f"{name}.svg").write_bytes(svg)
    for size in (16, 24, 32, 48, 256, 1024):
        render(svg, size).save(str(OUT / f"{name}-{size}.png"))
    x = 24 + index * 394
    painter.drawImage(x + 28, 59, render(svg, 304))
    text(painter, x + 18, 395, title, 21, True)
    text(painter, x + 18, 421, note, 14)
    for offset, size in ((48, 16), (112, 24), (180, 32), (256, 48)):
        painter.drawImage(x + offset - size // 2, 457 - size // 2, render(svg, size))
text(painter, 24, 505, "Малые размеры: 16 / 24 / 32 / 48 px; это эскизы без оптической адаптации.", 13)
painter.end()
board.save(str(OUT / "comparison.png"))
print("Два SVG и PNG: оба конца за пределами H, фон и буква сохранены")
