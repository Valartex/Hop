import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0
import "../Controls2"
import "../Components"

PageType {
    id: root
    ScrollView {
        id: routesScroll
        anchors.fill: parent
        anchors.margins: HopStyle.mobile ? 16 : 20
        anchors.topMargin: HopStyle.mobile ? 4 : 20
        contentWidth: availableWidth
        clip: true
    ColumnLayout {
        width: routesScroll.availableWidth
        height: HopStyle.mobile ? implicitHeight : Math.max(implicitHeight, routesScroll.availableHeight)
        Label { visible: !HopStyle.mobile; text: qsTr("Маршруты"); color: HopStyle.text; font.pixelSize: 22; Layout.bottomMargin: 8 }
        HopRoutesEditor { policy: RoutingPolicy; Layout.fillWidth: true; Layout.fillHeight: !HopStyle.mobile }
    }
    }
}
