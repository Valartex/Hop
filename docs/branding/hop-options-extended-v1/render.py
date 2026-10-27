"""Тот же утверждённый вынос траектории для остальных вариантов серии."""
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
APPROVED_PATH = OUT.parent / "hop-mint-extended-v1/hop-mint-extended.svg"
approved_bytes = APPROVED_PATH.read_bytes()
approved = ET.fromstring(approved_bytes)
arc = approved.findall(f"{{{NS}}}g/{{{NS}}}path")[-1].get("d")
OPTIONS = (
    ("01-light-rounded", "1. Светлый", "Умеренное скругление"),
    ("02-light-square", "2. Почти квадрат", "Та же палитра, другие углы"),
    ("03-hop-mint", "3. Фирменный Hop", "Уже утверждённый вынос"),
    ("04-dark-rounded", "4. Тёмный", "Светлая H на тёмной плашке"),
)


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


board = QImage(1120, 370, QImage.Format_ARGB32)
board.fill(QColor("#F0F3F4"))
painter = QPainter(board)
text(painter, 24, 34, "Hop / одинаковый вынос траектории во всей серии", 24, True)
for index, (name, title, note) in enumerate(OPTIONS):
    if name == "03-hop-mint":
        # Утверждённый третий вариант копируется без изменения содержимого.
        data = approved_bytes
        (OUT / f"{name}.svg").write_bytes(data)
        for size in (256, 512, 1024):
            (OUT / f"{name}-{size}.png").write_bytes(
                (OUT.parent / f"hop-mint-extended-v1/hop-mint-extended-{size}.png").read_bytes())
    else:
        original = ET.parse(OUT.parent / f"hop-final-options-v1/{name}.svg").getroot()
        svg = copy.deepcopy(original)
        for path in svg.findall(f"{{{NS}}}g/{{{NS}}}path"):
            path.set("d", arc)
        svg.find(f"{{{NS}}}title").text = "Hop — " + title + ", вынос траектории"
        svg.find(f"{{{NS}}}desc").text = "Исходное оформление серии с продлёнными за края H концами оранжевой траектории."
        assert ET.tostring(svg.find(f"{{{NS}}}g/{{{NS}}}g")) == ET.tostring(original.find(f"{{{NS}}}g/{{{NS}}}g"))
        assert [ET.tostring(node) for node in svg.findall(f"{{{NS}}}rect")] == [ET.tostring(node) for node in original.findall(f"{{{NS}}}rect")]
        data = ET.tostring(svg, encoding="utf-8")
        (OUT / f"{name}.svg").write_bytes(data)
        for size in (256, 512, 1024):
            render(data, size).save(str(OUT / f"{name}-{size}.png"))
    assert ET.fromstring(data).findall(f"{{{NS}}}g/{{{NS}}}path")[-1].get("d") == arc
    x = 24 + index * 274
    painter.drawImage(x + 15, 58, render(data, 224))
    text(painter, x + 12, 314, title, 20, True)
    text(painter, x + 12, 340, note, 13)
painter.end()
board.save(str(OUT / "variants.png"))
print("Вынос одинаков у всех четырёх; варианты 1, 2 и 4 готовы, вариант 3 скопирован без изменений")
