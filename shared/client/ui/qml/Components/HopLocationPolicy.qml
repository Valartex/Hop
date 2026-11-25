import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0

ColumnLayout {
    id: root
    Layout.minimumWidth: 0
    property var catalog
    property bool selectionLocked: false
    readonly property var modeValues: [0, 2, 3, 4]
    readonly property bool usesPool: !root.catalog || root.catalog.mode < 3
    readonly property var preferences: typeof RefreshPreferences !== "undefined" ? RefreshPreferences : null
    readonly property bool countryInPool: !!root.catalog && (root.catalog.priorities || []).some(item => item.kind === "country" && item.key === country.currentValue)
    spacing: HopStyle.mobile ? 8 : 12
    component PolicyLabel: Label {
        color: HopStyle.text
    }
    HopComboBox {
        objectName: "hopSelectionMode"
        Layout.fillWidth: true
        model: [qsTr("Ручной выбор"), qsTr("Автоматически — по порядку пула"), qsTr("Все локации — по порядку списка"), qsTr("Все локации — по наименьшему времени ответа")]
        currentIndex: root.catalog ? root.modeValues.indexOf(root.catalog.mode) : 0
        onActivated: if (root.catalog)
            root.catalog.mode = root.modeValues[currentIndex]
    }
    PolicyLabel {
        objectName: "hopSelectionModeDescription"
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: !root.catalog || root.catalog.mode === 0 ? qsTr("При потере связи Hop повторяет подключение к выбранной локации.") : root.catalog.mode >= 3 ? qsTr("Hop пропускает исключённые локации и локации с задержкой выше порога.") : qsTr("Если текущая локация недоступна, Hop пробует следующие из автопула.")
    }
    GridLayout {
        Layout.alignment: Qt.AlignLeft
        Layout.fillWidth: false
        Layout.preferredWidth: Math.min(implicitWidth, root.width)
        Layout.maximumWidth: root.width
        Layout.fillHeight: false
        columns: root.width >= 540 ? 2 : 1
        columnSpacing: 24
        rowSpacing: 12
        ColumnLayout {
            objectName: "hopLatencyDurationGroup"
            Layout.preferredWidth: Math.min(240, root.width)
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignTop
            spacing: 6
            PolicyLabel {
                objectName: "hopLatencyDurationLabel"
                text: qsTr("Длительность превышения, с")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            HopTextField {
                objectName: "hopLatencyDuration"
                Layout.preferredWidth: 85
                Layout.maximumWidth: 85
                text: root.preferences ? root.preferences.latencySeconds : 60
                validator: IntValidator {
                    bottom: 15
                    top: 3600
                }
                onEditingFinished: {
                    if (acceptableInput && root.preferences)
                        root.preferences.latencySeconds = Number(text);
                    text = root.preferences ? root.preferences.latencySeconds : 60;
                }
            }
        }
        ColumnLayout {
            objectName: "hopLatencyThresholdGroup"
            Layout.preferredWidth: Math.min(240, root.width)
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignTop
            spacing: 6
            PolicyLabel {
                objectName: "hopLatencyThresholdLabel"
                text: qsTr("Порог задержки, мс")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            HopTextField {
                objectName: "hopLatencyThreshold"
                Layout.preferredWidth: 85
                Layout.maximumWidth: 85
                text: root.preferences ? root.preferences.latencyThreshold : 1500
                validator: IntValidator {
                    bottom: 1
                    top: 10000
                }
                onEditingFinished: {
                    if (acceptableInput && root.preferences)
                        root.preferences.latencyThreshold = Number(text);
                    text = root.preferences ? root.preferences.latencyThreshold : 1500;
                }
            }
        }
    }
    PolicyLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("При длительном превышении порога Hop обновит подписку и проверит локацию снова. Отсутствие ответа тоже считается превышением.")
        color: HopStyle.muted
        font.pixelSize: 12
    }
    HopOrderTable {
        id: poolTable
        visible: root.usesPool
        objectName: "hopPriorityList"
        Layout.fillWidth: true
        Layout.fillHeight: !HopStyle.mobile
        rows: root.catalog ? root.catalog.priorities : []
        columns: [
            {
                title: qsTr("Приоритет"),
                role: "row",
                width: 100
            },
            {
                title: qsTr("Подписка"),
                role: "serviceName",
                width: root.width < 600 ? 140 : 220
            },
            {
                title: qsTr("Локация или страна"),
                role: "locationName",
                stretch: 1
            }
        ]
        editingEnabled: root.catalog !== null && root.catalog !== undefined
        emptyText: qsTr("Пул пуст. Добавьте страну здесь или локацию через меню «…» в разделе «Подключения».")
        onMoveRequested: function (row, delta) {
            if (root.catalog.movePriority(row, row + delta))
                poolTable.currentIndex = row + delta;
        }
        onRemoveRequested: function (row) {
            root.catalog.removePriority(row);
        }
    }
    GridLayout {
        columns: root.width < 400 ? 1 : 2
        visible: root.usesPool
        Layout.fillWidth: true
        Layout.fillHeight: false
        HopComboBox {
            id: country
            objectName: "hopPoolCountry"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Accessible.name: qsTr("Страна для автопула")
            model: root.catalog ? root.catalog.countries : []
            textRole: "label"
            valueRole: "code"
        }
        HopButton {
            objectName: "hopTogglePoolCountry"
            Layout.alignment: Qt.AlignBottom
            Layout.preferredWidth: HopStyle.mobile ? -1 : 200
            Layout.fillWidth: root.width < 400
            Layout.minimumWidth: 0
            text: root.countryInPool ? qsTr("Убрать страну из пула") : qsTr("Добавить страну в пул")
            enabled: country.count > 0
            onClicked: root.catalog.toggleCountry(country.currentValue)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Страна в пуле включает её доступные локации из всех подписок. Отдельно добавленные локации сохраняются при удалении страны.")
        }
    }
    Item {
        visible: !root.usesPool
        Layout.fillHeight: true
    }
}
