import QtQuick
import QtQuick.Window
import "MaterialSymbols.js" as Symbols
Item {
    id: root
    property string name: ""
    property color color: "black"
    implicitWidth: 24
    implicitHeight: 24
    Image {
        anchors.fill: parent
        sourceSize.width: Math.ceil(root.width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(root.height * Screen.devicePixelRatio)
        source: Symbols.icons[root.name] ? "data:image/svg+xml;charset=utf-8," + encodeURIComponent(
            Symbols.icons[root.name].replace("<svg ", "<svg fill=\"" + root.color + "\" ")) : ""
    }
}
