import QtQuick
import QtQuick.Controls
import "../../client/ui/qml/Components"

ApplicationWindow {
    id: window
    objectName: "hopPreviewWindow"
    visible: true
    title: "Hop — предпросмотр, демонстрационные данные"
    width: 760
    height: 680
    minimumWidth: 360
    minimumHeight: 520

    ListModel {
        id: demoServers
        ListElement { serverId: "demo-blanc"; name: "BlancVPN"; serverDescription: "Германия · VLESS Reality"; isServerFromGatewayApi: true; isSubscriptionExpired: false; isSubscriptionExpiringSoon: false }
        ListElement { serverId: "demo-amnezia"; name: "Amnezia VPN"; serverDescription: "Нидерланды · AmneziaWG 3.1"; isServerFromGatewayApi: true; isSubscriptionExpired: false; isSubscriptionExpiringSoon: false }
        ListElement { serverId: "demo-personal"; name: "Личный сервер"; serverDescription: "Финляндия · VLESS Reality"; isServerFromGatewayApi: false; isSubscriptionExpired: false; isSubscriptionExpiringSoon: false }
    }

    HopDashboard {
        id: dashboard
        objectName: "hopPreviewDashboard"
        anchors.fill: parent
        serverModel: demoServers
        selectedServerId: "demo-blanc"
        selectedServerName: "BlancVPN"
        selectedProtocol: "VLESS Reality"
        splitTunnelingEnabled: true

        onSelectRequested: function(row) {
            selectedServerId = demoServers.get(row).serverId
            selectedServerName = demoServers.get(row).name
            selectedProtocol = row === 1 ? "AmneziaWG 3.1" : "VLESS Reality"
        }
        onConnectRequested: demoNotice.open()
        onAddRequested: demoNotice.open()
        onRoutesRequested: demoNotice.open()
        onDetailsRequested: demoNotice.open()
        onProtocolRequested: demoNotice.open()
    }

    Dialog {
        id: demoNotice
        objectName: "hopPreviewNotice"
        anchors.centerIn: parent
        modal: true
        title: "Предпросмотр интерфейса"
        width: Math.min(380, window.width - 32)
        standardButtons: Dialog.Ok
        Label {
            width: parent.width
            text: "Это демонстрационные данные. Предпросмотр не подключается к VPN и не меняет сеть компьютера."
            wrapMode: Text.WordWrap
        }
    }
}
