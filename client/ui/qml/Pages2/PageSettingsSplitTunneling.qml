import QtQuick
import "../Controls2"

PageType {
    Loader {
        anchors.fill: parent
        source: typeof RoutingPolicy !== "undefined" ? "PageSettingsHopRoutes.qml" : "PageSettingsSplitTunnelingLegacy.qml"
    }
}
