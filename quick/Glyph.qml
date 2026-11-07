import QtQuick
Image {
    property string role: "storage"
    property color ink: "white"
    property bool dark: true
    source: role ? "image://glyph/" + role + "/" + ink.toString().replace("#", "") + "/" + (dark ? "dark" : "light") : ""
    sourceSize: Qt.size(Math.max(32, width * 3), Math.max(32, height * 3))
    fillMode: Image.PreserveAspectFit
    smooth: true
    asynchronous: false
}
