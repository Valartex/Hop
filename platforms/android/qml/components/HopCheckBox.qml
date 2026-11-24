import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Style 1.0
CheckBox {
    Material.theme: HopStyle.dark ? Material.Dark : Material.Light
    id: control
    implicitHeight: Math.max(48, contentItem.implicitHeight + topPadding + bottomPadding)
    Layout.minimumHeight: 48
    Material.accent: HopStyle.accent
    font.family: "Roboto"
    font.pixelSize: 16
    contentItem: Label {
        text: control.text; font: control.font
        color: control.enabled ? HopStyle.text : HopStyle.muted
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
        wrapMode: Text.WordWrap
    }
}
