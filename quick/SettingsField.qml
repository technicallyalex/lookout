import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: field
    required property var spec
    required property var values
    required property var state
    required property var api
    readonly property var current: spec.stateKey ? state[spec.stateKey] : values[spec.key]
    Layout.minimumHeight: field.spec.kind === "preview" ? 32 : 0
    spacing: 14

    Glyph {
        visible: !!field.spec.icon && field.spec.kind !== "command"
        Layout.preferredWidth: 19
        Layout.preferredHeight: 19
        sourceSize: Qt.size(64, 64)
        role: field.spec.icon || ""
        ink: field.state.dark ? "#f1f3f7" : "#20242a"
        dark: !!field.state.dark
    }
    ColumnLayout {
        visible: field.spec.kind !== "command" && field.spec.kind !== "sizes"
        Layout.fillWidth: true
        spacing: 2
        Label {
            Layout.fillWidth: true
            text: field.spec.label || ""
            font.bold: !!field.spec.heading
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: field.spec.note || ""
            color: field.state.dark ? "#b0b8c6" : "#515a67"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }
    Switch {
        id: settingSwitch
        visible: field.spec.kind === "toggle"
        enabled: !field.spec.disabled
        checked: !!field.current
        implicitWidth: implicitIndicatorWidth + leftPadding + rightPadding
        rightPadding: 0
        Accessible.name: field.spec.label || ""
        indicator: Rectangle {
            id: track
            implicitWidth: 40
            implicitHeight: 22
            x: settingSwitch.leftPadding + (settingSwitch.availableWidth - width) / 2
            y: settingSwitch.topPadding + (settingSwitch.availableHeight - height) / 2
            radius: height / 2
            color: settingSwitch.checked ? settingSwitch.palette.highlight :
                Qt.tint(settingSwitch.palette.window, field.state.dark ? "#42ffffff" : "#61000000")
            opacity: settingSwitch.enabled ? 1 : 0.5

            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                visible: settingSwitch.visualFocus
                color: "transparent"
                border.color: settingSwitch.palette.highlight
                radius: height / 2
            }
            Rectangle {
                width: track.height - 6
                height: width
                x: 3 + settingSwitch.visualPosition * (track.width - width - 6)
                anchors.verticalCenter: parent.verticalCenter
                radius: width / 2
                color: "#ffffff"
                border.color: "#26000000"
                Behavior on x {
                    enabled: !settingSwitch.pressed
                    NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                }
            }
        }
        onToggled: {
            if (field.spec.stateKey) field.api.setting(field.spec.key, checked);
            else field.api.setting("set", checked, field.spec.key);
        }
    }
    ComboBox {
        objectName: "choiceControl"
        visible: field.spec.kind === "choice"
        enabled: !field.spec.disabled
        Layout.preferredWidth: Math.max(180, Math.min(240, implicitContentWidth + 36))
        model: field.spec.options || []
        currentIndex: field.spec.ids ? field.spec.ids.indexOf(field.current) : Number(field.current || 0)
        Accessible.name: field.spec.label || ""
        onActivated: function(index) {
            field.api.setting("set", field.spec.ids ? field.spec.ids[index] : index, field.spec.key)
        }
    }
    SpinBox {
        id: spin
        objectName: "numberControl"
        visible: field.spec.kind === "number"
        enabled: !field.spec.disabled
        Layout.preferredWidth: 128
        readonly property int factor: field.spec.factor || 1
        from: Math.round((field.spec.min || 0) * factor)
        to: Math.round((field.spec.max || 100) * factor)
        value: Math.round(Number(field.current || 0) * factor)
        editable: true
        textFromValue: function(n) { return String(n / factor); }
        valueFromText: function(s) { return Math.round(Number(s) * factor); }
        validator: DoubleValidator { bottom: spin.from / spin.factor; top: spin.to / spin.factor }
        Accessible.name: field.spec.label || ""
        onValueModified: field.api.setting("set", value / factor, field.spec.key)
    }
    RowLayout {
        visible: field.spec.kind === "color"
        Layout.fillWidth: false
        Layout.preferredWidth: 128
        spacing: 6
        ToolButton {
            id: swatch
            Layout.preferredWidth: 26
            Layout.preferredHeight: 26
            Accessible.name: "Choose " + (field.spec.label || "")
            ToolTip.visible: hovered
            ToolTip.text: "Choose color"
            background: Rectangle {
                radius: 5
                color: field.current || "transparent"
                border.color: swatch.visualFocus ? swatch.palette.highlight : swatch.palette.mid
                border.width: swatch.visualFocus ? 2 : 1
            }
            onClicked: field.api.chooseGlowColor(field.spec.key, hex.acceptableInput ? hex.colorCode : field.current)
        }
        TextField {
            id: hex
            readonly property string colorCode: text.startsWith("#") ? text : "#" + text
            Layout.preferredWidth: 96
            text: field.current || ""
            validator: RegularExpressionValidator { regularExpression: /^#?[0-9A-Fa-f]{6}$/ }
            Accessible.name: field.spec.label || ""
            onEditingFinished: if (acceptableInput) field.api.setting("set", colorCode, field.spec.key)
        }
    }
    Button {
        visible: field.spec.kind === "action"
        enabled: !field.spec.disabled
        text: field.spec.button || field.spec.label
        onClicked: field.api.setting(field.spec.key, field.spec.value)
    }
    Item {
        visible: field.spec.kind === "command" && !!field.spec.alignRight
        Layout.fillWidth: true
    }
    Button {
        id: command
        visible: field.spec.kind === "command"
        enabled: !field.spec.disabled
        Layout.fillWidth: !!field.spec.primary || !!field.spec.icon
        Layout.alignment: field.spec.alignRight ? Qt.AlignRight : Qt.AlignLeft
        Layout.preferredWidth: field.spec.alignRight ? 128 : implicitWidth
        Layout.preferredHeight: field.spec.alignRight ? 30 : implicitHeight
        Layout.minimumHeight: field.spec.icon ? 40 : implicitHeight
        highlighted: !!field.spec.primary
        text: field.spec.label || ""
        icon.source: visible && field.spec.icon ? "qrc:/lookout/icons/" + (field.state.dark ? "dark/" : "light/") + field.spec.icon + ".svg" : ""
        icon.width: 18
        icon.height: 18
        background: Rectangle {
            radius: 7
            color: command.down ? command.palette.mid : command.highlighted ?
                Qt.tint(command.palette.button, Qt.rgba(command.palette.highlight.r, command.palette.highlight.g, command.palette.highlight.b, .3)) :
                command.hovered ? Qt.tint(command.palette.button, "#18ffffff") : command.palette.button
            border.color: command.highlighted || command.visualFocus ? command.palette.highlight :
                field.state.dark ? "#626b79" : "#8d96a3"
        }
        onClicked: field.api.setting(field.spec.key, field.spec.value)
    }
    Flow {
        visible: field.spec.kind === "sizes"
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: 5
        Repeater {
            model: field.spec.kind === "sizes" ? field.spec.options : []
            delegate: Button {
                required property string modelData
                required property int index
                implicitWidth: Math.max(56, implicitContentWidth + 22)
                text: modelData
                highlighted: field.current === index
                onClicked: field.api.setting("set", index, field.spec.key)
            }
        }
    }
    Button {
        objectName: field.spec.transitionPreview ? "transitionPreview" : "alertPreview"
        visible: field.spec.kind === "preview" || !!field.spec.thumbnailPreview || !!field.spec.transitionPreview
        enabled: !field.spec.transitionPreview || !field.state.transitionPreview
        Layout.preferredWidth: 92
        readonly property bool stopping: !!field.spec.thumbnailPreview && !!field.state.thumbnailAlertPreview
        text: stopping ? "Stop" : "Preview"
        icon.source: visible && !stopping ? "qrc:/lookout/icons/play.svg" : ""
        icon.width: 14
        icon.height: 14
        Accessible.name: field.spec.thumbnailPreview
            ? (stopping ? "Stop thumbnail alert preview" : "Preview thumbnail alert icon")
            : field.spec.transitionPreview ? "Preview show and hide transition" : "Preview " + field.spec.label
        onClicked: field.api.setting(field.spec.transitionPreview ? "previewTransition" :
                                    field.spec.thumbnailPreview ? "previewThumbnailAlert" : "previewAlert", field.spec.value)
    }
    RowLayout {
        visible: field.spec.kind === "glowPreview"
        spacing: 6
        Button {
            text: "Warning"
            Accessible.name: "Preview warning glow"
            onClicked: field.api.setting("previewGlow", false)
        }
        Button {
            text: "Critical"
            Accessible.name: "Preview critical glow"
            onClicked: field.api.setting("previewGlow", true)
        }
    }
    RowLayout {
        visible: field.spec.kind === "shortcut"
        Button {
            Layout.preferredWidth: 140
            text: field.api.shortcutIndex === field.spec.index ? "Press shortcut…" :
                ((field.state.shortcuts || [])[field.spec.index] || "Assign")
            onClicked: field.api.captureShortcut(field.spec.index)
        }
        Button {
            text: "Clear"
            onClicked: field.api.clearShortcut(field.spec.index)
        }
    }
}
