import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0
HopDialog {
    id: root
    objectName: "hopServiceDialog"
    property var info: ({})
    property var catalogState: ({})
    property bool busy: false
    signal refreshRequested()
    title: info.name || qsTr("Сведения о подключении")
    width: Math.min(480, parent ? parent.width - 32 : 480)
    modal: true
    component DetailValue: Label {
        Layout.fillWidth: true
        color: HopStyle.text
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
    ColumnLayout {
        width: parent.width
        spacing: 12
        GridLayout {
            columns: 2
            columnSpacing: 24
            rowSpacing: 10
            Layout.fillWidth: true
            Label { text: qsTr("Источник"); color: HopStyle.muted }
            DetailValue { text: root.info.kind === "api" ? qsTr("API-подписка") : root.info.kind === "http" ? qsTr("HTTPS-подписка") : qsTr("Конфигурация сервера") }
            Label { text: qsTr("Локаций"); color: HopStyle.muted }
            DetailValue { text: root.info.locations || 0 }
            Label { text: qsTr("Протокол"); color: HopStyle.muted }
            DetailValue { text: root.info.protocol || "—" }
            Label { visible: root.info.kind === "api"; text: qsTr("Конфигурации"); color: HopStyle.muted }
            DetailValue { visible: root.info.kind === "api"; text: root.info.maxConfigurations > 0 ? qsTr("%1 из %2").arg(root.info.activeConfigurations).arg(root.info.maxConfigurations) : qsTr("Нет данных") }
            Label { visible: !!root.info.validUntil; text: qsTr("Действует до"); color: HopStyle.muted }
            DetailValue { visible: !!root.info.validUntil; text: root.info.validUntil ? Qt.formatDateTime(new Date(root.info.validUntil), "dd.MM.yyyy") : "" }
            Label { visible: !!root.info.host; text: qsTr("Сервис"); color: HopStyle.muted }
            DetailValue { visible: !!root.info.host; text: root.info.host || "" }
        }
        Label {
            Layout.fillWidth: true
            visible: root.info.kind === "api" && !root.info.hasConfiguration
            text: qsTr("Конфигурация выбранной локации будет получена при подключении. Загрузка списка локаций не расходует лимит подписки.")
            color: HopStyle.muted
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            visible: root.info.maxConfigurations > 0 && root.info.activeConfigurations >= root.info.maxConfigurations
            text: qsTr("Лимит конфигураций исчерпан. Для нового подключения освободите ненужную конфигурацию у провайдера или импортируйте готовый файл.")
            color: HopStyle.danger
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.catalogState.message || ""
            color: root.catalogState.state === "error" ? HopStyle.danger : HopStyle.muted
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
    }
    footer: DialogButtonBox {
        delegate: HopButton {}
        HopButton {
            text: qsTr("Обновить подписку")
            visible: root.info.kind !== "native"
            enabled: !root.busy
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: root.refreshRequested()
        }
        HopButton { text: qsTr("Закрыть"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: root.close() }
        background: Rectangle { color: HopStyle.surface }
    }
}
