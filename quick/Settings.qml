import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    required property var controller
    readonly property var state: controller.settings
    readonly property var values: state.values || ({})
    readonly property var theme: state.palette || ({})
    readonly property color ink: state.dark ? "#f1f3f7" : "#20242a"
    readonly property color muted: state.dark ? "#b0b8c6" : "#515a67"
    readonly property color edge: state.dark ? "#474f5b" : "#cbd1da"
    readonly property color pageColor: theme.background || "#24272d"
    readonly property color surface: Qt.tint(pageColor, state.dark ? "#08ffffff" : "#08000000")
    property int page: 0
    readonly property bool widePage: page === 1 || page === 3 || page === 4
    readonly property bool twoColumns: widePage && width >= 960
    readonly property var sections: groups()
    implicitWidth: 1080
    implicitHeight: Math.ceil(Math.max(sidebar.implicitHeight + 24, heading.implicitHeight + 34 + pageBody.implicitHeight))
    readonly property var navigation: ["Overlay", "Panels", "Appearance", "Placement", "Alerts", "Shortcuts", "Dock", "About"]
    palette.window: pageColor
    palette.base: pageColor
    palette.button: surface
    palette.windowText: ink
    palette.buttonText: ink
    palette.text: ink
    palette.highlight: theme.accent || "#528be5"
    palette.highlightedText: "white"
    background: Rectangle { color: root.pageColor }
    Accessible.name: "lookout settings"

    function toggle(key,label,note,disabled) { return {kind:"toggle",key:key,label:label,note:note,disabled:disabled}; }
    function metric(key,label,icon) { return {kind:"toggle",key:key,label:label,icon:icon}; }
    function statLabel(id) { return (state.statLabels || {})[id] || ""; }
    function stat(id,icon,key) { return metric(key || "iconVisible/"+id,statLabel(id),icon || id); }
    function alert(id,label,note) {
        return {kind:"preview",key:"alert"+id,label:label,note:note,value:id};
    }
    function alertGroup(key,label,icon,fields) {
        const heading=metric(key,label,icon);
        heading.heading=true;
        return group("",[heading].concat(fields));
    }
    function choice(key,label,options,ids,disabled) { return {kind:"choice",key:key,label:label,options:options,ids:ids,disabled:disabled}; }
    function graph(id) {
        const field=choice("graph/"+id,statLabel(id),["Off","Mini","Full"]);
        field.icon=id === "bitrate" ? "graphs" : id;
        return field;
    }
    function number(key,label,min,max,factor,disabled) { return {kind:"number",key:key,label:label,min:min,max:max,factor:factor,disabled:disabled}; }
    function action(key,label,button,value,disabled) { return {kind:"action",key:key,label:label,button:button,value:value,disabled:disabled}; }
    function group(title,fields) { return {title:title,fields:fields}; }
    function groups() {
        const v=values;
        switch(page) {
        case 0: return [
            group("Visibility", [
                choice("panelMode","Show panels",["Only for alerts","Always","Off"]),
                toggle("showInCaptures","Include in recordings and streams",state.captureStatus || "Private by default")]),
            group("Transitions", [
                Object.assign(choice("panelTransition","Show and hide",["Instant","Fade","Flash","Slide","Blur","Fall"],
                       ["instant","fade","flash","slide","blur","fall"],state.transitionPreview),
                       {transitionPreview:true})])];
        case 1: return [
            group("Thumbnail", [
                toggle("thumbnail","Show thumbnail"),
                {kind:"toggle",key:"showAlertIcon",label:"Show alert symbol on thumbnail",
                 disabled:!v.thumbnail,thumbnailPreview:true}]),
            group("Stats", [
                choice("presentation","Display mode",state.displayModeLabels || [],state.displayModeIds || []),
                stat("recording"),stat("streaming"),stat("camera","virtual-camera"),stat("replay"),
                stat("storage"),stat("encoder"),stat("network"),stat("bitrate","graphs","showBitrate"),
                stat("cpu"),stat("memory"),stat("disk-time")]),
            group("Graphs", [
                graph("storage"),graph("encoder"),graph("network"),graph("cpu"),
                graph("memory"),graph("disk-time"),graph("bitrate"),graph("replay")]),
            group("History", [
                choice("graphWindow","Chart time range",["30 seconds","1 minute","5 minutes","15 minutes"],[30,60,300,900]),
                action("clearHistory","Discard samples collected this session","Clear history")])];
        case 2: return [
            group("Colors and charts", [
                choice("panelDesign","Colors",["Follow OBS","Violet","lookout","Teal","Amber"],["obs","layered","minimal","studio","signal"]),
                choice("chartLines","Chart line design",["Line","Filled area","Stepped","Line with grid"],["line","area","stepped","grid"]),
                choice("chartDetail","Chart detail",["Full info","Minimal"]),
                {kind:"choice",key:"logoTheme",label:"Appearance",options:["System","Light","Dark"],
                 disabled:v.panelDesign === "obs",note:v.panelDesign === "obs" ? "Uses the theme selected in OBS." : ""}]),
            group("Panel effects", [number("backgroundOpacity","Card opacity (%)",0,100),
                number("blurAmount","Blur amount (%)",0,100)])];
        case 3: return [
                group("Position", [
                    {kind:"toggle",key:"edit",stateKey:"editing",label:"Edit panel placement",
                     note:state.editing ? "Drag a card to move it or pull its corner grip to resize it." : "" },
                    choice("monitor","Display",state.monitors || []),
                    choice("corner","Anchor corner",["Top left","Top right","Bottom left","Bottom right"]),
                    choice("thumbnailPosition","Thumbnail placement",["Above metrics","Below metrics","Left of metrics","Right of metrics"],null,v.independentCards),
                    toggle("independentCards","Move cards individually","",v.iconStrip),
                    {kind:"command",key:"resetPosition",label:"Reset position",alignRight:true}]),
                group("Scale",[
                    {kind:"sizes",key:"panelScale",options:["Small","Normal","Large","Extra large","Custom"]},
                    number("customSizePercent","Custom scale (%)",45,300)]),
                group("Precision Placement",[
                number("offsetX","Horizontal inset (px)",0,10000),number("offsetY","Vertical inset (px)",0,10000),
                number("panelWidth","Metrics column width (px)",128,960),number("thumbnailWidth","Thumbnail width (px)",80,800),
                number("cardSpacing","Card spacing (px)",0,64),number("snapDistance","Snap distance (px)",0,64),
                toggle("snapCards","Snap to other cards"),toggle("snapScreen","Snap to display edges")])];
        case 4: return [
            alertGroup("storage","Recording storage","storage",[
                number("gib","Free-space threshold (GiB)",0.1,10000,10,!v.storage),
                alert(0,"Storage running low","Free space reaches the warning threshold."),
                alert(5,"Recording disk full","No free space remains."),
                alert(1,"Recording storage unavailable","The recording destination can’t be checked.")]),
            alertGroup("encoder","Encoding lag","encoder",[
                number("skip","Skipped-frame threshold (%)",1,100,1,!v.encoder),
                alert(2,"Encoder overload","Skipped frames reach the threshold.")]),
            alertGroup("network","Stream connection","network",[
                number("networkPercent","Dropped-frame threshold (%)",1,100,1,!v.network),
                alert(3,"Stream quality degraded","Dropped frames reach the threshold."),
                alert(4,"Stream disconnected","OBS starts trying to reconnect."),
                alert(6,"Stream reconnected","OBS restores the connection."),
                alert(7,"Stream stopped with an error","OBS reports an error when the stream stops.")]),
            group("Output changes",[
                toggle("lifecycle","Reveal panels on start or stop",
                    "Applies to recording, streaming, virtual camera, and replay buffer.")]),
            group("Delivery",[
                toggle("untilDismiss","Keep alerts until dismissed"),
                number("seconds","Auto-dismiss after (seconds)",2,60,1,v.untilDismiss),
                {kind:"command",key:"dismiss",label:"Dismiss alerts",alignRight:true,disabled:!state.hasAlerts}]),
            group("Glow Settings",[
                toggle("glowEnabled","Pulse around the affected card"),
                {kind:"color",key:"glowWarningColor",label:"Low-storage warning color"},
                {kind:"color",key:"glowCriticalColor",label:"Critical alert color"},
                number("glowIntensity","Intensity (%)",0,200),number("glowSpread","Spread (px)",4,64),
                number("glowSpeed","Pulse speed (×)",0.25,2,100),
                {kind:"glowPreview",key:"previewGlow",label:"Preview glow"},
                {kind:"command",key:"stopPreview",label:"Stop preview",alignRight:true,disabled:!state.hasPreview}])];
        case 5: return [group("OBS global shortcuts",
            ["Show panels","Hide panels until an alert","Disable panels and alerts","Toggle thumbnail",
             "Toggle Stats","Toggle capture visibility","Cycle anchor corners","Cycle saved positions"]
            .map((label,index) => ({kind:"shortcut",label:label,index:index})))];
        default: return [];
        }
    }

    contentItem: RowLayout {
        spacing: 0
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: root.width < 650 ? 154 : 184
            color: root.surface
            ColumnLayout {
                id: sidebar
                anchors.fill: parent
                anchors.margins: 12
                spacing: 5
                ColumnLayout {
                    Layout.bottomMargin: 14
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 2
                    BrandMark {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: root.width < 650 ? 74 : 82
                        Layout.preferredHeight: Layout.preferredWidth
                        night: !!root.state.logoDark
                        play: !!root.state.version && root.Window.window !== null && root.Window.window.visible
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: "lookout"
                        font.family: "Georgia"
                        font.pixelSize: root.width < 650 ? 34 : 38
                        color: root.state.dark ? "#fff0cf" : "#102e55"
                    }
                }
                Repeater {
                    model: root.navigation
                    delegate: Button {
                        id: destination
                        required property int index
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredHeight: 36
                        text: modelData
                        highlighted: root.page === index
                        background: Rectangle {
                            radius: 6
                            color: destination.highlighted ? root.palette.highlight :
                                destination.hovered ? root.pageColor : "transparent"
                            border.width: destination.visualFocus ? 1 : 0
                            border.color: root.palette.highlight
                        }
                        contentItem: Label {
                            text: destination.text
                            color: destination.highlighted ? root.palette.highlightedText : root.ink
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            font.weight: destination.highlighted ? Font.DemiBold : Font.Normal
                        }
                        onClicked: { controller.captureShortcut(-1); root.page=index; scroll.contentItem.contentY=0; }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Label {
                id: heading
                Layout.topMargin: 20
                Layout.leftMargin: 22
                Layout.bottomMargin: 14
                text: root.navigation[root.page]
                font.pixelSize: 24
                font.bold: true
            }
            ScrollView {
                id: scroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    id: pageBody
                    width: Math.max(1, scroll.availableWidth - 44)
                    x: 22
                    spacing: 12
                    RowLayout {
                        visible: root.sections.length > 0
                        Layout.fillWidth: true
                        spacing: 12
                        Repeater {
                            model: root.twoColumns ? 2 : 1
                            delegate: ColumnLayout {
                                id: groupColumn
                                required property int index
                                readonly property int splitAt: root.page === 4 ? 3 : 2
                                readonly property var groups: !root.twoColumns ? root.sections :
                                    index === 0 ? root.sections.slice(0, splitAt) : root.sections.slice(splitAt)
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.alignment: Qt.AlignTop
                                spacing: 12
                                Repeater {
                                    // Keep focused editors alive while settings values change.
                                    model: groupColumn.groups.length
                                    delegate: Rectangle {
                                        id: sectionCard
                                        required property int index
                                        readonly property var section: groupColumn.groups[index]
                                        Layout.fillWidth: true
                                        implicitHeight: rows.implicitHeight + 28
                                        radius: 10
                                        color: root.surface
                                        border.color: root.edge
                                        ColumnLayout {
                                            id: rows
                                            anchors.fill: parent
                                            anchors.margins: 14
                                            spacing: 5
                                            Label {
                                                text: sectionCard.section.title
                                                visible: text.length > 0
                                                font.bold: true
                                                Layout.bottomMargin: 4
                                            }
                                            Repeater {
                                                model: sectionCard.section.fields.length
                                                delegate: SettingsField {
                                                    required property int index
                                                    Layout.fillWidth: true
                                                    spec: sectionCard.section.fields[index]
                                                    objectName: "setting_" + (spec.key || spec.index)
                                                    values: root.values
                                                    state: root.state
                                                    api: root.controller
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        visible: root.page === 3
                        Layout.fillWidth: true
                        ComboBox { id: presets; Layout.fillWidth: true; model: root.state.presets || []; Accessible.name: "Saved positions" }
                        Button { text: "Load"; enabled: presets.currentIndex>=0; onClicked: controller.setting("loadPreset",presets.currentText) }
                        Button { text: "Remove"; enabled: presets.currentIndex>=0; onClicked: controller.setting("removePreset",presets.currentText) }
                    }
                    RowLayout {
                        visible: root.page === 3
                        Layout.fillWidth: true
                        TextField { id: presetName; Layout.fillWidth: true; placeholderText: "Position preset name"; maximumLength: 64 }
                        Button { text: "Save position"; enabled: presetName.text.trim().length>0; onClicked: { controller.setting("savePreset",presetName.text); presetName.clear(); } }
                    }
                    Loader {
                        active: root.page === 6
                        visible: active
                        Layout.fillWidth: true
                        sourceComponent: DockSettings {
                            controller: root.controller
                            options: root.state.dockOptions || []
                            order: root.state.dockOrder || []
                            surface: root.surface
                            edge: root.edge
                            ink: root.ink
                            dark: !!root.state.dark
                        }
                    }
                    Label {
                        visible: root.page === 7
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "lookout " + (root.state.version || "") + "\n" + (root.state.build || "") +
                              "\n\nRelease Candidate 2"
                    }
                    Button {
                        visible: root.page === 7
                        text: "GitHub Releases"
                        onClicked: Qt.openUrlExternally("https://github.com/technicallyalex/lookout/releases")
                    }
                    Item { Layout.preferredHeight: 16 }
                }
            }
        }
    }
}
