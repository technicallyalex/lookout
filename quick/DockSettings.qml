import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var controller
    required property var options
    required property var order
    required property color surface
    required property color edge
    required property color ink
    required property bool dark
    spacing: 12

    readonly property var onDock: order.map(id => options.find(option => option.id === id)).filter(Boolean)
    readonly property var available: options.filter(option => !order.includes(option.id))

    component ButtonList: Rectangle {
        id: card
        required property bool placed
        required property var buttons
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.alignment: Qt.AlignTop
        implicitHeight: rows.implicitHeight + 28
        radius: 10
        color: editor.surface
        border.color: editor.edge

        ColumnLayout {
            id: rows
            anchors.fill: parent
            anchors.margins: 14
            spacing: 6
            Label {
                text: card.placed ? "On the dock" : "Available buttons"
                font.bold: true
                Layout.bottomMargin: 4
            }
            Label {
                visible: card.buttons.length === 0
                text: card.placed ? "No buttons on the dock." : "All buttons are on the dock."
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: card.buttons
                delegate: RowLayout {
                    id: entry
                    required property var modelData
                    required property int index
                    objectName: "dockButton_" + modelData.id
                    Layout.fillWidth: true
                    Layout.minimumHeight: 28
                    spacing: 6
                    Glyph {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        sourceSize: Qt.size(54, 54)
                        role: entry.modelData.icon
                        dark: editor.dark
                        ink: editor.ink
                    }
                    Label {
                        Layout.fillWidth: true
                        text: entry.modelData.label
                        wrapMode: Text.WordWrap
                    }
                    ToolButton {
                        objectName: "moveDockButtonUp"
                        visible: card.placed
                        enabled: entry.index > 0
                        Layout.preferredWidth: 26
                        text: "↑"
                        font.pixelSize: 16
                        Accessible.name: "Move up: " + entry.modelData.label
                        ToolTip.visible: hovered
                        ToolTip.text: "Move up"
                        onClicked: editor.controller.setting("dockButton", "up", entry.modelData.id)
                    }
                    ToolButton {
                        objectName: "moveDockButtonDown"
                        visible: card.placed
                        enabled: entry.index + 1 < card.buttons.length
                        Layout.preferredWidth: 26
                        text: "↓"
                        font.pixelSize: 16
                        Accessible.name: "Move down: " + entry.modelData.label
                        ToolTip.visible: hovered
                        ToolTip.text: "Move down"
                        onClicked: editor.controller.setting("dockButton", "down", entry.modelData.id)
                    }
                    Button {
                        objectName: card.placed ? "removeDockButton" : "addDockButton"
                        text: card.placed ? "Remove" : "Add"
                        Accessible.name: text + " " + entry.modelData.label
                        onClicked: editor.controller.setting("dockButton", card.placed ? "remove" : "add", entry.modelData.id)
                    }
                }
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: width >= 680 ? 2 : 1
        columnSpacing: 12
        rowSpacing: 12
        ButtonList { placed: true; buttons: editor.onDock }
        ButtonList { placed: false; buttons: editor.available }
    }
    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: "Open settings from Tools → lookout Settings."
            wrapMode: Text.WordWrap
        }
        Button {
            objectName: "resetDockButtons"
            text: "Restore defaults"
            onClicked: editor.controller.setting("dockButton", "reset")
        }
    }
}
