"""Проверка QML и действий предпросмотра без изменения сетевых настроек."""

import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".tools" / "preview-runtime"))
os.environ.setdefault("QT_QUICK_BACKEND", "software")
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QPA_FONTDIR", str(ROOT / "shared" / "client" / "fonts"))

from PySide6.QtCore import QMetaObject, QObject, Qt, QUrl, qInstallMessageHandler
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuickControls2 import QQuickStyle
from PySide6.QtTest import QTest


def find_item(root, name):
    if root.objectName() == name:
        return root
    children = root.childItems() if hasattr(root, "childItems") else root.children()
    for child in children:
        found = find_item(child, name)
        if found is not None:
            return found
    return None


class DashboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.messages = []
        qInstallMessageHandler(lambda kind, context, message: cls.messages.append(message))
        cls.app = QGuiApplication.instance() or QGuiApplication([])
        QQuickStyle.setStyle("Basic")

    def setUp(self):
        self.messages.clear()
        self.engine = QQmlApplicationEngine()
        self.engine.load(QUrl.fromLocalFile(str(Path(__file__).with_name("main.qml"))))
        self.assertTrue(self.engine.rootObjects(), "QML must load")
        self.window = self.engine.rootObjects()[0]
        QTest.qWait(60)
        self.dashboard = find_item(self.window.contentItem(), "hopPreviewDashboard")
        self.assertIsNotNone(self.dashboard)

    def tearDown(self):
        self.window.close()
        self.engine.deleteLater()
        QTest.qWait(10)
        self.assertEqual(self.messages, [], "QML runtime warnings")

    def click(self, name):
        item = find_item(self.window.contentItem(), name)
        self.assertIsNotNone(item, name)
        self.assertTrue(item.property("enabled"), name)
        self.assertTrue(QMetaObject.invokeMethod(item, "clicked", Qt.DirectConnection))
        QTest.qWait(20)

    def test_selection_changes_service_and_protocol(self):
        self.click("hopSelectServer_1")
        self.assertEqual(self.dashboard.property("selectedServerId"), "demo-amnezia")
        self.assertEqual(self.dashboard.property("selectedProtocol"), "AmneziaWG 3.1")

    def test_connected_state_locks_selection(self):
        self.dashboard.setProperty("connected", True)
        QTest.qWait(20)
        self.assertFalse(find_item(self.window.contentItem(), "hopSelectServer_1").property("enabled"))
        self.assertFalse(find_item(self.window.contentItem(), "hopProtocolButton").property("enabled"))
        self.assertTrue(find_item(self.window.contentItem(), "hopConnectButton").property("enabled"))

    def test_connecting_state_locks_selection(self):
        self.dashboard.setProperty("connecting", True)
        QTest.qWait(20)
        self.assertFalse(find_item(self.window.contentItem(), "hopSelectServer_1").property("enabled"))

    def test_preview_does_not_claim_real_connection(self):
        self.click("hopConnectButton")
        notice = self.window.findChild(QObject, "hopPreviewNotice")
        self.assertIsNotNone(notice)
        self.assertTrue(notice.property("visible"))
        self.assertFalse(self.dashboard.property("connected"))

    def test_empty_state_disables_connection(self):
        self.dashboard.setProperty("selectedServerId", "")
        self.dashboard.setProperty("serverModel", None)
        QTest.qWait(20)
        self.assertFalse(find_item(self.window.contentItem(), "hopConnectButton").property("enabled"))
        self.assertEqual(find_item(self.window.contentItem(), "hopServersList").property("count"), 0)

    def test_narrow_view_is_renderable(self):
        self.window.setWidth(360)
        self.window.setHeight(640)
        QTest.qWait(20)
        image = self.window.grabWindow()
        self.assertFalse(image.isNull())
        self.assertEqual(image.width(), 360 * self.window.devicePixelRatio())

    def test_health_states_are_visible_without_claiming_throughput(self):
        self.dashboard.setProperty("healthAvailable", True)
        self.dashboard.setProperty("healthStatuses", {
            "demo-blanc": {"state": "reachable", "latencyMs": 380},
            "demo-amnezia": {"state": "unsupported", "latencyMs": -1},
            "demo-personal": {"state": "unreachable", "latencyMs": -1},
        })
        QTest.qWait(20)
        self.assertEqual(find_item(self.window.contentItem(), "hopHealthStatus_0").property("text"), "380 мс")
        self.assertEqual(find_item(self.window.contentItem(), "hopHealthStatus_1").property("text"), "—")
        self.assertEqual(find_item(self.window.contentItem(), "hopHealthStatus_2").property("text"), "Нет ответа")
        self.window.setWidth(480)
        QTest.qWait(20)
        self.assertFalse(self.window.grabWindow().isNull())


if __name__ == "__main__":
    unittest.main()
