# Queue Thumbnails Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for native execution,
> or superpowers:subagent-driven-development if the user chooses delegation.

**Goal:** Add responsive queue thumbnails using artwork and cached local-video frames.

**Architecture:** A Qt asynchronous image provider owns a dedicated, serialized
worker and bounded caches. A small QML preview component requests images only
for visible rows; the existing playlist and tree models already provide artwork
and media URLs. The provider uses VLC's thumbnail preparser independently of the
media library.

**Tech Stack:** C++17, Qt 6/Qt Quick, VLC preparser, Meson and Autotools.

**Spec:** [Queue Thumbnails Design](../queue-thumbnails-design.md).

## Global constraints

- Preview: 80 × 45 logical pixels; uniform row height: 64 logical pixels.
- Decode: one worker, fast seek at 15% (35%/65% fallback for black frames), total timeout 15 seconds, maximum 320 × 180.
- Cache: 16 MiB RAM; 64 MiB and 512 disk entries; failure suppression 60 seconds.
- Generate only for local regular files; all filesystem work stays off the GUI thread.
- Preserve current queue interactions and support builds without the media library.
- Use `/tmp/vlc-repeat-build` for verification and keep `video-tests/` ignored.

## Review focus

1. Fast scrolling, duplicate entries, and recycled delegates must not show another file's image.
2. A replaced file at the same URL must not reuse its old successful or failed result.
3. Cancellation and shutdown may race synchronous preparser callbacks; avoid deadlocks and use-after-free.
4. Unsupported, corrupt, empty, missing, and network media must retain a stable placeholder without retry storms.
5. Folder rows and larger thumbnail rows must preserve drag destinations, selection, and keyboard navigation.

## Task 1: Background provider and cache

**Files:** Create `modules/gui/qt/util/queue_thumbnail_provider.{hpp,cpp}` and
`queue_thumbnail_decoder.{hpp,cpp}`. Add `modules/gui/qt/tests/test_queue_thumbnail_provider.cpp`.
Update `modules/gui/qt/meson.build`, `Makefile.am`, and `maininterface/mainui.cpp`.

**Interfaces:** `QueueThumbnailProvider` implements `QQuickAsyncImageProvider`.
Use `explicit QueueThumbnailProvider(Options options)` with `QString cacheDirectory`
and `using Decoder = std::function<QImage(const QUrl&, const std::atomic_bool&)>`.
The cancellation flag remains alive until decoding completes; the production
adapter uses VLC's preparser. Register `image://queuethumbnail/<percent-encoded-media-URL>`
and a `queueThumbnailImages` context flag. Private scheduling details remain
the implementer's responsibility.

- [ ] Write failing Qt tests: successful decode, duplicate requests invoke the
  decoder once, maximum concurrent decoder count is one, cancelled queued work
  never decodes, and a cancelled completion never replaces a newer request.
- [ ] Add cache tests: same file survives provider recreation through disk cache;
  changing size/mtime causes a fresh decode; corrupt cache entries regenerate;
  configured byte/count limits evict old entries; cancellation is retryable;
  failures suppress retries only until expiry or file change.
- [ ] Implement the provider and production decoder. Cancel outside locks used
  by VLC callbacks. Own every request until its completion/cancellation and
  release each handle once. Check cancellation before encoding or cache writes.
- [ ] Register sources/tests in both build systems and the provider in `MainUI`.
- [ ] Build and run `test_qt_queue_thumbnail_provider` with isolated XDG paths.
  Exercise shutdown with active and queued work; require all assertions to pass.
- [ ] Commit the backend after its checks pass.

## Task 2: Queue row presentation and visible requests

**Files:** Create `modules/gui/qt/player/qml/redesign/RedesignQueueThumbnail.qml`.
Modify `RedesignQueue.qml`, `RedesignPlayer.qml`, `modules/gui/qt/meson.build`,
`Makefile.am`, and `tests/tst_RedesignQueue.qml`.

**Interfaces:** The preview component takes `artwork`, `mediaUrl`, `active`,
`playing`, `videoPreviewsAvailable`, and `theme`. `active` is true only while
its non-group row intersects the shown viewport. `RedesignPlayer` supplies
provider availability; standalone QML previews can disable generation.

- [ ] Add failing QML checks for artwork loading/failure, a stable placeholder,
  viewport gating, active-item indication, source changes after delegate reuse,
  and loading cancellation when the queue closes.
- [ ] Add the 16:9 component with asynchronous artwork loading and local-preview
  fallback. Disable Qt's URL-only cache for generated images so the provider
  revalidates file identity. Keep decorations out of the accessibility tree.
- [ ] Place the preview beside the name/duration column and preserve the
  existing drag handle, group disclosure, selection, and activation controls.
- [ ] Extend interaction coverage for mixed folders/items, new row heights,
  reordering after scrolling, compact widths, dark/light themes, and 150% scale.
- [ ] Run `tst_RedesignQueue`, `tst_RedesignCustomizationControls`, and relevant
  player-layout checks. Build VLC and resolve changes introduced by this work.
- [ ] Commit the queue UI after its checks pass.

## Task 3: Real-video verification and delivery

- [ ] Decode previews from all four files in `video-tests/`, including AV1, and
  record cold-generation time and warm-cache behavior. Check portrait/aspect handling
  with the supplied 1080 × 1920 clip and failure handling with a disposable corrupt file.
- [ ] Open enough queue entries to exceed the viewport; inspect request logs
  while scrolling, closing/reopening, reordering, and removing items. Confirm
  one decoder at a time and no generation for offscreen entries.
- [ ] Review the whole diff independently; address substantive findings and
  rerun affected checks. Report any unrelated baseline failures by name.
- [ ] Push the completed feature to `dev/queue-improvements`, verify its remote
  SHA, and launch the updated VLC with the local sample queue for user testing.
