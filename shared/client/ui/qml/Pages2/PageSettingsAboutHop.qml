import QtQuick
import Style 1.0
import QtQuick.Controls
import QtQuick.Layouts
import "../Controls2"
import "../Components"

PageType {
    objectName: "hopAboutPage"
    enableTimer: false
    ScrollView {
        id: aboutScroll
        anchors.fill: parent
        anchors.margins: 24
        contentWidth: availableWidth
        clip: true
        ScrollBar.vertical: HopScrollBar {}
    ColumnLayout {
        width: aboutScroll.availableWidth
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Image { source: "qrc:/images/hop/logo-preview.png"; sourceSize.width: 64; sourceSize.height: 64; Layout.preferredWidth: 64; Layout.preferredHeight: 64 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: "Hop"; color: HopStyle.text; font.pixelSize: 28; font.weight: Font.DemiBold }
                Label { Layout.fillWidth: true; color: HopStyle.muted; wrapMode: Text.Wrap; text: qsTr("Версия %1").arg(SettingsController.getAppVersion()) }
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Мультипротокольный VPN-клиент")
            color: HopStyle.text
            wrapMode: Text.WordWrap
            font.pixelSize: 18
        }
        Label {
            Layout.fillWidth: true
            objectName: "hopAboutDescription"
            text: qsTr("Поддержка VLESS / Reality и AmneziaWG 3.1. Подписки и импорт конфигураций, ручной выбор локаций и автоматическое переключение между ними. Раздельная маршрутизация трафика через VPN или напрямую, проверки задержки и история замеров.")
            color: HopStyle.muted
            wrapMode: Text.WordWrap
            lineHeight: 1.4
        }
        Label {
            id: githubLink
            objectName: "hopAboutGithubLink"
            Layout.fillWidth: true
            text: '<a href="https://github.com/Valartex/Hop">github.com/Valartex/Hop</a>'
            textFormat: Text.StyledText
            linkColor: HopStyle.accent
            wrapMode: Text.Wrap
            onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            HoverHandler { cursorShape: githubLink.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
        }
        Label {
            id: websiteLink
            objectName: "hopAboutWebsiteLink"
            Layout.fillWidth: true
            text: '<a href="https://windowls.com">windowls.com</a>'
            textFormat: Text.StyledText
            linkColor: HopStyle.accent
            wrapMode: Text.Wrap
            onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            HoverHandler { cursorShape: websiteLink.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
        }
    }
    }
}
