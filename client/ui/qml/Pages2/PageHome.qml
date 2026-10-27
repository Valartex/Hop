import QtQuick
import "../Controls2"
import "../Config"

PageType {
    id: root
    enableTimer: false
    isTabBarHidden: homeLoader.item ? homeLoader.item.isTabBarHidden : false

    Loader {
        id: homeLoader
        anchors.fill: parent
        source: GC.isDesktop() ? "PageHomeHop.qml" : "PageHomeLegacy.qml"
    }
}
