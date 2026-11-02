// SPDX-License-Identifier: GPL-2.0-or-later
#include "queue_thumbnail_provider.hpp"
#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QQuickTextureFactory>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace {
class ThumbnailResponse;
struct Subscriber {
    ThumbnailResponse* response = nullptr;
    std::atomic_bool cancelled{false};
};
struct Job {
    QUrl url;
    QString key;
    std::mutex mutex;
    std::vector<std::shared_ptr<Subscriber>> subscribers;
    std::atomic_bool cancelled{false};
};

class ThumbnailResponse final : public QQuickImageResponse
{
public:
    ThumbnailResponse(std::shared_ptr<Job> job, std::shared_ptr<Subscriber> subscriber, QSize size)
        : m_job(std::move(job)), m_subscriber(std::move(subscriber)), m_size(size) {}

    void cancel() override
    {
        m_subscriber->cancelled = true;
        std::lock_guard lock(m_job->mutex);
        if (!m_job->subscribers.empty() && std::all_of(m_job->subscribers.begin(), m_job->subscribers.end(),
                        [](const auto& subscriber) { return subscriber->cancelled.load(); }))
            m_job->cancelled = true;
    }

    void complete(const QImage& image)
    {
        if (!m_subscriber->cancelled) m_image = image;
        // The engine owns this response until finished. Queue notification so
        // even a cache hit cannot finish before the caller connects to it.
        QMetaObject::invokeMethod(this, [this] { emit finished(); }, Qt::QueuedConnection);
    }

    QString errorString() const override
    {
        return m_image.isNull() || m_subscriber->cancelled
            ? QStringLiteral("Queue preview unavailable") : QString{};
    }

    QQuickTextureFactory* textureFactory() const override
    {
        if (m_subscriber->cancelled || m_image.isNull()) return nullptr;
        const auto image = m_size.isValid() && (m_image.width() > m_size.width() || m_image.height() > m_size.height())
            ? m_image.scaled(m_size, Qt::KeepAspectRatio, Qt::SmoothTransformation) : m_image;
        return QQuickTextureFactory::textureFactoryForImage(image);
    }

private:
    std::shared_ptr<Job> m_job;
    std::shared_ptr<Subscriber> m_subscriber;
    QSize m_size;
    QImage m_image;
};

QString fingerprint(const QUrl& url)
{
    if (!url.isLocalFile()) return {};
    const QFileInfo file(url.toLocalFile());
    if (!file.isFile() || !file.isReadable()) return {};
    const auto path = file.canonicalFilePath();
    if (path.isEmpty()) return {};
    const QByteArray identity = QByteArray("queue-v1\0", 9) + QUrl::fromLocalFile(path).toEncoded()
        + '\0' + QByteArray::number(file.size()) + '\0'
        + QByteArray::number(file.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}
}

struct QueueThumbnailProvider::State
{
    explicit State(Options settings) : options(std::move(settings)), memory(std::max(0, options.memoryBytes))
    {
        if (!options.now) options.now = [] { return QDateTime::currentMSecsSinceEpoch(); };
        worker = std::thread([this] { run(); });
    }

    ~State()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            for (const auto& job : pending) job->cancelled = true;
            if (active) active->cancelled = true;
        }
        changed.notify_one();
        worker.join();
    }

    void touch(const QString& path)
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            file.setFileTime(QDateTime::fromMSecsSinceEpoch(options.now()), QFileDevice::FileModificationTime);
    }

    void prune()
    {
        if (options.cacheDirectory.isEmpty()) return;
        const QDir directory(options.cacheDirectory);
        const auto candidates = directory.entryInfoList({QStringLiteral("*.png")},
            QDir::Files | QDir::NoSymLinks, QDir::Time | QDir::Reversed);
        static const QRegularExpression owned(QStringLiteral("^[a-f0-9]{64}\\.png$"));
        QFileInfoList entries;
        qint64 bytes = 0;
        for (const auto& file : candidates) {
            if (!owned.match(file.fileName()).hasMatch()) continue;
            entries.append(file); bytes += file.size();
        }
        int count = entries.size();
        for (const auto& file : entries) {
            if (bytes <= options.diskBytes && count <= options.diskEntries) break;
            if (QFile::remove(file.filePath())) { bytes -= file.size(); --count; }
        }
    }

    QImage load(const std::shared_ptr<Job>& job)
    {
        if (job->cancelled) return {};
        const auto key = fingerprint(job->url);
        if (key.isEmpty() || job->cancelled) return {};
        if (const auto* cached = memory.object(key)) {
            if (!options.cacheDirectory.isEmpty()) touch(QDir(options.cacheDirectory).filePath(key + ".png"));
            return !job->cancelled && fingerprint(job->url) == key ? *cached : QImage{};
        }
        if (const auto* expiry = failures.object(key); expiry && *expiry > options.now()) return {};
        failures.remove(key);
        const auto cachePath = options.cacheDirectory.isEmpty() ? QString{}
            : QDir(options.cacheDirectory).filePath(key + ".png");
        if (!cachePath.isEmpty()) {
            QImageReader reader(cachePath, "PNG");
            const auto size = reader.size();
            if (size.isValid() && size.width() <= 320 && size.height() <= 180) {
                const auto cached = reader.read();
                if (!cached.isNull() && fingerprint(job->url) == key && !job->cancelled) {
                    touch(cachePath);
                    memory.insert(key, new QImage(cached), int(cached.sizeInBytes()));
                    return cached;
                }
            }
        }
        auto image = options.decode ? options.decode(job->url, job->cancelled) : QImage{};
        // A replacement at this path or a cancelled subscriber must not poison
        // either cache. Requests for a new version get their own fingerprint.
        if (job->cancelled || fingerprint(job->url) != key) return {};
        if (image.isNull()) {
            failures.insert(key, new qint64(options.now() + 60000));
            return {};
        }
        if (image.width() > 320 || image.height() > 180)
            image = image.scaled(320, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!cachePath.isEmpty() && QDir().mkpath(options.cacheDirectory)) {
            QSaveFile file(cachePath);
            if (file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && !job->cancelled
                && fingerprint(job->url) == key && file.commit()) {
                touch(cachePath);
                prune();
            }
        }
        if (job->cancelled || fingerprint(job->url) != key) return {};
        memory.insert(key, new QImage(image), int(image.sizeInBytes()));
        return image;
    }

    void run()
    {
        QThread::currentThread()->setPriority(QThread::LowPriority);
        prune();
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [this] { return stopping || !pending.empty(); });
                if (pending.empty()) return;
                job = pending.front(); pending.pop_front(); active = job;
            }
            const auto image = load(job);
            std::vector<std::shared_ptr<Subscriber>> subscribers;
            {
                std::lock_guard lock(mutex);
                if (jobs.value(job->key) == job) jobs.remove(job->key);
                active.reset();
                std::lock_guard jobLock(job->mutex);
                subscribers.swap(job->subscribers);
            }
            for (const auto& subscriber : subscribers)
                subscriber->response->complete(job->cancelled ? QImage{} : image);
        }
    }

    Options options;
    std::mutex mutex;
    std::condition_variable changed;
    bool stopping = false;
    std::deque<std::shared_ptr<Job>> pending;
    QHash<QString, std::shared_ptr<Job>> jobs;
    std::shared_ptr<Job> active;
    QCache<QString, QImage> memory;
    QCache<QString, qint64> failures{512};
    std::thread worker;
};

QueueThumbnailProvider::QueueThumbnailProvider(Options options)
    : m_state(std::make_unique<State>(std::move(options))) {}
QueueThumbnailProvider::~QueueThumbnailProvider() = default;

QQuickImageResponse* QueueThumbnailProvider::requestImageResponse(const QString& id, const QSize& size)
{
    // Qt versions can hand providers either the escaped path or an already
    // unwrapped URL. Do not decode filename escapes a second time.
    const auto bytes = id.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)
        ? id.toUtf8() : QByteArray::fromPercentEncoding(id.toUtf8());
    const auto url = QUrl::fromEncoded(bytes);
    const auto key = url.toString(QUrl::FullyEncoded);
    auto subscriber = std::make_shared<Subscriber>();
    std::lock_guard lock(m_state->mutex);
    auto job = m_state->jobs.value(key);
    std::unique_lock<std::mutex> jobLock;
    if (job) jobLock = std::unique_lock(job->mutex);
    if (!job || job->cancelled) {
        if (jobLock.owns_lock()) jobLock.unlock();
        job = std::make_shared<Job>(); job->url = url; job->key = key;
        jobLock = std::unique_lock(job->mutex);
        m_state->jobs.insert(key, job);
        m_state->pending.push_back(job);
    }
    auto* response = new ThumbnailResponse(job, subscriber, size);
    subscriber->response = response;
    job->subscribers.push_back(subscriber);
    m_state->changed.notify_one();
    return response;
}
