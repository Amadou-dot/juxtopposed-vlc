// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedQueue"
    width: 400; height: 600
    visible: true; when: windowShown
    property int activeBackdrops: 0
    readonly property url artworkFixture: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAJCAIAAAC0SDtlAAAAFUlEQVR4nGP8z0AaYCJR/agG2mgAAMeRARHH28OHAAAAAElFTkSuQmCC"
    Component {
        id: thumbnailProbe
        RedesignQueueThumbnail { theme: queue.theme; active: true }
    }
    Component {
        id: backdropProbe
        Item {
            objectName: "contextBackdropProbe"
            property real cornerRadius: 0
            property bool menuShadow: false
            Component.onCompleted: testCase.activeBackdrops++
            Component.onDestruction: testCase.activeBackdrops--
        }
    }
    ListModel { id: media }
    ListModel {
        id: treeFixture
        function populate() {
            clear()
            append({title: "Folder", isCurrent: false, queueIndex: -1, treeDepth: 0, treeGroup: true, treeExpanded: true, treeKey: "folder", treePlaylist: false, containsCurrent: true})
            for (let i = 0; i < 3; ++i) append({title: ["First", "Second", "Third"][i], isCurrent: i === 1, queueIndex: i, treeDepth: 1, treeGroup: false, treeExpanded: false, treeKey: String(i), treePlaylist: false, containsCurrent: false})
        }
        function sourceIndexAt(row) { return row >= 0 && row < count ? get(row).queueIndex : -1 }
        function rowForSourceIndex(index, reveal) { if (reveal && count === 1) populate(); return count > 1 ? index + 1 : -1 }
        function rowForGroupKey(key) { return key === "folder" ? 0 : -1 }
        function expandedAt(row) { return row === 0 && count > 1 }
        function setExpanded(row, expanded) { if (row !== 0) return; if (expanded) populate(); else { if (count > 1) remove(1, 3); setProperty(0, "treeExpanded", false) } }
        function toggleExpanded(row) { setExpanded(row, !expandedAt(row)) }
        function insertionIndexAt(row, after) { return row === 0 ? (after ? 3 : 0) : row - 1 + (after ? 1 : 0) }
        function rowForInsertion(index) { return index === 0 ? 0 : index + 1 }
    }
    RedesignQueue {
        id: queue
        anchors.fill: parent
        queueModel: media; itemCount: media.count; currentIndex: 1
        totalDurationText: "1h 12m"
        sortChoices: [{ criteria: 11, text: "Title" }, { criteria: 22, text: "Duration" }]
        onMoveRequested: (from, to) => media.move(from, to > from ? to - 1 : to, 1)
        onRemoveRequested: index => media.remove(index)
        onSortRequested: (key, descending) => { sortKey = key; sortDescending = descending }
    }
    SignalSpy { id: moveSpy; target: queue; signalName: "moveRequested" }
    SignalSpy { id: removeSpy; target: queue; signalName: "removeRequested" }
    SignalSpy { id: sortSpy; target: queue; signalName: "sortRequested" }
    SignalSpy { id: playSpy; target: queue; signalName: "playRequested" }
    SignalSpy { id: saveSpy; target: queue; signalName: "saveRequested" }
    SignalSpy { id: clearSpy; target: queue; signalName: "clearRequested" }
    function test_contextBackdropLifetime() {
        queue.videoBackdropContent = backdropProbe
        for (const name of ["queueSortMenu", "queueActionsMenu", "queueGroupMenu"]) {
            const menu = findChild(queue, name)
            compare(menu.videoBackdropContent, backdropProbe)
            menu.open()
            tryCompare(testCase, "activeBackdrops", 1)
            const backdrop = findChild(menu.background, "contextBackdropProbe")
            verify(backdrop !== null)
            compare(backdrop.width, menu.background.width)
            compare(backdrop.height, menu.background.height)
            compare(backdrop.cornerRadius, 10)
            compare(backdrop.menuShadow, true)
            for (let parent = menu.background; parent; parent = parent.parent)
                verify(parent !== queue, "The menu must stay outside its capture source.")
            menu.close()
            tryCompare(testCase, "activeBackdrops", 0)
        }
        queue.videoBackdropContent = null
    }
    function test_groupDisclosureFollowsMirroring() {
        queue.hierarchyModel = treeFixture
        const button = findChild(queue, "queueGroupToggle_0")
        verify(button !== null)
        compare(button.iconName, "chevron-down")
        try {
            queue.LayoutMirroring.childrenInherit = true
            queue.LayoutMirroring.enabled = true
            treeFixture.setExpanded(0, false)
            tryVerify(() => findChild(queue, "queueGroupToggle_0") && findChild(queue, "queueGroupToggle_0").iconName === "chevron-left")
            mouseClick(findChild(queue, "queueGroupToggle_0"))
            tryVerify(() => findChild(queue, "queueGroupToggle_0") && findChild(queue, "queueGroupToggle_0").iconName === "chevron-down")
            compare(playSpy.count, 0)
            compare(removeSpy.count, 0)
        } finally {
            queue.LayoutMirroring.enabled = false
            queue.LayoutMirroring.childrenInherit = false
        }
        treeFixture.setExpanded(0, false)
        tryVerify(() => findChild(queue, "queueGroupToggle_0") && findChild(queue, "queueGroupToggle_0").iconName === "chevron-right")
    }
    function init() {
        failOnWarning(/Binding loop|TypeError|ReferenceError|Detected recursive rearrange/)
        queue.videoBackdropContent = null
        queue.hierarchyModel = null; queue.selectedGroupKey = ""
        treeFixture.populate()
        queue.visible = true
        queue.scaleFactor = 1; queue.lightTheme = false
        media.clear()
        media.append({title: "First", isCurrent: false, artwork: "", url: ""})
        media.append({title: "Second", isCurrent: true, artwork: "", url: ""})
        media.append({title: "Third", isCurrent: false, artwork: "", url: ""})
        queue.selectedIndex = -1; queue.sortKey = -1; queue.sortDescending = false
        moveSpy.clear(); removeSpy.clear(); sortSpy.clear(); playSpy.clear(); saveSpy.clear(); clearSpy.clear()
        findChild(queue, "queueSortMenu").close()
        findChild(queue, "queueActionsMenu").close()
        findChild(queue, "queueGroupMenu").close()
        wait(50)
    }
    function test_treeGroupDisclosureAndKeyboardNeverPlayOrRemoveMedia() {
        queue.hierarchyModel = treeFixture
        wait(50)
        mouseDoubleClickSequence(findChild(queue, "queueGroup_0"))
        compare(treeFixture.count, 1)
        compare(playSpy.count, 0)
        queue.forceActiveFocus(); keyClick(Qt.Key_Delete)
        compare(removeSpy.count, 0); compare(media.count, 3)
        keyClick(Qt.Key_Right); compare(treeFixture.count, 4)
        keyClick(Qt.Key_Left); compare(treeFixture.count, 1)
        keyClick(Qt.Key_Menu)
        const menu = findChild(queue, "queueGroupMenu")
        tryCompare(menu, "opened", true); wait(150)
        mouseClick(findChild(menu, "libraryAction_toggle"))
        compare(treeFixture.count, 4); compare(playSpy.count, 0); compare(removeSpy.count, 0)
    }
    function test_treeChildActionsUseCoreIndexInsteadOfVisibleRow() {
        queue.hierarchyModel = treeFixture
        wait(50)
        const child = findChild(queue, "queueItem_1")
        mouseDoubleClickSequence(child)
        compare(playSpy.signalArguments[0][0], 1)
        mouseClick(child, 60, child.height / 2, Qt.RightButton)
        const menu = findChild(queue, "queueActionsMenu")
        tryCompare(menu, "opened", true); wait(150)
        compare(queue.selectedIndex, 1)
        mouseClick(findChild(menu, "libraryAction_remove"))
        compare(removeSpy.signalArguments[0][0], 1)
        compare(media.get(1).title, "Third")
    }
    function test_treeDragDoesNotCountVirtualHeadersAsQueueItems() {
        queue.hierarchyModel = treeFixture
        wait(50)
        const handle = findChild(queue, "queueMoveHandle_0")
        mouseDrag(handle, handle.width / 2, handle.height / 2, 0, handle.parent.height * 2)
        compare(moveSpy.count, 1)
        compare(Array.from(moveSpy.signalArguments[0]), [0, 3])
        compare(media.get(2).title, "First"); compare(playSpy.count, 0)
    }
    function test_singleClickSelectsWithoutPlaying() {
        const button = findChild(queue, "queueItem_0")
        mouseClick(button)
        compare(queue.selectedIndex, 0)
        compare(playSpy.count, 0)
        wait(500)
        mouseDoubleClickSequence(button)
        compare(playSpy.count, 1)
        queue.singleClickActivate = true
        wait(500)
        mouseClick(button)
        compare(playSpy.count, 2)
        queue.singleClickActivate = false
    }
    function test_summaryAndRemoveUseCurrentThenSelectedItem() {
        // VLC's translator uses the disambiguation string for plurals.
        // Standalone Qt has no VLC translator, so check the count and duration here.
        verify(findChild(queue, "queueSummary").text.startsWith("3 "))
        verify(findChild(queue, "queueSummary").text.endsWith(" - 1h 12m"))
        mouseClick(findChild(queue, "queueRemoveButton"))
        compare(removeSpy.signalArguments[0][0], 1)
        compare(media.get(1).title, "Third")
        mouseDoubleClickSequence(findChild(queue, "queueItem_0"))
        compare(playSpy.signalArguments[0][0], 0)
        mouseClick(findChild(queue, "queueRemoveButton"))
        compare(media.get(0).title, "Third")
        compare(findChild(queue, "queueSummary").text, "1 item - 1h 12m")
    }
    function test_dragReordersWithoutPlayingAndHandlesDownwardInsertion() {
        const handle = findChild(queue, "queueMoveHandle_0")
        mouseDrag(handle, handle.width / 2, handle.height / 2, 0, handle.parent.height * 2)
        compare(moveSpy.count, 1)
        compare(Array.from(moveSpy.signalArguments[0]), [0, 3])
        compare(media.get(0).title, "Second")
        compare(media.get(2).title, "First")
        compare(playSpy.count, 0)
    }
    function test_listKeepsOrdinaryArrowNavigation() {
        const list = findChild(queue, "queueList")
        list.currentIndex = 0
        list.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 1)
        compare(moveSpy.count, 0)
        compare(media.get(0).title, "First")
    }
    function test_keyboardMoveAndDeleteKeepSelectionOnMovedItem() {
        mouseClick(findChild(queue, "queueMoveHandle_0"))
        compare(queue.selectedIndex, 0)
        verify(queue.activeFocus)
        findChild(queue, "queueList").forceActiveFocus()
        keyClick(Qt.Key_Down, Qt.AltModifier)
        compare(media.get(1).title, "First")
        compare(queue.selectedIndex, 1)
        keyClick(Qt.Key_Delete)
        compare(media.get(0).title, "Second")
        compare(media.get(1).title, "Third")
        compare(playSpy.count, 0)
    }
    function test_hiddenQueueCancelsRowDrag() {
        const handle = findChild(queue, "queueMoveHandle_0")
        mousePress(handle, handle.width / 2, handle.height / 2)
        mouseMove(handle, handle.width / 2, handle.height / 2 + handle.parent.height * 2, 50)
        queue.visible = false
        mouseRelease(handle)
        compare(moveSpy.count, 0)
        compare(media.get(0).title, "First")
    }
    function test_reverseWithoutSortStartsWithFirstNativeCriterion() {
        queue.sortKey = 999
        mouseClick(findChild(queue, "queueSortButton"))
        const menu = findChild(queue, "queueSortMenu")
        tryCompare(menu, "opened", true); wait(200)
        mouseClick(findChild(menu, "libraryAction_reverse"))
        compare(Array.from(sortSpy.signalArguments[0]), [11, true])
    }
    function test_sortMenuAnchorsAndToggles() {
        const button = findChild(queue, "queueSortButton")
        const menu = findChild(queue, "queueSortMenu")
        mouseMove(testCase, 10, 580)
        button.forceActiveFocus(); keyClick(Qt.Key_Space)
        tryCompare(menu, "opened", true)
        const point = menu.background.mapToItem(testCase, 0, 0)
        compare(point.y, button.mapToItem(testCase, 0, button.height).y)
        verify(point.x <= button.mapToItem(testCase, 0, 0).x + button.width)
        mouseClick(button)
        tryCompare(menu, "visible", false)
    }
    function test_rightClickSelectsRowAtClickPosition_data() {
        return [{tag: "first", index: 0}, {tag: "scrolled", index: 12}]
    }
    function test_rightClickSelectsRowAtClickPosition(data) {
        for (let index = 3; index < 30; ++index) media.append({title: "Item " + index, isCurrent: false})
        const list = findChild(queue, "queueList")
        list.forceLayout()
        list.positionViewAtIndex(data.index, ListView.Beginning)
        wait(50)
        const row = findChild(queue, "queueItem_" + data.index)
        const point = row.mapToItem(testCase, 60, row.height / 2)
        mouseClick(row, 60, row.height / 2, Qt.RightButton)
        const menu = findChild(queue, "queueActionsMenu")
        tryCompare(menu, "opened", true)
        compare(queue.selectedIndex, data.index)
        const origin = menu.background.mapToItem(testCase, 0, 0)
        compare(origin.x, point.x); compare(origin.y, point.y)
        compare(playSpy.count, 0)
    }
    function test_keyboardContextScrollsToSelectedRow() {
        for (let index = 3; index < 30; ++index) media.append({title: "Item " + index, isCurrent: false})
        queue.selectedIndex = 25
        queue.forceActiveFocus(); keyClick(Qt.Key_F10, Qt.ShiftModifier)
        const menu = findChild(queue, "queueActionsMenu")
        const list = findChild(queue, "queueList")
        tryCompare(menu, "opened", true)
        verify(list.contentY > 0)
        const row = findChild(queue, "queueItem_25")
        verify(row !== null)
        const point = row.mapToItem(list, 0, 0)
        verify(point.y >= 0 && point.y + row.height <= list.height)
        compare(menu.parent, list)
        compare(playSpy.count, 0)
    }
    function test_sortAndReverseUseSelectedNativeCriterion() {
        mouseClick(findChild(queue, "queueSortButton"))
        const menu = findChild(queue, "queueSortMenu")
        tryCompare(menu, "opened", true); wait(200)
        mouseClick(findChild(menu, "libraryAction_22"))
        compare(Array.from(sortSpy.signalArguments[0]), [22, false])
        tryCompare(menu, "opened", false)
        mouseClick(findChild(queue, "queueSortButton"))
        tryCompare(menu, "opened", true); wait(200)
        verify(findChild(menu, "libraryAction_22").checked)
        mouseClick(findChild(menu, "libraryAction_reverse"))
        compare(Array.from(sortSpy.signalArguments[1]), [22, true])
    }
    function test_contextActionsSaveAndClearAndEmptyRemovalIsDisabled() {
        queue.forceActiveFocus(); keyClick(Qt.Key_Menu)
        const menu = findChild(queue, "queueActionsMenu")
        tryCompare(menu, "opened", true); wait(200)
        mouseClick(findChild(menu, "libraryAction_save"))
        compare(saveSpy.count, 1)
        tryCompare(menu, "opened", false)
        queue.forceActiveFocus(); keyClick(Qt.Key_Menu)
        tryCompare(menu, "opened", true); wait(200)
        mouseClick(findChild(menu, "libraryAction_clear"))
        compare(clearSpy.count, 1)
        media.clear()
        verify(!findChild(queue, "queueRemoveButton").enabled)
        queue.removeSelection(); compare(removeSpy.count, 0)
    }

    function test_thumbnailArtworkAndHiddenCancellation() {
        const thumbnail = createTemporaryObject(thumbnailProbe, testCase, {
            artwork: artworkFixture, mediaUrl: "file:///preview.mp4", videoPreviewsAvailable: true
        })
        verify(thumbnail)
        compare(thumbnail.width, 80); compare(thumbnail.height, 45)
        tryCompare(thumbnail, "ready", true)
        compare(findChild(thumbnail, "queueVideoImage").source.toString(), "")
        compare(findChild(thumbnail, "queueThumbnailPlaceholder").visible, false)
        thumbnail.active = false
        tryCompare(thumbnail, "ready", false)
        compare(findChild(thumbnail, "queueArtworkImage").source.toString(), "")
        compare(findChild(thumbnail, "queueVideoImage").source.toString(), "")
        compare(findChild(thumbnail, "queueThumbnailPlaceholder").visible, true)
        compare(thumbnail.width, 80); compare(thumbnail.height, 45)
    }

    function test_thumbnailFallbackAndFailurePlaceholder() {
        const thumbnail = createTemporaryObject(thumbnailProbe, testCase, {mediaUrl: "file:///clip%20%23%20%E6%97%A5.mp4"})
        const video = findChild(thumbnail, "queueVideoImage")
        compare(video.source.toString(), "")
        thumbnail.videoPreviewsAvailable = true
        compare(video.source.toString(), Qt.resolvedUrl("image://queuethumbnail/" + encodeURIComponent(thumbnail.mediaUrl.toString())).toString())
        tryCompare(video, "status", Image.Error) // Standalone runner has no native provider.
        compare(findChild(thumbnail, "queueThumbnailPlaceholder").visible, true)
        thumbnail.artwork = artworkFixture
        tryCompare(thumbnail, "ready", true)
        compare(video.source.toString(), "")
        thumbnail.artwork = "data:image/png;base64,broken"
        tryCompare(findChild(thumbnail, "queueArtworkImage"), "status", Image.Error)
        verify(video.source.toString().length > 0)
        thumbnail.mediaUrl = "https://example.invalid/video.mp4"
        compare(video.source.toString(), "")
        thumbnail.mediaUrl = "screen://"
        compare(video.source.toString(), "")
        compare(findChild(thumbnail, "queueThumbnailPlaceholder").visible, true)
    }

    function test_thumbnailRoleChangesClearOldArtwork() {
        media.setProperty(0, "artwork", artworkFixture.toString())
        const thumbnail = findChild(queue, "queueThumbnail_0")
        verify(thumbnail)
        tryCompare(thumbnail, "ready", true)
        media.setProperty(0, "artwork", "")
        media.setProperty(0, "url", "file:///replacement.mp4")
        tryCompare(thumbnail, "ready", false)
        compare(thumbnail.mediaUrl.toString(), "file:///replacement.mp4")
        compare(findChild(thumbnail, "queueArtworkImage").source.toString(), "")
    }

    function test_onlyVisibleRowsRequestImages() {
        for (let index = 3; index < 40; ++index) media.append({title: "Item " + index, isCurrent: false, artwork: artworkFixture.toString()})
        const list = findChild(queue, "queueList")
        list.cacheBuffer = 1000 // Deliberately keep offscreen delegates alive.
        list.forceLayout(); wait(50)
        function checkVisible() {
            for (let index = 0; index < list.count; ++index) {
                const row = list.itemAtIndex(index)
                if (!row) continue
                const thumbnail = findChild(row, "queueThumbnail_" + index)
                verify(thumbnail)
                const intersects = queue.visible && row.y + row.height > list.contentY && row.y < list.contentY + list.height
                compare(thumbnail.active, intersects, "viewport gating for row " + index)
                if (!intersects) compare(findChild(thumbnail, "queueArtworkImage").source.toString(), "")
            }
        }
        checkVisible()
        list.positionViewAtIndex(22, ListView.Beginning); wait(50); checkVisible()
        queue.visible = false; wait(50); checkVisible()
        queue.visible = true; wait(50); checkVisible()
        list.cacheBuffer = 320
    }

    function test_thumbnailRowsAtCompactWidthsAndScale_data() {
        return [{tag: "dark", scale: 1, light: false}, {tag: "light", scale: 1, light: true},
                {tag: "scaled", scale: 1.5, light: false}]
    }
    function test_thumbnailRowsAtCompactWidthsAndScale(data) {
        queue.scaleFactor = data.scale; queue.lightTheme = data.light
        const list = findChild(queue, "queueList")
        list.forceLayout(); wait(50)
        const row = list.itemAtIndex(1)
        const thumbnail = findChild(row, "queueThumbnail_1")
        verify(thumbnail)
        compare(row.height, 64 * data.scale)
        compare(thumbnail.width, 80 * data.scale); compare(thumbnail.height, 45 * data.scale)
        const indicator = findChild(thumbnail, "queuePlayingIndicator")
        compare(indicator.visible, true); compare(indicator.color, queue.theme.accent)
        compare(findChild(findChild(queue, "queueThumbnail_0"), "queuePlayingIndicator").visible, false)
        const title = findChild(row, "queueTitle_1")
        verify(title.width > 0)
        verify(title.mapToItem(row, title.width, 0).x <= row.width + 1)
        compare(findChild(row, "queueDuration_1").parent, title.parent)
        queue.hierarchyModel = treeFixture; list.forceLayout(); wait(50)
        compare(list.itemAtIndex(0).height, 64 * data.scale)
        compare(findChild(list.itemAtIndex(0), "queueThumbnail_-1").active, false)
    }

    function test_dragAfterScrollingUsesNewRowHeight() {
        for (let index = 3; index < 30; ++index) media.append({title: "Item " + index, isCurrent: false})
        const list = findChild(queue, "queueList")
        list.positionViewAtIndex(12, ListView.Beginning); wait(50)
        const handle = findChild(queue, "queueMoveHandle_12")
        verify(handle)
        mouseDrag(handle, handle.width / 2, handle.height / 2, 0, handle.parent.height * 2)
        compare(Array.from(moveSpy.signalArguments[0]), [12, 15])
        compare(media.get(14).title, "Item 12"); compare(playSpy.count, 0)
    }
}
