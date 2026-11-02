// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_QUEUE_THUMBNAIL_DECODER_HPP
#define VLC_QT_QUEUE_THUMBNAIL_DECODER_HPP

#include "queue_thumbnail_provider.hpp"
struct vlc_object_t;

// The caller keeps context alive until the provider's worker has stopped.
QueueThumbnailProvider::Decoder makeQueueThumbnailDecoder(vlc_object_t* context);

#endif
