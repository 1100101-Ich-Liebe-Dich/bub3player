// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Halcyon

// Single-line text input in the look of the active theme.
Rectangle {
    id: fld
    property alias text: input.text
    property alias input: input
    property string placeholder
    property bool numeric: false
    signal accepted()
    function focusInput() { input.forceActiveFocus(); input.selectAll() }

    implicitWidth: 220
    implicitHeight: 38
    radius: Math.min(19, Theme.radius + 9)
    color: Theme.alpha(Theme.text, 0.06)
    border.width: 1
    border.color: input.activeFocus ? Theme.accent : Theme.alpha(Theme.text, 0.1)

    TextField {
        id: input
        anchors.fill: parent
        leftPadding: 16
        rightPadding: 12
        placeholderText: fld.placeholder
        placeholderTextColor: Theme.textFaint
        color: Theme.text
        selectionColor: Theme.alpha(Theme.accent, 0.45)
        selectedTextColor: Theme.text
        font.family: Theme.uiFont
        font.pixelSize: 14
        background: null
        selectByMouse: true
        inputMethodHints: fld.numeric ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
        validator: fld.numeric ? numberCheck : null
        onAccepted: fld.accepted()
    }
    RegularExpressionValidator {
        id: numberCheck
        regularExpression: /-?[0-9]*([.,][0-9]*)?/
    }
}
