pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0

Rectangle {
    id: root
    objectName: "hopDashboard"

    property var serverModel
    property string selectedServerId: ""
    property string selectedServerName: ""
    property string selectedProtocol: ""
    property bool connected: false
    property bool connecting: false
    property bool disconnecting: false
    property bool cancelling: false
    property bool connectAvailable: true
    property bool selectionLocked: connected || connecting || disconnecting
    property string connectionStatus: cancelling ? qsTr("Отмена подключения…")
                                               : disconnecting ? qsTr("Отключение…") : connected ? qsTr("Подключено")
                                               : connecting ? qsTr("Подключение…") : qsTr("Отключено")
    property bool splitTunnelingEnabled: false
    property string warningText: ""
    property int subscriptionCount: 0
    property bool subscriptionsBusy: false
    property var healthStatuses: ({})
    property bool healthBusy: false
    property bool healthAvailable: false
    property bool protectionActive: false
    property bool protectionServiceAvailable: true
    property bool locationsAvailable: false
    property var locationMarks: ({})
    property var serviceNames: ({})
    property var serviceActions: ({})
    property var serviceStatuses: ({})
    property string activeLocationId: selectedServerId
    readonly property var currentHealth: healthStatuses[activeLocationId] || ({})
    property var locationActions: ({})
    property string selectionStatus: ""
    property var trafficRates: ({})

    property color surfaceColor: HopStyle.background
    property color rowColor: HopStyle.surface
    property color textColor: HopStyle.text
    property color secondaryTextColor: HopStyle.muted
    property color accentColor: HopStyle.accent
    property color lineColor: HopStyle.border
    property color successColor: "#76cda0"

    property string searchText: ""
    signal searchRequested(string query)
    signal connectRequested()
    signal addRequested()
    signal selectRequested(int row)
    signal detailsRequested(string serverId)
    signal locationHealthRequested(string locationId)
    signal locationStatsRequested(string locationId)
    signal serviceRefreshRequested(string serviceId)
    signal serviceRemoveRequested(string serviceId)
    signal refreshRequested()
    signal healthRequested()
    signal releaseProtectionRequested()
    signal poolLocationRequested(string locationId)

    color: surfaceColor

    component ActionButton: HopButton {}

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Подключения")
                color: root.textColor
                font.pixelSize: 23
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            ActionButton {
                objectName: "hopRefreshButton"
                visible: root.subscriptionCount > 0
                enabled: !root.subscriptionsBusy
                text: qsTr("Обновить")
                onClicked: root.refreshRequested()
            }
            ActionButton {
                objectName: "hopAddButton"
                text: qsTr("+ Добавить")
                onClicked: root.addRequested()
            }
        }

        Rectangle {
            objectName: "hopCurrentConnection"
            Layout.fillWidth: true
            implicitHeight: connectionRow.implicitHeight + 32
            color: root.rowColor
            radius: 5

            RowLayout {
                id: connectionRow
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Rectangle {
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: root.disconnecting ? root.accentColor : root.connected ? root.successColor : root.connecting ? root.accentColor : root.secondaryTextColor
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 7
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Label {
                        objectName: "hopConnectionStatus"
                        Layout.fillWidth: true
                        text: root.connectionStatus + (!root.disconnecting && root.connected && root.currentHealth.state === "reachable" && root.currentHealth.latencyMs >= 0
                              ? qsTr(" · Время ответа: %1 мс").arg(root.currentHealth.latencyMs) : !root.disconnecting && root.connected && root.currentHealth.state === "unreachable"
                              ? qsTr(" · Нет ответа от сервера") : "")
                        color: root.textColor
                        font.pixelSize: 17
                        font.weight: Font.Medium
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: "hopProtocolLabel"
                        Layout.fillWidth: true
                        text: root.selectedServerName.length ? root.selectedServerName + (root.selectedProtocol.length ? " · " + root.selectedProtocol : "") : qsTr("Добавьте сервис или сервер")
                        textFormat: Text.PlainText
                        color: root.secondaryTextColor
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    Label {
                        objectName: "hopRoutingMode"
                        text: root.splitTunnelingEnabled ? qsTr("Раздельное туннелирование: включено") : qsTr("Раздельное туннелирование: отключено")
                        color: root.secondaryTextColor
                        font.pixelSize: 13
                    }
                    Label {
                        objectName: "hopSelectionStatus"
                        Layout.fillWidth: true
                        visible: !root.disconnecting && root.selectionStatus.length > 0
                        text: root.selectionStatus
                        textFormat: Text.PlainText
                        color: !root.connected && !root.connecting ? HopStyle.danger : root.accentColor
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: "hopTrafficRates"
                        Layout.fillWidth: true
                        visible: root.connected && !root.disconnecting
                        function rate(key) { return root.trafficRates[key] >= 0 ? Number(root.trafficRates[key]).toLocaleString(Qt.locale(), 'f', 2) : "—" }
                        text: qsTr("VPN: ↓ %1 · ↑ %2 Мбит/с").arg(rate("vpnDown")).arg(rate("vpnUp"))
                              + "    " + qsTr("Напрямую: ↓ %1 · ↑ %2 Мбит/с").arg(rate("directDown")).arg(rate("directUp"))
                        color: root.secondaryTextColor
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
                ActionButton {
                    objectName: "hopConnectButton"
                    emphasized: !root.disconnecting
                    opacity: root.disconnecting || enabled ? 1 : 0.45
                    emphasisColor: root.connected ? "#e2a36a" : HopStyle.accent
                    text: root.cancelling ? qsTr("Отменяется…") : root.disconnecting ? qsTr("Отключается…")
                                         : root.connected ? qsTr("Отключить") : root.connecting ? qsTr("Отмена") : qsTr("Подключить")
                    enabled: !root.disconnecting && (root.connected || root.connecting || (root.selectedServerId.length > 0 && root.connectAvailable))
                    onClicked: root.connectRequested()
                }
            }
        }

        RowLayout {
            visible: root.protectionActive && !root.connected && !root.connecting && !root.disconnecting
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: root.protectionServiceAvailable
                      ? qsTr("VPN-трафик заблокирован до восстановления подключения. Прямые IP-маршруты доступны.")
                      : qsTr("Связь со службой потеряна. Последнее известное состояние: защита включена.")
                color: root.accentColor
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            ActionButton {
                objectName: "hopReleaseProtectionButton"
                text: qsTr("Снять защиту")
                onClicked: root.releaseProtectionRequested()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.warningText.length > 0
            text: root.warningText
            textFormat: Text.PlainText
            color: root.subscriptionsBusy ? root.secondaryTextColor : HopStyle.danger
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.locationsAvailable ? qsTr("Локации") : qsTr("Сервисы и серверы")
                color: root.textColor
                font.pixelSize: 17
                font.weight: Font.Medium
                Layout.fillWidth: true
            }
            ActionButton {
                objectName: "hopHealthButton"
                visible: root.healthAvailable
                enabled: !root.healthBusy
                text: root.healthBusy ? qsTr("Проверка…") : qsTr("Проверить")
                onClicked: root.healthRequested()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Проверить доступность локаций и время ответа")
            }
        }

        HopTextField {
            objectName: "hopLocationSearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Поиск по локациям и сервисам")
            maximumLength: 256
            selectByMouse: true
            text: root.searchText
            onTextEdited: root.searchRequested(text)
            color: HopStyle.text
            placeholderTextColor: HopStyle.muted
            background: Rectangle { color: HopStyle.field; radius: HopStyle.radius; border.color: parent.activeFocus ? HopStyle.accent : HopStyle.border }
        }

        ListView {
            id: servers
            objectName: "hopServersList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.serverModel
            clip: true
            reuseItems: true
            spacing: 8
            ScrollBar.vertical: HopScrollBar { id: locationsScroll }
            section.property: root.locationsAvailable ? "serviceId" : ""
            section.criteria: ViewSection.FullString
            section.delegate: Item {
                id: serviceSection
                required property string section
                readonly property var actions: root.serviceActions[section] || ({})
                width: servers.width - (locationsScroll.visible ? locationsScroll.width : 0)
                height: 56 + servers.spacing
                Rectangle {
                    objectName: "hopServiceHeader_" + serviceSection.section
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    height: 56
                    color: HopStyle.section
                    radius: HopStyle.radius
                    Rectangle { width: 3; height: parent.height - 16; anchors.verticalCenter: parent.verticalCenter; color: root.accentColor; radius: 1 }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12; anchors.rightMargin: 12
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Label {
                                Layout.fillWidth: true
                                text: root.serviceNames[serviceSection.section] || serviceSection.section
                                textFormat: Text.PlainText
                                color: root.textColor
                                elide: Text.ElideRight
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                            Label {
                                objectName: "hopServiceStatus_" + serviceSection.section
                                Layout.fillWidth: true
                                readonly property var status: root.serviceStatuses[serviceSection.section] || ({})
                                visible: text.length > 0
                                text: status.message || ""
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: status.state === "error" ? HopStyle.danger : HopStyle.muted
                                font.pixelSize: 12
                                ToolTip.visible: statusHover.hovered && truncated
                                ToolTip.text: text
                                HoverHandler { id: statusHover }
                            }
                        }
                        HopButton {
                            id: serviceActionsButton
                            objectName: "hopServiceActions_" + serviceSection.section
                            text: "…"; iconOnly: true
                            Accessible.name: qsTr("Действия с подпиской")
                            onClicked: serviceMenu.popup(serviceActionsButton, serviceActionsButton.width - serviceMenu.width, serviceActionsButton.height)
                            HopMenu {
                                id: serviceMenu
                                HopMenuItem {
                                    objectName: "hopServiceInfo_" + serviceSection.section
                                    text: qsTr("Сведения о подписке")
                                    onTriggered: root.detailsRequested(serviceSection.section)
                                }
                                HopMenuItem {
                                    objectName: "hopServiceRefresh_" + serviceSection.section
                                    text: qsTr("Обновить подписку")
                                    enabled: serviceSection.actions.canRefresh === true
                                    onTriggered: root.serviceRefreshRequested(serviceSection.section)
                                }
                                HopMenuItem {
                                    objectName: "hopServiceRemove_" + serviceSection.section
                                    text: qsTr("Удалить подписку…")
                                    enabled: serviceSection.actions.canRemove === true
                                    onTriggered: root.serviceRemoveRequested(serviceSection.section)
                                }
                            }
                        }
                    }
                }
            }

            delegate: Rectangle {
                id: serverRow
                objectName: "hopLocationRow_" + index
                required property int index
                required property string serverId
                required property string name
                required property string serverDescription
                required property bool isServerFromGatewayApi
                required property bool isSubscriptionExpired
                required property bool isSubscriptionExpiringSoon
                readonly property bool selected: serverId === root.selectedServerId
                readonly property var health: root.healthStatuses[serverId] || ({ state: "unknown", latencyMs: -1 })
                readonly property var marks: root.locationMarks[serverId] || ({})

                width: ListView.view.width - (locationsScroll.visible ? locationsScroll.width : 0)
                height: Math.max(64, serverContent.implicitHeight + 20)
                color: root.rowColor
                radius: 4
                border.width: selected ? 1 : 0
                border.color: root.accentColor

                RowLayout {
                    id: serverContent
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    RadioButton {
                        objectName: "hopSelectServer_" + serverRow.index
                        property bool isFocusable: true
                        checked: serverRow.selected
                        enabled: !root.selectionLocked && !serverRow.isSubscriptionExpired && serverRow.marks.available !== false
                        Accessible.name: qsTr("Выбрать %1").arg(serverRow.name)
                        onClicked: root.selectRequested(serverRow.index)
                        indicator: Rectangle {
                            implicitWidth: 18
                            implicitHeight: 18
                            x: 5
                            y: (parent.height - height) / 2
                            radius: 9
                            color: "transparent"
                            border.width: parent.activeFocus ? 2 : 1
                            border.color: serverRow.selected ? root.accentColor : root.secondaryTextColor
                            Rectangle {
                                anchors.centerIn: parent
                                width: 8
                                height: 8
                                radius: 4
                                color: root.accentColor
                                visible: serverRow.selected
                            }
                        }
                        implicitWidth: 30
                        implicitHeight: 40
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5
                        Label {
                            Layout.fillWidth: true
                            text: serverRow.marks.serviceName && serverRow.name.startsWith(serverRow.marks.serviceName + " · ")
                                  ? serverRow.name.substring(serverRow.marks.serviceName.length + 3) : serverRow.name
                            textFormat: Text.PlainText
                            color: root.textColor
                            font.pixelSize: 15
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }
                        Label {
                            visible: root.locationsAvailable && !serverRow.marks.excluded && !!serverRow.marks.automaticMember
                            text: serverRow.marks.allLocations ? qsTr("Автовыбор")
                                  : qsTr("Автопул · приоритет %1").arg(serverRow.marks.priority || 0)
                            color: root.accentColor
                            font.pixelSize: 11
                        }
                        Label {
                            Layout.fillWidth: true
                            text: serverRow.isSubscriptionExpired ? qsTr("Подписка истекла")
                                  : serverRow.marks.available === false ? qsTr("Локация недоступна в этой сборке")
                                  : serverRow.isSubscriptionExpiringSoon ? qsTr("Подписка скоро истекает")
                                  : serverRow.serverDescription
                            textFormat: Text.PlainText
                            color: serverRow.isSubscriptionExpired || serverRow.isSubscriptionExpiringSoon
                                   ? root.accentColor : root.secondaryTextColor
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }

                    ColumnLayout {
                        objectName: "hopHealthColumn_" + serverRow.index
                        visible: root.healthAvailable
                        Layout.minimumWidth: 112
                        Layout.preferredWidth: 112
                        Layout.maximumWidth: 112
                        spacing: 3
                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: 7
                            height: 7
                            radius: 4
                            color: serverRow.health.state === "reachable"
                                   ? serverRow.health.latencyMs < 500 ? root.successColor : root.accentColor
                                   : serverRow.health.state === "unreachable" ? "#e88484" : root.secondaryTextColor
                        }
                        Label {
                            objectName: "hopHealthStatus_" + serverRow.index
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: serverRow.health.state === "reachable" ? serverRow.health.latencyMs + qsTr(" мс")
                                  : serverRow.health.state === "unreachable" ? qsTr("Нет ответа")
                                  : serverRow.health.state === "configuration-limit" ? qsTr("Лимит подписки")
                                  : serverRow.health.state === "checking" ? qsTr("Проверка")
                                  : serverRow.health.state === "stale" ? qsTr("Устарело")
                                  : serverRow.health.state === "deferred" ? qsTr("После VPN")
                                  : serverRow.health.state === "unsupported" ? "—" : qsTr("Не проверено")
                            color: serverRow.health.state === "configuration-limit" ? HopStyle.danger : root.secondaryTextColor
                            font.pixelSize: 11
                            ToolTip.visible: healthHover.hovered && ["configuration-limit", "deferred", "unsupported"].indexOf(serverRow.health.state) >= 0
                            ToolTip.text: serverRow.health.state === "configuration-limit"
                                          ? qsTr("Провайдер отказал из-за лимита конфигураций. Задержка сервера не измерялась.")
                                          : serverRow.health.state === "deferred"
                                          ? qsTr("Проверка AWG доступна при отключённом VPN, чтобы не прервать текущее соединение.")
                                          : serverRow.health.state === "unsupported" ? qsTr("Для этого профиля нет безопасной проверки доступности. Подключение можно проверить вручную.") : ""
                            HoverHandler { id: healthHover }
                        }
                    }

                    ActionButton {
                        objectName: "hopDetailsButton_" + serverRow.index
                        id: locationActions
                        text: "…"
                        iconOnly: true
                        Accessible.name: qsTr("Действия для %1").arg(serverRow.name)
                        onClicked: locationMenu.popup(locationActions, locationActions.width - locationMenu.width, locationActions.height)
                        HopMenu {
                            id: locationMenu
                            objectName: "hopLocationMenu_" + serverRow.index
                            HopMenuItem {
                                objectName: "hopLocationCheck_" + serverRow.index
                                text: qsTr("Проверить время ответа")
                                enabled: root.locationActions[serverRow.serverId] ? root.locationActions[serverRow.serverId].canCheck === true
                                    : root.healthAvailable && serverRow.health.state !== "checking"
                                onTriggered: root.locationHealthRequested(serverRow.serverId)
                            }
                            HopMenuItem { objectName: "hopLocationStats_" + serverRow.index; text: qsTr("Статистика локации"); onTriggered: root.locationStatsRequested(serverRow.serverId) }
                            HopMenuItem {
                                visible: root.locationsAvailable
                                text: serverRow.marks.automaticMember ? qsTr("Убрать из автопереключения") : qsTr("Добавить в автопереключение")
                                onTriggered: root.poolLocationRequested(serverRow.serverId)
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: servers.count === 0
                text: root.searchText.length ? qsTr("Локации не найдены") : qsTr("Добавьте ссылку подписки\nили конфигурацию сервера")
                color: root.secondaryTextColor
                font.pixelSize: 15
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }
    }
}
