import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window
    width: 1520
    height: 940
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    title: "Vulkax Physics Studio"
    color: "#0a0d14"

    palette.window: "#0a0d14"
    palette.windowText: "#edf3ff"
    palette.base: "#111725"
    palette.text: "#edf3ff"
    palette.highlight: "#66e3c4"
    palette.button: "#1a2335"
    palette.buttonText: "#edf3ff"

    header: Rectangle {
        height: 62
        color: "#111827"
        border.color: "#26354c"
        border.width: 1
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 16
            spacing: 14
            Label {
                text: "VULKAX"
                color: "#73f1cf"
                font.bold: true
                font.pixelSize: 20
                font.letterSpacing: 1.5
            }
            Label { text: "PHYSICS LAB"; color: "#9eb0ca"; font.pixelSize: 12; font.bold: true }
            Rectangle { Layout.fillWidth: true; height: 1; color: "transparent" }
            Label { text: studio.status; color: "#aabbd3"; font.pixelSize: 12; Layout.maximumWidth: 380; elide: Label.ElideRight }
            Button { text: "Open"; onClicked: studio.openProjectDialog() }
            Button { text: "Save"; onClicked: studio.saveProjectDialog() }
            Button {
                text: "Export"
                onClicked: exportMenu.open()
            }
        }
    }

    SplitView {
        anchors.fill: parent
        anchors.margins: 1
        orientation: Qt.Horizontal

        Rectangle {
            SplitView.preferredWidth: 252
            SplitView.minimumWidth: 210
            color: "#101725"
            border.color: "#26354c"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12
                Label { text: "SIMULATION LIBRARY"; color: "#7e93b2"; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.2 }
                ListView {
                    id: presetList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    model: studio.presets
                    clip: true
                    delegate: ItemDelegate {
                        width: ListView.view.width
                        height: 70
                        highlighted: modelData.id === studio.selectedPreset
                        onClicked: studio.selectPreset(modelData.id)
                        background: Rectangle {
                            color: parent.highlighted ? "#183a42" : (parent.hovered ? "#182439" : "#131c2b")
                            border.color: parent.highlighted ? "#66e3c4" : "#26354c"
                            border.width: 1
                            radius: 4
                        }
                        contentItem: Column {
                            spacing: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 18
                            Label { text: modelData.name; color: "#eef5ff"; font.bold: true; font.pixelSize: 13 }
                            Label { text: modelData.description; color: "#9db0ca"; font.pixelSize: 10; width: parent.width; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#26354c" }
                Label { text: "LIVE ENGINE"; color: "#7e93b2"; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.2 }
                Label { text: "Metal UI + Vulkan compute\n" + studio.previewBackend; color: "#aabbd3"; font.pixelSize: 11; lineHeight: 1.25; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }

        Rectangle {
            id: centerPane
            SplitView.fillWidth: true
            color: "#090e18"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                Rectangle {
                    id: viewport
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#050914"
                    border.color: "#2a3a55"
                    border.width: 1
                    onWidthChanged: studio.setPreviewExtent(width, height, Screen.devicePixelRatio)
                    onHeightChanged: studio.setPreviewExtent(width, height, Screen.devicePixelRatio)
                    Component.onCompleted: studio.setPreviewExtent(width, height, Screen.devicePixelRatio)
                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        source: studio.previewUrl
                        cache: false
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                    Label {
                        anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 12
                        text: studio.playing ? "LIVE" : "PAUSED"
                        color: studio.playing ? "#79f2cf" : "#ffce79"; font.pixelSize: 11; font.bold: true
                        padding: 7
                        background: Rectangle { color: "#112332"; radius: 3; border.color: "#36536a" }
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: 14
                        text: studio.selectedPreset.toUpperCase() + "  /  t " + studio.timelineSeconds.toFixed(2) + " s"
                        color: "#eaf4ff"; font.pixelSize: 13; font.bold: true
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 112
                    color: "#101725"
                    border.color: "#26354c"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "REAL-TIME PLAYBACK"; color: "#7e93b2"; font.bold: true; font.pixelSize: 11; font.letterSpacing: 1.1 }
                            Item { Layout.fillWidth: true }
                            Label { text: studio.timelineSeconds.toFixed(2) + " s"; color: "#e8eceb"; font.family: "Menlo"; font.pixelSize: 12 }
                        }
                        Slider {
                            id: timeline
                            Layout.fillWidth: true
                            from: 0; to: 12; value: studio.timelineSeconds
                            onValueChanged: if (pressed) studio.seek(value)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Button { text: studio.playing ? "Pause live" : "Play live"; onClicked: studio.togglePlayback() }
                            Button { text: "Reset"; onClicked: studio.seek(0) }
                            Label { text: "Preview " + studio.renderFrameMilliseconds.toFixed(2) + " ms  |  GPU " + (studio.gpuDispatchMilliseconds < 0 ? "n/a" : studio.gpuDispatchMilliseconds.toFixed(3) + " ms") + "  |  " + studio.errorMetric + " " + studio.visualError.toFixed(5); color: "#9db0ca"; font.pixelSize: 11; Layout.leftMargin: 8 }
                        }
                    }
                }
            }
        }

        Rectangle {
            SplitView.preferredWidth: 336
            SplitView.minimumWidth: 270
            color: "#101725"
            border.color: "#26354c"
            ScrollView {
                anchors.fill: parent
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width - 28
                    x: 14
                    y: 14
                    spacing: 14
                    Label { text: "EQUATION WORKBENCH"; color: "#7e93b2"; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.2 }
                    TextArea {
                        id: expressionEditor
                        Layout.fillWidth: true
                        Layout.preferredHeight: 124
                        text: studio.expression
                        wrapMode: TextEdit.WrapAnywhere
                        selectByMouse: true
                        color: "#eef5ff"
                        font.family: "Menlo"
                        font.pixelSize: 12
                        background: Rectangle { color: "#0a0f1a"; border.color: "#2a3a55"; radius: 3 }
                        onTextChanged: if (activeFocus) studio.setExpression(text)
                    }
                    Button { text: "Compile and extract controls"; onClicked: studio.compileExpression() }
                    Label { text: studio.diagnostics; color: studio.diagnostics.startsWith("GLSL") ? "#79e6cd" : "#ffca78"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label { text: "Variables other than x, y, z and t become live controls."; color: "#7387a4"; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#26354c" }
                    Label { text: "LIVE PARAMETERS"; color: "#7e93b2"; font.pixelSize: 11; font.bold: true; font.letterSpacing: 1.2 }
                    Repeater {
                        model: studio.parameters
                        delegate: ColumnLayout {
                            id: parameterControl
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 5
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: parameterControl.modelData.name; color: "#eef5ff"; font.pixelSize: 12; font.bold: true }
                                Item { Layout.fillWidth: true }
                                Label { text: parameterControl.modelData.units; color: "#7e93b2"; font.pixelSize: 10 }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Slider {
                                    id: parameterSlider
                                    Layout.fillWidth: true
                                    from: parameterControl.modelData.minimum
                                    to: parameterControl.modelData.maximum
                                    value: parameterControl.modelData.value
                                    onValueChanged: if (pressed) studio.setParameter(parameterControl.modelData.name, value)
                                }
                                TextField {
                                    Layout.preferredWidth: 72
                                    text: Number(parameterControl.modelData.value).toFixed(3)
                                    selectByMouse: true
                                    validator: DoubleValidator { bottom: parameterControl.modelData.minimum; top: parameterControl.modelData.maximum }
                                    onEditingFinished: studio.setParameter(parameterControl.modelData.name, Number(text))
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Menu {
        id: exportMenu
        MenuItem { text: "PNG frame"; onTriggered: studio.exportPngDialog() }
        MenuItem { text: "OpenEXR frame"; onTriggered: studio.exportExrDialog() }
        MenuItem { text: "PNG sequence"; onTriggered: studio.exportSequenceDialog() }
    }

    Shortcut { sequence: "Space"; onActivated: studio.togglePlayback() }
    Shortcut { sequence: "Ctrl+S"; onActivated: studio.saveProjectDialog() }
    Shortcut { sequence: "Ctrl+O"; onActivated: studio.openProjectDialog() }
}
