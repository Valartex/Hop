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
        implicitWidth: 14
        implicitHeight: 14
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 2
        color: HopStyle.section
        border.color: control.activeFocus ? HopStyle.accent : HopStyle.border
        border.width: control.visualFocus ? 2 : 1
        opacity: control.enabled ? 1 : 0.55
        Canvas {
            anchors.fill: parent
            visible: control.checkState === Qt.Checked
            onVisibleChanged: if (visible) requestPaint()
            onPaint: {
                const context = getContext("2d")
                context.clearRect(0, 0, width, height)
                context.strokeStyle = HopStyle.text
                context.lineWidth = 1.5
                context.lineCap = "round"
                context.lineJoin = "round"
                context.beginPath()
                context.moveTo(3, 7)
                context.lineTo(6, 10)
                context.lineTo(11, 4)
                context.stroke()
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 8; height: 2
            color: HopStyle.text
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
