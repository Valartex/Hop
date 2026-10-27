"""Поиск более воздушной траектории при сохранении выбранной H и фона."""
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
    if QFontDatabase.addApplicationFont(font_path) < 0:
        raise RuntimeError("Не удалось загрузить шрифт")

NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", NS)
BASE = ET.parse(OUT.parent / "hop-final-options-v1/03-hop-mint.svg").getroot()
OPTIONS = (
    dict(id="01-high-arc", title="1. Высокая дуга", note="Тонкий след, больше воздуха снизу", paths=(
        "M16 31C21 12 39 6 50 23L48.8 24.2C37 10 24 15 18 32Z",
    )),
    dict(id="02-overflight", title="2. Полёт над H", note="Дуга поднимается над буквой", paths=(
        "M14 17C22 3 41 2.5 51 14L49.8 15.3C40 5.5 24 6.5 16 18Z",
    )),
    dict(id="03-takeoff", title="3. Отрыв", note="Разрыв между взлётом и полётом", paths=(
        "M16 31C17.5 26.3 19.5 22.3 22 20L23.3 21.3C21 23.9 19.2 27.9 18 32Z",
        "M25 16C34 8 44 10 50 22L48.5 23C43 13 35 11.5 26 17.8Z",
    )),
)


def source(option):
    svg = copy.deepcopy(BASE)
    svg.find(f"{{{NS}}}title").text = "Hop — " + option["title"]
    svg.find(f"{{{NS}}}desc").text = "H с более лёгкой оранжевой траекторией прыжка."
    group = svg.find(f"{{{NS}}}g")
    # Удаляется только прежняя лента, геометрия H и плашки остаётся прежней.
    for path in list(group.findall(f"{{{NS}}}path")):
        group.remove(path)
    for path_data in option["paths"]:
        ET.SubElement(group, f"{{{NS}}}path", {
            "d": path_data, "fill": "url(#tile)", "stroke": "url(#tile)",
            "stroke-width": "0.9", "stroke-linejoin": "round",
        })
        ET.SubElement(group, f"{{{NS}}}path", {"d": path_data, "fill": "#EF862F"})
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


def text(painter, x, y, value, size=15, bold=False, color="#213E49"):
    font = QFont("Segoe UI")
    font.setPixelSize(size)
    font.setBold(bold)
    painter.setFont(font)
    painter.setPen(QColor(color))
    painter.drawText(x, y, value)


original = ET.tostring(BASE, encoding="utf-8")
items = [("Было", "Широкая диагональная лента", original)]
for option in OPTIONS:
    svg = source(option)
    (OUT / f"{option['id']}.svg").write_bytes(svg)
    for size in (16, 24, 32, 48, 256, 1024):
        render(svg, size).save(str(OUT / f"{option['id']}-{size}.png"))
    items.append((option["title"], option["note"], svg))

board = QImage(1120, 380, QImage.Format_ARGB32)
board.fill(QColor("#F0F3F4"))
painter = QPainter(board)
text(painter, 24, 34, "Hop / от ленты к траектории полёта", 24, True)
for index, (title, note, svg) in enumerate(items):
    x = 24 + index * 274
    painter.drawImage(x + 15, 58, render(svg, 224))
    text(painter, x + 12, 314, title, 20, True)
    text(painter, x + 12, 340, note, 13)
painter.end()
board.save(str(OUT / "comparison.png"))

proof = QImage(1120, 264, QImage.Format_ARGB32)
proof.fill(QColor("#F0F3F4"))
painter = QPainter(proof)
text(painter, 24, 28, "16 / 24 / 32 / 48 px — фактический размер при масштабе 100%", 20, True)
for index, (title, _, svg) in enumerate(items):
    x = 24 + index * 274
    text(painter, x + 12, 62, title, 16, True)
    for row, (bg, top) in enumerate((("#FFFFFF", 76), ("#181B20", 155))):
        painter.fillRect(x, top, 252, 68, QColor(bg))
        for offset, size in ((30, 16), (82, 24), (138, 32), (206, 48)):
            painter.drawImage(x + offset - size // 2, top + 34 - size // 2, render(svg, size))
text(painter, 24, 250, "Это эскизы направления. Малые кадры ещё не адаптированы оптически.", 13)
painter.end()
proof.save(str(OUT / "small-sizes.png"))
print("Созданы 3 варианта полёта, SVG/PNG и сравнение с исходной лентой")
