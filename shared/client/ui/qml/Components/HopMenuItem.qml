import QtQuick
import QtQuick.Controls
import Style 1.0
MenuItem {
    id: entry
    implicitHeight: Qt.platform.os === "android" ? Math.max(HopStyle.controlHeight, contentItem.implicitHeight + 16) : HopStyle.controlHeight
    font.family: Qt.platform.os === "windows" ? "Segoe UI" : "PT Root UI VF"
    font.pixelSize: 13
    contentItem: Label {
        text: entry.text
        textFormat: Text.PlainText
        font: entry.font
        color: entry.enabled ? HopStyle.text : HopStyle.muted
        verticalAlignment: Text.AlignVCenter
        leftPadding: 10
        rightPadding: 10
        wrapMode: Qt.platform.os === "android" ? Text.WordWrap : Text.NoWrap
    }
    background: Rectangle { radius: 3; color: entry.highlighted ? HopStyle.hover : "transparent" }
}
