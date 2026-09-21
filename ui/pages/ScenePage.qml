import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import QtQuick3D
import QtQuick3D.Helpers
import Jarvis.Scene3D
import Jarvis.Theme

Pane {
    id: page
    padding: 0
    background: null
    palette.window: "#05080c"
    palette.base: "#0b1119"
    palette.button: "#142032"
    palette.buttonText: "#e0ecff"
    palette.text: "#e0ecff"
    palette.windowText: "#a8c1e8"
    palette.highlight: "#408cff"
    palette.mid: "#45607e"
    palette.placeholderText: "#7089b4"
    objectName: "scenePage"
    property var selectedSpec: Scene3D.selected >= 0 ? Scene3D.items[Scene3D.selected] : ({})
    property real turn: 0
    property real pulse: 1
    readonly property bool drawing: visible && Window.window && Window.window.visibility !== Window.Minimized && Window.window.visibility !== Window.Hidden
    function resetView() {
        orbit.position = Qt.vector3d(0,0,0)
        orbit.eulerRotation = Qt.vector3d(-18,25,0)
        camera.z = Math.max(540,Scene3D.items.length*260)
        page.turn = 0
    }
    Connections { target: Scene3D; function onCameraReset() { page.resetView() } }
    Component.onCompleted: resetView()
    NumberAnimation on turn { from: 0; to: 360; duration: 28000; loops: Animation.Infinite; running: page.drawing && Scene3D.rotating && Scene3D.items.length > 0 }
    SequentialAnimation on pulse {
        running: page.drawing && Scene3D.pulsing && Scene3D.items.length > 0; loops: Animation.Infinite
        NumberAnimation { to: 1.035; duration: 1200; easing.type: Easing.InOutSine }
        NumberAnimation { to: 1; duration: 1200; easing.type: Easing.InOutSine }
    }
    RowLayout {
        visible: !Scene3D.drawingMode
        anchors.fill: parent; spacing: 16
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
            RowLayout {
                Label { text: "3D / СТУДИЯ"; color: Theme.textPrimary; font.family: Theme.displayFamily; font.pixelSize: 24; Layout.fillWidth: true }
                Label { text: "ЛОКАЛЬНО"; color: Theme.success; font.pixelSize: 12 }
                Button { text: "Сброс камеры"; onClicked: page.resetView() }
                Button { text: "На весь экран"; onClicked: Scene3D.fullscreenRequested() }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; color: Theme.backgroundBase
                border.color: Theme.hairline; radius: 12; clip: true
                View3D {
                    id: viewport; anchors.fill: parent
                    environment: SceneEnvironment {
                        clearColor: Theme.backgroundBase; backgroundMode: SceneEnvironment.Color
                        antialiasingMode: SceneEnvironment.MSAA; antialiasingQuality: SceneEnvironment.Medium
                    }
                    Node {
                        id: orbit; eulerRotation: Qt.vector3d(-18,25,0)
                        PerspectiveCamera { id: camera; z: 600; clipNear: 1; clipFar: 20000 }
                    }
                    camera: camera
                    DirectionalLight { eulerRotation: Qt.vector3d(-35,-30,0); brightness: Scene3D.lighting*1.3; ambientColor: "#505050" }
                    DirectionalLight { eulerRotation: Qt.vector3d(20,140,0); brightness: Scene3D.lighting*0.75; color: "#9ac9ff" }
                    Node {
                        eulerRotation.y: page.turn
                        Repeater3D {
                            model: Scene3D.items
                            delegate: Model {
                                id: objectModel
                                required property var modelData
                                required property int index
                                property int itemIndex: index
                                x: (index-(Scene3D.items.length-1)/2)*350
                                scale: Qt.vector3d(100,100,100).times(Scene3D.pulsing?page.pulse:1)
                                pickable: true
                                geometry: SceneGeometry { id: mesh; spec: objectModel.modelData }
                                materials: PrincipledMaterial {
                                    baseColor: objectModel.modelData.color
                                    metalness: objectModel.modelData.material === "metal" ? 0.85 : 0
                                    roughness: objectModel.modelData.material === "plastic" ? 0.48 : 0.18
                                    opacity: objectModel.modelData.material === "glass" ? 0.35 : 1
                                    alphaMode: objectModel.modelData.material === "glass" ? PrincipledMaterial.Blend : PrincipledMaterial.Opaque
                                    cullMode: Material.NoCulling; vertexColorsEnabled: true
                                }
                                Node {
                                    y: -1.8
                                    scale: Qt.vector3d(0.01,0.01,0.01)
                                    Text { text: mesh.busy ? "Построение…" : mesh.error || (mesh.triangles.toLocaleString()+" треугольников"); color: mesh.error?Theme.danger:Theme.textSecondary; font.pixelSize: 14 }
                                }
                            }
                        }
                    }
                    // Axes use the same mathematical convention as the graph:
                    // world Y is function height z, world Z is mathematical y.
                    Model { source: "#Cube"; position: Qt.vector3d(0,-140,0); scale: Qt.vector3d(3.6,0.012,0.012); materials: PrincipledMaterial { baseColor: "#e77878"; lighting: PrincipledMaterial.NoLighting } }
                    Model { source: "#Cube"; position: Qt.vector3d(-180,40,0); scale: Qt.vector3d(0.012,3.6,0.012); materials: PrincipledMaterial { baseColor: "#78aef1"; lighting: PrincipledMaterial.NoLighting } }
                    Model { source: "#Cube"; position: Qt.vector3d(-180,-140,180); scale: Qt.vector3d(0.012,0.012,3.6); materials: PrincipledMaterial { baseColor: "#7bcca1"; lighting: PrincipledMaterial.NoLighting } }
                    Node { position: Qt.vector3d(190,-140,0); Text { text: "x"; color: "#e77878"; font.pixelSize: 18 } }
                    Node { position: Qt.vector3d(-180,230,0); Text { text: "z"; color: "#78aef1"; font.pixelSize: 18 } }
                    Node { position: Qt.vector3d(-180,-140,370); Text { text: "y"; color: "#7bcca1"; font.pixelSize: 18 } }
                }
                OrbitCameraController { anchors.fill: parent; origin: orbit; camera: camera; panEnabled: true }
                TapHandler { onTapped: function(eventPoint) { const hit=viewport.pick(eventPoint.position.x,eventPoint.position.y); if(hit.objectHit && hit.objectHit.itemIndex !== undefined) Scene3D.selected=hit.objectHit.itemIndex } }
                Label { anchors.centerIn: parent; visible: Scene3D.items.length===0; text: "Что построим?\nКуб, дом, кружку или график функции"; horizontalAlignment: Text.AlignHCenter; color: Theme.textSecondary; font.pixelSize: 20 }
                Label { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 16; text: "ЛКМ — вращать   ·   Ctrl + ЛКМ — перемещать   ·   Колесо — приблизить"; color: Theme.textSecondary; font.pixelSize: 12 }
            }
            RowLayout {
                TextField { id: prompt; Layout.fillWidth: true; placeholderText: "Покажи график z = sin(x) * cos(y)"; color: Theme.textPrimary; onAccepted: Scene3D.submit(text); selectByMouse: true }
                Button { text: "Построить"; onClicked: Scene3D.submit(prompt.text) }
            }
            Label { text: Scene3D.status; color: Theme.textSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 13 }
        }
        Rectangle {
            Layout.preferredWidth: 235; Layout.fillHeight: true; color: Theme.backgroundRaised; radius: 12; border.color: Theme.hairline
            ScrollView {
                anchors.fill: parent; anchors.margins: 14; contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: parent.width; spacing: 10
                    Label { text: "СЦЕНА"; color: Theme.textPrimary; font.pixelSize: 16 }
                    ComboBox {
                        Layout.fillWidth: true; model: Scene3D.items; textRole: "kind"; currentIndex: Scene3D.selected
                        onActivated: Scene3D.selected=currentIndex
                    }
                    RowLayout { Button { text: "Куб"; onClicked: Scene3D.submit("добавь куб") } Button { text: "Дом"; onClicked: Scene3D.submit("добавь дом") } }
                    RowLayout { Button { text: "Кружка"; onClicked: Scene3D.submit("добавь кружку") } Button { text: "Сфера"; onClicked: Scene3D.submit("добавь сферу") } }
                    Label { text: "Материал"; color: Theme.textSecondary }
                    ComboBox {
                        Layout.fillWidth: true; model: ["Пластик","Металл","Стекло"]
                        currentIndex: ["plastic","metal","glass"].indexOf(page.selectedSpec.material)
                        onActivated: Scene3D.setParameter("material",["plastic","metal","glass"][currentIndex])
                    }
                    Label { text: "Цвет"; color: Theme.textSecondary }
                    Row {
                        spacing: 6
                        Repeater {
                            model: ["#b4c9df","#408cff","#e25959","#58c58a","#d3a34f"]
                            delegate: Button {
                                required property string modelData
                                width: 34; height: 34; Accessible.name: "Цвет "+modelData
                                background: Rectangle { color: modelData; radius: 17; border.width: page.selectedSpec.color===modelData?3:0; border.color: Theme.textPrimary }
                                onClicked: Scene3D.setParameter("color",modelData)
                            }
                        }
                    }
                    TextField { Layout.fillWidth: true; placeholderText: "#RRGGBB"; text: page.selectedSpec.color || ""; onEditingFinished: Scene3D.setParameter("color",text) }
                    CheckBox { text: "Шахматная текстура"; checked: page.selectedSpec.texture==="checker"; onClicked: Scene3D.setParameter("texture",checked?"checker":"none") }
                    Label { text: "Размеры: ширина / высота / глубина"; color: Theme.textSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Repeater {
                        model: ["width","height","depth"]
                        delegate: Slider { required property string modelData; Layout.fillWidth: true; from: 0.2; to: 3; value: page.selectedSpec[modelData] || 1; onPressedChanged: if(!pressed) Scene3D.setParameter(modelData,value) }
                    }
                    Label { text: "Детализация"; color: Theme.textSecondary }
                    ComboBox { Layout.fillWidth: true; model: ["Быстро / 24","Обычно / 64","Подробно / 256","Максимум / 700"]; displayText: "Сетка: "+(page.selectedSpec.resolution || 48); onActivated: Scene3D.setParameter("resolution",[24,64,256,700][currentIndex]) }
                    Label { text: "Освещение"; color: Theme.textSecondary }
                    Slider { Layout.fillWidth: true; from: 0.2; to: 3; value: Scene3D.lighting; onMoved: Scene3D.lighting=value }
                    CheckBox { text: "Автовращение"; checked: Scene3D.rotating; onClicked: Scene3D.rotating=checked }
                    CheckBox { text: "Пульсация"; checked: Scene3D.pulsing; onClicked: Scene3D.pulsing=checked }
                    RowLayout { Button { text: "OBJ"; onClicked: Scene3D.requestExport("obj") } Button { text: "GLTF"; onClicked: Scene3D.requestExport("gltf") } }
                    Button { text: "Удалить модель"; onClicked: Scene3D.removeSelected(); Layout.fillWidth: true }
                }
            }
        }
    }
    DrawingView { anchors.fill: parent; visible: Scene3D.drawingMode }
}
