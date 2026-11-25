import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var updater
    required property string currentVersion
    required property string currentNotes
    readonly property var state: updater ? updater.state : ({})
    spacing: 12
    RowLayout {
        Layout.fillWidth: true
        Label {
            objectName: "installedVersion"
            Layout.fillWidth: true
            text: "lookout " + root.currentVersion
            font.pixelSize: 18
            font.bold: true
        }
        Button {
            objectName: "checkUpdates"
            text: "Check for updates"
            enabled: !!root.state.canCheck
            onClicked: root.updater.check()
        }
    }
    Label {
        visible: text.length > 0
        Layout.fillWidth: true
        text: root.state.status || ""
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }
    ProgressBar {
        Layout.fillWidth: true
        visible: !!root.state.busy
        indeterminate: root.state.progress < 0
        from: 0
        to: 100
        value: root.state.progress || 0
    }
    RowLayout {
        Button {
            objectName: "installUpdate"
            text: "Download and install after OBS closes"
            visible: !!root.state.version
            enabled: !!root.state.canInstall
            onClicked: root.updater.install()
        }
    }
    Label {
        visible: !!root.state.version
        text: "Patch notes · lookout " + (root.state.version || "")
        font.bold: true
    }
    Label {
        objectName: "updatePatchNotes"
        Layout.fillWidth: true
        visible: !!root.state.version
        text: root.state.notes || "No patch notes provided."
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.topMargin: 8
        text: "Patch notes"
        font.bold: true
    }
    Label {
        objectName: "currentPatchNotes"
        Layout.fillWidth: true
        text: root.currentNotes
        textFormat: Text.MarkdownText
        wrapMode: Text.WordWrap
    }
}
