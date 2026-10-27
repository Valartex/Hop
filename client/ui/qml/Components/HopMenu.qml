import QtQuick
import QtQuick.Controls
import Style 1.0

Menu {
    id: control
    popupType: Popup.Item
    implicitWidth: 260
    padding: 4
    delegate: MenuItem {
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
    background: Rectangle { color: HopStyle.surface; border.color: HopStyle.border; radius: HopStyle.radius }
}
