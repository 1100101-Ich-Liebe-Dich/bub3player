// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// A well-loved record sleeve: the cover with ring wear and worn edges, the
// record (in its paper inner sleeve) peeking out on the right.
Item {
    id: sl
    property var coverId: 0
    property string label: ""
    property real peek: 0.3        // 0..1 how far the record is pulled out
    property bool inner: true      // show the white inner sleeve instead of bare vinyl
    readonly property real side: Math.min(width / (1 + peek * 0.5), height)

    // record
    Item {
        x: sl.side * sl.peek * 0.5 + sl.side * 0.04
        y: (sl.height - sl.side) / 2 + sl.side * 0.04
        width: sl.side * 0.92
        height: width
        Rectangle { // inner paper sleeve
            visible: sl.inner
            anchors.fill: parent
            radius: 2
            color: "#EDE6D6"
            border.color: Qt.rgba(0, 0, 0, 0.2)
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.42; height: width; radius: width / 2
                color: "#D9D0BC"
            }
        }
        Rectangle { // bare vinyl
            visible: !sl.inner
            anchors.fill: parent
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#2a2a2c" }
                GradientStop { position: 0.5; color: "#0b0b0c" }
                GradientStop { position: 1; color: "#1c1c1e" }
            }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.34; height: width; radius: width / 2
                color: Theme.accent
                Rectangle { anchors.centerIn: parent; width: 6; height: 6; radius: 3; color: "#111" }
            }
        }
    }
    // sleeve
    Item {
        id: sleeve
        width: sl.side
        height: sl.side
        y: (sl.height - sl.side) / 2
        Rectangle { // shadow
            anchors.fill: parent
            anchors.topMargin: 6
            anchors.leftMargin: 3
            anchors.rightMargin: -5
            anchors.bottomMargin: -8
            radius: 3
            color: Qt.rgba(0, 0, 0, 0.42)
        }
        Rectangle { // cardboard
            anchors.fill: parent
            radius: 2
            color: "#d8ccb4"
        }
        CoverArt {
            anchors.fill: parent
            anchors.margins: 1
            radius: 1
            requestSize: 512
            coverId: sl.coverId
            label: sl.label
        }
        // ring wear: the outline of the record rubbed through the cardboard
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.9; height: width
            radius: width / 2
            color: "transparent"
            border.width: Math.max(2, parent.width * 0.012)
            border.color: Qt.rgba(1, 1, 1, 0.09)
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.86; height: width
            radius: width / 2
            color: "transparent"
            border.width: Math.max(1, parent.width * 0.006)
            border.color: Qt.rgba(1, 1, 1, 0.05)
        }
        // light falling from the upper left, worn edges
        Rectangle {
            anchors.fill: parent
            radius: 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.rgba(1, 0.95, 0.85, 0.08) }
                GradientStop { position: 0.6; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.12) }
            }
            border.width: 2
            border.color: Qt.rgba(1, 0.96, 0.88, 0.22)
        }
        // the opening of the sleeve on the right
        Rectangle {
            anchors.right: parent.right
            width: 2; height: parent.height
            color: Qt.rgba(0, 0, 0, 0.35)
        }
    }
}
