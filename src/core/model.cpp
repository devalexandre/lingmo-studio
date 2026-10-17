/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "model.h"

#include <QJsonArray>
#include <QUuid>

namespace Studio {

QString newId(const QString &prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

struct PresetDef {
    const char *id;
    int w, h, num, den;
};
static const PresetDef Presets[] = {
    {"1080p30", 1920, 1080, 30, 1}, {"1080p60", 1920, 1080, 60, 1}, {"1080p24", 1920, 1080, 24, 1},
    {"4k30", 3840, 2160, 30, 1},    {"4k60", 3840, 2160, 60, 1},    {"720p30", 1280, 720, 30, 1},
    {"vertical1080", 1080, 1920, 30, 1}, {"square1080", 1080, 1080, 30, 1},
};

QStringList ProjectSettings::presetIds()
{
    QStringList ids;
    for (const auto &p : Presets)
        ids << QString::fromLatin1(p.id);
    return ids;
}

ProjectSettings ProjectSettings::preset(const QString &id)
{
    ProjectSettings s;
    for (const auto &p : Presets) {
        if (id == QLatin1String(p.id)) {
            s.width = p.w;
            s.height = p.h;
            s.fpsNum = p.num;
            s.fpsDen = p.den;
        }
    }
    return s;
}

QString ProjectSettings::presetId() const
{
    for (const auto &p : Presets) {
        if (width == p.w && height == p.h && fpsNum == p.num && fpsDen == p.den)
            return QString::fromLatin1(p.id);
    }
    return {};
}

QJsonArray keyframesToJson(const QList<Keyframe> &kfs)
{
    QJsonArray a;
    for (const Keyframe &k : kfs)
        a.append(QJsonObject{{QStringLiteral("frame"), k.frame}, {QStringLiteral("values"), QJsonObject::fromVariantMap(k.values)}});
    return a;
}

QList<Keyframe> keyframesFromJson(const QJsonArray &a)
{
    QList<Keyframe> kfs;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        kfs.append({o.value(QStringLiteral("frame")).toInt(), o.value(QStringLiteral("values")).toObject().toVariantMap()});
    }
    return kfs;
}

QJsonObject Effect::toJson() const
{
    QJsonObject o{{QStringLiteral("type"), type}, {QStringLiteral("params"), QJsonObject::fromVariantMap(params)}};
    if (!enabled)
        o.insert(QStringLiteral("enabled"), false);
    if (!keyframes.isEmpty())
        o.insert(QStringLiteral("keyframes"), keyframesToJson(keyframes));
    return o;
}

Effect Effect::fromJson(const QJsonObject &o)
{
    Effect e;
    e.type = o.value(QStringLiteral("type")).toString();
    e.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    e.params = o.value(QStringLiteral("params")).toObject().toVariantMap();
    e.keyframes = keyframesFromJson(o.value(QStringLiteral("keyframes")).toArray());
    return e;
}

bool Clip::operator==(const Clip &o) const
{
    return toFullJson() == o.toFullJson();
}

QJsonObject Clip::toJson() const
{
    QJsonObject o{{QStringLiteral("id"), id}, {QStringLiteral("kind"), kind}};
    if (!mediaId.isEmpty())
        o.insert(QStringLiteral("media"), mediaId);
    if (!name.isEmpty())
        o.insert(QStringLiteral("name"), name);
    if (speed != 1.0)
        o.insert(QStringLiteral("speed"), speed);
    if (!speedRamp.isEmpty())
        o.insert(QStringLiteral("speedRamp"), keyframesToJson(speedRamp));
    if (freezeFrame >= 0)
        o.insert(QStringLiteral("freezeFrame"), freezeFrame);
    if (fadeIn)
        o.insert(QStringLiteral("fadeIn"), fadeIn);
    if (fadeOut)
        o.insert(QStringLiteral("fadeOut"), fadeOut);
    if (transitionIn) {
        o.insert(QStringLiteral("transitionIn"), transitionIn);
        o.insert(QStringLiteral("transition"), transition);
    }
    if (audioMuted)
        o.insert(QStringLiteral("audioMuted"), true);
    if (!linkId.isEmpty())
        o.insert(QStringLiteral("link"), linkId);
    if (!effects.isEmpty()) {
        QJsonArray a;
        for (const Effect &e : effects)
            a.append(e.toJson());
        o.insert(QStringLiteral("effects"), a);
    }
    if (!genParams.isEmpty())
        o.insert(QStringLiteral("gen"), QJsonObject::fromVariantMap(genParams));
    return o;
}

Clip Clip::fromJson(const QJsonObject &o)
{
    Clip c;
    c.id = o.value(QStringLiteral("id")).toString();
    c.kind = o.value(QStringLiteral("kind")).toString(QStringLiteral("media"));
    c.mediaId = o.value(QStringLiteral("media")).toString();
    c.name = o.value(QStringLiteral("name")).toString();
    c.speed = o.value(QStringLiteral("speed")).toDouble(1.0);
    c.speedRamp = keyframesFromJson(o.value(QStringLiteral("speedRamp")).toArray());
    c.freezeFrame = o.value(QStringLiteral("freezeFrame")).toInt(-1);
    c.fadeIn = o.value(QStringLiteral("fadeIn")).toInt();
    c.fadeOut = o.value(QStringLiteral("fadeOut")).toInt();
    c.transitionIn = o.value(QStringLiteral("transitionIn")).toInt();
    c.transition = o.value(QStringLiteral("transition")).toString(QStringLiteral("dissolve"));
    c.audioMuted = o.value(QStringLiteral("audioMuted")).toBool();
    c.linkId = o.value(QStringLiteral("link")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("effects")).toArray())
        c.effects.append(Effect::fromJson(v.toObject()));
    c.genParams = o.value(QStringLiteral("gen")).toObject().toVariantMap();
    return c;
}

QJsonObject Clip::toFullJson() const
{
    QJsonObject o = toJson();
    o.insert(QStringLiteral("position"), position);
    o.insert(QStringLiteral("in"), in);
    o.insert(QStringLiteral("out"), out);
    return o;
}

Clip Clip::fromFullJson(const QJsonObject &o)
{
    Clip c = fromJson(o);
    c.position = o.value(QStringLiteral("position")).toInt();
    c.in = o.value(QStringLiteral("in")).toInt();
    c.out = o.value(QStringLiteral("out")).toInt();
    return c;
}

bool Track::operator==(const Track &o) const
{
    return id == o.id && kind == o.kind && name == o.name && muted == o.muted && hidden == o.hidden
        && locked == o.locked && solo == o.solo && volumeDb == o.volumeDb && pan == o.pan && clips == o.clips;
}

int Timeline::duration() const
{
    int d = 0;
    for (const Track &t : tracks)
        for (const Clip &c : t.clips)
            d = std::max(d, c.end());
    return d;
}

int Timeline::indexOfTrack(const QString &id) const
{
    for (int i = 0; i < tracks.size(); ++i)
        if (tracks.at(i).id == id)
            return i;
    return -1;
}

QPair<int, int> Timeline::findClip(const QString &clipId) const
{
    for (int t = 0; t < tracks.size(); ++t)
        for (int c = 0; c < tracks.at(t).clips.size(); ++c)
            if (tracks.at(t).clips.at(c).id == clipId)
                return {t, c};
    return {-1, -1};
}

const Clip *Timeline::clip(const QString &clipId) const
{
    const auto [t, c] = findClip(clipId);
    return t < 0 ? nullptr : &tracks.at(t).clips.at(c);
}

Clip *Timeline::clip(const QString &clipId)
{
    const auto [t, c] = findClip(clipId);
    return t < 0 ? nullptr : &tracks[t].clips[c];
}

QString Timeline::nextTrackName(TrackKind kind) const
{
    const QString prefix = kind == TrackKind::Video ? QStringLiteral("V") : kind == TrackKind::Audio ? QStringLiteral("A") : QStringLiteral("S");
    int n = 0;
    for (const Track &t : tracks)
        if (t.kind == kind)
            ++n;
    return prefix + QString::number(n + 1);
}

Timeline Timeline::defaultTimeline()
{
    Timeline tl;
    auto make = [](TrackKind k, const QString &name) {
        Track t;
        t.id = newId(QStringLiteral("t"));
        t.kind = k;
        t.name = name;
        return t;
    };
    tl.tracks << make(TrackKind::Video, QStringLiteral("V2")) << make(TrackKind::Video, QStringLiteral("V1"))
              << make(TrackKind::Audio, QStringLiteral("A1")) << make(TrackKind::Audio, QStringLiteral("A2"));
    return tl;
}

QJsonObject MediaItem::toJson() const
{
    QJsonObject o{{QStringLiteral("id"), id},       {QStringLiteral("path"), path},
                  {QStringLiteral("name"), name},   {QStringLiteral("kind"), kind},
                  {QStringLiteral("length"), length}, {QStringLiteral("width"), width},
                  {QStringLiteral("height"), height}, {QStringLiteral("fps"), fps},
                  {QStringLiteral("vcodec"), videoCodec}, {QStringLiteral("acodec"), audioCodec},
                  {QStringLiteral("channels"), audioChannels}, {QStringLiteral("rate"), sampleRate},
                  {QStringLiteral("hasVideo"), hasVideo}, {QStringLiteral("hasAudio"), hasAudio}};
    if (!proxyPath.isEmpty())
        o.insert(QStringLiteral("proxy"), proxyPath);
    if (!transcript.isEmpty())
        o.insert(QStringLiteral("transcript"), transcript);
    return o;
}

MediaItem MediaItem::fromJson(const QJsonObject &o)
{
    MediaItem m;
    m.id = o.value(QStringLiteral("id")).toString();
    m.path = o.value(QStringLiteral("path")).toString();
    m.name = o.value(QStringLiteral("name")).toString();
    m.kind = o.value(QStringLiteral("kind")).toString();
    m.length = o.value(QStringLiteral("length")).toInt();
    m.width = o.value(QStringLiteral("width")).toInt();
    m.height = o.value(QStringLiteral("height")).toInt();
    m.fps = o.value(QStringLiteral("fps")).toDouble();
    m.videoCodec = o.value(QStringLiteral("vcodec")).toString();
    m.audioCodec = o.value(QStringLiteral("acodec")).toString();
    m.audioChannels = o.value(QStringLiteral("channels")).toInt();
    m.sampleRate = o.value(QStringLiteral("rate")).toInt();
    m.hasVideo = o.value(QStringLiteral("hasVideo")).toBool();
    m.hasAudio = o.value(QStringLiteral("hasAudio")).toBool();
    m.proxyPath = o.value(QStringLiteral("proxy")).toString();
    m.transcript = o.value(QStringLiteral("transcript")).toString();
    return m;
}

} // namespace Studio
