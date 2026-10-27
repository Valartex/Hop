import QtQuick
import Style 1.0
import QtQuick.Controls
import QtQuick.Layouts

HopDialog {
    id: root
    property var catalog
    property bool selectionLocked: false
    title: qsTr("Выбор и переключение локаций")
    modal: true
    standardButtons: Dialog.Close
    width: Math.min(560, parent ? parent.width - 32 : 560)
    height: Math.min(520, parent ? parent.height - 32 : 520)

    HopLocationPolicy { anchors.fill: parent; catalog: root.catalog; selectionLocked: root.selectionLocked }
}
