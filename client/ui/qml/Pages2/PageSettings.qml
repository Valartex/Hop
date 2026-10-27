import QtQuick
import "../Controls2"

PageType {
    enableTimer: false
    Loader {
        anchors.fill: parent
        source: (Qt.application.name === "Hop" && Qt.platform.os === "windows") ? "PageSettingsHop.qml" : "PageSettingsLegacy.qml"
    }
}
