// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// How statistics and the year in review look in the current theme (window.look):
// the kind of bars and cards, the fonts for big numbers, how slides change.
QtObject {
    readonly property string skin: Theme.skin

    // bars: "plain", "blocks" (terminal), "led" (Hi-Fi tower), "lamp" (Vintage), "neu" (Soft)
    readonly property string bars: ({ terminal: "blocks", tower: "led", vintage: "lamp", soft: "neu" })[skin] || "plain"
    // cards around numbers and lists in the review
    readonly property string card: ({ klassik: "none", soft: "neu", glas: "glass", aurora: "aura", vintage: "paper",
                                      synthwave: "hud", tower: "vfd", terminal: "box" })[skin] || "none"
    // how a slide arrives: "rise", "zoom", "glitch", "flicker", "type", "slide"
    readonly property string enter: ({ klassik: "zoom", soft: "rise", glas: "rise", aurora: "rise", vintage: "slide",
                                       synthwave: "glitch", tower: "flicker", terminal: "type" })[skin] || "rise"

    readonly property bool upper: skin === "synthwave" || skin === "tower"
    readonly property bool square: skin === "terminal" || skin === "tower"
    readonly property bool monoLabels: skin === "terminal" || skin === "tower"
    readonly property bool glow: skin === "aurora" || skin === "synthwave" || skin === "tower"

    // big numbers: seven segments on the tower, the theme's display font elsewhere
    readonly property string numberFont: skin === "tower" ? "DSEG7 Classic" : skin === "terminal" ? "VT323" : Theme.displayFont
    readonly property int numberWeight: skin === "tower" || skin === "terminal" || skin === "vintage" ? Font.Normal : Font.Black
    readonly property string handFont: skin === "vintage" ? "Permanent Marker" : Theme.uiFont

    // bar colours (plain bars)
    readonly property color barTop: skin === "synthwave" ? Theme.accent2 : skin === "glas" ? Theme.alpha("white", 0.75)
                                  : skin === "aurora" ? Qt.lighter(Theme.accent, 1.3) : Theme.accent
    readonly property color barBottom: skin === "synthwave" ? Theme.accent : skin === "glas" ? Theme.alpha("white", 0.35)
                                     : skin === "aurora" ? Theme.accent2 : Theme.accent
    readonly property color barBorder: skin === "glas" ? Theme.alpha("white", 0.6) : skin === "synthwave" ? Theme.alpha(Theme.accent2, 0.9) : "transparent"
    readonly property real dimBars: skin === "glas" ? 0.7 : 0.55

    // a decade by name: "80er", but "2000er" and "1910er" in full
    function decade(x) { return (x >= 2000 || x < 1920 ? String(x) : String(x).slice(2)) + "er" }

    // ink on Vintage's paper (cards of the review, the picture of the year)
    readonly property color paperInk: "#2B2118"
    readonly property color paperDim: "#6B5A44"
    readonly property color paperHi: "#B4471C"
}
