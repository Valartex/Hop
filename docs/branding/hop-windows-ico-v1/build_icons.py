"""Мультиразмерные ICO трёх вариантов Hop и проверка их загрузки Windows."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
SOURCE = OUT.parent / "hop-mint-no-frame-v1"
sys.path.insert(0, str(ROOT / ".tools" / "preview-runtime"))
from PySide6.QtCore import QByteArray, QBuffer, QIODevice
from PySide6.QtGui import QGuiApplication, QImage, QPainter, QIcon, QColor, QFont, QFontDatabase
from PySide6.QtSvg import QSvgRenderer

APP = QGuiApplication([])
for font_path in ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/segoeuib.ttf"):
    if QFontDatabase.addApplicationFont(font_path) < 0:
        raise RuntimeError("Не удалось загрузить шрифт")

# Набор размеров для трея, заголовка, панели задач, Пуска и Проводника при разных DPI.
SIZES = (16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 128, 256)
OPTIONS = (
    ("01-mint-rounded", "Hop-01-rounded", "1. Умеренное скругление"),
    ("02-mint-square", "Hop-02-square", "2. Почти квадрат"),
    ("03-mint-soft", "Hop-03-soft", "3. Сильное скругление"),
)
(OUT / "png").mkdir(exist_ok=True)
(OUT / "svg").mkdir(exist_ok=True)


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


def png_data(image):
    data = QByteArray()
    buffer = QBuffer(data)
    buffer.open(QIODevice.WriteOnly)
    if not image.save(buffer, "PNG"):
        raise RuntimeError("Не удалось создать PNG")
    return bytes(data)


def dib_data(image):
    # ICO хранит обычные кадры как нижнеориентированный BGRA DIB и однобитную AND-маску.
    size = image.width()
    stride = image.bytesPerLine()
    pixels = bytes(image.constBits())
    xor = b"".join(pixels[y * stride:y * stride + size * 4] for y in range(size - 1, -1, -1))
    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray(mask_stride * size)
    for row, y in enumerate(range(size - 1, -1, -1)):
        for x in range(size):
            if image.pixelColor(x, y).alpha() == 0:
                mask[row * mask_stride + x // 8] |= 0x80 >> (x % 8)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(xor), 0, 0, 0, 0)
    return header + xor + bytes(mask)


def write_ico(path, images):
    payloads = [png_data(image) if size == 256 else dib_data(image) for size, image in zip(SIZES, images)]
    offset = 6 + len(SIZES) * 16
    entries = []
    for size, data in zip(SIZES, payloads):
        entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset))
        offset += len(data)
    path.write_bytes(struct.pack("<HHH", 0, 1, len(SIZES)) + b"".join(entries) + b"".join(payloads))


class IconInfo(ctypes.Structure):
    _fields_ = [("fIcon", wintypes.BOOL), ("xHotspot", wintypes.DWORD), ("yHotspot", wintypes.DWORD),
                ("hbmMask", wintypes.HANDLE), ("hbmColor", wintypes.HANDLE)]


class Bitmap(ctypes.Structure):
    _fields_ = [("bmType", wintypes.LONG), ("bmWidth", wintypes.LONG), ("bmHeight", wintypes.LONG),
                ("bmWidthBytes", wintypes.LONG), ("bmPlanes", wintypes.WORD), ("bmBitsPixel", wintypes.WORD),
                ("bmBits", ctypes.c_void_p)]


USER32 = ctypes.WinDLL("user32", use_last_error=True)
GDI32 = ctypes.WinDLL("gdi32", use_last_error=True)
USER32.LoadImageW.argtypes = [wintypes.HINSTANCE, wintypes.LPCWSTR, wintypes.UINT, ctypes.c_int, ctypes.c_int, wintypes.UINT]
USER32.LoadImageW.restype = wintypes.HANDLE
USER32.GetIconInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(IconInfo)]
USER32.GetIconInfo.restype = wintypes.BOOL
USER32.DestroyIcon.argtypes = [wintypes.HANDLE]
USER32.DestroyIcon.restype = wintypes.BOOL
GDI32.GetObjectW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p]
GDI32.GetObjectW.restype = ctypes.c_int
GDI32.DeleteObject.argtypes = [wintypes.HANDLE]
GDI32.DeleteObject.restype = wintypes.BOOL


def check_win32(path, size):
    # Загрузка файла средствами самой Windows без запуска приложения и без показа окон.
    handle = USER32.LoadImageW(None, str(path), 1, size, size, 0x10)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    info = IconInfo()
    try:
        if not USER32.GetIconInfo(handle, ctypes.byref(info)):
            raise ctypes.WinError(ctypes.get_last_error())
        bitmap = Bitmap()
        if not GDI32.GetObjectW(info.hbmColor, ctypes.sizeof(bitmap), ctypes.byref(bitmap)):
            raise ctypes.WinError(ctypes.get_last_error())
        if (bitmap.bmWidth, bitmap.bmHeight, bitmap.bmBitsPixel) != (size, size, 32):
            raise RuntimeError(f"Win32 прочитал неверный кадр: {path.name}, {size}")
    finally:
        if info.hbmColor:
            GDI32.DeleteObject(info.hbmColor)
        if info.hbmMask:
            GDI32.DeleteObject(info.hbmMask)
        USER32.DestroyIcon(handle)


decoded, report = {}, []
for source_name, name, title in OPTIONS:
    source = (SOURCE / f"{source_name}.svg").read_bytes()
    (OUT / "svg" / f"{name}.svg").write_bytes(source)
    images = [render(source, size) for size in SIZES]
    for size, image in zip(SIZES, images):
        image.save(str(OUT / "png" / f"{name}-{size}.png"))
    path = OUT / f"{name}.ico"
    write_ico(path, images)

    icon = QIcon(str(path))
    if sorted(size.width() for size in icon.availableSizes()) != list(SIZES):
        raise RuntimeError(f"В ICO не найдены все кадры: {name}")
    for size, original in zip(SIZES, images):
        check_win32(path, size)
        image = icon.pixmap(size, size).toImage().convertToFormat(QImage.Format_ARGB32)
        if image.size() != original.size():
            raise RuntimeError(f"Неверный размер после декодирования: {name}, {size}")
        alpha_error = max(abs(image.pixelColor(x, y).alpha() - original.pixelColor(x, y).alpha())
                          for y in range(size) for x in range(size))
        if alpha_error:
            raise RuntimeError(f"Альфа-канал повреждён: {name}, {size}, ошибка {alpha_error}")
        decoded[name, size] = image
        report.append(dict(file=path.name, size=size, bpp=32, encoding="PNG" if size == 256 else "DIB+AND",
                           alpha_roundtrip_error=alpha_error, win32_load=True))


def text(painter, x, y, value, size=16, bold=False, color="#213E49"):
    font = QFont("Segoe UI")
    font.setPixelSize(size)
    font.setBold(bold)
    painter.setFont(font)
    painter.setPen(QColor(color))
    painter.drawText(x, y, value)


board = QImage(1056, 510, QImage.Format_ARGB32)
board.fill(QColor("#EEF2F4"))
painter = QPainter(board)
text(painter, 24, 34, "Hop / три Windows ICO — по 15 размеров в каждом", 23, True)
for index, (_, name, title) in enumerate(OPTIONS):
    x = 24 + index * 344
    painter.drawImage(x + 94, 52, decoded[name, 128])
    text(painter, x + 10, 211, title, 18, True)
    for shift, size in ((20, 16), (62, 20), (108, 24), (158, 32), (220, 48), (292, 64)):
        text(painter, x + shift - 7, 241, str(size), 12)
    for top, bg in ((252, "#FFFFFF"), (350, "#181E24")):
        painter.fillRect(x, top, 320, 88, QColor(bg))
        for shift, size in ((20, 16), (62, 20), (108, 24), (158, 32), (220, 48), (292, 64)):
            painter.drawImage(x + shift - size // 2, top + 44 - size // 2, decoded[name, size])
text(painter, 24, 475, "Показаны кадры, прочитанные из готовых ICO. Малые размеры — 1:1 при масштабе изображения 100%.", 14)
text(painter, 24, 499, "Форма, фон и траектория исходных SVG сохранены. Внешние углы прозрачные.", 13)
painter.end()
board.save(str(OUT / "preview.png"))
(OUT / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"Готово: 3 ICO по {len(SIZES)} кадров, {len(report)} проверок Win32 и точного сохранения альфа-канала")
