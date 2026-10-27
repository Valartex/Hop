import QtQuick
import QtQuick.Controls
import Style 1.0
MenuItem {
    id: entry
    implicitHeight: HopStyle.controlHeight
    font.family: "Segoe UI"
    font.pixelSize: 13
    contentItem: Label {
        text: entry.text
        textFormat: Text.PlainText
        font: entry.font
        color: entry.enabled ? HopStyle.text : HopStyle.muted
        verticalAlignment: Text.AlignVCenter
        leftPadding: 10
    }
    background: Rectangle { radius: 3; color: entry.highlighted ? HopStyle.hover : "transparent" }
}
