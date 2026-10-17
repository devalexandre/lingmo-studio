/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "plan.h"
#include "effects.h"

#include <QColor>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>

namespace Studio {

static constexpr int MaxNesting = 4;
static constexpr int UnlimitedLength = 15 * 60 * 60 * 60; // 15 h at 60 fps

QString formatNumber(double v)
{
    if (std::abs(v - std::round(v)) < 1e-9)
        return QString::number(qint64(std::llround(v)));
    return QString::number(v, 'f', 6).remove(QRegularExpression(QStringLiteral("0+$"))).remove(QRegularExpression(QStringLiteral("\\.$")));
}

QString mltColor(const QString &qmlColor)
{
    const QColor c(qmlColor);
    if (!c.isValid())
        return QStringLiteral("#00000000");
    return c.name(QColor::HexArgb);
}

int RenderPlan::producerIndex(const QString &key) const
{
    for (int i = 0; i < producers.size(); ++i)
        if (producers.at(i).key == key)
            return i;
    return -1;
}

Planner::Planner(const ProjectSettings &settings, const QList<MediaItem> &media, const QList<Sequence> &sequences, PlanOptions options)
    : m_settings(settings)
    , m_options(options)
{
    if (m_options.scale > 0 && std::abs(m_options.scale - 1.0) > 1e-6) {
        m_settings.width = std::max(2, int(std::lround(settings.width * m_options.scale / 2.0)) * 2);
        m_settings.height = std::max(2, int(std::lround(settings.height * m_options.scale / 2.0)) * 2);
    } else {
        m_options.scale = 1.0;
    }
    for (const MediaItem &m : media)
        m_media.insert(m.id, m);
    for (const Sequence &s : sequences)
        m_sequences.insert(s.id, s);
}

QStringList Planner::transitionTypes()
{
    return {QStringLiteral("dissolve"),   QStringLiteral("wipe-left"),  QStringLiteral("wipe-right"),
            QStringLiteral("wipe-down"),  QStringLiteral("wipe-up"),    QStringLiteral("barn-door-h"),
            QStringLiteral("barn-door-v"), QStringLiteral("diagonal"),  QStringLiteral("iris-circle"),
            QStringLiteral("iris-box"),   QStringLiteral("clock"),      QStringLiteral("matrix")};
}

QString Planner::lumaFor(const QString &type, bool *invert)
{
    if (invert)
        *invert = false;
    static const QHash<QString, QString> map = {
        {QStringLiteral("wipe-left"), QStringLiteral("%luma01.pgm")},   {QStringLiteral("wipe-right"), QStringLiteral("%luma01.pgm")},
        {QStringLiteral("wipe-down"), QStringLiteral("%luma02.pgm")},   {QStringLiteral("wipe-up"), QStringLiteral("%luma02.pgm")},
        {QStringLiteral("barn-door-h"), QStringLiteral("%luma03.pgm")}, {QStringLiteral("barn-door-v"), QStringLiteral("%luma04.pgm")},
        {QStringLiteral("diagonal"), QStringLiteral("%luma07.pgm")},    {QStringLiteral("matrix"), QStringLiteral("%luma09.pgm")},
        {QStringLiteral("iris-circle"), QStringLiteral("%luma16.pgm")}, {QStringLiteral("iris-box"), QStringLiteral("%luma18.pgm")},
        {QStringLiteral("clock"), QStringLiteral("%luma22.pgm")},
    };
    if (invert && (type == QLatin1String("wipe-right") || type == QLatin1String("wipe-up")))
        *invert = true;
    return map.value(type);
}

int Planner::sourceLength(const Clip &clip) const
{
    if (clip.kind == QLatin1String("compound")) {
        const auto it = m_sequences.constFind(clip.mediaId);
        return it == m_sequences.cend() ? 1 : std::max(1, it->timeline.duration());
    }
    if (clip.kind != QLatin1String("media"))
        return -1;
    const auto it = m_media.constFind(clip.mediaId);
    if (it == m_media.cend())
        return -1;
    if (it->kind == QLatin1String("image"))
        return -1;
    if (!clip.speedRamp.isEmpty())
        return -1; // the ramp decides; MLT holds the last frame
    const double speed = std::abs(clip.speed) > 0.001 ? std::abs(clip.speed) : 1.0;
    return std::max(1, int(std::floor(it->length / speed)));
}

int Planner::producerFor(RenderPlan &plan, const Clip &clip, int depth) const
{
    ProducerSpec spec;
    if (clip.kind == QLatin1String("title")) {
        spec.key = QStringLiteral("color:#00000000");
        spec.service = QStringLiteral("color");
        spec.resource = QStringLiteral("#00000000");
    } else if (clip.kind == QLatin1String("color")) {
        const QString c = mltColor(clip.genParams.value(QStringLiteral("color"), QStringLiteral("#000000")).toString());
        spec.key = QStringLiteral("color:") + c;
        spec.service = QStringLiteral("color");
        spec.resource = c;
    } else if (clip.kind == QLatin1String("compound")) {
        const auto it = m_sequences.constFind(clip.mediaId);
        if (it == m_sequences.cend() || depth >= MaxNesting)
            return -1;
        spec.key = QStringLiteral("seq:") + clip.mediaId;
        spec.service = QStringLiteral("tractor");
        spec.nested = std::make_shared<RenderPlan>(planInternal(it->timeline, depth + 1));
        if (m_options.storeModel)
            spec.props.append({QStringLiteral("studio:sequence"), clip.mediaId});
    } else {
        const auto it = m_media.constFind(clip.mediaId);
        if (it == m_media.cend())
            return -1;
        QString path = it->path;
        bool proxy = false;
        if (m_options.useProxies && !it->proxyPath.isEmpty() && QFileInfo::exists(it->proxyPath)) {
            path = it->proxyPath;
            proxy = true;
        }
        if (!clip.speedRamp.isEmpty()) {
            spec.key = QStringLiteral("ramp:") + clip.id + (proxy ? QStringLiteral(":proxy") : QString());
            spec.service = QStringLiteral("chain");
            spec.resource = path;
            FilterSpec link;
            link.service = QStringLiteral("timeremap");
            link.props.append({QStringLiteral("speed_map"), animString(clip.speedRamp, {{QStringLiteral("speed"), 1.0}}, QStringLiteral("speed"))});
            link.props.append({QStringLiteral("image_mode"), QStringLiteral("blend")});
            link.props.append({QStringLiteral("pitch"), QStringLiteral("1")});
            spec.links.append(link);
        } else if (std::abs(clip.speed - 1.0) > 1e-6 && it->kind != QLatin1String("image")) {
            const QString speed = formatNumber(clip.speed);
            spec.key = QStringLiteral("tw:%1:%2%3").arg(speed, it->id, proxy ? QStringLiteral(":proxy") : QString());
            spec.service = QStringLiteral("timewarp");
            spec.resource = speed + QLatin1Char(':') + path;
        } else {
            spec.key = QStringLiteral("m:%1%2").arg(it->id, proxy ? QStringLiteral(":proxy") : QString());
            spec.service = it->kind == QLatin1String("image") ? QStringLiteral("qimage") : QStringLiteral("avformat");
            spec.resource = path;
            if (it->kind == QLatin1String("image"))
                spec.props.append({QStringLiteral("ttl"), QStringLiteral("1")});
        }
        if (m_options.storeModel) {
            spec.props.append({QStringLiteral("studio:media"), it->id});
            if (proxy)
                spec.props.append({QStringLiteral("studio:original"), it->path});
        }
    }
    const int existing = plan.producerIndex(spec.key);
    if (existing >= 0)
        return existing;
    if (spec.service == QLatin1String("color") || spec.service == QLatin1String("qimage")) {
        spec.props.append({QStringLiteral("length"), QString::number(UnlimitedLength)});
        spec.props.append({QStringLiteral("out"), QString::number(UnlimitedLength - 1)});
    }
    plan.producers.append(spec);
    return plan.producers.size() - 1;
}

QList<FilterSpec> Planner::clipFilters(const Clip &clip, const Track &track, int depth) const
{
    Q_UNUSED(depth)
    QList<FilterSpec> filters;
    FilterContext ctx{m_settings.width, m_settings.height, clip.in, clip.out, m_options.scale};
    const bool videoTrack = !track.isAudio();
    const bool audioOn = !clip.audioMuted;

    if (clip.freezeFrame >= 0 && videoTrack) {
        FilterSpec f;
        f.service = QStringLiteral("freeze");
        f.in = clip.in;
        f.out = clip.out;
        f.props = {{QStringLiteral("frame"), QString::number(std::max(0, clip.freezeFrame - clip.in))},
                   {QStringLiteral("freeze_before"), QStringLiteral("1")},
                   {QStringLiteral("freeze_after"), QStringLiteral("1")}};
        filters.append(f);
    }
    for (const Effect &e : clip.effects) {
        const bool audioEffect = EffectRegistry::isAudio(e.type);
        if (audioEffect && !audioOn)
            continue;
        if (!audioEffect && !videoTrack)
            continue;
        filters.append(EffectRegistry::filters(e, ctx));
    }
    const int d = clip.duration();
    if (clip.fadeIn > 0 || clip.fadeOut > 0) {
        QStringList level, gain;
        if (clip.fadeIn > 0) {
            level << QStringLiteral("0=0") << QStringLiteral("%1=1").arg(clip.fadeIn - 1);
            gain << QStringLiteral("0=-60") << QStringLiteral("%1=0").arg(clip.fadeIn - 1);
        } else {
            level << QStringLiteral("0=1");
            gain << QStringLiteral("0=0");
        }
        if (clip.fadeOut > 0) {
            level << QStringLiteral("%1=1").arg(d - clip.fadeOut) << QStringLiteral("%1=0").arg(d - 1);
            gain << QStringLiteral("%1=0").arg(d - clip.fadeOut) << QStringLiteral("%1=-60").arg(d - 1);
        }
        if (videoTrack && clip.kind != QLatin1String("compound")) {
            FilterSpec f;
            f.service = QStringLiteral("brightness");
            f.in = clip.in;
            f.out = clip.out;
            f.props = {{QStringLiteral("level"), level.join(QLatin1Char(';'))}, {QStringLiteral("alpha"), QStringLiteral("-1")}};
            if (m_options.storeModel)
                f.props.append({QStringLiteral("studio:fade"), QStringLiteral("video")});
            filters.append(f);
        }
        if (audioOn && clip.kind != QLatin1String("title") && clip.kind != QLatin1String("color")) {
            FilterSpec f;
            f.service = QStringLiteral("volume");
            f.in = clip.in;
            f.out = clip.out;
            f.props = {{QStringLiteral("level"), gain.join(QLatin1Char(';'))}};
            if (m_options.storeModel)
                f.props.append({QStringLiteral("studio:fade"), QStringLiteral("audio")});
            filters.append(f);
        }
    }
    if (!audioOn) {
        FilterSpec f;
        f.service = QStringLiteral("volume");
        f.in = clip.in;
        f.out = clip.out;
        f.props = {{QStringLiteral("level"), QStringLiteral("-200")}};
        if (m_options.storeModel)
            f.props.append({QStringLiteral("studio:mute"), QStringLiteral("1")});
        filters.append(f);
    }
    return filters;
}

void Planner::planTrack(RenderPlan &plan, const Track &track, bool soloActive, int depth) const
{
    TrackSpec ts;
    const bool audioMuted = track.muted || (soloActive && !track.solo && track.isAudio());
    if (track.isAudio())
        ts.hide = audioMuted ? QStringLiteral("both") : QStringLiteral("video");
    else if (track.kind == TrackKind::Subtitle)
        ts.hide = track.hidden ? QStringLiteral("both") : QStringLiteral("audio");
    else if (track.hidden && track.muted)
        ts.hide = QStringLiteral("both");
    else if (track.hidden)
        ts.hide = QStringLiteral("video");
    else if (track.muted)
        ts.hide = QStringLiteral("audio");

    if (m_options.storeModel) {
        ts.props = {{QStringLiteral("studio:track"), track.id},
                    {QStringLiteral("studio:name"), track.name},
                    {QStringLiteral("studio:kind"), track.isAudio() ? QStringLiteral("audio")
                                                   : track.kind == TrackKind::Subtitle ? QStringLiteral("subtitle") : QStringLiteral("video")}};
        if (track.locked)
            ts.props.append({QStringLiteral("studio:locked"), QStringLiteral("1")});
        if (track.muted)
            ts.props.append({QStringLiteral("studio:muted"), QStringLiteral("1")});
        if (track.hidden)
            ts.props.append({QStringLiteral("studio:hidden"), QStringLiteral("1")});
        if (track.solo)
            ts.props.append({QStringLiteral("studio:solo"), QStringLiteral("1")});
        if (track.volumeDb != 0)
            ts.props.append({QStringLiteral("studio:volume"), formatNumber(track.volumeDb)});
        if (track.pan != 0)
            ts.props.append({QStringLiteral("studio:pan"), formatNumber(track.pan)});
    }
    if (track.volumeDb != 0) {
        FilterSpec f;
        f.service = QStringLiteral("volume");
        f.props = {{QStringLiteral("level"), formatNumber(track.volumeDb)}};
        if (m_options.storeModel)
            f.props.append({QStringLiteral("studio:mixer"), QStringLiteral("1")});
        ts.filters.append(f);
    }
    if (track.pan != 0) {
        FilterSpec f;
        f.service = QStringLiteral("panner");
        f.props = {{QStringLiteral("channel"), QStringLiteral("-1")}, {QStringLiteral("split"), formatNumber((track.pan + 1) / 2)}};
        if (m_options.storeModel)
            f.props.append({QStringLiteral("studio:mixer"), QStringLiteral("1")});
        ts.filters.append(f);
    }

    auto cutFor = [&](const Clip &c, int in, int out) {
        CutSpec cut;
        cut.producer = producerFor(plan, c, depth);
        cut.in = in;
        cut.out = out;
        cut.filters = clipFilters(c, track, depth);
        if (m_options.storeModel)
            cut.props.append({QStringLiteral("studio:clip"), QString::fromUtf8(QJsonDocument(c.toJson()).toJson(QJsonDocument::Compact))});
        return cut;
    };

    int pos = 0;
    const QList<Clip> &clips = track.clips;
    for (int i = 0; i < clips.size(); ++i) {
        const Clip &c = clips.at(i);
        const int start = c.position + c.transitionIn; // the head was played inside the previous mix
        if (start > pos) {
            SegmentSpec blank;
            blank.type = SegmentSpec::Blank;
            blank.length = start - pos;
            ts.segments.append(blank);
            pos = start;
        }
        const int nextOverlap = (i + 1 < clips.size()) ? clips.at(i + 1).transitionIn : 0;
        const int bodyIn = c.in + c.transitionIn;
        const int bodyOut = c.out - nextOverlap;
        if (bodyOut >= bodyIn) {
            SegmentSpec seg;
            seg.type = SegmentSpec::Cut;
            seg.length = bodyOut - bodyIn + 1;
            seg.cut = cutFor(c, bodyIn, bodyOut);
            if (seg.cut.producer >= 0)
                ts.segments.append(seg);
            else {
                seg.type = SegmentSpec::Blank;
                ts.segments.append(seg);
            }
            pos += seg.length;
        }
        if (nextOverlap > 0) {
            const Clip &n = clips.at(i + 1);
            SegmentSpec mix;
            mix.type = SegmentSpec::Mix;
            mix.length = nextOverlap;
            mix.a = cutFor(c, c.out - nextOverlap + 1, c.out);
            mix.b = cutFor(n, n.in, n.in + nextOverlap - 1);
            if (!track.isAudio()) {
                TransitionSpec luma;
                luma.service = QStringLiteral("luma");
                luma.in = 0;
                luma.out = nextOverlap - 1;
                bool invert = false;
                const QString res = lumaFor(n.transition, &invert);
                if (!res.isEmpty()) {
                    luma.props.append({QStringLiteral("resource"), res});
                    luma.props.append({QStringLiteral("softness"), QStringLiteral("0.1")});
                }
                if (invert)
                    luma.props.append({QStringLiteral("invert"), QStringLiteral("1")});
                mix.transitions.append(luma);
            }
            TransitionSpec audio;
            audio.service = QStringLiteral("mix");
            audio.in = 0;
            audio.out = nextOverlap - 1;
            audio.props = {{QStringLiteral("start"), QStringLiteral("-1")}};
            mix.transitions.append(audio);
            if (m_options.storeModel)
                mix.props = {{QStringLiteral("studio:transition"), n.transition}};
            if (mix.a.producer >= 0 && mix.b.producer >= 0)
                ts.segments.append(mix);
            else {
                mix.type = SegmentSpec::Blank;
                ts.segments.append(mix);
            }
            pos += nextOverlap;
        }
    }
    plan.tracks.append(ts);
}

RenderPlan Planner::planInternal(const Timeline &timeline, int depth) const
{
    RenderPlan plan;
    plan.settings = m_settings;
    plan.length = std::max(1, timeline.duration());

    // Background: black, silent
    ProducerSpec black;
    black.key = QStringLiteral("black");
    black.service = QStringLiteral("color");
    black.resource = QStringLiteral("#ff000000");
    black.props = {{QStringLiteral("length"), QString::number(UnlimitedLength)},
                   {QStringLiteral("out"), QString::number(UnlimitedLength - 1)},
                   {QStringLiteral("set.test_audio"), QStringLiteral("0")}};
    plan.producers.append(black);
    TrackSpec bg;
    SegmentSpec bgSeg;
    bgSeg.type = SegmentSpec::Cut;
    bgSeg.length = plan.length;
    bgSeg.cut.producer = 0;
    bgSeg.cut.in = 0;
    bgSeg.cut.out = plan.length - 1;
    bg.segments.append(bgSeg);
    if (m_options.storeModel)
        bg.props = {{QStringLiteral("studio:background"), QStringLiteral("1")}};
    plan.tracks.append(bg);

    bool soloActive = false;
    for (const Track &t : timeline.tracks)
        soloActive |= t.solo && t.isAudio();

    // MLT order: bottom video track first (display order is top first), then audio
    QList<const Track *> order;
    for (int i = timeline.tracks.size() - 1; i >= 0; --i)
        if (timeline.tracks.at(i).isVideo())
            order << &timeline.tracks.at(i);
    for (const Track &t : timeline.tracks)
        if (t.kind == TrackKind::Subtitle)
            order << &t;
    for (const Track &t : timeline.tracks)
        if (t.isAudio())
            order << &t;

    for (const Track *t : std::as_const(order)) {
        planTrack(plan, *t, soloActive, depth);
        const int index = plan.tracks.size() - 1;
        TransitionSpec mix;
        mix.service = QStringLiteral("mix");
        mix.aTrack = 0;
        mix.bTrack = index;
        mix.props = {{QStringLiteral("always_active"), QStringLiteral("1")}, {QStringLiteral("sum"), QStringLiteral("1")}};
        plan.transitions.append(mix);
        if (!t->isAudio()) {
            TransitionSpec blend;
            blend.service = QStringLiteral("qtblend");
            blend.aTrack = 0;
            blend.bTrack = index;
            blend.props = {{QStringLiteral("always_active"), QStringLiteral("1")}};
            plan.transitions.append(blend);
        }
    }
    return plan;
}

RenderPlan Planner::plan(const Timeline &timeline, const QList<Marker> &markers) const
{
    RenderPlan p = planInternal(timeline, 0);
    if (m_options.storeModel) {
        QJsonArray m;
        for (const Marker &mk : markers)
            m.append(QJsonObject{{QStringLiteral("frame"), mk.frame}, {QStringLiteral("name"), mk.name}, {QStringLiteral("color"), mk.color}});
        p.props.append({QStringLiteral("studio:markers"), QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact))});
    }
    return p;
}

} // namespace Studio
