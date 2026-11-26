import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0

CheckBox {
    id: control
    implicitHeight: Math.max(HopStyle.controlHeight, contentItem.implicitHeight + topPadding + bottomPadding)
    Layout.minimumHeight: HopStyle.controlHeight
    Layout.maximumHeight: Qt.platform.os === "android" ? Infinity : HopStyle.controlHeight
    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: control.text.length ? control.leftPadding : (control.width - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 3
        color: control.checkState !== Qt.Unchecked ? HopStyle.accent : HopStyle.section
        border.color: control.visualFocus ? HopStyle.text : control.checkState !== Qt.Unchecked || control.activeFocus ? HopStyle.accent : HopStyle.border
        border.width: control.visualFocus ? 2 : 1
        opacity: control.enabled ? 1 : 0.55
        Label {
            anchors.centerIn: parent
            text: "✓"
            visible: control.checkState === Qt.Checked
            color: HopStyle.background
            font.family: Qt.platform.os === "windows" ? "Segoe UI" : control.font.family
            font.pixelSize: 13
            font.bold: true
        }
        Rectangle {
            anchors.centerIn: parent
            width: 8; height: 2
            color: HopStyle.background
            visible: control.checkState === Qt.PartiallyChecked
        }
    }
    contentItem: Label {
        text: control.text
        font: control.font
        color: control.enabled ? HopStyle.text : HopStyle.muted
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator && control.indicator.visible ? control.indicator.width + control.spacing : 0
        wrapMode: Qt.platform.os === "android" ? Text.WordWrap : Text.NoWrap
    }
}
