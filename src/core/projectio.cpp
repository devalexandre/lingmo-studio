/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "projectio.h"
#include "mltbuilder.h"
#include "mltxml.h"
#include "timelineops.h"

#include <Mlt.h>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace Studio::ProjectIO {

static QString kindName(TrackKind k)
{
    return k == TrackKind::Audio ? QStringLiteral("audio") : k == TrackKind::Subtitle ? QStringLiteral("subtitle") : QStringLiteral("video");
}

static TrackKind kindFrom(const QString &s)
{
    return s == QLatin1String("audio") ? TrackKind::Audio : s == QLatin1String("subtitle") ? TrackKind::Subtitle : TrackKind::Video;
}

QJsonObject timelineToJson(const Timeline &tl)
{
    QJsonArray tracks;
    for (const Track &t : tl.tracks) {
        QJsonArray clips;
        for (const Clip &c : t.clips)
            clips.append(c.toFullJson());
        QJsonObject to{{QStringLiteral("id"), t.id}, {QStringLiteral("kind"), kindName(t.kind)},
                       {QStringLiteral("name"), t.name}, {QStringLiteral("muted"), t.muted},
                       {QStringLiteral("hidden"), t.hidden}, {QStringLiteral("locked"), t.locked},
                       {QStringLiteral("solo"), t.solo}, {QStringLiteral("volume"), t.volumeDb},
                       {QStringLiteral("pan"), t.pan}, {QStringLiteral("clips"), clips}};
        if (!t.captionStyle.isEmpty())
            to.insert(QStringLiteral("captionStyle"), QJsonObject::fromVariantMap(t.captionStyle));
        tracks.append(to);
    }
    QJsonArray markers;
    for (const Marker &m : tl.markers)
        markers.append(QJsonObject{{QStringLiteral("frame"), m.frame}, {QStringLiteral("name"), m.name}, {QStringLiteral("color"), m.color}});
    return {{QStringLiteral("tracks"), tracks}, {QStringLiteral("markers"), markers}};
}

static QList<Marker> markersFromJson(const QJsonArray &a)
{
    QList<Marker> out;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        out.append({o.value(QStringLiteral("frame")).toInt(), o.value(QStringLiteral("name")).toString(),
                    o.value(QStringLiteral("color")).toString(QStringLiteral("blue"))});
    }
    return out;
}

Timeline timelineFromJson(const QJsonObject &o)
{
    Timeline tl;
    for (const QJsonValue &v : o.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject to = v.toObject();
        Track t;
        t.id = to.value(QStringLiteral("id")).toString();
        t.kind = kindFrom(to.value(QStringLiteral("kind")).toString());
        t.name = to.value(QStringLiteral("name")).toString();
        t.muted = to.value(QStringLiteral("muted")).toBool();
        t.hidden = to.value(QStringLiteral("hidden")).toBool();
        t.locked = to.value(QStringLiteral("locked")).toBool();
        t.solo = to.value(QStringLiteral("solo")).toBool();
        t.volumeDb = to.value(QStringLiteral("volume")).toDouble();
        t.pan = to.value(QStringLiteral("pan")).toDouble();
        t.captionStyle = to.value(QStringLiteral("captionStyle")).toObject().toVariantMap();
        for (const QJsonValue &cv : to.value(QStringLiteral("clips")).toArray())
            t.clips.append(Clip::fromFullJson(cv.toObject()));
        tl.tracks.append(t);
    }
    tl.markers = markersFromJson(o.value(QStringLiteral("markers")).toArray());
    return tl;
}

QByteArray toXml(const ProjectData &data, const QString &title, bool useProxies)
{
    PlanOptions opts;
    opts.useProxies = useProxies;
    opts.storeModel = true;
    Planner planner(data.settings, data.media, data.doc.sequences, opts);
    RenderPlan plan = planner.plan(data.doc.timeline, data.doc.timeline.markers);

    QJsonArray bin;
    for (const MediaItem &m : data.media)
        bin.append(m.toJson());
    QJsonArray seqs;
    for (const Sequence &s : data.doc.sequences)
    {
        QJsonObject so{{QStringLiteral("id"), s.id}, {QStringLiteral("name"), s.name}, {QStringLiteral("timeline"), timelineToJson(s.timeline)}};
        if (!s.info.isEmpty())
            so.insert(QStringLiteral("info"), QJsonObject::fromVariantMap(s.info));
        seqs.append(so);
    }
    const Props root = {{QStringLiteral("studio:version"), QStringLiteral("1")},
                        {QStringLiteral("studio:samplerate"), QString::number(data.settings.sampleRate)},
                        {QStringLiteral("studio:bin"), QString::fromUtf8(QJsonDocument(bin).toJson(QJsonDocument::Compact))},
                        {QStringLiteral("studio:sequences"), QString::fromUtf8(QJsonDocument(seqs).toJson(QJsonDocument::Compact))}};
    return writeMltXml(plan, title, root);
}

bool save(const QString &path, const ProjectData &data, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(toXml(data, QFileInfo(path).completeBaseName()));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

namespace {

struct Part {
    int track;
    int start;
    int in;
    int out;
    Clip clip;
    int mixLength = 0; // > 0: head part of a transition (this clip's transitionIn)
    QString transition;
    bool mixTail = false;
};

class Loader
{
public:
    ProjectData *data;
    QHash<QString, QString> mediaByPath;
    QHash<QString, int> foreignIds;

    QString mediaFor(Mlt::Producer &parent)
    {
        const QString id = QString::fromUtf8(parent.get("studio:media"));
        if (!id.isEmpty())
            return id;
        QString resource = QString::fromUtf8(parent.get("resource"));
        const QString service = QString::fromUtf8(parent.get("mlt_service"));
        if (service == QLatin1String("timewarp")) {
            const QString warp = QString::fromUtf8(parent.get("warp_resource"));
            if (!warp.isEmpty())
                resource = warp;
            else if (resource.contains(QLatin1Char(':')))
                resource = resource.section(QLatin1Char(':'), 1);
        }
        if (resource.isEmpty())
            return {};
        const auto it = mediaByPath.constFind(resource);
        if (it != mediaByPath.cend())
            return it.value();
        MediaItem m;
        m.id = newId(QStringLiteral("m"));
        m.path = resource;
        m.name = QFileInfo(resource).fileName();
        const QString suffix = QFileInfo(resource).suffix().toLower();
        static const QStringList images = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp"),
                                           QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("tif"), QStringLiteral("tiff")};
        static const QStringList audio = {QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"),
                                          QStringLiteral("opus"), QStringLiteral("m4a"), QStringLiteral("aac")};
        m.kind = images.contains(suffix) ? QStringLiteral("image") : audio.contains(suffix) ? QStringLiteral("audio") : QStringLiteral("video");
        m.hasVideo = m.kind != QLatin1String("audio");
        m.hasAudio = m.kind != QLatin1String("image");
        m.length = parent.get_length();
        data->media.append(m);
        mediaByPath.insert(resource, m.id);
        data->warnings << QCoreApplication::translate("ProjectIO", "Imported %1 from a project made elsewhere").arg(m.name);
        return m.id;
    }

    Clip clipFromCut(Mlt::Producer &cut, Mlt::Producer &parent)
    {
        const QByteArray json = cut.get("studio:clip");
        Clip c;
        if (!json.isEmpty()) {
            c = Clip::fromJson(QJsonDocument::fromJson(json).object());
        } else {
            c.id = newId(QStringLiteral("c"));
            const QString service = QString::fromUtf8(parent.get("mlt_service"));
            if (parent.get("studio:sequence")) {
                c.kind = QStringLiteral("compound");
                c.mediaId = QString::fromUtf8(parent.get("studio:sequence"));
            } else if (service == QLatin1String("color") || service == QLatin1String("colour")) {
                c.kind = QStringLiteral("color");
                QString col = QString::fromUtf8(parent.get("resource"));
                c.genParams.insert(QStringLiteral("color"), col);
            } else {
                c.kind = QStringLiteral("media");
                c.mediaId = mediaFor(parent);
                if (service == QLatin1String("timewarp"))
                    c.speed = parent.get_double("warp_speed");
            }
        }
        if (c.id.isEmpty())
            c.id = newId(QStringLiteral("c"));
        if (c.kind == QLatin1String("media") && c.mediaId.isEmpty())
            c.mediaId = mediaFor(parent);
        return c;
    }

    // Files from other tools have no clip ids: a transition tail continues the
    // clip before it, and the body after a transition head continues that head.
    static void joinForeign(Part &part, const QList<Part> &parts)
    {
        if (parts.isEmpty())
            return;
        const Part &last = parts.last();
        if (last.track != part.track || last.clip.kind != part.clip.kind || last.clip.mediaId != part.clip.mediaId
            || last.out + 1 != part.in)
            return;
        if (part.mixTail || last.mixLength > 0)
            part.clip.id = last.clip.id;
    }

    static bool isTractor(Mlt::Producer &p)
    {
        return p.type() == mlt_service_tractor_type || p.get_int("_original_type") == mlt_service_tractor_type
            || qstrcmp(p.get("mlt_service"), "tractor") == 0;
    }

    void readPlaylist(Mlt::Playlist &pl, int track, QList<Part> &parts)
    {
        for (int j = 0; j < pl.count(); ++j) {
            if (pl.is_blank(j))
                continue;
            std::unique_ptr<Mlt::ClipInfo> info(pl.clip_info(j));
            if (!info || !info->producer || !info->cut)
                continue;
            Mlt::Producer &parent = *info->producer;
            if (isTractor(parent) && !parent.get("studio:sequence")) {
                Mlt::Tractor mix(reinterpret_cast<mlt_tractor>(parent.get_producer()));
                if (mix.count() != 2) {
                    data->warnings << QCoreApplication::translate("ProjectIO", "A nested timeline was skipped");
                    continue;
                }
                const int length = info->frame_count;
                const QString type = QString::fromUtf8(parent.get("studio:transition"));
                for (int k = 0; k < 2; ++k) {
                    std::unique_ptr<Mlt::Producer> trackProducer(mix.track(k));
                    if (!trackProducer)
                        continue;
                    // Our mixes hold one-cut playlists; Shotcut puts cuts directly
                    std::unique_ptr<Mlt::Producer> cut, cutParent;
                    if (trackProducer->type() == mlt_service_playlist_type || qstrcmp(trackProducer->get("mlt_type"), "playlist") == 0) {
                        Mlt::Playlist sub(*trackProducer);
                        std::unique_ptr<Mlt::ClipInfo> si(sub.clip_info(0));
                        if (!si || !si->cut)
                            continue;
                        cut = std::make_unique<Mlt::Producer>(*si->cut);
                        cutParent = std::make_unique<Mlt::Producer>(*si->producer);
                    } else {
                        cut = std::make_unique<Mlt::Producer>(*trackProducer);
                        cutParent = std::make_unique<Mlt::Producer>(trackProducer->parent());
                    }
                    Part part{track, info->start, cut->get_in(), cut->get_in() + length - 1, clipFromCut(*cut, *cutParent)};
                    if (k == 1) {
                        part.mixLength = length;
                        part.transition = type.isEmpty() ? QStringLiteral("dissolve") : type;
                    } else {
                        part.mixTail = true;
                    }
                    if (!cut->get("studio:clip"))
                        joinForeign(part, parts);
                    parts.append(part);
                }
                continue;
            }
            Part part{track, info->start, info->frame_in, info->frame_out, clipFromCut(*info->cut, parent)};
            if (!info->cut->get("studio:clip"))
                joinForeign(part, parts);
            parts.append(part);
        }
    }
};

} // namespace

bool loadXml(const QByteArray &xml, ProjectData *data, QString *error)
{
    initMlt();
    Mlt::Profile profile;
    profile.set_explicit(0);
    Mlt::Producer root(profile, "xml-string", xml.constData());
    if (!root.is_valid()) {
        if (error)
            *error = QCoreApplication::translate("ProjectIO", "This is not an MLT project");
        return false;
    }
    if (!(root.type() == mlt_service_tractor_type || root.get_int("_original_type") == mlt_service_tractor_type)) {
        if (error)
            *error = QCoreApplication::translate("ProjectIO", "The project has no timeline");
        return false;
    }
    Mlt::Tractor tractor(reinterpret_cast<mlt_tractor>(root.get_producer()));

    *data = ProjectData{};
    data->settings.width = profile.width();
    data->settings.height = profile.height();
    data->settings.fpsNum = profile.frame_rate_num();
    data->settings.fpsDen = profile.frame_rate_den();
    if (const int rate = tractor.get_int("studio:samplerate"))
        data->settings.sampleRate = rate;

    Loader loader;
    loader.data = data;
    for (const QJsonValue &v : QJsonDocument::fromJson(tractor.get("studio:bin")).array()) {
        const MediaItem m = MediaItem::fromJson(v.toObject());
        data->media.append(m);
        loader.mediaByPath.insert(m.path, m.id);
    }
    for (const QJsonValue &v : QJsonDocument::fromJson(tractor.get("studio:sequences")).array()) {
        const QJsonObject o = v.toObject();
        data->doc.sequences.append({o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("name")).toString(),
                                    timelineFromJson(o.value(QStringLiteral("timeline")).toObject()),
                                    o.value(QStringLiteral("info")).toObject().toVariantMap()});
    }
    data->doc.timeline.markers = markersFromJson(QJsonDocument::fromJson(tractor.get("studio:markers")).array());

    // Tracks in MLT order (background, bottom video ... top video, audio)
    QList<Track> mltTracks;
    QList<Part> parts;
    for (int i = 0; i < tractor.count(); ++i) {
        std::unique_ptr<Mlt::Producer> tp(tractor.track(i));
        if (!tp || !tp->is_valid())
            continue;
        if (tp->get_int("studio:background") || qstrcmp(tp->get("id"), "background") == 0 || qstrcmp(tp->get("id"), "black_track") == 0)
            continue;
        Mlt::Playlist pl(*tp);
        if (!pl.is_valid())
            continue;
        Track t;
        t.id = QString::fromUtf8(tp->get("studio:track"));
        if (t.id.isEmpty())
            t.id = newId(QStringLiteral("t"));
        const int hide = tp->get_int("hide");
        const QString kind = QString::fromUtf8(tp->get("studio:kind"));
        t.kind = !kind.isEmpty() ? kindFrom(kind) : (hide & 1) && !(hide & 2) ? TrackKind::Audio : TrackKind::Video;
        t.name = QString::fromUtf8(tp->get("studio:name"));
        t.locked = tp->get_int("studio:locked");
        t.muted = tp->get_int("studio:muted") || (kind.isEmpty() && (hide & 2));
        t.hidden = tp->get_int("studio:hidden") || (kind.isEmpty() && t.kind == TrackKind::Video && (hide & 1));
        t.solo = tp->get_int("studio:solo");
        t.volumeDb = tp->get_double("studio:volume");
        t.pan = tp->get_double("studio:pan");
        if (const char *style = tp->get("studio:captionstyle"))
            t.captionStyle = QJsonDocument::fromJson(QByteArray(style)).object().toVariantMap();
        mltTracks.append(t);
        loader.readPlaylist(pl, mltTracks.size() - 1, parts);
    }

    // Merge the parts of each clip (body + transition tails/heads)
    for (const Part &p : std::as_const(parts)) {
        Track &t = mltTracks[p.track];
        Clip *existing = nullptr;
        for (Clip &c : t.clips)
            if (c.id == p.clip.id)
                existing = &c;
        if (!existing) {
            Clip c = p.clip;
            c.position = p.start;
            c.in = p.in;
            c.out = p.out;
            if (p.mixLength) {
                c.transitionIn = p.mixLength;
                c.transition = p.transition;
            }
            t.clips.append(c);
            continue;
        }
        if (p.start < existing->position) {
            existing->position = p.start;
        }
        existing->in = std::min(existing->in, p.in);
        existing->out = std::max(existing->out, p.out);
        if (p.mixLength) {
            existing->transitionIn = p.mixLength;
            existing->transition = p.transition;
        }
    }

    // Display order: video top-first, subtitle, then audio
    QList<Track> video, subs, audio;
    for (Track &t : mltTracks) {
        Ops::sortTrack(t);
        if (t.kind == TrackKind::Audio)
            audio.append(t);
        else if (t.kind == TrackKind::Subtitle)
            subs.append(t);
        else
            video.prepend(t);
    }
    data->doc.timeline.tracks = subs + video + audio;
    auto fillNames = [&](TrackKind k, const QString &prefix) {
        int n = 0;
        QList<Track *> list;
        for (Track &t : data->doc.timeline.tracks)
            if (t.kind == k)
                list << &t;
        if (k == TrackKind::Video)
            std::reverse(list.begin(), list.end());
        for (Track *t : list) {
            ++n;
            if (t->name.isEmpty())
                t->name = prefix + QString::number(n);
        }
    };
    fillNames(TrackKind::Video, QStringLiteral("V"));
    fillNames(TrackKind::Audio, QStringLiteral("A"));
    fillNames(TrackKind::Subtitle, QStringLiteral("S"));
    if (data->doc.timeline.tracks.isEmpty())
        data->doc.timeline = Timeline::defaultTimeline();
    return true;
}

bool load(const QString &path, ProjectData *data, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return loadXml(f.readAll(), data, error);
}

} // namespace Studio::ProjectIO
