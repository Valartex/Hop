"""Продление исходной траектории третьего варианта без изменения её середины."""
from pathlib import Path
import copy
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / ".tools" / "preview-runtime"))
from PySide6.QtCore import QByteArray
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

APP = QGuiApplication([])
NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", NS)
original = ET.parse(OUT.parent / "hop-final-options-v1/03-hop-mint.svg").getroot()
svg = copy.deepcopy(original)
group = svg.find(f"{{{NS}}}g")

# Исходные два кубических сегмента внутри H оставлены дословно.
# К ним добавлены только продолжения слева и справа.
arc = (
    "M8 39.6C9.4 38 10.7 36.9 12 35.8"
    "C25.5 24.5 43.1 15.9 52.05 21.25"
    "C53.3 22 54.4 22.8 55.5 23.65L55.5 24.05"
    "C54.4 23.05 53.3 22.1 52.05 21.65"
    "C43 18.7 27 30 12 42.8"
    "C10.7 43.9 9.4 45 8 46.1Z"
)
for path in group.findall(f"{{{NS}}}path"):
    path.set("d", arc)
svg.find(f"{{{NS}}}title").text = "Hop — фирменный вариант с выносом траектории"
svg.find(f"{{{NS}}}desc").text = "Исходная H и оранжевая дуга; начало и конец дуги продлены за боковые края буквы."

# Геометрия H и плашка остаются идентичными выбранному исходнику.
assert ET.tostring(group.find(f"{{{NS}}}g")) == ET.tostring(original.find(f"{{{NS}}}g/{{{NS}}}g"))
assert [ET.tostring(node) for node in svg.findall(f"{{{NS}}}rect")] == [ET.tostring(node) for node in original.findall(f"{{{NS}}}rect")]
data = ET.tostring(svg, encoding="utf-8")
(OUT / "hop-mint-extended.svg").write_bytes(data)
renderer = QSvgRenderer(QByteArray(data))
if not renderer.isValid():
    raise RuntimeError("Некорректный SVG")
for size in (256, 512, 1024):
    image = QImage(size, size, QImage.Format_ARGB32)
    image.fill(0)
    painter = QPainter(image)
    renderer.render(painter)
    painter.end()
    image.save(str(OUT / f"hop-mint-extended-{size}.png"))
print("Продлены только края траектории; исходные H, плашка и центральная дуга сохранены")
