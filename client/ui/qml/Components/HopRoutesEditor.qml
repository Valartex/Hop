import QtQuick
import Style 1.0
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root
    property var policy
    property bool pendingInput: false
    readonly property var targetKeys: ["vpn", "direct", "block"]
    readonly property var targetNames: [qsTr("Через VPN"), qsTr("Напрямую"), qsTr("Блокировать")]
    readonly property var kindKeys: ["domain", "ip"]
    function targetName(key) { return targetNames[targetKeys.indexOf(key)] || key }
    padding: 0
    palette.windowText: HopStyle.text
    palette.text: HopStyle.text
    palette.base: HopStyle.surface
    palette.button: HopStyle.border
    palette.buttonText: "#f1f1f3"
    palette.highlight: HopStyle.accent
    palette.highlightedText: HopStyle.background
    palette.placeholderText: "#878b91"
    palette.dark: HopStyle.muted
    palette.mid: "#878b91"
    background: Rectangle { color: HopStyle.background }
    contentItem: ColumnLayout {
    spacing: 10
    Connections {
        target: root.policy
        function onChanged() {
            if (root.pendingInput && !root.policy.busy) {
                if (!root.policy.lastError) value.clear()
                root.pendingInput = false
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        HopSwitch {
            objectName: "hopRoutesEnabled"
            text: qsTr("Раздельное туннелирование")
            checked: root.policy ? root.policy.enabled : false
            enabled: root.policy && !root.policy.busy && !root.policy.orderPending
            onClicked: root.policy.setEnabled(checked)
            Layout.fillWidth: true
        }
        BusyIndicator { running: root.policy ? root.policy.busy : false; implicitWidth: 28; implicitHeight: 28 }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Первое совпавшее правило определяет маршрут. Порядок меняется стрелками и применяется одной кнопкой. Остальные изменения применяются сразу с переподключением VPN.")
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Остальной трафик") }
        HopComboBox {
            objectName: "hopRoutesDefault"
            Layout.fillWidth: true
            model: root.targetNames
            currentIndex: root.policy ? root.targetKeys.indexOf(root.policy.defaultTarget) : 0
            enabled: root.policy && !root.policy.busy && !root.policy.orderPending
            onActivated: root.policy.setDefaultTarget(root.targetKeys[currentIndex])
        }
    }
    Label {
        objectName: "hopRoutesError"
        Layout.fillWidth: true
        text: root.policy ? root.policy.lastError : ""
        visible: text.length > 0
        color: "#e7a276"
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }
    RowLayout {
        visible: root.policy ? root.policy.orderPending : false
        Layout.fillWidth: true
        Label {
            objectName: "hopRoutesOrderPending"
            Layout.fillWidth: true
            text: qsTr("Порядок изменён. Примените его, чтобы обновить маршруты VPN.")
            color: HopStyle.accent
            wrapMode: Text.WordWrap
        }
        HopButton {
            objectName: "hopRoutesOrderDiscard"
            text: qsTr("Отменить")
            enabled: root.policy && !root.policy.busy
            onClicked: root.policy.discardOrder()
        }
        HopButton {
            objectName: "hopRoutesOrderApply"
            text: qsTr("Применить")
            emphasized: true
            enabled: root.policy && !root.policy.busy
            onClicked: root.policy.applyOrder()
        }
    }
    ListView {
        objectName: "hopRoutesList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 110
        clip: true
        model: root.policy ? root.policy.rules : []
        ScrollBar.vertical: HopScrollBar { id: routesScroll }
        delegate: RowLayout {
            required property var modelData
            required property int index
            width: ListView.view.width - (routesScroll.visible ? routesScroll.width : 0)
            height: 58
            spacing: 4
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label { Layout.fillWidth: true; text: modelData.value; textFormat: Text.PlainText; elide: Text.ElideMiddle }
                Label {
                    Layout.fillWidth: true
                    text: (modelData.kind === "geoip" ? "GeoIP" : modelData.kind === "geosite" ? "GeoSite" : modelData.kind === "ip" ? "IP" : qsTr("Домен")) + " · " + root.targetName(modelData.target)
                    opacity: 0.7; elide: Text.ElideRight
                }
            }
            HopButton { objectName: "hopRoutesUp_" + index; text: "↑"; implicitWidth: 36; enabled: index > 0 && !root.policy.busy; onClicked: root.policy.moveRule(index, -1); Accessible.name: qsTr("Выше") }
            HopButton { objectName: "hopRoutesDown_" + index; text: "↓"; implicitWidth: 36; enabled: index + 1 < root.policy.rules.length && !root.policy.busy; onClicked: root.policy.moveRule(index, 1); Accessible.name: qsTr("Ниже") }
            HopButton { text: "×"; implicitWidth: 36; enabled: !root.policy.busy && !root.policy.orderPending; onClicked: root.policy.removeRule(index); Accessible.name: qsTr("Удалить правило") }
        }
        Label { anchors.centerIn: parent; visible: parent.count === 0; text: qsTr("Правил пока нет"); opacity: 0.65 }
    }
    RowLayout {
        Layout.fillWidth: true
        HopComboBox { id: kind; objectName: "hopRoutesKind"; model: [qsTr("Домен"), "IP / CIDR"]; Layout.preferredWidth: 125 }
        HopTextField {
            id: value
            objectName: "hopRoutesValue"
            Layout.fillWidth: true
            placeholderText: kind.currentIndex === 0 ? "example.com" : "192.0.2.0/24"
            maximumLength: 4096
            selectByMouse: true
        }
    }
    RowLayout {
        Layout.fillWidth: true
        HopComboBox { id: target; objectName: "hopRoutesTarget"; Layout.fillWidth: true; model: root.targetNames }
        HopButton {
            objectName: "hopRoutesAdd"
            text: qsTr("Добавить")
            enabled: root.policy && !root.policy.busy && !root.policy.orderPending && value.text.trim().length > 0
            onClicked: root.pendingInput = root.policy.addRule(root.kindKeys[kind.currentIndex], value.text, root.targetKeys[target.currentIndex])
        }
    }
    HopButton {
        objectName: "hopRoutesDat"
        Layout.fillWidth: true
        text: qsTr("Категории GeoIP / GeoSite")
        enabled: root.policy && !root.policy.busy && !root.policy.orderPending
        onClicked: geoData.open()
    }
    HopGeoDataDialog { id: geoData; routingPolicy: root.policy; parent: Overlay.overlay }
    }
}
