"""Частые горизонтальные прорези в H, два фона и цельная оранжевая дуга."""
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

ARC = "M13.5 37C24.5 28 40 17.8 50.5 24.2L50.5 24.9C40 20 25 31.9 13.5 43.8Z"
SLAT_COUNT = 36
LETTER_TOP = 12.5
LETTER_HEIGHT = 40.5
SLAT_PITCH = LETTER_HEIGHT / SLAT_COUNT
GAP_HEIGHT = 0.36


def source(green=False):
    background = "#76B83F" if green else "url(#tile)"
    # Просветы показывают тот же градиент с теми же координатами, что и плашка.
    gaps = "\n".join(
        f'<rect x="13.5" y="{LETTER_TOP + index * SLAT_PITCH - GAP_HEIGHT / 2:.4f}" width="37" height="{GAP_HEIGHT}"/>'
        for index in range(1, SLAT_COUNT)
    )
    return f'''<svg xmlns="{SVG_NS}" width="1024" height="1024" viewBox="0 0 64 64">
  {gradient_definitions}
  <rect {tile_geometry} fill="{background}"/>
  <g fill="#102932">
    <rect x="13.5" y="12.5" width="12.5" height="40.5" rx="1.4"/>
    <rect x="38" y="12.5" width="12.5" height="40.5" rx="1.4"/>
    <rect x="24" y="30.3" width="16" height="6.7"/>
  </g>
  <g fill="{background}">
    {gaps}
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
        image.save(str(OUT / f"{name}-{size}.png"))

preview = QImage(864, 432, QImage.Format_ARGB32)
preview.fill(QColor("#E9ECEF"))
painter = QPainter(preview)
for x, green in ((24, False), (456, True)):
    painter.drawImage(QRectF(x, 24, 384, 384), render(source(green), 384))
painter.end()
preview.save(str(OUT / "comparison.png"))
print(f"Сохранены два варианта: {SLAT_COUNT - 1} частых прорезей, дуга без разрывов")
