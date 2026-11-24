import QtQuick
import "../Controls2"

PageType {
    id: root
    enableTimer: false
    property bool backupRequested: false
    function showBackupSection() {
        backupRequested = true
        if (settingsLoader.status === Loader.Ready)
            Qt.callLater(settingsLoader.item.showBackupSection)
    }
    Loader {
        id: settingsLoader
        anchors.fill: parent
        source: Qt.application.name === "Hop" ? "PageSettingsHop.qml" : "PageSettingsLegacy.qml"
        onLoaded: if (root.backupRequested) Qt.callLater(item.showBackupSection)
    }
}
