// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QQuickTextureFactory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QThread>
#include <QElapsedTimer>
#include <QQuickView>
#include <QQuickItem>
#include <mutex>
#include <vlc/vlc.h>
#include "../../../../lib/libvlc_internal.h"
#include "../util/queue_thumbnail_provider.hpp"
#include "../util/queue_thumbnail_decoder.hpp"

extern "C" const char vlc_module_name[] = "qt_queue_thumbnail_test";

namespace {
QImage frame(QColor color = Qt::red)
{
    QImage image(32, 18, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}
QUrl media(const QString& directory, const QString& name = "video.mp4")
{
    const auto path = QDir(directory).filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write("video");
    return QUrl::fromLocalFile(path);
}
QString id(const QUrl& url)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(url.toString(QUrl::FullyEncoded)));
}
QQuickItem* visualChild(QQuickItem* parent, const QString& name)
{
    if (parent->objectName() == name) return parent;
    for (auto* child : parent->childItems())
        if (auto* match = visualChild(child, name)) return match;
    return nullptr;
}
struct Pending
{
    std::unique_ptr<QQuickImageResponse> response;
    QSignalSpy finished;
    Pending(QueueThumbnailProvider& provider, const QUrl& url)
        : response(provider.requestImageResponse(id(url), {})),
          finished(response.get(), &QQuickImageResponse::finished) {}
    ~Pending()
    {
        if (!finished.count()) { response->cancel(); finished.wait(5000); }
    }
    bool wait() { return finished.count() || finished.wait(5000); }
    QImage image() const
    {
        std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
        return texture ? texture->image() : QImage{};
    }
};
}

class QueueThumbnailProviderTest : public QObject
{
    Q_OBJECT
private slots:
    void serializesAndSharesVisibleRequests()
    {
        QTemporaryDir directory;
        const auto first = media(directory.path());
        const auto second = media(directory.path(), "second.mp4");
        std::atomic_int calls{0};
        std::atomic_bool release{false}, offGuiThread{false};
        QueueThumbnailProvider::Options options;
        options.decode = [&](const QUrl&, const std::atomic_bool& cancelled) {
            offGuiThread = QThread::currentThread() != qApp->thread();
            ++calls;
            while (!release && !cancelled) QThread::msleep(1);
            return frame();
        };
        QueueThumbnailProvider provider(options);
        Pending a(provider, first), duplicate(provider, first), b(provider, second);
        QTRY_COMPARE(calls.load(), 1);
        QVERIFY(offGuiThread.load());
        duplicate.response->cancel();
        release = true;
        QVERIFY(a.wait()); QVERIFY(duplicate.wait()); QVERIFY(b.wait());
        QCOMPARE(a.image().pixelColor(0, 0), QColor(Qt::red));
        QVERIFY(duplicate.image().isNull());
        QCOMPARE(b.image().size(), QSize(32, 18));
        QCOMPARE(calls.load(), 2);
        QCOMPARE(a.finished.count(), 1);
    }

    void cancelledQueuedWorkDoesNotDecode()
    {
        QTemporaryDir directory;
        std::atomic_int calls{0};
        std::atomic_bool release{false};
        QueueThumbnailProvider::Options options;
        options.decode = [&](const QUrl&, const std::atomic_bool& cancelled) {
            ++calls;
            while (!release && !cancelled) QThread::msleep(1);
            return frame();
        };
        QueueThumbnailProvider provider(options);
        Pending a(provider, media(directory.path()));
        QTRY_COMPARE(calls.load(), 1);
        Pending b(provider, media(directory.path(), "obsolete.mp4"));
        b.response->cancel(); release = true;
        QVERIFY(a.wait()); QVERIFY(b.wait());
        QVERIFY(b.image().isNull()); QCOMPARE(calls.load(), 1);
    }

    void cancelledActiveRequestCanBeRequestedAgain()
    {
        QTemporaryDir directory;
        const auto url = media(directory.path());
        std::atomic_int calls{0};
        QueueThumbnailProvider::Options options;
        options.decode = [&](const QUrl&, const std::atomic_bool& cancelled) {
            if (++calls == 1) {
                while (!cancelled) QThread::msleep(1);
                return frame(Qt::red);
            }
            return frame(Qt::green);
        };
        QueueThumbnailProvider provider(options);
        Pending old(provider, url);
        QTRY_COMPARE(calls.load(), 1);
        old.response->cancel();
        Pending current(provider, url);
        QVERIFY(old.wait()); QVERIFY(current.wait());
        QVERIFY(old.image().isNull());
        QCOMPARE(current.image().pixelColor(0, 0), QColor(Qt::green));
        QCOMPARE(calls.load(), 2);
    }

    void cachePersistsAndRevalidatesSource()
    {
        QTemporaryDir directory;
        const auto url = media(directory.path());
        std::atomic_int calls{0};
        QueueThumbnailProvider::Options options;
        options.cacheDirectory = QDir(directory.path()).filePath("cache");
        options.decode = [&](const QUrl&, const std::atomic_bool&) { ++calls; return frame(); };
        {
            QueueThumbnailProvider provider(options);
            Pending a(provider, url); QVERIFY(a.wait()); QVERIFY(!a.image().isNull());
            Pending b(provider, url); QVERIFY(b.wait()); QCOMPARE(calls.load(), 1);
        }
        {
            QueueThumbnailProvider provider(options);
            Pending cached(provider, url); QVERIFY(cached.wait()); QVERIFY(!cached.image().isNull());
            QCOMPARE(calls.load(), 1);
            QFile file(url.toLocalFile()); QVERIFY(file.open(QIODevice::Append));
            file.write("changed"); file.close();
            Pending changed(provider, url); QVERIFY(changed.wait());
            QCOMPARE(calls.load(), 2);
        }
    }

    void changedFileDuringDecodeCannotPublishStaleFrame()
    {
        QTemporaryDir directory;
        const auto url = media(directory.path());
        std::atomic_int calls{0};
        std::atomic_bool release{false};
        QueueThumbnailProvider::Options options;
        options.decode = [&](const QUrl&, const std::atomic_bool& cancelled) {
            ++calls;
            while (!release && !cancelled) QThread::msleep(1);
            return frame();
        };
        QueueThumbnailProvider provider(options);
        Pending old(provider, url);
        QTRY_COMPARE(calls.load(), 1);
        QFile file(url.toLocalFile()); QVERIFY(file.open(QIODevice::Append));
        file.write("new content"); file.close(); release = true;
        QVERIFY(old.wait()); QVERIFY(old.image().isNull());
        Pending current(provider, url); QVERIFY(current.wait());
        QVERIFY(!current.image().isNull()); QCOMPARE(calls.load(), 2);
    }

    void corruptDiskCacheRegenerates()
    {
        QTemporaryDir directory;
        const auto url = media(directory.path());
        std::atomic_int calls{0};
        QueueThumbnailProvider::Options options;
        options.cacheDirectory = QDir(directory.path()).filePath("cache");
        options.decode = [&](const QUrl&, const std::atomic_bool&) { ++calls; return frame(); };
        {
            QueueThumbnailProvider provider(options);
            Pending a(provider, url); QVERIFY(a.wait());
        }
        const QDir cache(options.cacheDirectory);
        const auto entries = cache.entryList({"*.png"}, QDir::Files);
        QCOMPARE(entries.size(), 1);
        QFile corrupt(cache.filePath(entries.first()));
        QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("broken"); corrupt.close();
        QueueThumbnailProvider provider(options);
        Pending regenerated(provider, url); QVERIFY(regenerated.wait());
        QVERIFY(!regenerated.image().isNull()); QCOMPARE(calls.load(), 2);
    }

    void failedFilesRetryAfterExpiryOrChange()
    {
        QTemporaryDir directory;
        const auto url = media(directory.path());
        std::atomic_int calls{0};
        std::atomic<qint64> now{QDateTime::currentMSecsSinceEpoch()};
        QueueThumbnailProvider::Options options;
        options.now = [&] { return now.load(); };
        options.decode = [&](const QUrl&, const std::atomic_bool&) { ++calls; return QImage{}; };
        QueueThumbnailProvider provider(options);
        Pending a(provider, url); QVERIFY(a.wait()); QVERIFY(a.image().isNull());
        Pending b(provider, url); QVERIFY(b.wait()); QCOMPARE(calls.load(), 1);
        now += 61000;
        Pending expired(provider, url); QVERIFY(expired.wait()); QCOMPARE(calls.load(), 2);
        QFile file(url.toLocalFile()); QVERIFY(file.open(QIODevice::Append));
        file.write("new"); file.close();
        Pending changed(provider, url); QVERIFY(changed.wait()); QCOMPARE(calls.load(), 3);
    }

    void failureCacheIsBounded()
    {
        QTemporaryDir directory;
        std::atomic_int calls{0};
        QueueThumbnailProvider::Options options;
        options.now = [] { return qint64(1); };
        options.decode = [&](const QUrl&, const std::atomic_bool&) { ++calls; return QImage{}; };
        QueueThumbnailProvider provider(options);
        const auto first = media(directory.path());
        { Pending result(provider, first); QVERIFY(result.wait()); }
        for (int i = 0; i < 512; ++i) {
            Pending result(provider, media(directory.path(), QString::number(i) + ".mp4"));
            QVERIFY(result.wait());
        }
        QCOMPARE(calls.load(), 513);
        Pending evicted(provider, first); QVERIFY(evicted.wait());
        QCOMPARE(calls.load(), 514);
    }

    void memoryCacheEvictsLeastRecentlyUsedFrames()
    {
        QTemporaryDir directory;
        const auto a = media(directory.path(), "a.mp4"), b = media(directory.path(), "b.mp4"), c = media(directory.path(), "c.mp4");
        std::atomic_int calls{0};
        QueueThumbnailProvider::Options options;
        options.memoryBytes = int(frame().sizeInBytes()) * 2;
        options.decode = [&](const QUrl&, const std::atomic_bool&) { ++calls; return frame(); };
        QueueThumbnailProvider provider(options);
        for (const auto& url : {a, b, a, c}) {
            Pending result(provider, url); QVERIFY(result.wait()); QVERIFY(!result.image().isNull());
        }
        QCOMPARE(calls.load(), 3);
        Pending evicted(provider, b); QVERIFY(evicted.wait()); QCOMPARE(calls.load(), 4);
    }

    void evictsDiskEntriesByRecencyAndBudget()
    {
        QTemporaryDir directory;
        const auto a = media(directory.path(), "a.mp4"), b = media(directory.path(), "b.mp4"), c = media(directory.path(), "c.mp4");
        std::atomic_int calls{0};
        std::atomic<qint64> now{QDateTime::currentMSecsSinceEpoch()};
        QueueThumbnailProvider::Options options;
        options.memoryBytes = 1;
        options.diskEntries = 2;
        options.cacheDirectory = QDir(directory.path()).filePath("cache");
        options.now = [&] { return now.fetch_add(1000); };
        options.decode = [&](const QUrl&, const std::atomic_bool&) { ++calls; return frame(); };
        {
            QueueThumbnailProvider provider(options);
            for (const auto& url : {a, b, a, c}) {
                Pending result(provider, url); QVERIFY(result.wait()); QVERIFY(!result.image().isNull());
            }
            QCOMPARE(calls.load(), 3);
            QCOMPARE(QDir(options.cacheDirectory).entryList({"*.png"}, QDir::Files).size(), 2);
            Pending evicted(provider, b); QVERIFY(evicted.wait()); QCOMPARE(calls.load(), 4);
        }
        options.diskBytes = 1;
        QueueThumbnailProvider tiny(options);
        Pending result(tiny, c); QVERIFY(result.wait()); QVERIFY(!result.image().isNull());
        QCOMPARE(QDir(options.cacheDirectory).entryList({"*.png"}, QDir::Files).size(), 0);
    }

    void rejectsNonFilesWithoutInvokingDecoder()
    {
        QTemporaryDir directory;
        std::atomic_int calls{0};
        QueueThumbnailProvider::Options options;
        options.decode = [&](const QUrl&, const std::atomic_bool&) { ++calls; return frame(); };
        QueueThumbnailProvider provider(options);
        for (const auto& url : {QUrl("https://example.invalid/stream.mp4"), QUrl::fromLocalFile(directory.path()), QUrl::fromLocalFile(directory.filePath("missing.mp4"))}) {
            Pending result(provider, url); QVERIFY(result.wait()); QVERIFY(result.image().isNull());
        }
        QCOMPARE(calls.load(), 0);
    }

    void shutdownCancelsActiveAndQueuedWork()
    {
        QTemporaryDir directory;
        std::atomic_int calls{0};
        QueueThumbnailProvider::Options options;
        options.decode = [&](const QUrl&, const std::atomic_bool& cancelled) {
            ++calls;
            while (!cancelled) QThread::msleep(1);
            return QImage{};
        };
        auto provider = std::make_unique<QueueThumbnailProvider>(options);
        Pending active(*provider, media(directory.path()));
        QTRY_COMPARE(calls.load(), 1);
        Pending queued(*provider, media(directory.path(), "queued.mp4"));
        provider.reset();
        QVERIFY(active.wait()); QVERIFY(queued.wait());
        QCOMPARE(calls.load(), 1);
    }

    void qmlPreservesUnicodeAndReservedCharactersInUrls()
    {
        QTemporaryDir directory;
        const auto url = media(directory.path(), QString::fromUtf8("clip # %23 ? 日本語.mp4"));
        QUrl decoded;
        QueueThumbnailProvider::Options options;
        options.decode = [&](const QUrl& value, const std::atomic_bool&) { decoded = value; return frame(); };
        QQmlEngine engine;
        engine.addImageProvider("queuethumbnail", new QueueThumbnailProvider(options));
        engine.rootContext()->setContextProperty("previewUrl", QUrl("image://queuethumbnail/" + id(url)));
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nImage { source: previewUrl; cache: false; width: 80; height: 45 }", {});
        std::unique_ptr<QObject> image(component.create());
        QVERIFY2(image != nullptr, qPrintable(component.errorString()));
        QTRY_COMPARE(image->property("status").toInt(), 1);
        QCOMPARE(decoded, url);
    }

    void nativeDecoderFitsVideoFrames_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("landscape") << QSize(64, 36);
        QTest::newRow("portrait") << QSize(36, 64);
    }

    void visibleQueueRowsCancelAndResumeNativeRequests()
    {
        QTemporaryDir directory;
        QStringList urls;
        for (int i = 0; i < 40; ++i)
            urls.append(media(directory.path(), QString::fromUtf8("clip # %23 日本語 %1.mp4").arg(i)).toString());
        std::mutex recordsMutex;
        QList<QUrl> requested;
        std::atomic_int calls{0}, cancellations{0};
        std::atomic_bool release{false};
        QueueThumbnailProvider::Options options;
        options.decode = [&](const QUrl& url, const std::atomic_bool& cancelled) {
            { std::lock_guard lock(recordsMutex); requested.append(url); }
            ++calls;
            while (!release && !cancelled) QThread::msleep(1);
            if (cancelled) ++cancellations;
            return frame();
        };
        QQuickView view;
        view.engine()->addImageProvider("queuethumbnail", new QueueThumbnailProvider(options));
        view.rootContext()->setContextProperty("fixtureUrls", urls);
        QQmlComponent component(view.engine());
        component.setData("import QtQuick\nimport \".\" as Redesign\n"
            "Redesign.RedesignQueue { width: 400; height: 300; videoPreviewsAvailable: true;"
            "queueModel: ListModel { id: media }\nitemCount: media.count;"
            "Component.onCompleted: { for (let i=0; i<fixtureUrls.length; ++i)"
            "media.append({title: 'Clip '+i, isCurrent: i===0, artwork: '', url: fixtureUrls[i]}) } }",
            QUrl::fromLocalFile(QStringLiteral(TOP_SRCDIR "/modules/gui/qt/player/qml/redesign/")));
        auto* root = qobject_cast<QQuickItem*>(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        view.setContent({}, &component, root); view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_COMPARE(calls.load(), 1);
        auto* list = root->findChild<QQuickItem*>("queueList"); QVERIFY(list);
        QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, 20), Q_ARG(int, 0)));
        QTRY_COMPARE(cancellations.load(), 1);
        QTRY_COMPARE(calls.load(), 2);
        {
            std::lock_guard lock(recordsMutex);
            QCOMPARE(requested[0], QUrl(urls[0]));
            QCOMPARE(requested[1], QUrl(urls[20]));
        }
        root->setVisible(false);
        QTRY_COMPARE(cancellations.load(), 2);
        QTest::qWait(50); QCOMPARE(calls.load(), 2);
        release = true;
        root->setVisible(true);
        auto* thumbnail = visualChild(root, "queueThumbnail_20"); QVERIFY(thumbnail);
        QTRY_VERIFY(thumbnail->property("ready").toBool());
        QTest::qWait(100);
        {
            std::lock_guard lock(recordsMutex);
            for (int i = 2; i < requested.size(); ++i) {
                const auto index = urls.indexOf(requested[i].toString());
                QVERIFY2(index >= 20 && index <= 23, "An offscreen queue entry was decoded");
            }
        }
        // Destroy the engine while a new uncached request is still decoding.
        release = false;
        const auto previousCalls = calls.load();
        QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, 30), Q_ARG(int, 0)));
        QTRY_VERIFY(calls.load() > previousCalls);
    }

    void nativeDecoderFitsVideoFrames()
    {
        QFETCH(QSize, size);
        QTemporaryDir directory;
        QFile file(directory.filePath("red.y4m"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("YUV4MPEG2 W" + QByteArray::number(size.width()) + " H" + QByteArray::number(size.height())
                   + " F25:1 Ip A1:1 C420jpeg\n");
        const auto pixels = size.width() * size.height();
        const auto red = QByteArray(pixels, char(81)) + QByteArray(pixels / 4, char(90)) + QByteArray(pixels / 4, char(240));
        const auto black = QByteArray(pixels, char(16)) + QByteArray(pixels / 2, char(128));
        for (int i = 0; i < 25; ++i) { file.write("FRAME\n"); file.write(i < 6 ? black : red); }
        file.close();
        const char* args[] = {"--ignore-config", "--no-media-library", "--no-plugins-cache", "--quiet"};
        std::unique_ptr<libvlc_instance_t, decltype(&libvlc_release)> vlc(libvlc_new(4, args), libvlc_release);
        QVERIFY(vlc);
        const auto decode = makeQueueThumbnailDecoder(VLC_OBJECT(vlc->p_libvlc_int));
        std::atomic_bool cancelled{false};
        const auto image = decode(QUrl::fromLocalFile(file.fileName()), cancelled);
        QVERIFY(!image.isNull());
        QVERIFY(image.width() <= 320 && image.height() <= 180);
        QVERIFY(qAbs(double(image.width()) / image.height() - double(size.width()) / size.height()) < .02);
        const auto center = image.pixelColor(image.width() / 2, image.height() / 2);
        QVERIFY(center.red() > 240 && center.green() < 10 && center.blue() < 10);
        cancelled = true;
        QVERIFY(decode(QUrl::fromLocalFile(file.fileName()), cancelled).isNull());
    }

    void localVideoFixtures()
    {
        const auto fixtureDirectory = qEnvironmentVariable("VLC_QUEUE_THUMBNAIL_FIXTURES");
        if (fixtureDirectory.isEmpty()) QSKIP("No local video fixtures were requested.");
        const auto outputDirectory = qEnvironmentVariable("VLC_QUEUE_THUMBNAIL_OUTPUT");
        if (!outputDirectory.isEmpty()) QVERIFY(QDir().mkpath(outputDirectory));
        const char* args[] = {"--ignore-config", "--no-media-library", "--no-plugins-cache", "--quiet"};
        std::unique_ptr<libvlc_instance_t, decltype(&libvlc_release)> vlc(libvlc_new(4, args), libvlc_release);
        QVERIFY(vlc);
        QTemporaryDir cache;
        std::atomic_int calls{0};
        QueueThumbnailProvider::Options options;
        options.cacheDirectory = cache.path();
        const auto decode = makeQueueThumbnailDecoder(VLC_OBJECT(vlc->p_libvlc_int));
        options.decode = [&](const QUrl& url, const std::atomic_bool& cancelled) { ++calls; return decode(url, cancelled); };
        const auto files = QDir(fixtureDirectory).entryInfoList({"*.mp4"}, QDir::Files, QDir::Name);
        QVERIFY(!files.isEmpty());
        for (int pass = 0; pass < 2; ++pass) {
            QueueThumbnailProvider provider(options);
            for (const auto& file : files) {
                QElapsedTimer timer; timer.start();
                Pending result(provider, QUrl::fromLocalFile(file.absoluteFilePath()));
                QVERIFY(result.wait());
                const auto image = result.image();
                QVERIFY2(!image.isNull(), qPrintable(file.fileName()));
                if (file.fileName() == "soulframe.mp4") {
                    // The fast keyframe near 15% is a fade to black; the frame
                    // at the requested position contains the actual scene.
                    int brightPixels = 0;
                    for (int y = 0; y < image.height(); ++y)
                        for (int x = 0; x < image.width(); ++x)
                            brightPixels += qGray(image.pixel(x, y)) > 20;
                    QVERIFY2(brightPixels > image.width() * image.height() / 4, "Seek landed on a black keyframe");
                }
                qInfo().noquote() << (pass ? "disk" : "cold") << timer.elapsed() << "ms" << image.size() << file.fileName();
                if (!pass && !outputDirectory.isEmpty())
                    QVERIFY(image.save(QDir(outputDirectory).filePath(file.completeBaseName() + ".png")));
            }
        }
        QCOMPARE(calls.load(), files.size());
        QueueThumbnailProvider provider(options);
        Pending corrupt(provider, media(cache.path(), "corrupt.mp4"));
        QVERIFY(corrupt.wait()); QVERIFY(corrupt.image().isNull());
    }
};

QTEST_MAIN(QueueThumbnailProviderTest)
#include "test_queue_thumbnail_provider.moc"
