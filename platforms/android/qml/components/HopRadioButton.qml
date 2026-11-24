import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Style 1.0
RadioButton {
    Material.theme: HopStyle.dark ? Material.Dark : Material.Light
    property bool isFocusable: true
    property color accentColor: HopStyle.accent
    property color secondaryColor: HopStyle.muted
    implicitWidth: 48
    implicitHeight: 48
    Layout.minimumWidth: 48
    Layout.minimumHeight: 48
    Material.accent: accentColor
}
