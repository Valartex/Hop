import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "../Controls2"
import "../Components"

PageType {
    id: root
    objectName: "hopHomePage"

    readonly property bool selectionLocked: ConnectionController.isConnected || ConnectionController.isConnectionInProgress
                                           || ConnectionController.isDisconnecting
                                           || (root.locationSession && root.locationSession.running)
    readonly property var healthController: typeof EndpointHealth === "undefined" ? null : EndpointHealth
    readonly property var locations: typeof Locations === "undefined" ? null : Locations
    readonly property var locationSession: typeof LocationSession === "undefined" ? null : LocationSession

    readonly property var actions: typeof ConnectionActions === "undefined" ? null : ConnectionActions

    function openServerDetails(id) {
        if (!root.locations) return
        serviceDialog.currentId = id
        serviceDialog.info = root.locations.serviceInfo(id)
        serviceDialog.open()
    }

    function openLocationStatistics(id) {
        if (!root.locations || !root.locations.marks[id]) return
        locationStatistics.locationId = id
        locationStatistics.info = root.locations.marks[id]
        locationStatistics.notice = root.actions ? (root.actions.locationActions[id] || {}).notice || "" : ""
        locationStatistics.open()
    }
    function checkLocation(id) {
        if (root.actions && !root.actions.checkLocation(id))
            PageController.showNotificationMessage(root.actions.lastError)
    }

    Connections {
        target: root.locations
        function onCatalogChanged() {
            if (serviceDialog.visible) serviceDialog.info = root.locations.serviceInfo(serviceDialog.currentId)
            if (locationStatistics.visible) {
                if (!root.locations.marks[locationStatistics.locationId]) locationStatistics.close()
                else locationStatistics.info = root.locations.marks[locationStatistics.locationId]
            }
        }
    }

    Connections {
        target: ConnectionController
        function onPreparingConfig() {
            PageController.showNotificationMessage(qsTr("Нельзя отключиться во время подготовки конфигурации"))
        }
    }

    Connections {
        target: root.locationSession
        function onPreparationFailed(message) { PageController.showErrorMessage(message) }
    }

    HopDashboard {
        anchors.fill: parent
        serverModel: typeof LocationSearch !== "undefined" ? LocationSearch : root.locations ? root.locations : ServersModel
        selectedServerId: root.locations ? root.locations.selectedId : ServersUiController.defaultServerId
        selectedServerName: root.locations ? root.locations.selectedName : ServersUiController.defaultServerName
        selectedProtocol: root.locations ? root.locations.selectedProtocol : ServersUiController.defaultServerDefaultContainerName
        connected: ConnectionController.isConnected && (!root.locationSession || root.locationSession.verified)
        connectAvailable: !root.locations || root.locations.mode >= 2 || !root.locations.marks[root.locations.selectedId]
                          || root.locations.marks[root.locations.selectedId].available !== false
        connecting: ConnectionController.isConnectionInProgress || (root.locationSession && root.locationSession.running && !root.locationSession.verified)
        disconnecting: ConnectionController.isDisconnecting
        cancelling: ConnectionController.isCancelling
        protectionActive: ConnectionController.trafficProtectionActive === true
        protectionServiceAvailable: ConnectionController.protectionServiceAvailable === true
        subscriptionCount: HttpSubscriptions.sourceCount + (root.locations ? root.locations.apiServiceCount : 0)
        subscriptionsBusy: HttpSubscriptions.busy || (root.locationSession && root.locationSession.refreshing)
        healthStatuses: root.actions ? root.actions.healthStatuses : ({})
        serviceActions: root.actions ? root.actions.serviceActions : ({})
        serviceStatuses: root.actions ? root.actions.serviceStatuses : ({})
        activeLocationId: root.locationSession && root.locationSession.running ? root.locationSession.activeId : selectedServerId
        locationActions: root.actions ? root.actions.locationActions : ({})
        healthBusy: root.healthController ? root.healthController.busy : false
        healthAvailable: root.healthController ? root.healthController.available : false
        locationsAvailable: root.locations !== null
        locationMarks: root.locations ? root.locations.marks : ({})
        serviceNames: root.locations ? root.locations.services : ({})
        selectionStatus: root.locationSession ? root.locationSession.status : ""
        trafficRates: root.locationSession ? root.locationSession.trafficRates : ({})
        splitTunnelingEnabled: typeof RoutingPolicy !== "undefined" ? RoutingPolicy.enabled : IpSplitTunnelingController.isSplitTunnelingEnabled
                               || AppSplitTunnelingController.isSplitTunnelingEnabled
                               || ServersUiController.isDefaultServerDefaultContainerHasSplitTunneling
        warningText: ServersUiController.defaultServerHasOutdatedAwgContainer
                     ? qsTr("Конфигурация AmneziaWG устарела. Для AmneziaWG 3.1 требуется обновление конфигурации сервера.")
                     : ""

        searchText: typeof LocationSearch !== "undefined" ? LocationSearch.query : ""
        onSearchRequested: function(query) { if (typeof LocationSearch !== "undefined") LocationSearch.query = query }
        onConnectRequested: ConnectionController.connectButtonClicked()
        onReleaseProtectionRequested: ConnectionController.releaseTrafficProtection()
        onAddRequested: addDialog.open()
        onRefreshRequested: if (root.actions) root.actions.refreshAll()
        onServiceRefreshRequested: function(id) { if (root.actions && !root.actions.refreshService(id)) PageController.showErrorMessage(root.actions.lastError) }
        onServiceRemoveRequested: function(id) {
            removeDialog.serviceId = id
            removeDialog.serviceName = (root.locations.serviceInfo(id) || {}).name || id
            removeDialog.open()
        }
        onHealthRequested: if (root.actions) root.actions.checkAllLocations()
        onPoolLocationRequested: function(id) { if (root.locations) root.locations.toggleMembership(id) }
        onSelectRequested: function(row) {
            if (!root.selectionLocked) {
                if (root.locations) root.locations.selectRow(typeof LocationSearch !== "undefined" ? LocationSearch.sourceRow(row) : row)
                else ServersUiController.setDefaultServerAtIndex(row)
            }
        }
        onDetailsRequested: function(serverId) { root.openServerDetails(serverId) }
        onLocationStatsRequested: function(id) { root.openLocationStatistics(id) }
        onLocationHealthRequested: function(id) { root.checkLocation(id) }
    }

    Connections {
        target: ConnectionController
        function onProtectionRecoveryRequired() {
            PageController.showErrorMessage(qsTr("Служба Hop недоступна. Запустите службу или утилиту hop_guard_recover.exe с параметром --clear-hop-protection от администратора, чтобы снять сохранённую защиту."))
        }
    }

    HopDialog {
        id: addDialog
        title: qsTr("Добавить подключение")
        modal: true
        anchors.centerIn: parent
        width: Math.min(420, root.width - 32)

        ColumnLayout {
            width: parent.width
            spacing: 12
            Label {
                text: qsTr("Ссылка подписки или ключ конфигурации")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            HopTextField {
                id: subscriptionLink
                objectName: "hopSubscriptionLink"
                Layout.fillWidth: true
                placeholderText: "https://… / vpn://… / vless://…"
                echoMode: TextInput.Password
                selectByMouse: true
                enabled: !HttpSubscriptions.busy
            }
            HopTextField {
                id: subscriptionName
                Layout.fillWidth: true
                visible: subscriptionLink.text.trim().startsWith("https://")
                placeholderText: qsTr("Название сервиса, например BlancVPN")
                maximumLength: 120
            }
            HopButton {
                text: qsTr("Выбрать файл…")
                onClicked: {
                    addDialog.close()
                    const file = SystemController.getFileName(qsTr("Открыть конфигурацию"), "Config files (*.vpn *.conf *.json *.ovpn)")
                    if (file && ImportController.extractConfigFromFile(file)) PageController.goToPage(PageEnum.PageSetupWizardViewConfig)
                }
            }
        }

        footer: DialogButtonBox {
            HopButton {
                text: qsTr("Добавить")
                objectName: "hopSubscriptionAdd"
                enabled: subscriptionLink.text.trim().length > 0 && !HttpSubscriptions.busy
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            HopButton {
                text: qsTr("Отмена")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }
        onAccepted: {
            const text = subscriptionLink.text.trim()
            if (ConnectionImport.prepareConnectionImport(text, subscriptionName.text)) {
                subscriptionLink.clear()
                subscriptionName.clear()
            }
        }
    }

    HopLocationStatsDialog {
        id: locationStatistics
        parent: Overlay.overlay
        anchors.centerIn: parent
        controller: root.healthController
        checkAvailable: root.actions ? (root.actions.locationActions[locationId] || {}).canCheck === true : false
        onCheckRequested: root.checkLocation(locationId)
        Connections {
            target: root.healthController
            function onHistoryChanged() {
                const state = (root.healthController.statuses[locationStatistics.locationId] || {}).state
                if (locationStatistics.visible && !locationStatistics.info.api && ["reachable", "unreachable", "stale"].indexOf(state) >= 0)
                    locationStatistics.notice = ""
            }
        }
    }

    HopServiceDialog {
        id: serviceDialog
        property string currentId: ""
        anchors.centerIn: parent
        catalogState: root.locationSession ? root.locationSession.catalogStatuses[info.serverId] || ({}) : ({})
        busy: HttpSubscriptions.busy || (root.locationSession && root.locationSession.refreshing)
        onRefreshRequested: if (root.actions && !root.actions.refreshService(currentId)) PageController.showErrorMessage(root.actions.lastError)
    }

    HopDialog {
        id: removeDialog
        objectName: "hopRemoveServiceDialog"
        property string serviceId: ""
        property string serviceName: ""
        title: qsTr("Удалить подписку?")
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, root.width - 32)
        Label {
            width: parent.width
            text: qsTr("Удалить «%1» и её локации из Hop? Данные у провайдера останутся без изменений.").arg(removeDialog.serviceName)
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        footer: DialogButtonBox {
            HopButton { objectName: "hopConfirmRemoveService"; text: qsTr("Удалить"); DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
            HopButton { text: qsTr("Отмена"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        }
        onAccepted: if (root.actions && !root.actions.removeService(serviceId)) PageController.showErrorMessage(root.actions.lastError)
    }
}
