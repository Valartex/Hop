import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Style 1.0

ColumnLayout {
    id: root
    property string restorePath: ""
    spacing: 16

    Label {
        Layout.fillWidth: true
        text: qsTr("Копия содержит настройки, пароли и закрытые ключи подключений. Храните её в безопасном месте.")
        color: HopStyle.muted
        wrapMode: Text.WordWrap
    }
    GridLayout {
        Layout.fillWidth: HopStyle.mobile
        Layout.alignment: Qt.AlignLeft
        columns: root.width < 360 ? 1 : 2
        columnSpacing: 8
        rowSpacing: 8
        HopButton {
            objectName: "hopSaveBackup"
            Layout.fillWidth: HopStyle.mobile
            text: qsTr("Сохранить копию…")
            onClicked: Qt.callLater(function() {
                if (Qt.platform.os === "android") {
                    SettingsController.backupAppConfig("Hop.backup")
                    return
                }
                const file = SystemController.getFileName(qsTr("Сохранить копию"), "Backup files (*.backup)", StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/Hop", true, ".backup")
                if (file) SettingsController.backupAppConfig(file)
            })
        }
        HopButton {
            objectName: "hopRestoreBackup"
            Layout.fillWidth: HopStyle.mobile
            text: qsTr("Восстановить…")
            enabled: !ConnectionController.isConnected && !ConnectionController.isConnectionInProgress
            onClicked: Qt.callLater(function() {
                root.restorePath = SystemController.getFileName(qsTr("Открыть резервную копию"), "Backup files (*.backup)")
                if (root.restorePath) restoreConfirm.open()
            })
        }
    }
    HopDialog {
        id: restoreConfirm
        title: qsTr("Восстановить настройки?")
        anchors.centerIn: parent
        width: Math.min(420, root.width - 32)
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label {
            width: parent.width
            color: HopStyle.text
            text: qsTr("Текущие настройки Hop будут заменены данными резервной копии.")
            wrapMode: Text.WordWrap
        }
        onAccepted: Qt.callLater(function() { SettingsController.restoreAppConfig(root.restorePath) })
    }
}
