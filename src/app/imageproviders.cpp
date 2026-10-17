/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "imageproviders.h"
#include "captions.h"
#include "mediaprobe.h"
#include "scopes.h"

#include <QCache>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <mlt++/Mlt.h>
#include <QFile>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QRunnable>
#include <QThreadPool>

using namespace Studio;

namespace MediaRegistry {
static QMutex s_lock;
static QHash<QString, MediaItem> s_media;
static ProjectSettings s_settings;

void set(const QList<MediaItem> &media, const ProjectSettings &settings)
{
    QMutexLocker l(&s_lock);
    s_media.clear();
    for (const MediaItem &m : media)
        s_media.insert(m.id, m);
    s_settings = settings;
}

bool find(const QString &id, MediaItem *out, ProjectSettings *settings)
{
    QMutexLocker l(&s_lock);
    const auto it = s_media.constFind(id);
    if (it == s_media.cend())
        return false;
    *out = it.value();
    *settings = s_settings;
    return true;
}
} // namespace MediaRegistry

namespace {

class Response : public QQuickImageResponse, public QRunnable
{
public:
    explicit Response(std::function<QImage()> work)
        : m_work(std::move(work))
    {
        setAutoDelete(false);
    }
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    void run() override
    {
        m_image = m_work();
        Q_EMIT finished();
    }

private:
    std::function<QImage()> m_work;
    QImage m_image;
};

QQuickImageResponse *start(std::function<QImage()> work)
{
    auto *r = new Response(std::move(work));
    static QThreadPool pool;
    pool.setMaxThreadCount(2);
    pool.start(r);
    return r;
}

QMutex s_cacheLock;
QCache<QString, QImage> s_thumbs(400);
QCache<QString, QList<float>> s_peaks(64);

QImage placeholder(const QSize &size)
{
    QImage img(size.isValid() ? size : QSize(16, 9), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

} // namespace

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    return start([id, requestedSize]() -> QImage {
        const QStringList parts = id.split(QLatin1Char('/'));
        const QSize size = requestedSize.isValid() && requestedSize.width() > 0 ? requestedSize : QSize(160, 90);
        const QString key = id + QStringLiteral("@%1x%2").arg(size.width()).arg(size.height());
        {
            QMutexLocker l(&s_cacheLock);
            if (QImage *img = s_thumbs.object(key))
                return *img;
        }
        MediaItem m;
        ProjectSettings s;
        if (!MediaRegistry::find(parts.value(0), &m, &s) || m.kind == QLatin1String("audio"))
            return placeholder(size);
        const QString path = !m.proxyPath.isEmpty() && QFile::exists(m.proxyPath) ? m.proxyPath : m.path;
        const QImage img = MediaProbe::thumbnail(path, parts.value(1).toInt(), size, s);
        if (img.isNull())
            return placeholder(size);
        QMutexLocker l(&s_cacheLock);
        s_thumbs.insert(key, new QImage(img));
        return img;
    });
}

QQuickImageResponse *WaveformProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    return start([id, requestedSize]() -> QImage {
        // <mediaId>/<in>/<out>/<color without #>
        const QStringList parts = id.split(QLatin1Char('/'));
        MediaItem m;
        ProjectSettings s;
        const QSize size(std::clamp(requestedSize.width() > 0 ? requestedSize.width() : 400, 8, 4096),
                         std::clamp(requestedSize.height() > 0 ? requestedSize.height() : 40, 8, 512));
        if (!MediaRegistry::find(parts.value(0), &m, &s) || !m.hasAudio)
            return placeholder(size);
        QList<float> peaks;
        {
            QMutexLocker l(&s_cacheLock);
            if (QList<float> *p = s_peaks.object(m.id))
                peaks = *p;
        }
        if (peaks.isEmpty()) {
            // ~100 peaks per second of media
            const int buckets = std::clamp(int(m.length / std::max(1.0, s.fps()) * 100), 50, 200000);
            peaks = MediaProbe::audioPeaks(m.path, buckets);
            QMutexLocker l(&s_cacheLock);
            s_peaks.insert(m.id, new QList<float>(peaks));
        }
        QImage img = placeholder(size);
        if (peaks.isEmpty() || m.length <= 0)
            return img;
        const int in = parts.value(1).toInt();
        const int out = std::max(in + 1, parts.value(2).toInt());
        QColor color(QLatin1Char('#') + parts.value(3, QStringLiteral("ffffff")));
        color.setAlphaF(0.75);
        QPainter p(&img);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        const double mid = size.height() / 2.0;
        QPainterPath path;
        path.moveTo(0, mid);
        QList<QPointF> lower;
        for (int x = 0; x < size.width(); ++x) {
            const double f0 = in + double(x) / size.width() * (out - in + 1);
            const double f1 = in + double(x + 1) / size.width() * (out - in + 1);
            const int b0 = std::clamp(int(f0 / m.length * peaks.size()), 0, int(peaks.size()) - 1);
            const int b1 = std::clamp(int(f1 / m.length * peaks.size()), b0, int(peaks.size()) - 1);
            float v = 0;
            for (int b = b0; b <= b1; ++b)
                v = std::max(v, peaks.at(b));
            const double h = std::max(0.5, v * mid);
            path.lineTo(x, mid - h);
            lower.prepend(QPointF(x, mid + h));
        }
        for (const QPointF &pt : lower)
            path.lineTo(pt);
        path.closeSubpath();
        p.drawPath(path);
        return img;
    });
}

QQuickImageResponse *ScopeProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize)
    const QString kind = id.section(QLatin1Char('/'), 0, 0);
    return start([kind]() -> QImage {
        const QImage img = Scopes::latest(kind);
        return img.isNull() ? placeholder(QSize(256, 256)) : img;
    });
}

// ------------------------------------------------------------------ caption previews

QImage CaptionPreviewProvider::render(const QVariantMap &styleMap, qint64 ms, const QSize &size, bool transparent)
{
    using namespace Studio::Captions;
    const Style style = Style::fromMap(styleMap);
    const int w = std::max(16, size.width() / 2 * 2), h = std::max(16, size.height() / 2 * 2);
    // Two short cues, the second by "speaker 2", 400 ms per word
    const QStringList text = QCoreApplication::translate("Captions", "Captions everyone can read easily").split(QLatin1Char(' '));
    const int half = std::max(1, int(text.size() / 2));
    QList<Cue> cues;
    for (int part = 0; part < 2; ++part) {
        Cue c;
        c.speaker = part + 1;
        const int from = part * half, to = part == 0 ? half : int(text.size());
        for (int i = from; i < to; ++i)
            c.words.append({i * 400LL, i * 400LL + 330, text.at(i)});
        if (c.words.isEmpty())
            continue;
        c.startMs = c.words.first().startMs;
        c.endMs = part == 0 ? to * 400LL : to * 400LL + 600;
        const Rules r = style.effectiveRules(w, h);
        c.lines = breakLines(c.words, r.maxCharsPerLine, r.maxLines);
        if (c.lines.isEmpty())
            c.lines = wrapLines(c.words, r.maxCharsPerLine);
        cues.append(c);
    }
    if (!cues.isEmpty() && style.effect != QLatin1String("twocolor"))
        cues.last().speaker = 1;
    const QString ass = toAss(cues, style, w, h, 30);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/captions");
    QDir().mkpath(dir);
    const QString file = QDir(dir).filePath(QStringLiteral("preview-%1.ass").arg(QString::fromLatin1(QCryptographicHash::hash(ass.toUtf8(), QCryptographicHash::Sha1).toHex().left(16))));
    if (!QFile::exists(file)) {
        QSaveFile f(file);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(ass.toUtf8());
            f.commit();
        }
    }

    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    if (transparent) {
        out.fill(Qt::transparent);
    } else {
        // a neutral "video" behind the sample
        QPainter p(&out);
        QLinearGradient g(0, 0, w, h);
        g.setColorAt(0, QColor(0x3a, 0x4a, 0x5c));
        g.setColorAt(1, QColor(0x14, 0x1a, 0x24));
        p.fillRect(out.rect(), g);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 28));
        p.drawEllipse(QPointF(w * 0.5, h * 0.36), std::min(w, h) * 0.18, std::min(w, h) * 0.22);
        p.drawRoundedRect(QRectF(w * 0.22, h * 0.56, w * 0.56, h * 0.5), w * 0.2, w * 0.2);
    }
    Mlt::Profile profile;
    profile.set_width(w);
    profile.set_height(h);
    profile.set_frame_rate(30, 1);
    profile.set_sample_aspect(1, 1);
    profile.set_display_aspect(w, h);
    profile.set_progressive(1);
    profile.set_explicit(1);
    Mlt::Producer bg(profile, "color:#00000000");
    Mlt::Filter filter(profile, "avfilter.ass");
    if (!bg.is_valid() || !filter.is_valid())
        return out;
    filter.set("av.filename", file.toUtf8().constData());
    filter.set("av.alpha", 1);
    bg.attach(filter);
    bg.seek(int(ms * 30 / 1000));
    std::unique_ptr<Mlt::Frame> frame(bg.get_frame());
    if (!frame)
        return out;
    mlt_image_format fmt = mlt_image_rgba;
    int fw = w, fh = h;
    const uint8_t *img = frame->get_image(fmt, fw, fh);
    if (!img || fw != w || fh != h)
        return out;
    const QImage text_(img, fw, fh, fw * 4, QImage::Format_RGBA8888);
    QPainter p(&out);
    p.drawImage(0, 0, text_);
    p.end();
    return out;
}

QQuickImageResponse *CaptionPreviewProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    return start([id, requestedSize]() -> QImage {
        // <ms>/<WxH>/<style json>
        const qint64 ms = id.section(QLatin1Char('/'), 0, 0).toLongLong();
        const QString dims = id.section(QLatin1Char('/'), 1, 1);
        QSize size(dims.section(QLatin1Char('x'), 0, 0).toInt(), dims.section(QLatin1Char('x'), 1, 1).toInt());
        if (size.width() <= 0 || size.height() <= 0)
            size = requestedSize.isValid() ? requestedSize : QSize(270, 480);
        const QString json = QUrl::fromPercentEncoding(id.section(QLatin1Char('/'), 2).toUtf8());
        const QVariantMap style = QJsonDocument::fromJson(json.toUtf8()).object().toVariantMap();
        return render(style, ms, size.boundedTo(QSize(1080, 1920)));
    });
}
