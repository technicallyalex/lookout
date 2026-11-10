import QtQuick
import QtQuick.Controls

Switch {
    id: control
    required property bool dark
    implicitWidth: implicitIndicatorWidth + leftPadding + rightPadding
    rightPadding: 0

    indicator: Rectangle {
        id: track
        implicitWidth: 40
        implicitHeight: 22
        x: control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? control.palette.highlight :
            Qt.tint(control.palette.window, control.dark ? "#42ffffff" : "#61000000")
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            visible: control.visualFocus
            color: "transparent"
            border.color: control.palette.highlight
            radius: height / 2
        }
        Rectangle {
            width: track.height - 6
            height: width
            x: 3 + control.visualPosition * (track.width - width - 6)
            anchors.verticalCenter: parent.verticalCenter
            radius: width / 2
            color: "#ffffff"
            border.color: "#26000000"
            Behavior on x {
                enabled: !control.pressed
                NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
            }
        }
    }
}
