import QtQuick
import Halcyon

// Label (+ current value) above a control.
Column {
    id: opt
    property string label
    property string value
    property string hint
    default property alias control: holder.data
    width: (parent ? parent.width : 600)
    spacing: 8
    opacity: enabled ? 1 : 0.5
    Row {
        spacing: 10
        Text {
            text: opt.label
            font.family: Theme.uiFont
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Text {
            visible: opt.value !== ""
            text: opt.value
            font.family: Theme.uiFont
            font.pixelSize: 14
            color: Theme.accent
        }
    }
    Item {
        id: holder
        width: parent.width
        implicitHeight: childrenRect.height
        height: childrenRect.height
    }
    Text {
        visible: opt.hint !== ""
        width: parent.width
        wrapMode: Text.Wrap
        text: opt.hint
        font.family: Theme.uiFont
        font.pixelSize: 13
        color: Theme.textFaint
    }
}
