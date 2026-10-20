// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Halcyon

// Settings section: title, explanation, then the controls.
Column {
    id: sec
    property string title
    property string text
    default property alias content: body.data
    width: (parent ? parent.width : 600)
    spacing: 12
    Text {
        text: sec.title
        font.family: Theme.displayFont
        font.pixelSize: 24
        font.weight: Font.Bold
        font.capitalization: Theme.style.titleUpper ? Font.AllUppercase : Font.MixedCase
        font.letterSpacing: Theme.style.titleUpper ? 1.5 : 0
        color: Theme.text
    }
    Text {
        visible: sec.text !== ""
        width: sec.width
        wrapMode: Text.Wrap
        text: sec.text
        font.family: Theme.uiFont
        font.pixelSize: 14
        lineHeight: 1.25
        color: Theme.textDim
    }
    Column {
        id: body
        width: sec.width
        spacing: 14
        topPadding: 6
    }
}
