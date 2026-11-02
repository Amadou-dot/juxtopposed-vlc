# Queue Thumbnails Design

Status: approved implementation for `dev/queue-improvements`.

## Intent and scope

Make queue items recognizable without opening each video. The user approved
existing artwork plus background generation for local videos without artwork.
Keep playback, scrolling, selection, reordering, and folder expansion responsive.

## Row presentation

- Use an 80 × 45 logical-pixel preview, scaled with the interface: a 16:9 frame.
- Put the existing display name above the duration beside the preview. Preserve
  metadata titles and the existing filename fallback, with elision and full-name
  tooltips.
- Show an orange play indicator for the current item. Retain selection styling,
  keyboard actions, drag handles, context menus, and activation preferences.
- Keep a stable placeholder under loading, missing, failed, or unsupported images.
  Fit images without distortion; letterbox portrait videos and square cover art.
- Use uniform 64-pixel rows, including folder headers, so the existing drag
  calculations remain valid. Folder headers keep their folder/playlist icon.
- Test dark/light themes, compact queue widths, scaling, and nested folders.

## Image selection and scheduling

Prefer the playlist model's artwork. If it is absent or fails to load, request
a generated frame for a local regular file. Network streams and devices keep
the placeholder; this feature does not probe them for video frames.

Only rows intersecting the visible queue viewport request images. Hiding the
queue or scrolling a row away withdraws its request. One dedicated background
decoder processes the remaining visible work. Duplicate requests share work.
Obsolete work is cancelled; stale completions cannot update recycled rows.
No directory scanning, file-stat calls, decoding, or cache maintenance runs on
the GUI thread.

Use VLC's existing thumbnail preparser rather than an external program. Seek
quickly to approximately 15% of the file, with a 15-second timeout. Export a
frame bounded to 320 × 180 pixels while preserving aspect ratio. This must work
when the media-library feature is disabled.

## Cache and lifecycle

Use a 16 MiB memory cache and a disk cache capped at both 64 MiB and 512 images
under the application's cache directory. Identify content by normalized local
URL, file size, modification time, and a cache-format version. Recheck identity
before publishing a generated result. Write cache files atomically and evict
least-recently-used entries. Never write beside the source videos.

Remember failures for 60 seconds in a bounded memory cache to avoid repeated
decoding of broken media. File changes invalidate failures and successful
images. Cancellation is not a cached failure. Closing VLC cancels pending work
and joins worker activity before releasing VLC resources.

## Alternatives and decisions

Artwork-only rows would be smaller but leave ordinary local clips blank; the
user selected artwork plus generated frames. Eagerly generating an entire
folder would compete with playback, so generation is limited to visible rows.
The existing media-library collector requires library IDs; a queue-specific
asynchronous image provider supports ordinary dropped files and disabled-library
builds. Dimensions, cache limits, seek position, and timeout are implementation
defaults chosen for this first pass.

## Verification

Cover successful artwork, fallback generation, file changes, duplicate requests,
cache eviction, cancellation, failed decoding, and shutdown. Exercise real VLC
decoding with the local `video-tests/` clips, including 4K AV1 and the long H.264
walkthrough. Measure cold/warm requests and verify that opening a large queue
does not request offscreen files. Keep all sample videos out of Git.
