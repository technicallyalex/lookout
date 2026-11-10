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
            values: {thumbnail: false, showAlertIcon: false, textStats: true,
                     iconVisibility: 2, graphVisibility: 0},
            statLabels: {storage: "Disk space available", encoder: "Skipped frames due to encoding lag",
                         network: "Dropped Frames (Network)"},
            monitors: ["Display"],
            presets: ["Gaming"],
            shortcuts: ["Ctrl+F1"],
            dockOptions: [{id: "settings", label: "Settings", icon: "settings"},
                          {id: "show", label: "Show panels", icon: "show"},
                          {id: "hide", label: "Hide panels", icon: "hide"},
                          {id: "thumbnail", label: "Toggle thumbnail", icon: "thumbnail"}],
            dockOrder: ["settings", "show", "hide"]
        })
        property int shortcutIndex: -1
        property string lastAction: ""
        property string lastKey: ""
        property var lastValue
        property bool applyEdits: false
        function setting(action, value, key) {
            lastAction = action
            lastKey = key || ""
            lastValue = value
            if (applyEdits && action === "set") {
                Qt.callLater(() => {
                    const values = Object.assign({}, settings.values)
                    values[key] = value
                    if (key === "customSizePercent")
                        values.panelScale = 4
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
            compare(settings.sections[1].title, "Stats and icons")
            verify(!findChild(settings, "setting_presentation"))
            verify(findChild(settings, "setting_textStats"))
            const iconVisibility = findChild(findChild(settings, "setting_iconVisibility"), "choiceControl")
            const graphVisibility = findChild(findChild(settings, "setting_graphVisibility"), "choiceControl")
            compare(iconVisibility.currentText, "Off")
            compare(graphVisibility.currentText, "On")
            for (const control of [iconVisibility, graphVisibility])
                compare(control.model, ["On", "Active", "Off"])
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

        function test_thumbnailPlacementOnPanels() {
            const previous = api.settings
            const settings = createTemporaryObject(settingsComponent, fixture, {page: 1})
            verify(settings)
            const panels = findChild(findChild(settings, "setting_thumbnailPosition"), "choiceControl")
            compare(panels.currentText, "Above metrics")
            api.applyEdits = true
            panels.activated(3)
            tryVerify(() => api.settings.values.thumbnailPosition === 3)
            settings.page = 3
            verify(!findChild(settings, "setting_thumbnailPosition"))
            settings.page = 1
            const restored = findChild(findChild(settings, "setting_thumbnailPosition"), "choiceControl")
            compare(restored.currentText, "Right of metrics")
            api.applyEdits = false
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
            verify(field.spec.note.includes("Following OBS (Dark)"))
            compare(field.current, 1)
            const colors = findChild(findChild(settings, "setting_panelDesign"), "choiceControl")
            const appearance = findChild(field, "choiceControl")
            api.applyEdits = true
            colors.activated(3)
            tryCompare(appearance, "enabled", true)
            compare(field.current, 1)
            appearance.activated(2)
            tryVerify(() => api.settings.values.logoTheme === 2)
            colors.activated(0)
            tryCompare(appearance, "enabled", false)
            compare(field.current, 2)
            api.applyEdits = false
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

        function test_customScaleCanBeEditedFromPreset() {
            const previous = api.settings
            api.settings = Object.assign({}, previous, {values: {panelScale: 1, customSizePercent: 86}})
            const settings = createTemporaryObject(settingsComponent, fixture, {page: 3})
            verify(settings)
            const row = findChild(settings, "setting_customSizePercent")
            const spin = findChild(row, "numberControl")
            verify(spin.enabled && row.opacity < 1)
            const editor = spin.contentItem
            mouseClick(editor)
            tryCompare(row, "opacity", 1)
            compare(api.settings.values.panelScale, 1)
            api.applyEdits = true
            editor.selectAll()
            keyClick(Qt.Key_9)
            keyClick(Qt.Key_2)
            keyClick(Qt.Key_Return)
            tryVerify(() => api.settings.values.customSizePercent === 92 && api.settings.values.panelScale === 4)
            settings.forceActiveFocus()
            compare(row.opacity, 1)
            api.applyEdits = false
            api.settings = previous
        }

        function test_dockSwitchesAndDrag() {
            const previous = api.settings
            const settings = createTemporaryObject(settingsComponent, fixture, {page: 6})
            verify(settings)
            const first = findChild(settings, "dockButton_settings")
            const last = findChild(settings, "dockButton_hide")
            const available = findChild(settings, "dockButton_thumbnail")
            verify(first.parent === last.parent && last.parent === available.parent)
            verify(first.y < last.y && last.y < available.y)
            const source = findChild(first, "dockDragHandle")
            const destination = findChild(last, "dockDragHandle")
            verify(source.visible && destination.visible && !findChild(available, "dockDragHandle").visible)
            const point = destination.mapToItem(source, destination.width / 2, destination.height / 2)
            mouseDrag(source, source.width / 2, source.height / 2,
                      0, point.y - source.height / 2)
            compare(api.lastAction, "dockButton")
            compare(api.lastKey, "settings")
            compare(api.lastValue, 2)
            const enabled = findChild(first, "dockButtonSwitch")
            const disabled = findChild(available, "dockButtonSwitch")
            verify(enabled.checked && !disabled.checked)
            mouseClick(enabled)
            compare(api.lastValue, "remove")
            compare(api.lastKey, "settings")
            mouseClick(disabled)
            compare(api.lastValue, "add")
            compare(api.lastKey, "thumbnail")
            const mode = findChild(settings, "dockLabelMode")
            compare(mode.currentText, "Icons only")
            mode.activated(1)
            compare(api.lastAction, "dockLabels")
            compare(api.lastValue, true)
            api.settings = Object.assign({}, previous, {dockLabels: true,
                dockOrder: ["hide", "thumbnail", "settings", "show"]})
            compare(mode.currentText, "Icons and text labels")
            const newlyEnabled = findChild(settings, "dockButton_thumbnail")
            verify(findChild(newlyEnabled, "dockDragHandle").visible)
            tryVerify(() => findChild(settings, "dockButton_hide").y < newlyEnabled.y &&
                            newlyEnabled.y < findChild(settings, "dockButton_settings").y)
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
