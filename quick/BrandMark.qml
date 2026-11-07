import QtQuick

Item {
    id: mark
    property bool night: false
    property bool play: false
    property real phase: 0
    readonly property int frame: Math.round(phase * 128) % 128

    function transition() {
        const wasRunning = animation.running;
        animation.stop();
        if (!play)
            return;
        phase = wasRunning ? phase % 1 : (night ? 0 : 0.5);
        let end = night ? 0.5 : 1;
        if (end < phase)
            end += 1;
        animation.to = end;
        animation.duration = Math.round((end - phase) * 6200);
        animation.start();
    }

    onPlayChanged: if (!play) animation.stop()
    onNightChanged: if (play) transition()

    Image {
        anchors.fill: parent
        source: mark.night ? "qrc:/lookout/brand/moon.svg" : "qrc:/lookout/brand/light.svg"
        sourceSize: Qt.size(width * 2, height * 2)
        fillMode: Image.PreserveAspectFit
        visible: !animation.running
    }
    Item {
        anchors.fill: parent
        clip: true
        visible: animation.running
        Image {
            source: "qrc:/quick/cycle.png"
            width: mark.width * 16
            height: mark.height * 8
            x: -(mark.frame % 16) * mark.width
            y: -Math.floor(mark.frame / 16) * mark.height
            smooth: true
        }
    }
    NumberAnimation {
        id: animation
        target: mark
        property: "phase"
        easing.type: Easing.InOutSine
    }
}
