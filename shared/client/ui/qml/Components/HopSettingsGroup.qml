import QtQuick
import QtQuick.Controls
import Style 1.0
GroupBox {
    id: control
    padding: 16
    topPadding: label.implicitHeight + 28
    label: Label {
        x: control.leftPadding
        y: 14
        text: control.title
        font.pixelSize: 15
        font.weight: Font.DemiBold
        color: HopStyle.text
    }
    background: Rectangle { color: HopStyle.surface; border.color: HopStyle.border; radius: HopStyle.radius }
}
