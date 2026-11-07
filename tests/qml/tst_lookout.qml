import QtQuick
import QtTest
import "qrc:/quick" as Lookout

Item {
    id: fixture
    width: 1100
    height: 820

    QtObject {
        id: api
        property var settings: ({
            dark: true,
            values: {thumbnail: false, showAlertIcon: false},
            statLabels: {storage: "Disk space available", encoder: "Skipped frames due to encoding lag",
                         network: "Dropped Frames (Network)"},
            displayModeLabels: ["Icons", "Active icons", "Custom"],
            displayModeIds: ["icons", "active-icons", "custom"],
            monitors: ["Display"],
            presets: ["Gaming"],
            shortcuts: ["Ctrl+F1"],
            dockOptions: [{id: "settings", label: "Settings", icon: "settings"}],
            dockOrder: ["settings"]
        })
        property int shortcutIndex: -1
        property string lastAction: ""
        property bool applyEdits: false
        function setting(action, value, key) {
            lastAction = action
            if (applyEdits && action === "set") {
                Qt.callLater(() => {
                    const values = Object.assign({}, settings.values)
                    values[key] = value
                    settings = Object.assign({}, settings, {values: values})
                })
            }
        }
    }

    Component {
        id: settingsComponent
        Lookout.Settings { width: 1080; height: 800; controller: api }
    }

    Component {
        id: graphComponent
        Lookout.Graph {
            width: 260
            height: 125
            model: ({
                graphs: true, rate: true, min: 0, max: 100,
                label: "Bitrate", value: "50 kbps", threshold: 80,
                readings: [{icon: "streaming", label: "Streaming", color: "#5294ff"}],
                series: [{color: "#5294ff", points: [[0, 20], [0.3, null], [0.6, 30], [1, 50]]}]
            })
        }
    }

    TestCase {
        name: "LookoutQml"
        when: windowShown

        function init() {
            failOnWarning(/.*/)
        }

        function test_settingsPages() {
            const settings = createTemporaryObject(settingsComponent, fixture)
            verify(settings)
            for (let page = 0; page < settings.navigation.length; ++page) {
                settings.page = page
                wait(0)
                verify(settings.implicitHeight > 0, settings.navigation[page])
            }
            settings.width = 800
            settings.page = 1
            compare(settings.navigation[1], "Panels")
            compare(settings.sections[0].title, "Thumbnail")
            verify(findChild(settings, "setting_thumbnail"))
            compare(settings.sections[1].title, "Stats")
            for (const id of ["storage", "encoder", "network"]) {
                compare(findChild(settings, "setting_iconVisible/" + id).spec.label, api.settings.statLabels[id])
                compare(findChild(settings, "setting_graph/" + id).spec.label, api.settings.statLabels[id])
            }
            verify(!settings.twoColumns)
            settings.width = 1080
            verify(settings.twoColumns)
        }

        function test_thumbnailAlertPreview() {
            const settings = createTemporaryObject(settingsComponent, fixture, {page: 1})
            verify(settings)
            const field = findChild(settings, "setting_showAlertIcon")
            verify(field)
            const button = findChild(field, "alertPreview")
            verify(button && button.enabled && button.visible)
            compare(button.text, "Preview")
            button.clicked()
            compare(api.lastAction, "previewThumbnailAlert")
            const previous = api.settings
            api.settings = Object.assign({}, previous, {thumbnailAlertPreview: true})
            compare(button.text, "Stop")
            api.settings = previous
        }

        function test_transitionPreview() {
            const settings = createTemporaryObject(settingsComponent, fixture)
            verify(settings)
            const row = findChild(settings, "setting_panelTransition")
            const button = findChild(row, "transitionPreview")
            verify(button && button.visible && button.enabled)
            button.clicked()
            compare(api.lastAction, "previewTransition")
            const previous = api.settings
            api.settings = Object.assign({}, previous, {transitionPreview: true})
            verify(!button.enabled)
            api.settings = previous
        }

        function test_followObsDisablesAppearance() {
            const settings = createTemporaryObject(settingsComponent, fixture, {page: 2})
            verify(settings)
            const field = findChild(settings, "setting_logoTheme")
            verify(field)
            const previous = api.settings
            api.settings = Object.assign({}, previous, {values: {panelDesign: "obs", logoTheme: 1}})
            verify(field.spec.disabled)
            compare(field.current, 1)
            api.settings = Object.assign({}, previous, {values: {panelDesign: "studio", logoTheme: 1}})
            verify(!field.spec.disabled)
            compare(field.current, 1)
            api.settings = previous
        }

        function test_effectEditsKeepAppearanceSelection() {
            const previous = api.settings
            api.settings = Object.assign({}, previous, {values: {
                panelDesign: "minimal", logoTheme: 2, backgroundOpacity: 100, blurAmount: 0
            }})
            const settings = createTemporaryObject(settingsComponent, fixture, {page: 2})
            verify(settings)
            const row = findChild(settings, "setting_logoTheme")
            const choice = findChild(row, "choiceControl")
            verify(choice)
            tryCompare(choice, "currentIndex", 2)
            api.applyEdits = true
            const edits = [["backgroundOpacity", "25", 1, "Light"], ["blurAmount", "50", 2, "Dark"]]
            for (const [key, amount, theme, label] of edits) {
                const editor = findChild(findChild(settings, "setting_" + key), "numberControl").contentItem
                editor.forceActiveFocus()
                editor.selectAll()
                for (const digit of amount)
                    keyClick(Qt.Key_0 + Number(digit))
                mouseClick(choice)
                tryCompare(choice.popup, "opened", true)
                tryVerify(() => api.settings.values[key] === Number(amount))
                const option = choice.popup.contentItem.itemAtIndex(theme)
                verify(option)
                mouseClick(option)
                tryCompare(choice, "currentIndex", theme)
                compare(choice.currentText, label)
            }
            api.applyEdits = false
            api.settings = previous
        }

        function test_chartModes() {
            const graph = createTemporaryObject(graphComponent, fixture)
            verify(graph && graph.hasPlot)
            for (const lines of ["line", "area", "stepped", "grid"]) {
                graph.scene = {design: "obs", chartLines: lines, chartDetail: 0, text: "#ffffff", top: "#292929"}
                verify(!graph.mini && !graph.minimal)
                graph.scene = {design: "obs", chartLines: lines, chartDetail: 1, text: "#141414", top: "#eeeeee"}
                verify(graph.minimal)
                wait(0)
            }
            graph.model = Object.assign({}, graph.model, {compact: true})
            verify(graph.mini)
            const points = graph.model.series[0].points
            compare(graph.tracePath(points, 100, 100, false).match(/M /g).length, 2)
        }
    }
}
