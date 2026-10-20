// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// 3-way speaker of the mini system: tweeter, midrange, a woofer that pumps
// with the beat inside a ring of LEDs, and a bass reflex port.
Item {
    id: sp
    property color ink: "#45E3FF"
    property real beat: 0
    property real energy: 0
    property real phase: 0         // seconds, drives the LED chaser
    property bool lights: true
    property string side: "L"
    readonly property real w: width

    Rectangle { anchors.fill: parent; anchors.topMargin: 8; anchors.bottomMargin: -10; anchors.leftMargin: 4; anchors.rightMargin: -4; radius: 14; color: Qt.rgba(0, 0, 0, 0.55) }
    Material { anchors.fill: parent; material: "plastic"; radius: 14; seed: sp.side === "L" ? 7 : 9 }
    Rectangle { // side bevel
        anchors.fill: parent
        radius: 14
        color: "transparent"
        border.color: "#2e3237"
        border.width: 2
    }
    Rectangle { // chrome trim
        x: 14; width: parent.width - 28; y: 0; height: 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "#00ffffff" }
            GradientStop { position: 0.5; color: "#b8c0c8" }
            GradientStop { position: 1; color: "#00ffffff" }
        }
    }

    // tweeter plate
    Rectangle {
        id: tweetPlate
        anchors.horizontalCenter: parent.horizontalCenter
        y: sp.height * 0.05
        width: sp.w * 0.5; height: sp.w * 0.3
        radius: height / 2
        gradient: Gradient {
            GradientStop { position: 0; color: "#2c2f34" }
            GradientStop { position: 1; color: "#111214" }
        }
        border.color: "#3a3e44"
        Driver { anchors.centerIn: parent; width: parent.height * 0.72; dome: true; pump: sp.beat * 0.01 }
    }
    Text {
        id: badge
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: tweetPlate.bottom
        anchors.topMargin: 6
        text: "BuB3"
        font.family: "Barlow Condensed"; font.pixelSize: Math.round(sp.w * 0.06); font.weight: Font.Black; font.italic: true; font.letterSpacing: 2
        color: "#c9d0d7"
    }
    // midrange
    Driver {
        id: mid
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: badge.bottom
        anchors.topMargin: 8
        width: sp.w * 0.42
        pump: sp.energy * 0.015 + sp.beat * 0.01
    }
    // woofer with LED ring
    Item {
        id: wooferBox
        anchors.horizontalCenter: parent.horizontalCenter
        y: sp.height * 0.42
        width: sp.w * 0.86; height: width
        Repeater {
            model: 28
            Rectangle {
                readonly property real a: index / 28 * Math.PI * 2
                readonly property bool chase: Math.floor(sp.phase * 14) % 28 === index || Math.floor(sp.phase * 14 + 14) % 28 === index
                readonly property real lvl: sp.lights ? Math.max(sp.beat, chase ? 1 : 0) : 0
                x: wooferBox.width / 2 + Math.cos(a) * wooferBox.width * 0.5 - 3
                y: wooferBox.height / 2 + Math.sin(a) * wooferBox.height * 0.5 - 3
                width: 6; height: 6; radius: 3
                color: Qt.rgba(sp.ink.r, sp.ink.g, sp.ink.b, 0.15 + 0.85 * lvl)
            }
        }
        SoftGlow {
            anchors.centerIn: parent
            width: parent.width * 1.25; height: width
            opacity: sp.lights ? sp.beat * 0.7 : 0
            color: Qt.rgba(sp.ink.r, sp.ink.g, sp.ink.b, 0.5)
        }
        Driver { anchors.centerIn: parent; width: parent.width * 0.9; pump: sp.beat * 0.06 + sp.energy * 0.02; label: "SUPER BASS" }
    }
    // bass reflex port
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: sp.height * 0.86
        width: sp.w * 0.5; height: sp.w * 0.13
        radius: height / 2
        gradient: Gradient {
            GradientStop { position: 0; color: "#3a3e44" }
            GradientStop { position: 1; color: "#15171a" }
        }
        Rectangle {
            anchors.fill: parent; anchors.margins: 4
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#000000" }
                GradientStop { position: 1; color: "#141518" }
            }
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: sp.height * 0.015
        text: "3-WAY BASS REFLEX  ·  120 W"
        font.family: "Barlow Condensed"; font.pixelSize: Math.max(8, Math.round(sp.w * 0.042)); font.weight: Font.Bold; font.letterSpacing: 1
        color: "#8c949d"
    }

    // A cone driver: chrome ring, rubber surround, ribbed cone, dust cap.
    component Driver: Item {
        id: drv
        property real pump: 0
        property bool dome: false
        property string label: ""
        height: width
        Rectangle { // chrome mounting ring
            anchors.fill: parent
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#e4e8ec" }
                GradientStop { position: 0.5; color: "#6d737a" }
                GradientStop { position: 1; color: "#c3c9cf" }
            }
        }
        Item {
            anchors.fill: parent
            anchors.margins: drv.width * 0.045
            scale: 1 + drv.pump
            Behavior on scale { NumberAnimation { duration: 60 } }
            Rectangle { // surround
                anchors.fill: parent
                radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: "#2b2d31" }
                    GradientStop { position: 1; color: "#0c0d0f" }
                }
            }
            Rectangle { // cone
                anchors.fill: parent
                anchors.margins: drv.dome ? parent.width * 0.06 : parent.width * 0.09
                radius: width / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: drv.dome ? "#d9dde2" : "#3b3e44" }
                    GradientStop { position: 1; color: drv.dome ? "#6f757c" : "#16171a" }
                }
                Repeater { // ribs
                    model: drv.dome ? 0 : 3
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * (0.82 - index * 0.18); height: width
                        radius: width / 2
                        color: "transparent"
                        border.color: Qt.rgba(1, 1, 1, 0.05)
                    }
                }
                Rectangle { // dust cap
                    visible: !drv.dome
                    anchors.centerIn: parent
                    width: parent.width * 0.32; height: width
                    radius: width / 2
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#f0f2f4" }
                        GradientStop { position: 1; color: "#5d636a" }
                    }
                }
                Text {
                    visible: drv.label !== ""
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: parent.height * 0.72
                    text: drv.label
                    font.family: "Barlow Condensed"; font.pixelSize: Math.max(7, Math.round(parent.width * 0.06)); font.weight: Font.Black; font.italic: true; font.letterSpacing: 1.5
                    color: "#9aa2ab"
                }
            }
        }
    }
}
