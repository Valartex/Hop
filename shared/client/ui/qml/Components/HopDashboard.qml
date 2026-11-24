pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import Hop.Models 1.0
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
    property string connectionStatus: cancelling ? qsTr("Отмена подключения…") : disconnecting ? qsTr("Отключение…") : connected ? qsTr("Подключено") : connecting ? qsTr("Подключение…") : qsTr("Отключено")
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
    property var locationCatalog: null
    property var expandedServices: locationCatalog ? locationCatalog.serviceExpansion : ({})
    readonly property bool compact: width < 640
    readonly property bool onboarding: HopStyle.mobile && !selectedServerId.length && subscriptionCount === 0 && servers.count === 0

    property color surfaceColor: HopStyle.background
    property color rowColor: HopStyle.surface
    property color textColor: HopStyle.text
    property color secondaryTextColor: HopStyle.muted
    property color accentColor: HopStyle.accent
    property color lineColor: HopStyle.border
    property color successColor: HopStyle.success

    property string searchText: ""
    signal searchRequested(string query)
    signal connectRequested
    signal addRequested
    signal backupRequested
    signal selectRequested(int row)
    signal detailsRequested(string serverId)
    signal locationHealthRequested(string locationId)
    signal locationStatsRequested(string locationId)
    signal serviceRefreshRequested(string serviceId)
    signal serviceRemoveRequested(string serviceId)
    signal refreshRequested
    signal healthRequested
    signal releaseProtectionRequested
    signal poolLocationRequested(string locationId)
    signal restrictedLocationRequested(string locationId, bool enabled)

    function serviceExpanded(id) {
        return !id || searchText.trim().length > 0 || (expandedServices[id] === undefined ? !HopStyle.mobile : expandedServices[id]);
    }

    function toggleService(id) {
        if (locationCatalog)
            locationCatalog.setServiceExpanded(id, !serviceExpanded(id));
        else
            expandedServices = Object.assign({}, expandedServices, {
                [id]: !serviceExpanded(id)
            });
        servers.forceLayout();
    }

    color: surfaceColor

    component ActionButton: HopButton {}

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.topMargin: HopStyle.mobile ? 12 : 16
        anchors.bottomMargin: HopStyle.mobile ? 8 : 16
        spacing: HopStyle.mobile ? 10 : 14

        GridLayout {
            columns: HopStyle.mobile ? 4 : root.compact ? 2 : 3
            columnSpacing: HopStyle.mobile ? 8 : 5
            Layout.fillWidth: true
            Item {
                visible: HopStyle.mobile
                Layout.preferredWidth: HopStyle.controlHeight
            }
            Label {
                Layout.columnSpan: root.compact && !HopStyle.mobile ? 2 : 1
                text: qsTr("Подключения")
                color: root.textColor
                font.pixelSize: HopStyle.mobile ? 22 : 23
                font.weight: HopStyle.mobile ? Font.Medium : Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            ActionButton {
                objectName: "hopRefreshButton"
                Layout.fillWidth: root.compact && !HopStyle.mobile
                iconOnly: HopStyle.mobile
                iconName: HopStyle.mobile ? "refresh" : ""
                visible: root.subscriptionCount > 0
                enabled: !root.subscriptionsBusy
                text: qsTr("Обновить")
                onClicked: root.refreshRequested()
            }
            ActionButton {
                objectName: "hopAddButton"
                Layout.fillWidth: root.compact && !HopStyle.mobile
                visible: !root.onboarding
                iconOnly: HopStyle.mobile
                iconName: HopStyle.mobile ? "add" : ""
                text: qsTr("+ Добавить")
                onClicked: root.addRequested()
            }
        }

        Rectangle {
            objectName: "hopCurrentConnection"
            visible: !root.onboarding
            Layout.fillWidth: true
            implicitHeight: connectionRow.implicitHeight + 32
            color: root.rowColor
            radius: HopStyle.mobile ? 16 : 5

            GridLayout {
                id: connectionRow
                columns: root.compact ? 2 : 3
                anchors.fill: parent
                anchors.margins: 16
                columnSpacing: 12
                rowSpacing: 12

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
                        text: root.connectionStatus + (!root.disconnecting && root.connected && root.currentHealth.state === "reachable" && root.currentHealth.latencyMs >= 0 ? (HopStyle.mobile ? qsTr(" · Время ответа: %1\u00a0мс") : qsTr(" · Время ответа: %1 мс")).arg(root.currentHealth.latencyMs) : !root.disconnecting && root.connected && root.currentHealth.state === "unreachable" ? qsTr(" · Нет ответа от сервера") : "")
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
                        wrapMode: HopStyle.mobile ? Text.Wrap : Text.NoWrap
                        elide: HopStyle.mobile ? Text.ElideNone : Text.ElideRight
                    }
                    Label {
                        objectName: "hopRoutingMode"
                        Layout.fillWidth: true
                        text: root.splitTunnelingEnabled ? qsTr("Раздельное туннелирование: включено") : qsTr("Раздельное туннелирование: отключено")
                        color: root.secondaryTextColor
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
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
                        function rate(key) {
                            return root.trafficRates[key] >= 0 ? Number(root.trafficRates[key]).toLocaleString(Qt.locale(), 'f', 2) : "—";
                        }
                        text: qsTr("VPN: ↓ %1 · ↑ %2 Мбит/с").arg(rate("vpnDown")).arg(rate("vpnUp")) + (HopStyle.mobile ? "\n" : "    ") + qsTr("Напрямую: ↓ %1 · ↑ %2 Мбит/с").arg(rate("directDown")).arg(rate("directUp"))
                        color: root.secondaryTextColor
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
                ActionButton {
                    objectName: "hopConnectButton"
                    Layout.columnSpan: root.compact ? 2 : 1
                    Layout.fillWidth: root.compact
                    emphasized: !root.disconnecting
                    opacity: root.disconnecting || enabled ? 1 : 0.45
                    emphasisColor: root.connected && !HopStyle.mobile ? "#e2a36a" : HopStyle.accent
                    text: root.cancelling ? qsTr("Отменяется…") : root.disconnecting ? qsTr("Отключается…") : root.connected ? qsTr("Отключить") : root.connecting ? qsTr("Отмена") : qsTr("Подключить")
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
                text: root.protectionServiceAvailable ? qsTr("VPN-трафик заблокирован до восстановления подключения. Прямые IP-маршруты доступны.") : qsTr("Связь со службой потеряна. Последнее известное состояние: защита включена.")
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
            visible: !root.onboarding
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

        HopSearchField {
            objectName: "hopLocationSearch"
            visible: !root.onboarding
            Layout.fillWidth: true
            placeholderText: qsTr("Поиск по локациям и сервисам")
            maximumLength: 256
            selectByMouse: true
            text: root.searchText
            onTextEdited: root.searchRequested(text)
            color: HopStyle.text
            placeholderTextColor: HopStyle.muted
        }

        Item {
            visible: root.onboarding
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 320)
                spacing: 16
                Image {
                    source: "qrc:/images/hop/logo-preview.png"
                    sourceSize.width: 72
                    sourceSize.height: 72
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 72
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: qsTr("Добавьте подключение")
                    font.pixelSize: 24
                    font.weight: Font.Medium
                    color: root.textColor
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTr("Добавьте ссылку подписки, ключ доступа или файл конфигурации.")
                    font.pixelSize: 14
                    color: root.secondaryTextColor
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                HopButton {
                    objectName: "hopEmptyAddButton"
                    text: qsTr("Добавить подключение")
                    emphasized: true
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    onClicked: root.addRequested()
                }
                HopButton {
                    objectName: "hopEmptyRestoreButton"
                    text: qsTr("Восстановить копию")
                    Layout.fillWidth: true
                    onClicked: root.backupRequested()
                }
            }
        }

        ListView {
            id: servers
            visible: !root.onboarding
            objectName: "hopServersList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: HopLocationListModel {
                id: visibleLocations
                sourceModel: root.serverModel || null
                grouping: root.locationsAvailable
                expandedServices: root.expandedServices
                marks: root.locationMarks
                searching: root.searchText.trim().length > 0
                collapsedByDefault: HopStyle.mobile
            }
            clip: true
            reuseItems: true
            spacing: 0
            readonly property real scrollWidth: HopStyle.mobile ? (mobileLocationsScroll.visible ? mobileLocationsScroll.width : 0) : (locationsScroll.visible ? locationsScroll.width : 0)
            ScrollBar.vertical: HopScrollBar {
                id: mobileLocationsScroll
                policy: HopStyle.mobile && size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }
            HopScrollBar {
                id: locationsScroll
                objectName: "hopLocationsScrollBar"
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                orientation: Qt.Vertical
                policy: !HopStyle.mobile && size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                size: visibleLocations.contentHeight > 0 ? Math.min(1, servers.height / visibleLocations.contentHeight) : 1
                position: size < 1 ? Math.max(0, Math.min(1, (servers.contentY - servers.originY) / Math.max(1, servers.contentHeight - servers.height))) * (1 - size) : 0
                onPositionChanged: {
                    if (!HopStyle.mobile) {
                        const targetY = servers.originY + (size < 1 ? position / (1 - size) : 0) * Math.max(0, servers.contentHeight - servers.height)
                        if (Math.abs(servers.contentY - targetY) > 0.5) servers.contentY = targetY
                    }
                }
            }
            delegate: Item {
                id: serverRow
                objectName: serviceHeader ? "hopServiceRow_" + serviceId : "hopLocationRow_" + sourceIndex
                required property int index
                required property int sourceIndex
                required property bool serviceHeader
                required property string serviceId
                required property real rowExtent
                required property string serverId
                required property string name
                required property string serverDescription
                required property bool isServerFromGatewayApi
                required property bool isSubscriptionExpired
                required property bool isSubscriptionExpiringSoon
                readonly property bool selected: serverId === root.selectedServerId
                readonly property var health: root.healthStatuses[serverId] || ({
                        state: "unknown",
                        latencyMs: -1
                    })
                readonly property var marks: root.locationMarks[serverId] || ({})
                readonly property bool canSelect: !root.selectionLocked && !isSubscriptionExpired && marks.available !== false

                width: ListView.view.width - servers.scrollWidth
                height: !HopStyle.mobile ? rowExtent : serviceHeader ? (serviceHeaderLoader.item?.implicitHeight || 64) : Math.max(52, (serverContent.item?.implicitHeight || 0) + 4)
                Loader {
                    id: serviceHeaderLoader
                    anchors.fill: parent
                    active: serverRow.serviceHeader
                    sourceComponent: Item {
                        id: serviceSection
                        readonly property string section: serverRow.serviceId
                        readonly property var actions: root.serviceActions[section] || ({})
                        readonly property bool expanded: root.serviceExpanded(section)
                        width: servers.width - servers.scrollWidth
                        implicitHeight: serviceCard.height + 8
                        Rectangle {
                            id: serviceCard
                            objectName: "hopServiceHeader_" + serviceSection.section
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: HopStyle.mobile ? Math.max(52, serviceContent.implicitHeight + 4) : 56
                            color: serviceHeaderHover.hovered ? Qt.lighter(HopStyle.section, 1.18) : HopStyle.section
                            radius: HopStyle.mobile ? 8 : HopStyle.radius
                            HoverHandler {
                                id: serviceHeaderHover
                                enabled: !HopStyle.mobile && !root.searchText.trim().length
                            }
                            RowLayout {
                                id: serviceContent
                                anchors.fill: parent
                                anchors.leftMargin: HopStyle.mobile ? 4 : 12
                                anchors.rightMargin: HopStyle.mobile ? 4 : 12
                                spacing: HopStyle.mobile ? 2 : 10
                                HopButton {
                                    id: serviceToggle
                                    objectName: "hopServiceToggle_" + serviceSection.section
                                    background: Rectangle {
                                        radius: HopStyle.radius
                                        color: "transparent"
                                        border.width: serviceToggle.visualFocus ? 1 : 0
                                        border.color: root.accentColor
                                    }
                                    enabled: !root.searchText.trim().length
                                    iconOnly: true
                                    padding: 0
                                    Layout.alignment: Qt.AlignVCenter
                                    text: "›"
                                    contentItem: Item {
                                        Shape {
                                            objectName: "hopServiceChevron_" + serviceSection.section
                                            anchors.centerIn: parent
                                            width: 12
                                            height: 12
                                            rotation: serviceSection.expanded ? 90 : 0
                                            ShapePath {
                                                strokeColor: root.textColor
                                                strokeWidth: 1.5
                                                fillColor: "transparent"
                                                capStyle: ShapePath.RoundCap
                                                joinStyle: ShapePath.RoundJoin
                                                PathMove {
                                                    x: 4
                                                    y: 2
                                                }
                                                PathLine {
                                                    x: 8
                                                    y: 6
                                                }
                                                PathLine {
                                                    x: 4
                                                    y: 10
                                                }
                                            }
                                        }
                                    }
                                    Accessible.name: (serviceSection.expanded ? qsTr("Свернуть %1") : qsTr("Развернуть %1")).arg(root.serviceNames[serviceSection.section] || serviceSection.section)
                                    onClicked: root.toggleService(serviceSection.section)
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: HopStyle.mobile
                                    spacing: 3
                                    TapHandler {
                                        enabled: !root.searchText.trim().length
                                        onTapped: root.toggleService(serviceSection.section)
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.serviceNames[serviceSection.section] || serviceSection.section
                                        textFormat: Text.PlainText
                                        color: root.textColor
                                        wrapMode: HopStyle.mobile ? Text.Wrap : Text.NoWrap
                                        elide: HopStyle.mobile ? Text.ElideNone : Text.ElideRight
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
                                        wrapMode: HopStyle.mobile ? Text.Wrap : Text.NoWrap
                                        elide: HopStyle.mobile ? Text.ElideNone : Text.ElideRight
                                        color: status.state === "error" ? HopStyle.danger : HopStyle.muted
                                        font.pixelSize: 12
                                        ToolTip.visible: statusHover.hovered && truncated
                                        ToolTip.text: text
                                        HoverHandler {
                                            id: statusHover
                                        }
                                    }
                                }
                                HopButton {
                                    id: serviceActionsButton
                                    objectName: "hopServiceActions_" + serviceSection.section
                                    text: "…"
                                    iconOnly: true
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
                }

                Rectangle {
                    visible: !serverRow.serviceHeader
                    anchors.fill: parent
                    anchors.bottomMargin: HopStyle.mobile ? 0 : 8
                    color: HopStyle.mobile && serverRow.selected ? HopStyle.section : root.rowColor
                    radius: HopStyle.mobile ? 0 : HopStyle.radius
                    border.width: !HopStyle.mobile && serverRow.selected ? 1 : 0
                    border.color: root.accentColor
                }
                Rectangle {
                    visible: !serverRow.serviceHeader && HopStyle.mobile
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: root.lineColor
                    opacity: 0.24
                }
                Rectangle {
                    visible: !serverRow.serviceHeader && HopStyle.mobile && serverRow.selected
                    width: 3
                    height: parent.height
                    color: root.accentColor
                }

                Loader {
                    id: serverContent
                    anchors.fill: parent
                    active: !serverRow.serviceHeader
                    anchors.margins: HopStyle.mobile ? 4 : 12
                    anchors.leftMargin: 12
                    anchors.bottomMargin: HopStyle.mobile ? 4 : 20
                    sourceComponent: RowLayout {
                        spacing: HopStyle.mobile ? 4 : 10

                        HopRadioButton {
                            objectName: "hopSelectServer_" + serverRow.sourceIndex
                            visible: !HopStyle.mobile
                            accentColor: root.accentColor
                            secondaryColor: root.secondaryTextColor
                            checked: serverRow.selected
                            enabled: serverRow.canSelect
                            Accessible.name: qsTr("Выбрать %1").arg(serverRow.name)
                            onClicked: root.selectRequested(serverRow.sourceIndex)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: HopStyle.mobile
                            spacing: HopStyle.mobile ? 2 : 5
                            Accessible.ignored: !HopStyle.mobile
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: qsTr("Выбрать %1").arg(serverRow.name)
                            Accessible.checkable: true
                            Accessible.checked: serverRow.selected
                            Accessible.onPressAction: if (serverRow.canSelect)
                                root.selectRequested(serverRow.sourceIndex)
                            TapHandler {
                                enabled: HopStyle.mobile && serverRow.canSelect
                                onTapped: root.selectRequested(serverRow.sourceIndex)
                            }
                            Label {
                                Layout.fillWidth: true
                                text: serverRow.marks.serviceName && serverRow.name.startsWith(serverRow.marks.serviceName + " · ") ? serverRow.name.substring(serverRow.marks.serviceName.length + 3) : serverRow.name
                                textFormat: Text.PlainText
                                color: root.textColor
                                font.pixelSize: 15
                                font.weight: Font.Medium
                                wrapMode: HopStyle.mobile ? Text.Wrap : Text.NoWrap
                                elide: HopStyle.mobile ? Text.ElideNone : Text.ElideRight
                            }
                            Label {
                                visible: root.locationsAvailable && !serverRow.marks.excluded && !!serverRow.marks.automaticMember
                                text: serverRow.marks.allLocations ? qsTr("Автовыбор") : qsTr("Автопул · приоритет %1").arg(serverRow.marks.priority || 0)
                                color: root.accentColor
                                font.pixelSize: 11
                            }
                            Label {
                                Layout.fillWidth: true
                                readonly property string protocolLabel: serverRow.isSubscriptionExpired ? qsTr("Подписка истекла") : serverRow.marks.available === false ? qsTr("Локация недоступна в этой сборке") : serverRow.isSubscriptionExpiringSoon ? qsTr("Подписка скоро истекает") : serverRow.serverDescription
                                text: HopStyle.mobile && root.healthAvailable && serverRow.health.state !== "unknown" ? [protocolLabel, healthLabel.text].filter(value => value.length).join(" · ") : protocolLabel
                                textFormat: Text.PlainText
                                color: serverRow.isSubscriptionExpired || serverRow.isSubscriptionExpiringSoon ? root.accentColor : root.secondaryTextColor
                                font.pixelSize: 12
                                wrapMode: HopStyle.mobile ? Text.Wrap : Text.NoWrap
                                elide: HopStyle.mobile ? Text.ElideNone : Text.ElideRight
                            }
                        }

                        ColumnLayout {
                            objectName: "hopHealthColumn_" + serverRow.sourceIndex
                            visible: !HopStyle.mobile && root.healthAvailable
                            Layout.minimumWidth: root.compact ? 76 : 112
                            Layout.preferredWidth: root.compact ? 76 : 112
                            Layout.maximumWidth: root.compact ? 76 : 112
                            spacing: 3
                            Rectangle {
                                Layout.alignment: Qt.AlignHCenter
                                width: 7
                                height: 7
                                radius: 4
                                color: serverRow.health.state === "reachable" ? serverRow.health.latencyMs < 500 ? root.successColor : root.accentColor : serverRow.health.state === "unreachable" ? HopStyle.healthError : root.secondaryTextColor
                            }
                            Label {
                                id: healthLabel
                                objectName: "hopHealthStatus_" + serverRow.sourceIndex
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: serverRow.health.state === "reachable" ? serverRow.health.latencyMs + qsTr(" мс") : serverRow.health.state === "unreachable" ? qsTr("Нет ответа") : serverRow.health.state === "configuration-limit" ? qsTr("Лимит подписки") : serverRow.health.state === "checking" ? qsTr("Проверка") : serverRow.health.state === "stale" ? qsTr("Устарело") : serverRow.health.state === "deferred" ? qsTr("После VPN") : serverRow.health.state === "unsupported" ? "—" : qsTr("Не проверено")
                                color: serverRow.health.state === "configuration-limit" ? HopStyle.danger : root.secondaryTextColor
                                font.pixelSize: 11
                                ToolTip.visible: healthHover.hovered && ["configuration-limit", "deferred", "unsupported"].indexOf(serverRow.health.state) >= 0
                                ToolTip.text: serverRow.health.state === "configuration-limit" ? qsTr("Провайдер отказал из-за лимита конфигураций. Задержка сервера не измерялась.") : serverRow.health.state === "deferred" ? qsTr("Проверка AWG доступна при отключённом VPN, чтобы не прервать текущее соединение.") : serverRow.health.state === "unsupported" ? qsTr("Для этого профиля нет безопасной проверки доступности. Подключение можно проверить вручную.") : ""
                                HoverHandler {
                                    id: healthHover
                                }
                            }
                        }

                        ActionButton {
                            id: locationActions
                            objectName: "hopDetailsButton_" + serverRow.sourceIndex
                            text: "…"
                            iconOnly: true
                            Accessible.name: qsTr("Действия для %1").arg(serverRow.name)
                            onClicked: locationMenu.popup(locationActions, locationActions.width - locationMenu.width, locationActions.height)
                            HopMenu {
                                id: locationMenu
                                objectName: "hopLocationMenu_" + serverRow.sourceIndex
                                HopMenuItem {
                                    objectName: "hopLocationCheck_" + serverRow.sourceIndex
                                    text: qsTr("Проверить время ответа")
                                    enabled: root.locationActions[serverRow.serverId] ? root.locationActions[serverRow.serverId].canCheck === true : root.healthAvailable && serverRow.health.state !== "checking"
                                    onTriggered: root.locationHealthRequested(serverRow.serverId)
                                }
                                HopMenuItem {
                                    objectName: "hopLocationStats_" + serverRow.sourceIndex
                                    text: qsTr("Статистика локации")
                                    onTriggered: root.locationStatsRequested(serverRow.serverId)
                                }
                                HopMenuItem {
                                    readonly property bool included: Qt.platform.os === "android" && root.locationsAvailable
                                    visible: included
                                    height: included ? implicitHeight : 0
                                    text: serverRow.marks.restrictedNetwork ? qsTr("Убрать из профилей обхода") : qsTr("Использовать для обхода белых списков")
                                    onTriggered: root.restrictedLocationRequested(serverRow.serverId, !serverRow.marks.restrictedNetwork)
                                }
                                HopMenuItem {
                                    readonly property bool included: root.locationsAvailable
                                    visible: included
                                    height: included ? implicitHeight : 0
                                    objectName: "hopLocationPool_" + serverRow.sourceIndex
                                    text: serverRow.marks.automaticMember ? qsTr("Убрать из автопереключения") : qsTr("Добавить в автопереключение")
                                    onTriggered: root.poolLocationRequested(serverRow.serverId)
                                }
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
