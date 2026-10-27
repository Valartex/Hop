"""ICO мятного зайца Hop с прозрачностью и проверкой каждого кадра Windows."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
SOURCE = OUT.parent / "hop-rabbit-colors-v1/01-rabbit-mint.png"
sys.path.insert(0, str(ROOT / ".tools" / "preview-runtime"))
from PySide6.QtCore import QByteArray, QBuffer, QIODevice, Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter, QIcon, QColor, QFont, QFontDatabase

APP = QGuiApplication([])
for path in ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/segoeuib.ttf"):
    QFontDatabase.addApplicationFont(path)
SIZES = (16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 128, 256)
original = QImage(str(SOURCE))
if original.isNull() or not original.hasAlphaChannel():
    raise RuntimeError("Не удалось прочитать прозрачный исходный PNG")
original = original.convertToFormat(QImage.Format_ARGB32_Premultiplied)
(OUT / "png").mkdir(exist_ok=True)


def frame(size):
    # Пропорции исходника сохранены, прямоугольный знак вписан в квадрат без обрезки.
    scaled = original.scaled(size, size, Qt.KeepAspectRatio, Qt.SmoothTransformation)
    image = QImage(size, size, QImage.Format_ARGB32_Premultiplied)
    image.fill(0)
    painter = QPainter(image)
    painter.drawImage((size - scaled.width()) // 2, (size - scaled.height()) // 2, scaled)
    painter.end()
    return image.convertToFormat(QImage.Format_ARGB32)


def png_data(image):
    data = QByteArray()
    buffer = QBuffer(data)
    buffer.open(QIODevice.WriteOnly)
    if not image.save(buffer, "PNG"):
        raise RuntimeError("Не удалось сохранить PNG")
    return bytes(data)


def dib_data(image):
    size = image.width()
    pixels, stride = bytes(image.constBits()), image.bytesPerLine()
    xor = b"".join(pixels[y * stride:y * stride + size * 4] for y in range(size - 1, -1, -1))
    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray(mask_stride * size)
    for row, y in enumerate(range(size - 1, -1, -1)):
        for x in range(size):
            if image.pixelColor(x, y).alpha() == 0:
                mask[row * mask_stride + x // 8] |= 0x80 >> (x % 8)
    return struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(xor), 0, 0, 0, 0) + xor + bytes(mask)


frames = [frame(size) for size in SIZES]
payloads = [png_data(image) if size == 256 else dib_data(image) for size, image in zip(SIZES, frames)]
offset = 6 + len(SIZES) * 16
entries = []
for size, payload, image in zip(SIZES, payloads, frames):
    entries.append(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(payload), offset))
    offset += len(payload)
    image.save(str(OUT / "png" / f"Hop-Rabbit-Mint-{size}.png"))
ICO = OUT / "Hop-Rabbit-Mint.ico"
ICO.write_bytes(struct.pack("<HHH", 0, 1, len(SIZES)) + b"".join(entries) + b"".join(payloads))


class IconInfo(ctypes.Structure):
    _fields_ = [("fIcon", wintypes.BOOL), ("xHotspot", wintypes.DWORD), ("yHotspot", wintypes.DWORD),
                ("hbmMask", wintypes.HANDLE), ("hbmColor", wintypes.HANDLE)]


class Bitmap(ctypes.Structure):
    _fields_ = [("bmType", wintypes.LONG), ("bmWidth", wintypes.LONG), ("bmHeight", wintypes.LONG),
                ("bmWidthBytes", wintypes.LONG), ("bmPlanes", wintypes.WORD), ("bmBitsPixel", wintypes.WORD),
                ("bmBits", ctypes.c_void_p)]


user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
user32.LoadImageW.argtypes = [wintypes.HINSTANCE, wintypes.LPCWSTR, wintypes.UINT, ctypes.c_int, ctypes.c_int, wintypes.UINT]
user32.LoadImageW.restype = wintypes.HANDLE
user32.GetIconInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(IconInfo)]
user32.GetIconInfo.restype = wintypes.BOOL
user32.DestroyIcon.argtypes = [wintypes.HANDLE]
gdi32.GetObjectW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p]
gdi32.GetObjectW.restype = ctypes.c_int
gdi32.DeleteObject.argtypes = [wintypes.HANDLE]

icon = QIcon(str(ICO))
if sorted(size.width() for size in icon.availableSizes()) != list(SIZES):
    raise RuntimeError("В ICO отсутствуют необходимые размеры")
decoded, report = {}, []
for size, expected in zip(SIZES, frames):
    handle = user32.LoadImageW(None, str(ICO), 1, size, size, 0x10)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    info = IconInfo()
    try:
        if not user32.GetIconInfo(handle, ctypes.byref(info)):
            raise ctypes.WinError(ctypes.get_last_error())
        bitmap = Bitmap()
        if not gdi32.GetObjectW(info.hbmColor, ctypes.sizeof(bitmap), ctypes.byref(bitmap)):
            raise ctypes.WinError(ctypes.get_last_error())
        if (bitmap.bmWidth, bitmap.bmHeight, bitmap.bmBitsPixel) != (size, size, 32):
            raise RuntimeError(f"Windows прочитала неверный размер или глубину: {size}")
    finally:
        if info.hbmColor:
            gdi32.DeleteObject(info.hbmColor)
        if info.hbmMask:
            gdi32.DeleteObject(info.hbmMask)
        user32.DestroyIcon(handle)
    image = icon.pixmap(size, size).toImage().convertToFormat(QImage.Format_ARGB32)
    if image.size() != expected.size():
        raise RuntimeError("Неверный размер декодированного кадра")
    alpha_error = max(abs(image.pixelColor(x, y).alpha() - expected.pixelColor(x, y).alpha())
                      for y in range(size) for x in range(size))
    if alpha_error or image.pixelColor(0, 0).alpha() != 0:
        raise RuntimeError(f"Повреждена прозрачность кадра: {size}")
    decoded[size] = image
    report.append(dict(size=size, bpp=32, encoding="PNG" if size == 256 else "DIB+AND",
                       win32_load=True, alpha_roundtrip_error=alpha_error))
(OUT / "verification.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


def text(painter, x, y, value, size=16, bold=False, color="#213E49"):
    font = QFont("Segoe UI")
    font.setPixelSize(size)
    font.setBold(bold)
    painter.setFont(font)
    painter.setPen(QColor(color))
    painter.drawText(x, y, value)


board = QImage(960, 400, QImage.Format_ARGB32)
board.fill(QColor("#EEF2F4"))
painter = QPainter(board)
text(painter, 24, 34, "Hop / мятный заяц — один ICO, 15 размеров", 23, True)
painter.drawImage(24, 44, decoded[128])
text(painter, 180, 86, "Прозрачный фон, исходные пропорции", 19, True)
text(painter, 180, 118, "Все кадры прочитаны из готового файла и проверены средствами Windows.", 15)
shown = ((52, 16), (128, 20), (210, 24), (300, 32), (410, 48), (540, 64), (734, 128))
for center, size in shown:
    text(painter, center - 8, 196, str(size), 13)
for top, bg in ((210, "#FFFFFF"), (292, "#181E24")):
    painter.fillRect(16, top, 928, 76, QColor(bg))
    for center, size in shown:
        if size == 128:
            continue
        painter.drawImage(center - size // 2, top + 38 - size // 2, decoded[size])
# Крупный кадр занимает высоту двух строк, без масштабирования.
painter.fillRect(650, 210, 280, 158, QColor("#414A50"))
painter.drawImage(670, 225, decoded[128])
text(painter, 24, 392, "Малые кадры показаны 1:1 при масштабе изображения 100%.", 13)
painter.end()
board.save(str(OUT / "preview.png"))
print(f"Готов один ICO: {len(SIZES)} кадров; Win32 и альфа-канал проверены для каждого размера")


