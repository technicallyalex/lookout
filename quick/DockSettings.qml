import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var controller
    required property var options
    required property var order
    required property bool showLabels
    required property color surface
    required property color edge
    required property color ink
    required property bool dark
    spacing: 12

    readonly property var onDock: order.map(id => options.find(option => option.id === id)).filter(Boolean)
    readonly property var available: options.filter(option => !order.includes(option.id))

    RowLayout {
        Layout.fillWidth: true
        Label {
            text: "Button display"
            Layout.fillWidth: true
        }
        ComboBox {
            objectName: "dockLabelMode"
            Layout.preferredWidth: 180
            model: ["Icons only", "Icons and text labels"]
            currentIndex: editor.showLabels ? 1 : 0
            Accessible.name: "Button display"
            onActivated: index => editor.controller.setting("dockLabels", index === 1)
        }
    }

    Rectangle {
        id: card
        property int draggedIndex: -1
        property int targetIndex: -1
        Layout.fillWidth: true
        implicitHeight: rows.implicitHeight + 28
        radius: 10
        color: editor.surface
        border.color: editor.edge

        function targetAt(y) {
            for (let i = 0; i < editor.onDock.length; ++i) {
                const row = entries.itemAt(i)
                if (y < row.y + row.height)
                    return i
            }
            return editor.onDock.length - 1
        }

        function endDrag() {
            draggedIndex = -1
            targetIndex = -1
        }

        ColumnLayout {
            id: rows
            anchors.fill: parent
            anchors.margins: 14
            spacing: 6
            Label {
                text: "Dock buttons"
                font.bold: true
            }
            Label {
                text: "Drag to reorder"
                color: editor.dark ? "#b0b8c6" : "#515a67"
                font.pixelSize: 12
                Layout.bottomMargin: 4
            }
            Repeater {
                id: entries
                model: editor.onDock.concat(editor.available)
                delegate: Item {
                    id: entry
                    required property var modelData
                    required property int index
                    readonly property bool placed: index < editor.onDock.length
                    objectName: "dockButton_" + modelData.id
                    Layout.fillWidth: true
                    Layout.minimumHeight: 32
                    implicitHeight: row.implicitHeight
                    RowLayout {
                        id: row
                        anchors.fill: parent
                        spacing: 8
                        ToolButton {
                            id: handle
                            objectName: "dockDragHandle"
                            visible: entry.placed
                            enabled: editor.onDock.length > 1
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 26
                            Accessible.name: "Reorder " + entry.modelData.label
                            Accessible.description: "Drag or use the Up and Down keys."
                            Keys.onUpPressed: editor.controller.setting("dockButton", "up", entry.modelData.id)
                            Keys.onDownPressed: editor.controller.setting("dockButton", "down", entry.modelData.id)
                            contentItem: Item {
                                Grid {
                                    anchors.centerIn: parent
                                    columns: 2
                                    spacing: 3
                                    Repeater {
                                        model: 6
                                        Rectangle {
                                            width: 3
                                            height: 3
                                            radius: 1.5
                                            color: editor.ink
                                            opacity: grip.pressed || grip.containsMouse || handle.visualFocus ? 1 : 0.55
                                        }
                                    }
                                }
                            }
                            MouseArea {
                                id: grip
                                anchors.fill: parent
                                hoverEnabled: true
                                preventStealing: true
                                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                onPressed: {
                                    handle.forceActiveFocus()
                                    card.draggedIndex = entry.index
                                    card.targetIndex = entry.index
                                }
                                onPositionChanged: mouse => {
                                    if (pressed)
                                        card.targetIndex = card.targetAt(mapToItem(rows, mouse.x, mouse.y).y)
                                }
                                onReleased: mouse => {
                                    const target = card.targetAt(mapToItem(rows, mouse.x, mouse.y).y)
                                    card.endDrag()
                                    if (target >= 0 && target !== entry.index)
                                        editor.controller.setting("dockButton", target, entry.modelData.id)
                                }
                                onCanceled: card.endDrag()
                            }
                        }
                        Item {
                            visible: !entry.placed
                            Layout.preferredWidth: 24
                        }
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
                        SettingsSwitch {
                            objectName: "dockButtonSwitch"
                            dark: editor.dark
                            checked: entry.placed
                            Accessible.name: "Show on dock: " + entry.modelData.label
                            onToggled: editor.controller.setting("dockButton", checked ? "add" : "remove", entry.modelData.id)
                        }
                    }
                    Rectangle {
                        visible: card.targetIndex === entry.index && card.draggedIndex !== entry.index
                        width: parent.width
                        height: 2
                        y: card.draggedIndex < entry.index ? parent.height + 2 : -4
                        color: handle.palette.highlight
                        radius: 1
                    }
                }
            }
        }
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
