"""Варианты подложки Hop и сравнение значков на разных фонах."""
from pathlib import Path
import struct
import sys
import json

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / ".tools" / "preview-runtime"))
from PySide6.QtCore import QByteArray, QBuffer, QIODevice, QRectF, Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter, QColor, QFont, QFontDatabase, QIcon
from PySide6.QtSvg import QSvgRenderer

APP = QGuiApplication([])
for font_path in ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/segoeuib.ttf"):
    if QFontDatabase.addApplicationFont(font_path) < 0:
        raise RuntimeError(f"Не удалось загрузить шрифт: {font_path}")

SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
OPTIONS = (
    dict(id="01-light-rounded", title="1. Светлый", note="Умеренное скругление", bg="#E1E9ED", ink="#073641", radius=10),
    dict(id="02-light-square", title="2. Почти квадрат", note="Та же палитра, другие углы", bg="#E1E9ED", ink="#073641", radius=3),
    dict(id="03-hop-mint", title="3. Фирменный Hop", note="Мятный градиент, мягкие углы", bg="url(#tile)", ink="#073641", radius=18),
    dict(id="04-dark-rounded", title="4. Тёмный", note="Светлая H на тёмной плашке", bg="#102932", ink="#E7F0F2", radius=10),
)


def source(option, micro=False):
    bg, ink, radius = option["bg"], option["ink"], option["radius"]
    # В малом размере дальний конец сохраняет оранжевую полосу, а не уходит в долю пикселя.
    arc = ("M12 35.8C25.5 24.5 43.1 15.9 52.05 21.25L52.05 23.1C43 19.9 27 30 12 42.8Z"
           if micro else "M12 35.8C25.5 24.5 43.1 15.9 52.05 21.25L52.05 21.65C43 18.7 27 30 12 42.8Z")
    transform = ' transform="translate(-3.2 -3.2) scale(1.1)"' if micro else ""
    clearance = 1.45 if micro else 1.2
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 64 64" role="img" aria-labelledby="title desc">
  <title id="title">Hop — {option['title']}</title>
  <desc id="desc">Буква H с оранжевой траекторией прыжка на непрозрачной подложке.</desc>
  <defs>
    <linearGradient id="tile" x1="8" y1="4" x2="56" y2="60" gradientUnits="userSpaceOnUse">
      <stop stop-color="#8CEBDD"/><stop offset="1" stop-color="#42BFC2"/>
    </linearGradient>
  </defs>
  <rect x="1.7" y="1.7" width="60.6" height="60.6" rx="{radius+0.3}" fill="#4C6570"/>
  <rect x="2.3" y="2.3" width="59.4" height="59.4" rx="{radius-0.3}" fill="#E5EEF0"/>
  <rect x="2.65" y="2.65" width="58.7" height="58.7" rx="{radius-0.65}" fill="{bg}"/>
  <g{transform}>
    <g fill="{ink}">
      <rect x="12" y="10.8" width="13.4" height="42.5" rx="1.35"/>
      <rect x="38.6" y="10.8" width="13.45" height="42.5" rx="1.35"/>
      <rect x="24" y="28.85" width="16" height="7.2"/>
    </g>
    <path d="{arc}" fill="{bg}" stroke="{bg}" stroke-width="{clearance}" stroke-linejoin="round"/>
    <path d="{arc}" fill="#EF862F"/>
  </g>
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


def png_bytes(image):
    data = QByteArray()
    buffer = QBuffer(data)
    buffer.open(QIODevice.WriteOnly)
    if not image.save(buffer, "PNG"):
        raise RuntimeError("Не удалось сохранить PNG")
    return bytes(data)


def ico(path, images):
    offset = 6 + len(SIZES) * 16
    entries, payload = [], []
    for size, image in zip(SIZES, images):
        data = png_bytes(image)
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset))
        payload.append(data)
        offset += len(data)
    path.write_bytes(struct.pack("<HHH", 0, 1, len(SIZES)) + b"".join(entries) + b"".join(payload))


def text(painter, x, y, value, size=15, color="#213E49", bold=False):
    font = QFont("Segoe UI")
    font.setPixelSize(size)
    font.setBold(bold)
    painter.setFont(font)
    painter.setPen(QColor(color))
    painter.drawText(x, y, value)


images, verification = {}, []
for option in OPTIONS:
    option_id = option["id"]
    for micro in (False, True):
        suffix = "-micro" if micro else ""
        (OUT / f"{option_id}{suffix}.svg").write_bytes(source(option, micro))
    frames = []
    for size in (*SIZES, 1024):
        image = render(source(option, size <= 24), size)
        image.save(str(OUT / f"{option_id}-{size}.png"))
        images[option_id, size] = image
        if size in SIZES:
            frames.append(image)
        # На 16 px почти квадратный угол может частично покрывать крайний пиксель.
        if image.pixelColor(0, 0).alpha() == 255 or (size >= 64 and image.pixelColor(0, 0).alpha() != 0):
            raise RuntimeError("Потеряна прозрачность внешнего угла")
        # Прямоугольник внутри плашки целиком непрозрачен: чужой фон не просвечивает через H.
        for y in range(size // 4, size * 3 // 4):
            for x in range(size // 4, size * 3 // 4):
                if image.pixelColor(x, y).alpha() != 255:
                    raise RuntimeError(f"Прозрачный пиксель внутри плашки: {option_id}, {size}")
        orange_pixels = sum(
            1 for y in range(size) for x in range(size)
            if (lambda c: c.alpha() > 200 and c.red() > c.green() * 1.3 and c.green() > c.blue() * 1.3)(image.pixelColor(x, y))
        ) if size <= 48 else None
        if size <= 48 and orange_pixels < 3:
            raise RuntimeError(f"Оранжевая траектория исчезла: {option_id}, {size}")
        verification.append(dict(variant=option_id, size=size, orange_pixels=orange_pixels))
    ico(OUT / f"{option_id}.ico", frames)
    available_sizes = sorted(size.width() for size in QIcon(str(OUT / f"{option_id}.ico")).availableSizes())
    if available_sizes != list(SIZES):
        raise RuntimeError(f"Qt не прочитал все кадры ICO: {option_id}, {available_sizes}")

# Основной лист вариантов: геометрия знака одна, меняются подложка, углы и инверсия H.
board = QImage(1120, 370, QImage.Format_ARGB32)
board.fill(QColor("#F0F3F4"))
painter = QPainter(board)
text(painter, 24, 34, "Hop / варианты оформления выбранного знака", 24, bold=True)
for index, option in enumerate(OPTIONS):
    x = 24 + index * 274
    painter.drawImage(x + 15, 58, render(source(option), 224))
    text(painter, x + 12, 314, option["title"], 20, bold=True)
    text(painter, x + 12, 340, option["note"], 13)
painter.end()
board.save(str(OUT / "variants.png"))

# Честный показ маленьких кадров: каждый значок рисуется в собственном физическом размере.
proof = QImage(1120, 634, QImage.Format_ARGB32)
proof.fill(QColor("#F0F3F4"))
painter = QPainter(proof)
text(painter, 24, 33, "16 / 24 / 32 / 48 px — фактические размеры при масштабе 100%", 21, bold=True)
for index, option in enumerate(OPTIONS):
    text(painter, 160 + index * 235, 70, option["title"], 16, bold=True)
    for shift, size in ((18, 16), (65, 24), (119, 32), (181, 48)):
        text(painter, 160 + index * 235 + shift - 7, 94, str(size), 12)
for row, (name, bg, foreground) in enumerate((
    ("Светлая", "#FFFFFF", "#173640"),
    ("Тёмная", "#181B20", "#EDF3F5"),
    ("Цветная", "#5C4A82", "#FFFFFF"),
    ("Неоднородная", "pattern", "#FFFFFF"),
)):
    y = 108 + row * 112
    painter.fillRect(16, y, 1088, 104, QColor(bg if bg != "pattern" else "#384A54"))
    if bg == "pattern":
        for x in range(16, 1104, 16):
            for cell_y in range(y, y + 104, 16):
                if ((x - 16) // 16 + (cell_y - y) // 16) % 2:
                    painter.fillRect(x, cell_y, 16, min(16, y + 104 - cell_y), QColor("#A9876B"))
    text(painter, 30, y + 58, name, 14, foreground, True)
    for index, option in enumerate(OPTIONS):
        for shift, size in ((18, 16), (65, 24), (119, 32), (181, 48)):
            painter.drawImage(160 + index * 235 + shift - size // 2, y + 52 - size // 2, images[option["id"], size])
text(painter, 24, 593, "Одинаковая непрозрачная подложка сохраняет внутренний контраст при смене темы.", 15)
text(painter, 24, 618, "Для 16–24 px: немного увеличен знак и утолщён дальний конец дуги. Проверка реального трея ещё не выполнена.", 13)
painter.end()
proof.save(str(OUT / "backgrounds-and-sizes.png"))

# Отдельно увеличенные кадры 16 px, чтобы можно было оценить пиксели без сглаживающего увеличения.
zoom = QImage(800, 220, QImage.Format_ARGB32)
zoom.fill(QColor("#F0F3F4"))
painter = QPainter(zoom)
for index, option in enumerate(OPTIONS):
    text(painter, index * 200 + 16, 28, option["title"], 15, bold=True)
    painter.drawImage(QRectF(index * 200 + 32, 49, 128, 128), images[option["id"], 16])
text(painter, 18, 206, "16 × 16, увеличение ×8 без сглаживания", 13)
painter.end()
zoom.save(str(OUT / "16px-enlarged.png"))
(OUT / "verification.json").write_text(json.dumps(verification, ensure_ascii=False, indent=2), encoding="utf-8")
print("Созданы 4 варианта: SVG, отдельные PNG, ICO с 9 размерами, листы сравнения; проверки пройдены")
