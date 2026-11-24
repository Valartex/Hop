import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Style 1.0
TextField {
    Material.theme: HopStyle.dark ? Material.Dark : Material.Light
    implicitHeight: HopStyle.controlHeight
    Layout.minimumHeight: HopStyle.controlHeight
    font.family: "Roboto"
    font.pixelSize: 16
    Material.accent: HopStyle.accent
    Material.containerStyle: Material.Outlined
    selectByMouse: true
}
