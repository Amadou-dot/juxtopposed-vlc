// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_QUEUE_THUMBNAIL_PROVIDER_HPP
#define VLC_QT_QUEUE_THUMBNAIL_PROVIDER_HPP

#include <QQuickAsyncImageProvider>
#include <QImage>
#include <QUrl>
#include <atomic>
#include <functional>
#include <memory>

class QueueThumbnailProvider final : public QQuickAsyncImageProvider
{
public:
    using Decoder = std::function<QImage(const QUrl&, const std::atomic_bool&)>;
    struct Options {
        QString cacheDirectory;
        Decoder decode;
        int memoryBytes = 16 * 1024 * 1024;
        qint64 diskBytes = 64 * 1024 * 1024;
        int diskEntries = 512;
        std::function<qint64()> now;
    };
    explicit QueueThumbnailProvider(Options options);
    ~QueueThumbnailProvider() override;
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

#endif
