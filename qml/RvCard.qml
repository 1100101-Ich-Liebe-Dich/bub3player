// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// The card a slide of the year in review sits on, made of the theme's material:
// frosted glass (Glas), a soft raised surface (Soft), a dark veil with a glowing
// rim (Aurora), a paper note with tape (Vintage), a neon HUD panel (Synthwave), a
// display window behind smoked glass (Hi-Fi tower), a line box (Terminal). Klassik
// has none: its slides are colour fields.
Item {
    id: card
    property string title: ""            // the terminal writes it into the frame
    property int padding: 40
    default property alias content: inner.data
    readonly property string kind: window.look.card
    implicitWidth: inner.childrenRect.width + 2 * padding
    implicitHeight: inner.childrenRect.height + 2 * padding
    rotation: kind === "paper" ? -1.2 : 0

    // ---- the material ----
    GlassPanel {
        visible: card.kind === "glass"
        anchors.fill: parent
        backdrop: window.skin && window.skin.softTex ? window.skin.softTex : null
        ref: window.skin
        radius: 32
        blur: 0.8
        tint: GraphicsInfo.api === GraphicsInfo.Software ? Theme.panel : "#ffffff"
        tintAmount: 0.16
        shadowAlpha: 0.32
        rim: 1.0
        rim1: "#ffffff"
        rim2: Qt.rgba(1, 1, 1, 0.55)
    }
    NeuBox {
        visible: card.kind === "neu"
        anchors.fill: parent
        mode: "raised"
        radius: 36
        depth: 10
    }
    Rectangle { // Aurora: a dark veil, the light shines through the rim
        visible: card.kind === "aura"
        anchors.fill: parent
        radius: 34
        color: Theme.alpha(Theme.bg, 0.55)
        border.width: 1.5
        border.color: Theme.alpha(Theme.accent, 0.55)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -10
            z: -1
            radius: 44
            color: "transparent"
            border.width: 10
            border.color: Theme.alpha(Theme.accent, 0.08)
        }
    }
    Item { // Vintage: a note on cream paper, held by two strips of tape
        visible: card.kind === "paper"
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.topMargin: 8
            color: Qt.rgba(0, 0, 0, 0.35)
            radius: 2
        }
        Rectangle {
            anchors.fill: parent
            radius: 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#F4EAD2" }
                GradientStop { position: 1; color: "#E6D6B2" }
            }
            Repeater { // faint lines of a notepad
                model: Math.floor(parent.height / 34)
                Rectangle {
                    x: 24
                    y: 60 + index * 34
                    width: parent.width - 48
                    height: 1
                    color: Qt.rgba(0.35, 0.45, 0.6, 0.18)
                    visible: y < parent.height - 20
                }
            }
        }
        Repeater {
            model: 2
            Rectangle {
                x: index === 0 ? 40 : parent.width - 150
                y: -14
                width: 110
                height: 30
                rotation: index === 0 ? -6 : 5
                color: Qt.rgba(0.96, 0.93, 0.8, 0.7)
                border.color: Qt.rgba(0, 0, 0, 0.06)
            }
        }
    }
    HudPanel {
        visible: card.kind === "hud"
        anchors.fill: parent
    }
    Rectangle { // Hi-Fi tower: a display window
        visible: card.kind === "vfd"
        anchors.fill: parent
        radius: 10
        color: "#05080A"
        border.width: 3
        border.color: "#1E2328"
        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            radius: 8
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.06) }
                GradientStop { position: 0.4; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }
    }
    Item { // Terminal: a box of lines, the title set into the top border
        visible: card.kind === "box"
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 9
            color: Theme.bg
            border.color: Theme.text
            border.width: 1
        }
        Rectangle {
            visible: card.title !== ""
            x: 18
            width: boxTitle.implicitWidth + 10
            height: boxTitle.implicitHeight
            color: Theme.bg
            Text {
                id: boxTitle
                x: 5
                text: "┤ " + card.title + " ├"
                font.family: Theme.monoFont
                font.pixelSize: 15
                font.weight: Font.Bold
                color: Theme.text
            }
        }
    }

    Item {
        id: inner
        x: card.padding
        y: card.padding
        width: card.width - 2 * card.padding
        height: card.height - 2 * card.padding
    }
}
