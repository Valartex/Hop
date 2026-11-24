import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0
import "../Controls2"
import "../Components"

PageType {
    id: root
    enableTimer: false
    Connections {
        target: ImportController
        function onImportFinished() { PageController.showBusyIndicator(false); PageController.goToPageHome() }
        function onImportErrorOccurred(error, goToPageHome) { PageController.showBusyIndicator(false) }
    }
    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 20
        spacing: 16
        Label { text: qsTr("Добавление конфигурации"); font.pixelSize: 22; color: HopStyle.text }
        Label { text: ImportController.configFileName || qsTr("Ссылка подключения"); textFormat: Text.PlainText; color: HopStyle.muted }
        Label {
            Layout.fillWidth: true
            visible: ImportController.maliciousWarningText.length > 0
            text: ImportController.maliciousWarningText
            textFormat: Text.PlainText
            color: HopStyle.danger
            wrapMode: Text.WordWrap
        }
        HopCheckBox { id: convertWireGuard; visible: ImportController.isNativeWireGuardConfig; text: qsTr("Преобразовать WireGuard в AmneziaWG"); checked: false }
        RowLayout {
            spacing: 8
            HopButton {
                text: qsTr("Добавить")
                emphasized: true
                onClicked: {
                    if (convertWireGuard.visible && convertWireGuard.checked) ImportController.processNativeWireGuardConfig()
                    PageController.showBusyIndicator(true)
                    ImportController.importConfig()
                }
            }
            HopButton { text: qsTr("Отмена"); onClicked: PageController.goToPageHome() }
        }
    }
}
