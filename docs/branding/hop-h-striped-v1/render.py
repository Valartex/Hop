"""Чистый векторный вариант эскизов с точной плашкой текущего Hop."""
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / ".tools" / "preview-runtime"))
from PySide6.QtCore import QByteArray, QRectF
from PySide6.QtGui import QGuiApplication, QImage, QPainter, QColor
from PySide6.QtSvg import QSvgRenderer

APP = QGuiApplication([])
SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)
current = ET.parse(ROOT / "client/images/hop/mark.svg").getroot()
tile_attributes = dict(current.find(f"{{{SVG_NS}}}rect").attrib)
gradient_definitions = ET.tostring(current.find(f"{{{SVG_NS}}}defs"), encoding="unicode")
tile_geometry = " ".join(f'{name}="{value}"' for name, value in tile_attributes.items() if name != "fill")

# Траектория повторяет геометрию выбранного эскиза и сужается в перспективе.
ARC = "M13.5 37C24.5 28 40 17.8 50.5 24.2L50.5 24.9C40 20 25 31.9 13.5 43.8Z"


def pillar(x):
    # Отдельные фигуры гарантируют одинаковые прорези во всех SVG-рендерерах.
    right = x + 12.5
    top = f'<path d="M{x+1.4} 12.5H{right-1.4}Q{right} 12.5 {right} 13.9V20.5H{x}V13.9Q{x} 12.5 {x+1.4} 12.5Z"/>'
    middle = "".join(f'<rect x="{x}" y="{y}" width="12.5" height="{height}"/>'
                     for y, height in ((21.8, 7.2), (30.3, 6.7), (38.3, 6.7)))
    bottom = f'<path d="M{x} 46.3H{right}V51.6Q{right} 53 {right-1.4} 53H{x+1.4}Q{x} 53 {x} 51.6Z"/>'
    return top + middle + bottom


def source(green=False):
    background = "#76B83F" if green else "url(#tile)"
    return f'''<svg xmlns="{SVG_NS}" width="1024" height="1024" viewBox="0 0 64 64">
  {gradient_definitions}
  <rect {tile_geometry} fill="{background}"/>
  <g fill="#102932">
    {pillar(13.5)}
    {pillar(38)}
    <rect x="24" y="30.3" width="16" height="6.7"/>
  </g>
  <path d="{ARC}" fill="{background}" stroke="{background}" stroke-width="1.2" stroke-linejoin="round"/>
  <path d="{ARC}" fill="#EF862F"/>
</svg>
'''.encode("utf-8")


def render(svg, size):
    image = QImage(size, size, QImage.Format_ARGB32)
    image.fill(0)
    renderer = QSvgRenderer(QByteArray(svg))
    if not renderer.isValid():
        raise RuntimeError("Некорректный SVG")
    painter = QPainter(image)
    renderer.render(painter)
    painter.end()
    return image


for name, green in (("01-hop-background", False), ("02-utorrent-green", True)):
    svg = source(green)
    (OUT / f"{name}.svg").write_bytes(svg)
    for size in (256, 1024):
        image = render(svg, size)
        if image.pixelColor(0, 0).alpha() != 0:
            raise RuntimeError("Углы должны быть прозрачными")
        if image.pixelColor(size // 2, size // 8).alpha() != 255:
            raise RuntimeError("Плашка должна быть непрозрачной")
        image.save(str(OUT / f"{name}-{size}.png"))

preview = QImage(864, 432, QImage.Format_ARGB32)
preview.fill(QColor("#E9ECEF"))
painter = QPainter(preview)
for x, green in ((24, False), (456, True)):
    painter.drawImage(QRectF(x, 24, 384, 384), render(source(green), 384))
painter.end()
preview.save(str(OUT / "comparison.png"))
print("SVG и PNG: два фона, четыре прорези, одинаковая геометрия; углы прозрачные")
