import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Jarvis.Scene3D
import Jarvis.App

Item {
    id: root
    property real progress: 0
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property real yaw: 0
    property real pitch: 0
    property var segments: []
    property real totalLength: 1
    property real span: 10
    property var center: [0,0,0]
    function rebuild() {
        let result=[],length=0,min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity]
        for (const path of Scene3D.strokes) {
            const points=path.points
            for (let i=0;i<points.length;i++) {
                const p=[points[i][0],points[i][1],points[i][2]||0]
                for(let j=0;j<3;j++){min[j]=Math.min(min[j],p[j]);max[j]=Math.max(max[j],p[j])}
                if(i>0){const a=[points[i-1][0],points[i-1][1],points[i-1][2]||0];const d=Math.hypot(p[0]-a[0],p[1]-a[1],p[2]-a[2]);result.push({a:a,b:p,start:length,length:d,color:path.color||"#65cfff"});length+=d}
            }
        }
        for(const label of Scene3D.drawingLabels){const p=[label.x,label.y,0];for(let j=0;j<3;j++){min[j]=Math.min(min[j],p[j]-(j<2?1:0));max[j]=Math.max(max[j],p[j]+(j<2?1:0))}}
        segments=result;totalLength=Math.max(.001,length)
        const populated=result.length || Scene3D.drawingLabels.length
        center=populated?min.map((v,i)=>(v+max[i])/2):[0,0,0]
        span=populated?Math.max(1,max[0]-min[0],max[1]-min[1],max[2]-min[2]):10
        resetCamera();reveal.restart()
    }
    function resetCamera(){zoom=Scene3D.drawingLabels.length?1.45:1;panX=0;panY=0;yaw=0;pitch=0;canvas.requestPaint()}
    Connections { target: Scene3D; function onDrawingChanged(){root.rebuild()} }
    Component.onCompleted: rebuild()
    NumberAnimation { id: reveal; target: root; property: "progress"; from: 0; to: 1; duration: Math.min(10000,Math.max(2200,root.segments.length*8)); paused: !root.visible || (root.Window.window && root.Window.window.visibility === Window.Minimized) }
    onProgressChanged: canvas.requestPaint()
    Rectangle { anchors.fill: parent; color: "#000000" }
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true; spacing: 5
                Label { text: "J A R V I S  /  LIVE DRAW"; color: "#65cfff"; font.pixelSize: 12 }
                Label { text: Scene3D.drawingTitle; color: "#e3f4ff"; font.pixelSize: 24; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            Button { text: "Заново"; enabled: root.segments.length>0 || Scene3D.drawingLabels.length>0; onClicked: reveal.restart() }
            Button { text: "Сброс вида"; onClicked: root.resetCamera() }
            Button { text: "Во весь экран"; onClicked: Scene3D.fullscreenRequested() }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            Canvas {
                id: canvas; anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx=getContext("2d");ctx.reset();ctx.clearRect(0,0,width,height)
                    const scale=Math.min(width,height)*.76/root.span*root.zoom
                    function project(p){const x=p[0]-root.center[0],y=p[1]-root.center[1],z=p[2]-root.center[2];const xx=x*Math.cos(root.yaw)+z*Math.sin(root.yaw),zz=-x*Math.sin(root.yaw)+z*Math.cos(root.yaw);return [width/2+root.panX+xx*scale,height/2+root.panY-(y*Math.cos(root.pitch)-zz*Math.sin(root.pitch))*scale]}
                    const end=root.progress*root.totalLength;let tip=null
                    ctx.lineCap="round";ctx.lineJoin="round"
                    for(const s of root.segments){if(s.start>end)break;const t=Math.min(1,(end-s.start)/Math.max(s.length,.000001));const a=project(s.a),b=project(s.a.map((v,i)=>v+(s.b[i]-v)*t));ctx.beginPath();ctx.moveTo(a[0],a[1]);ctx.lineTo(b[0],b[1]);ctx.strokeStyle=s.color;ctx.globalAlpha=.13;ctx.lineWidth=6;ctx.stroke();ctx.globalAlpha=1;ctx.lineWidth=1.5;ctx.stroke();tip=b}
                    if(tip && root.progress<1){ctx.beginPath();ctx.arc(tip[0],tip[1],3,0,Math.PI*2);ctx.fillStyle="#e4fbff";ctx.fill()}
                    ctx.globalAlpha=1;ctx.textAlign="center";ctx.textBaseline="middle"
                    for(const label of Scene3D.drawingLabels){if(root.progress<(label.at||0))continue;const p=project([label.x,label.y,0]);const lines=label.text.split("\n");const size=18;ctx.font=size+"px Segoe UI";for(let i=0;i<lines.length;i++){const y=p[1]+(i-(lines.length-1)/2)*size*1.5;const w=ctx.measureText(lines[i]).width;ctx.fillStyle="#000000";ctx.fillRect(p[0]-w/2-5,y-size*.65,w+10,size*1.3);ctx.fillStyle=i===0?"#e4f6ff":"#95b7ca";ctx.fillText(lines[i],p[0],y)}}
                }
            }
            MouseArea {
                anchors.fill: parent; acceptedButtons: Qt.LeftButton|Qt.RightButton
                property real lastX: 0; property real lastY: 0
                onPressed: function(mouse){lastX=mouse.x;lastY=mouse.y}
                onPositionChanged: function(mouse){if(!pressed)return;const dx=mouse.x-lastX,dy=mouse.y-lastY;if(mouse.buttons&Qt.RightButton){root.yaw+=dx*.007;root.pitch+=dy*.007}else{root.panX+=dx;root.panY+=dy}lastX=mouse.x;lastY=mouse.y;canvas.requestPaint()}
                onWheel: function(wheel){root.zoom=Math.max(.2,Math.min(10,root.zoom*Math.pow(1.001,wheel.angleDelta.y)));canvas.requestPaint()}
                onDoubleClicked: root.resetCamera()
            }
            Label { anchors.centerIn: parent; visible: root.segments.length===0 && Scene3D.drawingLabels.length===0; text: "Опиши — я нарисую\nИли попроси объяснить по шагам"; color: "#708b9f"; font.pixelSize: 22; horizontalAlignment: Text.AlignHCenter }
            Label { anchors.bottom: parent.bottom; text: root.progress<1 && root.segments.length?"РИСУЮ  ·  "+Math.round(root.progress*100)+"%":"ЛКМ — перемещение   ·   Колесо — масштаб   ·   ПКМ — поворот"; color: "#6a97b2"; font.pixelSize: 12 }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: request; Layout.fillWidth: true; placeholderText: "Нарисуй дом с большим круглым окном…"; enabled: !Agent.busy; onAccepted: {Scene3D.requestDrawing(text);text=""} }
            Button { text: Agent.busy?"Думаю…":"Нарисовать"; enabled: !Agent.busy && request.text.trim().length>0; onClicked: {Scene3D.requestDrawing(request.text);request.text=""} }
        }
    }
}
