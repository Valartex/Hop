import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Style 1.0
import "../Controls2"
import "../Components"

PageType {
    id: root
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        Label { text: qsTr("Маршруты"); color: HopStyle.text; font.pixelSize: 22; Layout.bottomMargin: 8 }
        HopRoutesEditor { policy: RoutingPolicy; Layout.fillWidth: true; Layout.fillHeight: true }
    }
}
