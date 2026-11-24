import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Style 1.0
Menu {
    Material.theme: HopStyle.dark ? Material.Dark : Material.Light
    implicitWidth: Math.min(320, ApplicationWindow.window ? ApplicationWindow.window.width - 32 : 320)
    Material.roundedScale: Material.MediumScale
}
