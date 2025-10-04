import QtQuick 2.15
import QtQuick.Controls 2.15

ApplicationWindow {
    width: 800
    height: 600
    visible: true
    title: "Desktop Live Whisper (Preview)"

    Column {
        import QtQuick
        import QtQuick.Controls
        padding: 8

        Button {
            id: listenBtn
            text: "Listen"
        }

        ScrollView {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            height: parent.height - listenBtn.height - 32
            TextArea {
                id: transcript
                readOnly: true
                wrapMode: TextArea.Wrap
                text: ""
            }
        }
    }
}
