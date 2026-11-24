import QtQuick
import Style 1.0
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Pane {
    id: root
    property var policy
    property bool pendingInput: false
    readonly property var targetKeys: ["vpn", "direct", "block"]
    readonly property var targetNames: [qsTr("Через VPN"), qsTr("Напрямую"), qsTr("Блокировать")]
    readonly property var kindKeys: ["domain", "ip", "geoip-source", "geosite-source"]
    readonly property bool sourceInput: kind.currentIndex >= 2
    function sourceKind() {
        return kind.currentIndex === 2 ? "geoip" : "geosite";
    }
    function targetName(key) {
        return targetNames[targetKeys.indexOf(key)] || key;
    }
    function kindName(key) {
        return key === "geoip-source" ? "GeoIP" : key === "geosite-source" ? "GeoSite" : key === "ip" ? "IP" : qsTr("Домен");
    }
    function updateLabel(rule) {
        if (!rule.sourceRule)
            return "";
        if (rule.updating)
            return qsTr("Обновление...");
        if (rule.sourceError)
            return qsTr("Ошибка обновления");
        return rule.updated ? Qt.formatDateTime(new Date(rule.updated), Qt.DefaultLocaleShortDate) : qsTr("Не обновлялось");
    }
    function discardOrder() {
        const selected = root.policy.rules[routesTable.currentIndex];
        const selectedKey = selected ? JSON.stringify([selected.kind, selected.value, selected.target]) : "";
        root.policy.discardOrder();
        routesTable.currentIndex = selectedKey ? root.policy.rules.findIndex(rule => JSON.stringify([rule.kind, rule.value, rule.target]) === selectedKey) : -1;
    }
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
    background: Rectangle {
        color: HopStyle.background
    }
    contentItem: ColumnLayout {
        spacing: 10
        Connections {
            target: root.policy
            function onChanged() {
                if (root.pendingInput && !root.policy.busy) {
                    if (!root.policy.lastError)
                        value.clear();
                    root.pendingInput = false;
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
            BusyIndicator {
                running: root.policy ? root.policy.busy : false
                implicitWidth: 28
                implicitHeight: 28
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("Трафик вне маршрутов")
                color: HopStyle.text
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
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
        GridLayout {
            columns: 2
            visible: root.policy ? root.policy.orderPending : false
            Layout.fillWidth: true
            Label {
                objectName: "hopRoutesOrderPending"
                Layout.columnSpan: 2
                Layout.fillWidth: true
                text: qsTr("Порядок изменён. Примените его, чтобы обновить маршруты VPN.")
                color: HopStyle.accent
                wrapMode: Text.WordWrap
            }
            HopButton {
                objectName: "hopRoutesOrderDiscard"
                Layout.fillWidth: true
                text: qsTr("Отменить")
                enabled: root.policy && !root.policy.busy
                onClicked: root.discardOrder()
            }
            HopButton {
                objectName: "hopRoutesOrderApply"
                Layout.fillWidth: true
                text: qsTr("Применить")
                emphasized: true
                enabled: root.policy && !root.policy.busy
                onClicked: root.policy.applyOrder()
            }
        }
        HopOrderTable {
            id: routesTable
            objectName: "hopRoutesList"
            Layout.fillWidth: true
            Layout.fillHeight: !HopStyle.mobile
            rows: root.policy ? root.policy.rules.map(rule => ({
                        value: rule.value,
                        kindLabel: root.kindName(rule.kind),
                        targetLabel: root.targetName(rule.target),
                        sourceRule: rule.sourceRule || false,
                        autoUpdate: rule.autoUpdate || false,
                        updating: rule.updating || false,
                        updatedLabel: root.updateLabel(rule)
                    })) : []
            columns: [
                {
                    title: qsTr("Приоритет"),
                    role: "row",
                    width: 100
                },
                {
                    title: qsTr("Тип"),
                    role: "kindLabel",
                    width: 76
                },
                {
                    title: qsTr("Правило"),
                    role: "value",
                    stretch: 1,
                    elideMiddle: true
                },
                {
                    title: qsTr("Направление"),
                    role: "targetLabel",
                    width: 130
                },
                {
                    title: qsTr("Автообновление"),
                    role: "autoUpdate",
                    checkbox: true,
                    width: 140
                },
                {
                    title: qsTr("Последнее обновление"),
                    role: "updatedLabel",
                    width: 190
                }
            ]
            minimumColumnsWidth: 720
            hasRefreshAction: true
            editingEnabled: root.policy && !root.policy.busy
            movementEnabled: editingEnabled && root.policy.ready
            removalEnabled: root.policy && !root.policy.orderPending
            emptyText: qsTr("Правил пока нет")
            onMoveRequested: function (row, delta) {
                if (root.policy.moveRule(row, delta))
                    routesTable.currentIndex = row + delta;
            }
            onRemoveRequested: function (row) {
                root.policy.removeRule(row);
            }
            onRefreshRequested: function (row) {
                root.policy.refreshRule(row);
            }
            onCellToggled: function (row, role, checked) {
                root.policy.setRuleAutoUpdate(row, checked);
            }
        }
        Label {
            Layout.fillWidth: true
            readonly property var selectedRule: root.policy && routesTable.currentIndex >= 0 ? root.policy.rules[routesTable.currentIndex] : null
            text: selectedRule ? (selectedRule.sourceError || "") : ""
            visible: text.length > 0
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: "#e7a276"
        }
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            columns: root.width < 700 ? 1 : 4
            columnSpacing: 6
            ColumnLayout {
                Layout.preferredWidth: root.width < 700 ? -1 : 140
                Layout.fillWidth: root.width < 700
                spacing: 6
                Label {
                    text: qsTr("Тип")
                    color: HopStyle.text
                }
                HopComboBox {
                    id: kind
                    objectName: "hopRoutesKind"
                    model: [qsTr("Домен"), "IP / CIDR", "GeoIP", "GeoSite"]
                    Layout.fillWidth: true
                    onActivated: if (root.sourceInput)
                        value.text = root.policy.defaultSource(root.sourceKind())
                    else
                        value.clear()
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 6
                Label {
                    text: qsTr("Правило")
                    color: HopStyle.text
                }
                RowLayout {
                    Layout.fillWidth: true
                    HopTextField {
                        id: value
                        objectName: "hopRoutesValue"
                        Layout.fillWidth: true
                        placeholderText: root.sourceInput ? qsTr("HTTPS-ссылка или путь к файлу DAT") : kind.currentIndex === 0 ? "example.com" : "192.0.2.0/24"
                        maximumLength: 4096
                        selectByMouse: true
                    }
                    HopButton {
                        objectName: "hopRoutesChooseFile"
                        visible: root.sourceInput
                        text: "…"
                        iconOnly: true
                        Accessible.name: qsTr("Выбрать файл")
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        onClicked: sourceFile.open()
                    }
                }
            }
            ColumnLayout {
                Layout.preferredWidth: root.width < 700 ? -1 : 170
                Layout.fillWidth: root.width < 700
                spacing: 6
                Label {
                    text: qsTr("Направление")
                    color: HopStyle.text
                }
                HopComboBox {
                    id: target
                    objectName: "hopRoutesTarget"
                    Layout.fillWidth: true
                    model: root.targetNames
                }
            }
            HopButton {
                objectName: "hopRoutesAdd"
                Layout.alignment: Qt.AlignBottom
                Layout.fillWidth: root.width < 700
                text: qsTr("Добавить")
                enabled: root.policy && !root.policy.busy && !root.policy.orderPending && value.text.trim().length > 0
                onClicked: {
                    const accepted = root.policy.addRule(root.kindKeys[kind.currentIndex], value.text, root.targetKeys[target.currentIndex]);
                    if (root.sourceInput) {
                        if (accepted)
                            value.clear();
                    } else
                        root.pendingInput = accepted;
                }
            }
        }
        FileDialog {
            id: sourceFile
            title: qsTr("Выбрать файл DAT")
            nameFilters: ["DAT (*.dat)", qsTr("Все файлы (*)")]
            onAccepted: value.text = selectedFile.toString()
        }
    }
}
