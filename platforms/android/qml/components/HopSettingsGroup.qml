import QtQuick
import QtQuick.Controls
import Style 1.0
GroupBox {
    id: control
    padding: 12
    topPadding: label.implicitHeight + 22
    label: Label {
        x: control.leftPadding; y: 10
        text: control.title
        font.family: "Roboto"; font.pixelSize: 14; font.weight: Font.Medium
        color: HopStyle.accent
    }
    background: Rectangle { color: HopStyle.surface; radius: 16 }
}
