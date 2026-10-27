import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Style 1.0
import "../Controls2"
import "../Components"

PageType {
    id: root
    enableTimer: false
    property string restorePath: ""
    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 20
        spacing: 16
        Label { text: qsTr("Резервная копия"); font.pixelSize: 22; color: HopStyle.text }
        Label {
            Layout.fillWidth: true
            text: qsTr("Копия содержит настройки, пароли и закрытые ключи подключений. Храните её в безопасном месте.")
            color: HopStyle.muted
            wrapMode: Text.WordWrap
        }
        RowLayout {
            spacing: 8
            HopButton {
                text: qsTr("Сохранить копию…")
                onClicked: {
                    const file = SystemController.getFileName(qsTr("Сохранить копию"), "Backup files (*.backup)", StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/Hop", true, ".backup")
                    if (file) SettingsController.backupAppConfig(file)
                }
            }
            HopButton {
                text: qsTr("Восстановить…")
                enabled: !ConnectionController.isConnected && !ConnectionController.isConnectionInProgress
                onClicked: {
                    root.restorePath = SystemController.getFileName(qsTr("Открыть резервную копию"), "Backup files (*.backup)")
                    if (root.restorePath) restoreConfirm.open()
                }
            }
        }
    }
    HopDialog {
        id: restoreConfirm
        title: qsTr("Восстановить настройки?")
        anchors.centerIn: parent
        width: Math.min(420, root.width - 32)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { width: parent.width; text: qsTr("Текущие настройки Hop будут заменены данными резервной копии."); wrapMode: Text.WordWrap }
        onAccepted: SettingsController.restoreAppConfig(root.restorePath)
    }
}
