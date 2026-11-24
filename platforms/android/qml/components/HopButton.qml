import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Style 1.0
Button {
    Material.theme: HopStyle.dark ? Material.Dark : Material.Light
    id: control
    property bool emphasized: false
    property color emphasisColor: HopStyle.accent
    property bool iconOnly: false
    property string iconName: ""
    property bool isFocusable: true
    property alias displayText: control.text
    readonly property string symbol: iconName.length ? iconName : text === "↑" ? "arrow_upward" : text === "↓" ? "arrow_downward" : text === "×" ? "close" : text === "…" ? "more_vert" : ""
    implicitWidth: iconOnly || symbol.length ? 48 : 160
    implicitHeight: Math.max(HopStyle.controlHeight, implicitContentHeight + topPadding + bottomPadding)
    Layout.minimumHeight: implicitHeight
    flat: iconOnly || symbol.length > 0
    highlighted: false
    font.family: "Roboto"
    font.pixelSize: 14
    font.weight: Font.Medium
    icon.source: symbol.length ? "qrc:/images/hop/material/" + symbol + ".svg" : ""
    icon.width: 24; icon.height: 24
    icon.color: emphasized ? HopStyle.accentText : HopStyle.accent
    display: symbol.length || iconOnly ? AbstractButton.IconOnly : AbstractButton.TextOnly
    Material.accent: emphasisColor
    Material.foreground: emphasized ? HopStyle.accentText : HopStyle.accent
    Material.roundedScale: Material.FullScale
    Accessible.name: displayText
    readonly property color foregroundColor: !enabled ? Qt.rgba(HopStyle.text.r, HopStyle.text.g, HopStyle.text.b, 0.38)
        : emphasized ? HopStyle.accentText : HopStyle.accent
    topPadding: 12; bottomPadding: 12
    leftPadding: flat ? 12 : 16; rightPadding: flat ? 12 : 16
    topInset: 4
    bottomInset: 4
    contentItem: Item {
        property color color: control.foregroundColor
        implicitWidth: control.symbol.length ? 24 : caption.implicitWidth
        implicitHeight: control.symbol.length ? 24 : Math.max(24, caption.contentHeight)
        Label {
            id: caption
            anchors.fill: parent
            visible: !control.symbol.length
            text: control.displayText
            textFormat: Text.PlainText
            font: control.font
            color: parent.color
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }
        MaterialIcon {
            anchors.centerIn: parent
            width: 24; height: 24
            visible: control.symbol.length > 0
            name: control.symbol
            color: parent.color
        }
    }
    background: Rectangle {
        implicitWidth: 64
        implicitHeight: 40
        radius: height / 2
        color: !control.enabled ? Qt.rgba(HopStyle.text.r, HopStyle.text.g, HopStyle.text.b, 0.12)
             : control.emphasized ? control.emphasisColor : control.flat ? "transparent" : HopStyle.surface
        border.width: !control.emphasized && !control.flat ? 1 : 0
        border.color: HopStyle.border
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: control.emphasized ? HopStyle.accentText : HopStyle.accent
            opacity: !control.enabled ? 0 : control.down ? 0.12 : control.hovered || control.visualFocus ? 0.08 : 0
        }
    }
}
