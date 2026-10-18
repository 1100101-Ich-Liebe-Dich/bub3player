import QtQuick
import Halcyon

// "BuB3Player" with the presenter line underneath. Every skin shows it at the top.
Column {
    id: brand
    property int nameSize: 23
    property color nameColor: Theme.text
    property color markColor: Theme.accent
    property color signatureColor: Theme.textFaint
    property color linkColor: Theme.textDim
    property string nameFont: Theme.displayFont
    property string signatureFont: Theme.uiFont
    property int signatureSize: 11
    property int nameWeight: Font.Bold
    property real nameSpacing: -0.3
    property bool upper: false
    property bool showMark: true
    property bool oneLine: false
    property int maxWidth: 220
    spacing: 4

    Row {
        spacing: 10
        Row {
            visible: brand.showMark
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Repeater {
                model: [0.45, 0.85, 0.6]
                Rectangle {
                    width: Math.max(3, Math.round(brand.nameSize * 0.17))
                    height: Math.round(brand.nameSize * modelData)
                    radius: width / 2
                    anchors.bottom: parent.bottom
                    color: brand.markColor
                }
            }
        }
        Text {
            text: brand.upper ? "BUB3PLAYER" : "BuB3Player"
            font.family: brand.nameFont
            font.pixelSize: brand.nameSize
            font.weight: brand.nameWeight
            font.letterSpacing: brand.nameSpacing
            color: brand.nameColor
        }
    }
    Text {
        width: brand.oneLine ? implicitWidth : Math.min(implicitWidth, brand.maxWidth)
        wrapMode: brand.oneLine ? Text.NoWrap : Text.WordWrap
        textFormat: Text.StyledText
        linkColor: brand.linkColor
        text: qsTr("Projekt präsentiert von") + (brand.oneLine ? " " : "<br>")
              + "<a href=\"https://www.bauenundbooten.de\">www.BAUENUNDBOOTEN.de</a>"
        font.family: brand.signatureFont
        font.pixelSize: brand.signatureSize
        font.weight: Font.Medium
        color: brand.signatureColor
        onLinkActivated: (link) => Qt.openUrlExternally(link)
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }
}
