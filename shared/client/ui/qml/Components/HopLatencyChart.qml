import QtQuick
import QtQuick.Controls
import Style 1.0
import "HopLatencyCurve.js" as Curve

Item {
    id: root
    objectName: "hopLocationLatencyGraph"
    property var samples: []
    property real maximum: 0
    property real windowStart: 0
    property real windowEnd: 0
    property bool aggregated: false
    readonly property real plotLeft: 52
    readonly property real plotTop: 14
    readonly property real plotWidth: Math.max(1, width - plotLeft - 14)
    readonly property real plotHeight: Math.max(1, height - plotTop - 48)
    readonly property real failureTop: plotTop + plotHeight + 8
    readonly property var geometry: Curve.build(samples, plotLeft, plotTop, plotWidth, plotHeight, maximum, windowStart, windowEnd)
    property int selectedIndex: -1
    readonly property var selected: selectedIndex >= 0 && selectedIndex < geometry.points.length ? geometry.points[selectedIndex] : null
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Время ответа, мс")
    function requestPaint() { if (canvas) canvas.requestPaint() }
    Connections {
        target: HopStyle
        function onBackgroundChanged() { root.requestPaint() }
    }
    function timeLabel(at) { return Qt.formatDateTime(new Date(at), geometry.span >= 3 * 86400000 ? "dd.MM" : geometry.span >= 86400000 ? "dd.MM HH:mm" : geometry.span < 300000 ? "HH:mm:ss" : "HH:mm") }
    function sampleText() {
        if (!selected) return ""
        const sample = samples[selected.index]
        if (!aggregated) return Qt.formatDateTime(new Date(Number(sample.at)), "dd.MM HH:mm:ss") + " · "
            + (selected.ok ? Math.round(sample.latencyMs) + qsTr(" мс") : qsTr("Нет ответа"))
        const format = sample.to - sample.from >= 60000 ? "dd.MM HH:mm" : "dd.MM HH:mm:ss"
        return Qt.formatDateTime(new Date(Number(sample.from)), format) + " — "
            + Qt.formatDateTime(new Date(Number(sample.to)), format) + "\n"
            + (selected.ok ? qsTr("Среднее: %1 мс · %2–%3 мс").arg(Math.round(sample.latencyMs)).arg(sample.minimum).arg(sample.maximum) : qsTr("Нет ответа"))
            + "\n" + qsTr("Замеров: %1 · Без ответа: %2").arg(sample.count).arg(sample.failures)
    }
    onGeometryChanged: { selectedIndex = -1; requestPaint() }
    onSelectedIndexChanged: requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d"), data = root.geometry
            ctx.clearRect(0, 0, width, height)
            ctx.font = "11px \"PT Root UI VF\""
            ctx.lineWidth = 1
            for (let i = 0; i < data.ticks.length; ++i) {
                const tick = data.ticks[i], y = tick.y
                ctx.strokeStyle = HopStyle.border
                ctx.beginPath(); ctx.moveTo(root.plotLeft, y); ctx.lineTo(root.plotLeft + root.plotWidth, y); ctx.stroke()
                ctx.fillStyle = HopStyle.muted; ctx.textAlign = "right"; ctx.textBaseline = "middle"
                ctx.fillText(Math.round(tick.value), root.plotLeft - 9, y)
            }
            ctx.textBaseline = "top"
            const ticks = data.span >= 3 * 86400000 ? Math.max(3, Math.min(8, Math.floor(width / 70))) : width >= 500 ? 5 : 3
            for (let i = 0; i < ticks; ++i) {
                ctx.textAlign = i === 0 ? "left" : i === ticks - 1 ? "right" : "center"
                ctx.fillText(root.timeLabel(data.start + data.span * i / (ticks - 1)),
                             root.plotLeft + root.plotWidth * i / (ticks - 1), root.failureTop + 12)
            }
            ctx.fillStyle = Qt.alpha(HopStyle.accent, 0.18)
            for (let i = 0; i < data.points.length; ++i) {
                const point = data.points[i]
                if (point.ok && point.low > point.high)
                    ctx.fillRect(point.from, point.high, Math.max(1, point.to - point.from), point.low - point.high)
            }
            for (let i = 0; i < data.segments.length; ++i) {
                const segment = data.segments[i], first = segment.points[0], last = segment.points[segment.points.length - 1]
                ctx.beginPath(); ctx.moveTo(first.x, first.y)
                for (let j = 0; j < segment.curves.length; ++j) {
                    const c = segment.curves[j]; ctx.bezierCurveTo(c.x1, c.y1, c.x2, c.y2, c.x, c.y)
                }
                ctx.lineTo(last.x, root.plotTop + root.plotHeight); ctx.lineTo(first.x, root.plotTop + root.plotHeight); ctx.closePath()
                const fill = ctx.createLinearGradient(0, root.plotTop, 0, root.plotTop + root.plotHeight)
                fill.addColorStop(0, Qt.alpha(HopStyle.accent, 0.22)); fill.addColorStop(1, Qt.alpha(HopStyle.accent, 0.02))
                ctx.fillStyle = fill; ctx.fill()
                // Линия поверх заливки остаётся отчётливой на малых значениях.
                ctx.beginPath(); ctx.moveTo(first.x, first.y)
                for (let j = 0; j < segment.curves.length; ++j) {
                    const c = segment.curves[j]; ctx.bezierCurveTo(c.x1, c.y1, c.x2, c.y2, c.x, c.y)
                }
                ctx.strokeStyle = HopStyle.accent; ctx.lineWidth = 2; ctx.stroke()
                if (segment.points.length === 1 || data.points.length <= 24) {
                    ctx.fillStyle = HopStyle.accent
                    for (let j = 0; j < segment.points.length; ++j) {
                        const point = segment.points[j]; ctx.beginPath(); ctx.arc(point.x, point.y, 2.5, 0, 2 * Math.PI); ctx.fill()
                    }
                }
            }
            ctx.fillStyle = HopStyle.danger
            for (let i = 0; i < data.points.length; ++i) {
                const point = data.points[i]
                if (!point.failureFraction) continue
                ctx.globalAlpha = 0.3 + 0.7 * point.failureFraction
                ctx.fillRect(point.from - (point.from === point.to ? 1 : 0), root.failureTop, Math.max(2, point.to - point.from), 6)
            }
            ctx.globalAlpha = 1
            if (root.selected) {
                const point = root.selected
                ctx.strokeStyle = HopStyle.muted; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(point.x, root.plotTop); ctx.lineTo(point.x, root.plotTop + root.plotHeight); ctx.stroke()
                if (point.ok) {
                    ctx.fillStyle = HopStyle.accent; ctx.beginPath(); ctx.arc(point.x, point.y, 4, 0, 2 * Math.PI); ctx.fill()
                }
            }
        }
    }
    MouseArea {
        objectName: "hopLatencyChartHover"
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        function selectPoint(mouse) {
            if (mouse.x < root.plotLeft || mouse.x > root.plotLeft + root.plotWidth || mouse.y > root.failureTop + 6) { root.selectedIndex = -1; return }
            let nearest = -1, distance = Infinity
            for (let i = 0; i < root.geometry.points.length; ++i) {
                const delta = Math.abs(root.geometry.points[i].x - mouse.x)
                if (delta < distance) { nearest = i; distance = delta }
            }
            const point = nearest >= 0 ? root.geometry.points[nearest] : null
            root.selectedIndex = point && (distance <= 12 || (mouse.x >= point.from && mouse.x <= point.to)) ? nearest : -1
        }
        onPositionChanged: function(mouse) { selectPoint(mouse) }
        onPressed: function(mouse) { selectPoint(mouse) }
        onExited: if (Qt.platform.os !== "android") root.selectedIndex = -1
    }
    Rectangle {
        visible: root.selected !== null
        x: Math.max(root.plotLeft, Math.min(root.selected ? root.selected.x + 12 : 0, root.width - width - 6))
        y: root.plotTop + 4
        width: Math.min(details.implicitWidth + 20, root.width - root.plotLeft - 8, 330)
        height: details.implicitHeight + 14
        color: HopStyle.surface
        border.color: HopStyle.border
        radius: HopStyle.radius
        Label {
            id: details
            objectName: "hopLatencyChartSample"
            anchors.centerIn: parent
            width: parent.width - 20
            text: root.sampleText()
            color: HopStyle.text
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }
    Label {
        anchors.centerIn: parent
        visible: root.samples.length > 0 && !root.geometry.hasResponses
        text: qsTr("Нет успешных ответов за выбранный период")
        color: HopStyle.muted
        width: root.plotWidth - 20
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
    }
}
