import QtQuick
import QtQuick.Effects
import Lookout 1.0

Item {
    id: root
    property var s: controller.scene
    readonly property var motion: controller.motion
    property real pulse: 0
    readonly property color ink: s.text || "white"
    readonly property real fontPx: s.fontPx || 14
    readonly property real cardOpacity: Math.max(s.edit ? .035 : 0, s.opacity === undefined ? 1 : s.opacity)
    ListModel { id: cardsModel }
    // Keep delegates (especially the live GPU texture) alive across samples.
    // Replacing the whole Repeater model would briefly blank the thumbnail.
    function syncCards() {
        var next = s.cards || [];
        for (var i=0; i<next.length; ++i) {
            var key=next[i].id + ":" + (next[i].label || next[i].type), found=-1;
            for(var j=i;j<cardsModel.count;++j) if(cardsModel.get(j).cardKey === key){found=j;break;}
            var data=JSON.stringify(next[i]);
            if(found < 0) cardsModel.insert(i,{cardKey:key,cardData:data});
            else { if(found !== i) cardsModel.move(found,i,1); if(cardsModel.get(i).cardData !== data) cardsModel.setProperty(i,"cardData",data); }
        }
        if(cardsModel.count > next.length) cardsModel.remove(next.length,cardsModel.count-next.length);
    }
    onSChanged: syncCards()
    Component.onCompleted: syncCards()
    NumberAnimation on pulse {
        from: 0; to: 1; duration: root.s.glowPeriod || 4000
        loops: Animation.Infinite
        running: !!root.s.visible && !!root.s.glow
    }
    readonly property real breath: 0.08 + 0.92 * (0.5 - 0.5 * Math.cos(pulse * Math.PI * 2))
    function asColor(value) { return Qt.tint("transparent", value); }
    function alpha(color, a) { return Qt.rgba(color.r, color.g, color.b, Math.max(0, Math.min(1, a))); }
    function targeted(c) {
        if (!s.glow) return false;
        var targets = s.glowTargets || [];
        for (var i = 0; i < targets.length; ++i) {
            var t = targets[i];
            if (Math.abs(t.x-c.x) < 3 && Math.abs(t.y-c.y) < 3) return true;
        }
        return false;
    }
    Repeater {
        model: cardsModel
        delegate: Item {
            id: card
            required property string cardKey
            required property string cardData
            property var c: JSON.parse(cardData)
            readonly property point origin: root.motion.offset || Qt.point(0, 0)
            readonly property point travel: (root.motion.cards || ({}))[cardKey] || Qt.point(0, 0)
            x: c.x + origin.x + travel.x
            y: c.y + origin.y + travel.y
            width: c.w; height: c.h
            opacity: root.motion.opacity === undefined ? 1 : root.motion.opacity
            property bool glowing: root.targeted(c)
            readonly property real cornerRadius: 8
            Repeater {
                model: card.glowing ? 32 : 0
                Rectangle {
                    required property int index
                    property real distance: (index + 0.5) / 32 * (root.s.glowSpread || 24) * (0.12 + 0.88 * root.breath)
                    x: -distance; y: -distance
                    width: card.width + 2 * distance; height: card.height + 2 * distance
                    radius: card.cornerRadius + distance
                    color: "transparent"
                    border.width: (root.s.glowSpread || 24) / 32 + 0.6
                    border.color: root.alpha(root.asColor(root.s.glowColor || "#ffd43b"), 0.64 * (root.s.glowIntensity || 0) * root.breath * Math.pow(1-index/32, 2))
                    antialiasing: true
                }
            }
            Item {
                anchors.fill: parent
                layer.enabled: (root.motion.blur || 0) > 0
                layer.effect: MultiEffect {
                    blurEnabled: true
                    blurMax: 24
                    blur: root.motion.blur || 0
                }
                Rectangle {
                    anchors.fill: parent
                    radius: card.cornerRadius
                    gradient: Gradient {
                        GradientStop { position: 0; color: root.alpha(root.asColor(root.s.top || "#303030"), root.cardOpacity * (root.s.glass ? .68 : 1)) }
                        GradientStop { position: 1; color: root.alpha(root.asColor(root.s.bottom || "#303030"), root.cardOpacity * (root.s.glass ? .43 : 1)) }
                    }
                    border.width: 1
                    border.color: root.alpha(root.asColor(root.s.edge || "#666666"), root.cardOpacity * (root.s.glass ? .82 : .65))
                    antialiasing: true
                }
                Repeater {
                    model: card.c.rows || []
                    delegate: Item {
                        required property var modelData
                        property var r: modelData
                        property color tint: r.flash ? Qt.tint(root.asColor(r.color), root.alpha(root.asColor(r.flashColor), root.breath)) : root.asColor(r.color)
                        Glyph {
                            x: parent.r.iconRect.x; y: parent.r.iconRect.y
                            width: parent.r.iconRect.w; height: parent.r.iconRect.h
                            role: parent.r.icon; ink: parent.tint; dark: !!root.s.dark
                        }
                        Text {
                            x: parent.r.nameRect.x; y: parent.r.nameRect.y
                            width: parent.r.nameRect.w; height: parent.r.nameRect.h
                            visible: parent.r.names
                            text: parent.r.label; color: parent.tint
                            font.family: root.s.font || "Segoe UI"; font.pixelSize: root.fontPx
                            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                        }
                        Text {
                            x: parent.r.valueRect.x; y: parent.r.valueRect.y
                            width: parent.r.valueRect.w; height: parent.r.valueRect.h
                            visible: parent.r.values
                            text: parent.r.value; color: parent.tint
                            font.family: root.s.font || "Segoe UI"; font.pixelSize: root.fontPx
                            horizontalAlignment: Text.AlignRight; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                        }
                    }
                }
                Loader {
                    anchors.fill: parent
                    active: card.c.type === "chart"
                    sourceComponent: Graph { model: card.c; scene: root.s; pulse: root.breath }
                }
                Text {
                    anchors.fill: parent; anchors.margins: 8
                    visible: card.c.type === "alert"
                    text: card.c.html || ""; textFormat: Text.RichText
                    color: root.ink; wrapMode: Text.WordWrap
                    font.family: root.s.font || "Segoe UI"; font.pixelSize: root.fontPx
                }
                Loader {
                    anchors.fill: parent
                    active: card.c.type === "thumbnail"
                    sourceComponent: Item {
                        SharedPreview {
                            id: thumbnail
                            objectName: "liveThumbnail"
                            x: card.c.preview.x; y: card.c.preview.y
                            width: card.c.preview.w; height: card.c.preview.h
                            descriptor: root.s.texture || ({})
                            property bool frameReady: false
                            property bool failed: false
                            onFrameCopied: frameReady = true
                            onDescriptorChanged: { frameReady = false; failed = false; }
                            onImportFailed: failed = true
                        }
                        Text { anchors.centerIn: thumbnail; visible: !thumbnail.frameReady; text: thumbnail.failed ? "Thumbnail unavailable" : root.s.texture && root.s.texture.supported === false ? "Thumbnail needs Direct3D 11" : "Waiting for OBS video…"; color: root.s.muted || "#aaaaaa"; font.pixelSize: root.fontPx }
                        Rectangle {
                            anchors.centerIn: thumbnail
                            width: Math.min(thumbnail.width, thumbnail.height)*0.5
                            height: width; radius: width/2
                            color: "#99000000"
                            visible: !!card.c.alertIcon
                            Glyph { anchors.fill: parent; anchors.margins: parent.width*0.18; role: card.c.alertRole || "storage"; ink: root.asColor(card.c.alertColor || root.s.glowColor || "#ffd43b"); dark: true }
                        }
                    }
                }
                Flow {
                    anchors.fill: parent; anchors.margins: 8 * (root.s.scale || 1)
                    spacing: 8 * (root.s.scale || 1)
                    visible: card.c.type === "icons"
                    Repeater {
                        model: card.c.icons || []
                        Glyph {
                            required property var modelData
                            width: 24 * (root.s.scale || 1); height: width
                            role: modelData.icon; ink: root.asColor(modelData.color); dark: !!root.s.dark
                            SequentialAnimation on scale {
                                running: !!modelData.flash
                                NumberAnimation { from: 0.82; to: 1.12; duration: 120; easing.type: Easing.OutCubic }
                                NumberAnimation { to: 1; duration: 100; easing.type: Easing.InOutQuad }
                            }
                        }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: card.cornerRadius
                    color: root.s.dark ? "#edf5ff" : "#ffffff"
                    opacity: root.motion.flash || 0
                    visible: opacity > 0
                    antialiasing: true
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: !!root.s.edit
                acceptedButtons: Qt.LeftButton
                hoverEnabled: enabled
                cursorShape: Qt.SizeAllCursor
                onPressed: controller.beginTransform(card.c.id, false)
            }
            MouseArea {
                anchors.right: parent.right; anchors.bottom: parent.bottom
                width: 20; height: 20
                visible: !!root.s.edit
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                cursorShape: root.s.independent && card.c.id !== "thumbnail" ? Qt.SizeHorCursor : Qt.SizeFDiagCursor
                onPressed: controller.beginTransform(card.c.id, true)
                Rectangle {
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.margins: 4
                    width: 10; height: 2; radius: 1
                    color: root.ink
                }
                Rectangle {
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.margins: 4
                    width: 2; height: 10; radius: 1
                    color: root.ink
                }
            }
        }
    }
}
