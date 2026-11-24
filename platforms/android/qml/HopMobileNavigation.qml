import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Style 1.0

ToolButton {
    id: root
    objectName: "hopMobileMenu"
    property int selectedIndex: 0
    property var labels: []
    signal activated(int index)

    Material.theme: HopStyle.dark ? Material.Dark : Material.Light
    Material.accent: HopStyle.accent
    implicitWidth: HopStyle.controlHeight
    implicitHeight: HopStyle.controlHeight
    text: qsTr("Меню навигации")
    Accessible.name: text
    Accessible.description: root.labels[root.selectedIndex] || ""
    onClicked: navigation.popup(root, 0, root.height)

    contentItem: Item {
        Column {
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: 3
                Rectangle { width: 18; height: 2; radius: 1; color: HopStyle.text }
            }
        }
    }

    HopMenu {
        id: navigation
        objectName: "hopMobileDestinations"
        width: Math.min(264, root.Window.width - 32)
        Material.accent: HopStyle.accent
        Instantiator {
            model: root.labels
            delegate: HopMenuItem {
                required property int index
                required property string modelData
                objectName: "hopMobileTab_" + index
                text: modelData
                highlighted: index === root.selectedIndex
                icon.source: "qrc:/images/hop/material/" + ["vpn_key", "route", "public", "settings"][index] + ".svg"
                icon.width: 24
                icon.height: 24
                icon.color: highlighted ? HopStyle.accent : HopStyle.muted
                onTriggered: root.activated(index)
            }
            onObjectAdded: (index, item) => navigation.insertItem(index, item)
            onObjectRemoved: (index, item) => navigation.removeItem(item)
        }
    }
}
