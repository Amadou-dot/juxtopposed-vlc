// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_preparser.h>
#include <vlc_picture.h>
#include <vlc_block.h>
#include "queue_thumbnail_decoder.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>

QueueThumbnailProvider::Decoder makeQueueThumbnailDecoder(vlc_object_t* context)
{
    return [context](const QUrl& url, const std::atomic_bool& cancelled) -> QImage {
        if (cancelled || !url.isLocalFile()) return {};
        vlc_preparser_cfg config{};
        config.types = VLC_PREPARSER_TYPE_THUMBNAIL;
        config.max_thumbnailer_threads = 1;
        config.timeout = VLC_TICK_FROM_SEC(15);
        config.external_process = false;
        std::unique_ptr<vlc_preparser_t, decltype(&vlc_preparser_Delete)> preparser(
            vlc_preparser_New(context, &config), vlc_preparser_Delete);
        if (!preparser) return {};
        std::unique_ptr<input_item_t, decltype(&input_item_Release)> item(
            input_item_New(url.toEncoded().constData(), nullptr), input_item_Release);
        if (!item) return {};
        // A background frame must not use every CPU or open hardware outputs
        // while the user's main player is decoding another video.
        for (const char* option : {"avcodec-hw=none", "avcodec-threads=1", "dav1d-thread-frames=1"})
            input_item_AddOption(item.get(), option, VLC_INPUT_OPTION_TRUSTED);
        struct Result {
            Result(vlc_object_t* context, const std::atomic_bool& cancelled)
                : context(context), cancelled(cancelled) {}
            vlc_object_t* context;
            const std::atomic_bool& cancelled;
            std::mutex mutex;
            std::condition_variable changed;
            bool ended = false;
            QImage image;
        } result{context, cancelled};
        static const vlc_thumbnailer_cbs callbacks = {
            [](vlc_preparser_req*, int status, picture_t* picture, void* opaque) {
                auto& result = *static_cast<Result*>(opaque);
                QImage image;
                if (status == VLC_SUCCESS && picture && !result.cancelled) {
                    const auto& source = picture->format;
                    const double aspect = double(source.i_visible_width) / std::max(1u, source.i_visible_height)
                        * (source.i_sar_num && source.i_sar_den ? double(source.i_sar_num) / source.i_sar_den : 1.0);
                    block_t* pixels = nullptr;
                    video_format_t format{};
                    const bool wide = aspect >= 320.0 / 180;
                    if (picture_Export(result.context, &pixels, &format, picture, VLC_CODEC_RGBA,
                                       wide ? 320 : 0, wide ? 0 : 180, false) == VLC_SUCCESS) {
                        if (format.i_width > 0 && format.i_width <= 320 && format.i_height > 0
                            && format.i_height <= 180 && pixels->i_buffer >= size_t(format.i_width) * format.i_height * 4)
                            image = QImage(pixels->p_buffer, format.i_width, format.i_height,
                                           format.i_width * 4, QImage::Format_RGBA8888).copy();
                        block_Release(pixels);
                    }
                }
                {
                    std::lock_guard lock(result.mutex);
                    result.image = std::move(image);
                    result.ended = true;
                }
                result.changed.notify_one();
            }
        };
        vlc_thumbnailer_arg args{};
        args.seek.type = decltype(args.seek)::VLC_THUMBNAILER_SEEK_POS;
        args.seek.pos = 0.15;
        args.seek.speed = decltype(args.seek)::VLC_THUMBNAILER_SEEK_FAST;
        std::unique_ptr<vlc_preparser_req, decltype(&vlc_preparser_req_Release)> request(
            vlc_preparser_req_NewThumbnail(preparser.get(), item.get(), &args, &callbacks, &result),
            vlc_preparser_req_Release);
        if (!request || vlc_preparser_Submit(preparser.get(), request.get()) != VLC_SUCCESS) return {};
        msg_Dbg(context, "Queue thumbnail decode started");
        std::unique_lock lock(result.mutex);
        while (!result.ended && !cancelled)
            result.changed.wait_for(lock, std::chrono::milliseconds(25));
        if (!result.ended) {
            // Cancel may invoke the callback synchronously: never hold its lock.
            lock.unlock();
            vlc_preparser_Cancel(preparser.get(), request.get());
            lock.lock();
            result.changed.wait(lock, [&] { return result.ended; });
        }
        const auto image = cancelled ? QImage{} : result.image;
        lock.unlock();
        // Join callback execution before destroying its stack-owned context.
        preparser.reset();
        msg_Dbg(context, "Queue thumbnail decode %s", cancelled ? "cancelled" : image.isNull() ? "unavailable" : "finished");
        return image;
    };
}
