import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes

Item {
    id: chart
    property var model: ({})
    property var scene: ({})
    property real pulse: 0
    readonly property string design: scene.design || "obs"
    readonly property bool mini: !!model.compact
    readonly property bool hasPlot: model.graphs === true
    readonly property bool minimal: scene.chartDetail === 1
    readonly property bool layered: scene.chartLines === "area"
    readonly property bool signalStyle: scene.chartLines === "stepped"
    readonly property bool studio: scene.chartLines === "grid"
    readonly property real unit: scene.scale || 1
    readonly property real fontSize: scene.fontPx || 14
    readonly property color ink: scene.text || "white"
    readonly property color surface: scene.top || "#303030"
    readonly property color muted: design === "obs" ?
        Qt.tint(ink, Qt.rgba(surface.r, surface.g, surface.b, .16)) : scene.muted || "#aab1bf"
    readonly property color accent: scene.accent || "#87b6ff"
    readonly property color alert: scene.glowColor || "#ff525b"
    readonly property color valueInk: model.flash ? Qt.tint(model.headerColor || ink,
        Qt.rgba(alert.r, alert.g, alert.b, pulse)) : model.headerColor || ink
    readonly property real low: Number(model.min || 0)
    readonly property real high: Math.max(low + 0.00001, Number(model.max || 1))
    readonly property var readings: model.readings || []
    readonly property var history: model.series || []
    readonly property real padding: 9 * unit
    readonly property real axisFont: Math.max(8, fontSize * .78)

    function axisValue(value) {
        return Number((value / (model.axisScale || 1)).toPrecision(4)).toString();
    }

    function vertical(value, height) {
        return height * (1 - Math.max(0, Math.min(1, (value - low) / (high - low))));
    }

    function tracePath(points, width, height, fill) {
        const paths = [];
        let segment = [];
        for (const sample of points) {
            if (sample[1] === null || sample[1] === undefined) {
                if (segment.length) paths.push(segment);
                segment = [];
            } else {
                segment.push([Math.max(0, Math.min(1, sample[0])) * width, vertical(sample[1], height)]);
            }
        }
        if (segment.length) paths.push(segment);
        return paths.map(function(line) {
            let svg = "M " + line[0][0] + " " + line[0][1];
            if (line.length === 1 && !fill)
                return svg + " h -0.8";
            for (let i = 1; i < line.length; ++i) {
                if (signalStyle) svg += " H " + line[i][0] + " V " + line[i][1];
                else svg += " L " + line[i][0] + " " + line[i][1];
            }
            if (fill) svg += " L " + line[line.length - 1][0] + " " + height +
                            " L " + line[0][0] + " " + height + " Z";
            return svg;
        }).join(" ");
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: chart.padding
        spacing: 4 * chart.unit
        RowLayout {
            visible: !chart.mini
            Layout.fillWidth: true
            spacing: 7 * chart.unit
            Glyph {
                Layout.preferredWidth: chart.fontSize + 2
                Layout.preferredHeight: chart.fontSize + 2
                sourceSize: Qt.size(64, 64)
                visible: chart.studio
                role: chart.model.icon || "graphs"
                ink: chart.accent
                dark: !!chart.scene.dark
            }
            Text {
                Layout.fillWidth: true
                text: chart.model.label || ""
                color: chart.valueInk
                font.pixelSize: chart.fontSize
                font.weight: chart.minimal ? Font.Normal : Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                Layout.maximumWidth: chart.width * .53
                text: chart.model.value || ""
                color: chart.valueInk
                font.pixelSize: chart.fontSize
                font.family: chart.signalStyle ? "Consolas" : chart.scene.font || "Segoe UI"
                elide: Text.ElideRight
            }
        }
        Flow {
            visible: !chart.mini && chart.readings.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            spacing: 9 * chart.unit
            Repeater {
                model: chart.readings
                delegate: Row {
                    required property var modelData
                    spacing: 4
                    Glyph {
                        width: chart.axisFont + 2; height: width
                        role: modelData.icon
                        ink: modelData.color
                        dark: !!chart.scene.dark
                    }
                    Text {
                        color: chart.ink
                        text: modelData.label
                        font.pixelSize: chart.axisFont
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: chart.hasPlot
            spacing: 6 * chart.unit
            Column {
                visible: chart.mini
                spacing: 2
                Repeater {
                    model: chart.model.rate ? (chart.readings.length ? chart.readings : chart.model.idleIcons || []) :
                        [{icon: chart.model.icon || "graphs", color:chart.valueInk}]
                    delegate: Row {
                        required property var modelData
                        spacing: 4
                        Glyph {
                            width: Math.max(10, Math.min(chart.fontSize + 1, 18 * chart.unit)); height: width
                            role: modelData.icon
                            ink: modelData.color || chart.valueInk
                            dark: !!chart.scene.dark
                        }
                    }
                }
            }
            ColumnLayout {
                visible: !chart.mini && !chart.minimal
                Layout.fillHeight: true
                spacing: 0
                Text { text: chart.axisValue(chart.high); color: chart.muted; font.pixelSize: chart.axisFont }
                Item { Layout.fillHeight: true }
                Text { text: chart.axisValue(chart.low); color: chart.muted; font.pixelSize: chart.axisFont }
            }
            Item {
                id: plot
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                Rectangle {
                    anchors.fill: parent
                    radius: 5
                    color: Qt.rgba(chart.muted.r, chart.muted.g, chart.muted.b, chart.studio ? .10 : 0)
                }
                Repeater {
                    model: chart.mini || chart.minimal ? 1 : 4
                    delegate: Rectangle {
                        required property int index
                        width: plot.width; height: 1
                        y: Math.max(0, plot.height * (index + 1) / (chart.mini || chart.minimal ? 1 : 4) - 1)
                        color: chart.muted
                        opacity: .20
                    }
                }
                Repeater {
                    model: chart.studio && !chart.mini && !chart.minimal ? 7 : 0
                    delegate: Rectangle {
                        required property int index
                        height: plot.height; width: 1
                        x: plot.width * (index + 1) / 8
                        color: chart.muted
                        opacity: .16
                    }
                }
                Shape {
                    anchors.fill: parent
                    visible: chart.model.threshold !== undefined && chart.model.threshold >= chart.low &&
                        chart.model.threshold <= chart.high
                    ShapePath {
                        fillColor: "transparent"
                        strokeColor: Qt.rgba(chart.alert.r, chart.alert.g, chart.alert.b, .66)
                        strokeWidth: 1
                        strokeStyle: ShapePath.DashLine
                        dashPattern: [4, 4]
                        startX: 0
                        startY: chart.vertical(Number(chart.model.threshold || 0), plot.height)
                        PathLine { x: plot.width; y: chart.vertical(Number(chart.model.threshold || 0), plot.height) }
                    }
                }
                Repeater {
                    model: chart.history
                    delegate: Shape {
                        id: trace
                        required property var modelData
                        readonly property color seriesInk: modelData.color || chart.accent
                        anchors.fill: parent
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeWidth: -1
                            fillGradient: LinearGradient {
                                x1: 0; y1: 0; x2: 0; y2: plot.height
                                GradientStop { position: 0; color: Qt.rgba(trace.seriesInk.r, trace.seriesInk.g, trace.seriesInk.b, chart.layered ? .70 : 0) }
                                GradientStop { position: 1; color: Qt.rgba(trace.seriesInk.r, trace.seriesInk.g, trace.seriesInk.b, chart.layered ? .10 : 0) }
                            }
                            PathSvg { path: chart.layered ? chart.tracePath(trace.modelData.points || [], plot.width, plot.height, true) : "" }
                        }
                        ShapePath {
                            fillColor: "transparent"
                            strokeColor: trace.seriesInk
                            strokeWidth: chart.mini || chart.minimal ? 1.4 : 2
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            PathSvg { path: chart.tracePath(trace.modelData.points || [], plot.width, plot.height, false) }
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width
                    visible: !chart.mini && text.length > 0 && text !== chart.model.value
                    text: chart.model.hint || ""
                    color: chart.muted
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: chart.axisFont
                    elide: Text.ElideRight
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: chart.hasPlot && !chart.mini && !chart.minimal
            Text {
                Layout.fillWidth: true
                text: (chart.model.footnote || "") || (chart.model.seconds < 60 ? "−" + chart.model.seconds + "s" : "−" + Math.round((chart.model.seconds || 60) / 60) + "m")
                color: chart.muted
                font.pixelSize: chart.axisFont
                elide: Text.ElideRight
            }
            Text {
                text: chart.model.unit || "Now"
                color: chart.muted
                font.pixelSize: chart.axisFont
            }
        }
    }
}
