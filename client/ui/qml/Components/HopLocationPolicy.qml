import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0

ColumnLayout {
        id: root
        property var catalog
        property bool selectionLocked: false
        readonly property var modeValues: [0, 2, 3, 4]
        readonly property bool usesPool: !root.catalog || root.catalog.mode < 3
        readonly property var preferences: typeof RefreshPreferences !== "undefined" ? RefreshPreferences : null
        spacing: 12
        HopComboBox {
            objectName: "hopSelectionMode"
            Layout.fillWidth: true
            model: [qsTr("Ручной выбор"), qsTr("Автоматически — по порядку пула"),
                    qsTr("Все локации — по порядку списка"), qsTr("Все локации — по наименьшему времени ответа")]
            currentIndex: root.catalog ? root.modeValues.indexOf(root.catalog.mode) : 0
            onActivated: if (root.catalog) root.catalog.mode = root.modeValues[currentIndex]
        }
        Label {
            objectName: "hopSelectionModeDescription"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: !root.catalog || root.catalog.mode === 0
                ? qsTr("Вы выбираете локацию. При ошибке или потере связи клиент повторяет подключение к ней, не переходя на другие локации. Первоначальное подключение ограничено тремя минутами; повторы можно отменить.")
                : root.catalog.mode >= 3
                ? qsTr("Работающая локация сохраняется. Локации выше порога пропускаются. Выбор по задержке использует свежие доступные замеры; новая локация проверяется через её туннель. Исключённые локации не участвуют. Для стран без сохранённой конфигурации проверка возможна после подключения; фоновая проверка не создаёт профилей подписки.")
                : qsTr("Работающая локация сохраняется. При недоступности клиент пробует следующие разрешённые локации автопула; выбор страны включает её локации из всех сервисов.")
        }
        GridLayout {
            Layout.fillWidth: true
            columns: root.width >= 600 ? 4 : 2
            columnSpacing: 10
            rowSpacing: 8
            Label { objectName: "hopLatencyDurationLabel"; text: qsTr("Длительность превышения, с"); }
            HopTextField {
                objectName: "hopLatencyDuration"
                Layout.preferredWidth: 85
                text: root.preferences ? root.preferences.latencySeconds : 60
                validator: IntValidator { bottom: 15; top: 3600 }
                onEditingFinished: {
                    if (acceptableInput && root.preferences) root.preferences.latencySeconds = Number(text)
                    text = root.preferences ? root.preferences.latencySeconds : 60
                }
            }
            Label { objectName: "hopLatencyThresholdLabel"; text: qsTr("Порог задержки, мс"); }
            HopTextField {
                objectName: "hopLatencyThreshold"
                Layout.preferredWidth: 85
                text: root.preferences ? root.preferences.latencyThreshold : 1500
                validator: IntValidator { bottom: 1; top: 10000 }
                onEditingFinished: {
                    if (acceptableInput && root.preferences) root.preferences.latencyThreshold = Number(text)
                    text = root.preferences ? root.preferences.latencyThreshold : 1500
                }
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Если время ответа непрерывно превышает порог в течение указанной длительности, сначала обновляется подписка, затем повторно проверяется текущая локация и перебираются разрешённые локации. Ответ в пределах порога сбрасывает отсчёт. Отсутствие ответа считается превышением. Контроль выполняется раз в 15 секунд; переключение доступно в автоматическом режиме.")
            color: "#92a4b2"
            font.pixelSize: 12
        }
        RowLayout {
            visible: root.usesPool
            Layout.fillWidth: true
            HopComboBox {
                id: country
                objectName: "hopPoolCountry"
                Layout.fillWidth: true
                model: root.catalog ? root.catalog.countries : []
                textRole: "label"
                valueRole: "code"
            }
            HopButton {
                objectName: "hopTogglePoolCountry"
                text: qsTr("Изменить пул")
                enabled: country.count > 0
                onClicked: root.catalog.toggleCountry(country.currentValue)
            }
        }
        Label { visible: root.usesPool; text: qsTr("Приоритеты пула — сверху вниз"); font.bold: true }
        ListView {
            visible: root.usesPool
            objectName: "hopPriorityList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: root.catalog ? root.catalog.priorities : []
            ScrollBar.vertical: HopScrollBar { id: prioritiesScroll }
            delegate: RowLayout {
                required property var modelData
                required property int index
                width: ListView.view.width - (prioritiesScroll.visible ? prioritiesScroll.width : 0)
                Label { text: (index + 1) + "." }
                Label {
                    Layout.fillWidth: true
                    text: modelData.label
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                }
                HopButton { iconOnly: true; text: "↑"; enabled: index > 0; onClicked: root.catalog.movePriority(index, index - 1); Accessible.name: qsTr("Повысить приоритет") }
                HopButton { iconOnly: true; text: "↓"; enabled: index + 1 < root.catalog.priorities.length; onClicked: root.catalog.movePriority(index, index + 1); Accessible.name: qsTr("Понизить приоритет") }
                HopButton { iconOnly: true; text: "×"; onClicked: root.catalog.removePriority(index); Accessible.name: qsTr("Убрать из пула") }
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: parent.count === 0
                text: qsTr("Пул пуст. Добавьте страну здесь или локацию через меню «…» в разделе «Подключения».")
            }
        }
        Item { visible: !root.usesPool; Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Страна выбранной локации:") }
            HopTextField {
                id: explicitCountry
                objectName: "hopExplicitLocationCountry"
                maximumLength: 2
                placeholderText: "NL"
                Layout.preferredWidth: 64
                validator: RegularExpressionValidator { regularExpression: /[A-Za-z]{0,2}/ }
            }
            HopButton {
                text: qsTr("Указать")
                enabled: root.catalog && root.catalog.selectedId.length > 0 && explicitCountry.text.length === 2
                onClicked: root.catalog.setCountry(root.catalog.selectedId, explicitCountry.text)
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Если в данных сервиса нет страны, её можно указать кодом ISO. Из названий городов страна автоматически не определяется.")
            font.pixelSize: 12
        }
    }
