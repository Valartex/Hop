"""Три мятных варианта без внешней рамки, с прежней H и выносом дуги."""
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
OPTIONS = (
    ("01-light-rounded", "01-mint-rounded", "1. Умеренное скругление"),
    ("02-light-square", "02-mint-square", "2. Почти квадрат"),
    ("03-hop-mint", "03-mint-soft", "3. Сильное скругление"),
)


def render(data, size):
    renderer = QSvgRenderer(QByteArray(data))
    if not renderer.isValid():
        raise RuntimeError("Некорректный SVG")
    image = QImage(size, size, QImage.Format_ARGB32)
    image.fill(0)
    painter = QPainter(image)
    renderer.render(painter)
    painter.end()
    if image.pixelColor(0, 0).alpha() != 0:
        raise RuntimeError("Внешние углы должны оставаться прозрачными")
    return image


board = QImage(960, 370, QImage.Format_ARGB32)
board.fill(QColor("#F0F3F4"))
painter = QPainter(board)
font = QFont("Segoe UI")
font.setPixelSize(24)
font.setBold(True)
painter.setFont(font)
painter.setPen(QColor("#213E49"))
painter.drawText(24, 36, "Hop / единый мятный фон, без внешней окантовки")

for index, (source_name, name, title) in enumerate(OPTIONS):
    original = ET.parse(OUT.parent / f"hop-options-extended-v1/{source_name}.svg").getroot()
    svg = copy.deepcopy(original)
    rectangles = list(svg.findall(f"{{{NS}}}rect"))
    if len(rectangles) != 3:
        raise RuntimeError("Не найдены ожидаемые слои рамки и подложки")
    # Два слоя внешней окантовки удаляются, геометрия цветной плашки сохраняется.
    for border in rectangles[:2]:
        svg.remove(border)
    rectangles[2].set("fill", "url(#tile)")
    group = svg.find(f"{{{NS}}}g")
    paths = group.findall(f"{{{NS}}}path")
    # Зазор между H и дугой получает тот же градиент и те же координаты, что и фон.
    paths[0].set("fill", "url(#tile)")
    paths[0].set("stroke", "url(#tile)")
    svg.find(f"{{{NS}}}title").text = "Hop — " + title + ", без внешней рамки"
    svg.find(f"{{{NS}}}desc").text = "Мятная подложка, тёмная H и оранжевая дуга с выносом за боковые края буквы."
    assert len(svg.findall(f"{{{NS}}}rect")) == 1
    assert ET.tostring(group.find(f"{{{NS}}}g")) == ET.tostring(original.find(f"{{{NS}}}g/{{{NS}}}g"))
    assert [node.get("d") for node in paths] == [node.get("d") for node in original.findall(f"{{{NS}}}g/{{{NS}}}path")]
    data = ET.tostring(svg, encoding="utf-8")
    (OUT / f"{name}.svg").write_bytes(data)
    for size in (256, 512, 1024):
        render(data, size).save(str(OUT / f"{name}-{size}.png"))
    x = 24 + index * 312
    painter.drawImage(x + 21, 58, render(data, 256))
    font.setPixelSize(18)
    painter.setFont(font)
    painter.drawText(x + 8, 343, title)

painter.end()
board.save(str(OUT / "variants.png"))
print("Готовы варианты 1–3: одинаковый мятный фон, без внешней рамки, прежние H и дуга")
