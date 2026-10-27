import QtQuick
import Style 1.0
import QtQuick.Controls
import QtQuick.Layouts
import "../Controls2"
import "../Components"

PageType {
    objectName: "hopAboutPage"
    enableTimer: false
    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 24
        spacing: 16

        RowLayout {
            spacing: 16
            Image { source: "qrc:/images/hop/logo-preview.png"; sourceSize.width: 64; sourceSize.height: 64; Layout.preferredWidth: 64; Layout.preferredHeight: 64 }
            ColumnLayout {
                spacing: 4
                Label { text: "Hop"; font.pixelSize: 28; font.weight: Font.DemiBold }
                Label { text: qsTr("Версия %1").arg(SettingsController.getAppVersion()) }
            }
        }
        Label {
            text: qsTr("Мультипротокольный VPN-клиент")
            font.pixelSize: 18
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("VLESS Reality · AmneziaWG 3.1\nПодписки, выбор локаций и раздельная маршрутизация трафика.")
            wrapMode: Text.WordWrap
            lineHeight: 1.4
        }
    }
}
