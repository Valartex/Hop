"""Предпросмотр общего QML-компонента без VPN-службы и реальных ключей."""

import argparse
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / ".tools" / "preview-runtime"
if RUNTIME.exists():
    sys.path.insert(0, str(RUNTIME))
os.environ.setdefault("QT_QUICK_BACKEND", "software")
os.environ.setdefault("QT_QPA_FONTDIR", str(ROOT / "client" / "fonts"))

from PySide6.QtCore import QTimer, QUrl, qInstallMessageHandler
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuickControls2 import QQuickStyle


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--width", type=int, default=760)
    parser.add_argument("--height", type=int, default=680)
    args = parser.parse_args()

    if args.capture:
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

    messages = []

    def handle_message(kind, context, message):
        messages.append(message)
        print(message, file=sys.stderr)

    qInstallMessageHandler(handle_message)
    app = QGuiApplication(sys.argv[:1])
    QQuickStyle.setStyle("Basic")
    engine = QQmlApplicationEngine()
    engine.load(QUrl.fromLocalFile(str(Path(__file__).with_name("main.qml"))))
    if not engine.rootObjects():
        return 1
    window = engine.rootObjects()[0]
    window.setWidth(args.width)
    window.setHeight(args.height)

    if args.capture:
        def capture():
            args.capture.parent.mkdir(parents=True, exist_ok=True)
            saved = window.grabWindow().save(str(args.capture))
            print(f"Saved preview: {args.capture}" if saved else "Preview capture failed")
            app.exit(0 if saved and not messages else 1)

        QTimer.singleShot(1000, capture)
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
