"""Синхронизация выбранного ICO Hop без изменения его кадров и геометрии."""
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".tools" / "preview-runtime"))
from PySide6.QtGui import QGuiApplication, QIcon

app = QGuiApplication([])
assets = ROOT / "client" / "images" / "hop"
source = ROOT / "docs/branding/hop-rabbit-windows-ico-v1/Hop-Rabbit.ico"
disconnected_source = ROOT / "docs/branding/hop-rabbit-mint-windows-ico-v1/Hop-Rabbit-Mint.ico"


shutil.copyfile(source, assets / "hop.ico")
shutil.copyfile(disconnected_source, assets / "hop-disconnected.ico")
disconnected_icon = QIcon(str(disconnected_source))
if disconnected_icon.isNull() or len(disconnected_icon.availableSizes()) != 15:
    raise RuntimeError("ICO отключённого состояния должен содержать 15 кадров")
icon = QIcon(str(source))
if icon.isNull() or len(icon.availableSizes()) != 15:
    raise RuntimeError("Выбранный ICO должен содержать 15 кадров")
if not icon.pixmap(256, 256).save(str(assets / "logo-preview.png")):
    raise RuntimeError("Не удалось создать предпросмотр")
print("Hop: выбранный ICO скопирован без изменений (15 размеров); предпросмотр обновлён")
