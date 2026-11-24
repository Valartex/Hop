"""Контракт нового экрана с интерфейсами контроллеров; сетевые вызовы заменены."""

import os
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / ".tools" / "preview-runtime"))
os.environ.setdefault("QT_QUICK_BACKEND", "software")
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QPA_FONTDIR", str(ROOT / "shared" / "client" / "fonts"))

from PySide6.QtCore import Q_ARG, QMetaObject, Qt, QUrl, qInstallMessageHandler
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine, QQmlComponent, qmlRegisterType
from PySide6.QtQuickControls2 import QQuickStyle
from PySide6.QtTest import QTest
from test_dashboard import find_item


class HomeIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.messages = []
        qInstallMessageHandler(lambda kind, context, message: cls.messages.append(message))
        cls.app = QGuiApplication.instance() or QGuiApplication([])
        QQuickStyle.setStyle("Basic")
        header = (ROOT / "shared/client/ui/utils/pageEnum.h").read_text(encoding="utf-8")
        enum_block = re.search(r"enum class PageEnum\s*\{(.*?)\};", header, re.S).group(1)
        enum_names = re.findall(r"\b(Page\w+)\b", enum_block)
        cls.enums = {name: index for index, name in enumerate(enum_names)}
        enum_file = ROOT / ".research/preview-mocks/PageEnum.qml"
        enum_file.parent.mkdir(parents=True, exist_ok=True)
        enum_file.write_text("import QtQml\nQtObject { enum Pages { " + ", ".join(enum_names) + " } }\n", encoding="utf-8")
        qmlRegisterType(QUrl.fromLocalFile(str(enum_file)), "PageEnum", 1, 0, "PageEnum")

    def create_mock(self, name, body, model=False):
        component = QQmlComponent(self.engine)
        source = "import QtQuick\n" + ("ListModel {" if model else "QtObject {") + body + "}"
        component.setData(source.encode(), QUrl())
        obj = component.create()
        self.assertIsNotNone(obj, str(component.errors()))
        obj.setParent(self.engine)
        self.mocks[name] = obj
        self.engine.rootContext().setContextProperty(name, obj)
        return obj

    def setUp(self):
        self.messages.clear()
        self.engine = QQmlApplicationEngine()
        self.engine.addImportPath(str(ROOT / "shared/client/ui/qml/Modules"))
        self.mocks = {}
        self.servers = self.create_mock("ServersUiController", '''
            property string defaultServerId: "test-a"
            property string defaultServerName: "Test A"
            property string defaultServerDefaultContainerName: "AmneziaWG"
            property bool isDefaultServerFromApi: true
            property bool defaultServerHasOutdatedAwgContainer: false
            property bool isDefaultServerDefaultContainerHasSplitTunneling: false
            property string processedServerId: ""
            property int selections: 0
            function setDefaultServerAtIndex(row) { selections++; defaultServerId = row === 0 ? "test-a" : "test-b" }
            function setProcessedServerId(serverId) { processedServerId = serverId }
            function isServerFromApi(serverId) { return isDefaultServerFromApi }
            function isServerCountrySelectionAvailable(serverId) { return true }
        ''')
        self.connection = self.create_mock("ConnectionController", '''
            property bool isConnected: false
            property bool isConnectionInProgress: false
            property int connectClicks: 0
            signal preparingConfig()
            function connectButtonClicked() { connectClicks++ }
        ''')
        self.pages = self.create_mock("PageController", '''
            property int lastPage: -1
            property bool busy: false
            property string notification: ""
            signal restorePageHomeState(bool isContainerInstalled)
            function goToPage(page) { lastPage = page }
            function showBusyIndicator(value) { busy = value }
            function showNotificationMessage(message) { notification = message }
        ''')
        self.subscription = self.create_mock("SubscriptionUiController", '''
            property string protocol: "awg"
            property bool updateSucceeds: true
            function availableProtocols(serverId) { return ["awg", "vless"] }
            function currentProtocol(serverId) { return protocol }
            function setCurrentProtocol(serverId, value) { protocol = value }
            function updateServiceFromGateway(serverId, country, name, force) { return updateSucceeds }
            function getAccountInfo(serverId, force) { return true }
        ''')
        self.create_mock("HttpSubscriptions", '''
            property bool busy: false
            property string lastError: ""
            property int sourceCount: 0
            function refreshAll() {}
            function importSubscription(url, name) { return true }
        ''')
        self.create_mock("ImportController", "function extractConfigFromData(data) { return true }")
        self.create_mock("ConnectionImport", "function prepareConnectionImport(data, name) { return true }")
        for name in ("IpSplitTunnelingController", "AppSplitTunnelingController"):
            self.create_mock(name, "property bool isSplitTunnelingEnabled: false")
        self.create_mock("FocusController", "function resetRootObject() {}\nfunction setFocusOnDefaultItem() {}")
        self.create_mock("ServersModel", '''
            ListElement { serverId: "test-a"; name: "Test A"; serverDescription: "DE"; isServerFromGatewayApi: true; isSubscriptionExpired: false; isSubscriptionExpiringSoon: false }
            ListElement { serverId: "test-b"; name: "Test B"; serverDescription: "NL"; isServerFromGatewayApi: true; isSubscriptionExpired: false; isSubscriptionExpiringSoon: false }
        ''', model=True)
        self.engine.load(QUrl.fromLocalFile(str(Path(__file__).with_name("integration.qml"))))
        self.assertTrue(self.engine.rootObjects())
        self.window = self.engine.rootObjects()[0]
        QTest.qWait(100)

    def tearDown(self):
        self.window.close()
        self.engine.deleteLater()
        QTest.qWait(20)
        self.assertEqual(self.messages, [], "QML runtime warnings")

    def click(self, name):
        item = find_item(self.window.contentItem(), name)
        self.assertIsNotNone(item, name)
        self.assertTrue(QMetaObject.invokeMethod(item, "clicked", Qt.DirectConnection))
        QTest.qWait(30)

    def test_connect_uses_existing_controller(self):
        self.click("hopConnectButton")
        self.assertEqual(self.connection.property("connectClicks"), 1)

    def test_routes_open_native_split_tunneling(self):
        self.click("hopRoutesButton")
        self.assertEqual(self.pages.property("lastPage"), self.enums["PageSettingsSplitTunneling"])

    def test_selection_rechecks_controller_lock(self):
        self.connection.setProperty("isConnected", True)
        self.click("hopSelectServer_1")
        self.assertEqual(self.servers.property("selections"), 0)

    def test_api_protocol_failure_rolls_back(self):
        self.subscription.setProperty("updateSucceeds", False)
        self.click("hopProtocolButton")
        self.click("hopApiProtocol_vless")
        self.assertEqual(self.subscription.property("protocol"), "awg")
        self.assertFalse(self.pages.property("busy"))

    def test_api_protocol_success_is_retained(self):
        self.click("hopProtocolButton")
        self.click("hopApiProtocol_vless")
        self.assertEqual(self.subscription.property("protocol"), "vless")
        self.assertFalse(self.pages.property("busy"))


if __name__ == "__main__":
    unittest.main()
