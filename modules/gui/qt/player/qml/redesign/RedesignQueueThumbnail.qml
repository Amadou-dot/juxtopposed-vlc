// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Rectangle {
    id: root
    required property RedesignTheme theme
    property url artwork
    property url mediaUrl
    property bool active: false
    property bool playing: false
    property bool videoPreviewsAvailable: false
    readonly property bool ready: artworkImage.status === Image.Ready || videoImage.status === Image.Ready

    implicitWidth: theme.dp(80)
    implicitHeight: theme.dp(45)
    color: theme.field
    clip: true
    Accessible.ignored: true

    RedesignIcon {
        objectName: "queueThumbnailPlaceholder"
        anchors.centerIn: parent
        width: root.theme.dp(22); height: width
        name: "video"; color: root.theme.playerLabel
        visible: !root.ready
        Accessible.ignored: true
    }
    Image {
        id: artworkImage
        objectName: "queueArtworkImage"
        anchors.fill: parent
        source: root.active ? root.artwork : ""
        asynchronous: true
        sourceSize: Qt.size(320, 180)
        fillMode: Image.PreserveAspectFit
        visible: status === Image.Ready
        Accessible.ignored: true
    }
    Image {
        id: videoImage
        objectName: "queueVideoImage"
        anchors.fill: parent
        // Withdrawing the source cancels offscreen work. The provider validates
        // file revisions; Qt's URL-only image cache must not bypass that check.
        source: root.active && root.videoPreviewsAvailable && root.mediaUrl.toString().startsWith("file:")
            && (!root.artwork.toString().length || artworkImage.status === Image.Error)
            ? "image://queuethumbnail/" + encodeURIComponent(root.mediaUrl.toString()) : ""
        asynchronous: true
        cache: false
        sourceSize: Qt.size(320, 180)
        fillMode: Image.PreserveAspectFit
        visible: status === Image.Ready
        Accessible.ignored: true
    }
    Rectangle {
        objectName: "queuePlayingIndicator"
        anchors.left: parent.left; anchors.bottom: parent.bottom
        anchors.margins: root.theme.dp(3)
        width: root.theme.dp(18); height: width
        radius: root.theme.dp(3)
        color: root.theme.accent
        visible: root.playing
        Accessible.ignored: true
        RedesignIcon {
            anchors.centerIn: parent
            width: root.theme.dp(12); height: width
            name: "play"; color: "#161616"
            Accessible.ignored: true
        }
    }
}
