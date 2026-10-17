/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "mltxml.h"

#include <QXmlStreamWriter>
#include <numeric>

namespace Studio {

namespace {

class Writer
{
public:
    explicit Writer(QXmlStreamWriter &xml)
        : x(xml)
    {
    }

    void props(const Props &p)
    {
        for (const auto &kv : p) {
            x.writeStartElement(QStringLiteral("property"));
            x.writeAttribute(QStringLiteral("name"), kv.first);
            x.writeCharacters(kv.second);
            x.writeEndElement();
        }
    }

    void prop(const QString &name, const QString &value) { props({{name, value}}); }

    void filter(const FilterSpec &f, const QString &element = QStringLiteral("filter"))
    {
        x.writeStartElement(element);
        x.writeAttribute(QStringLiteral("id"), QStringLiteral("%1%2").arg(element).arg(counter++));
        if (f.in >= 0) {
            x.writeAttribute(QStringLiteral("in"), QString::number(f.in));
            x.writeAttribute(QStringLiteral("out"), QString::number(f.out));
        }
        prop(QStringLiteral("mlt_service"), f.service);
        props(f.props);
        x.writeEndElement();
    }

    void transition(const TransitionSpec &t)
    {
        x.writeStartElement(QStringLiteral("transition"));
        x.writeAttribute(QStringLiteral("id"), QStringLiteral("transition%1").arg(counter++));
        if (t.in >= 0) {
            x.writeAttribute(QStringLiteral("in"), QString::number(t.in));
            x.writeAttribute(QStringLiteral("out"), QString::number(t.out));
        }
        prop(QStringLiteral("a_track"), QString::number(t.aTrack));
        prop(QStringLiteral("b_track"), QString::number(t.bTrack));
        prop(QStringLiteral("mlt_service"), t.service);
        props(t.props);
        x.writeEndElement();
    }

    void entry(const CutSpec &c, const QStringList &ids)
    {
        x.writeStartElement(QStringLiteral("entry"));
        x.writeAttribute(QStringLiteral("producer"), ids.value(c.producer));
        x.writeAttribute(QStringLiteral("in"), QString::number(c.in));
        x.writeAttribute(QStringLiteral("out"), QString::number(c.out));
        props(c.props);
        for (const FilterSpec &f : c.filters)
            filter(f);
        x.writeEndElement();
    }

    void singleCutPlaylist(const QString &id, const CutSpec &c, const QStringList &ids)
    {
        x.writeStartElement(QStringLiteral("playlist"));
        x.writeAttribute(QStringLiteral("id"), id);
        entry(c, ids);
        x.writeEndElement();
    }

    // Writes everything a plan needs; returns the id of its tractor.
    QString plan(const RenderPlan &p, const Props &rootProps)
    {
        const QString prefix = QStringLiteral("n%1_").arg(counter++);
        const bool top = depth == 0;
        const QString pre = top ? QString() : prefix;
        QStringList ids;
        for (int i = 0; i < p.producers.size(); ++i) {
            const ProducerSpec &ps = p.producers.at(i);
            if (ps.nested) {
                ++depth;
                ids << plan(*ps.nested, ps.props);
                --depth;
                continue;
            }
            const QString id = ps.key == QLatin1String("black") ? pre + QStringLiteral("black") : QStringLiteral("%1producer%2").arg(pre).arg(i);
            ids << id;
            const bool chain = ps.service == QLatin1String("chain");
            x.writeStartElement(chain ? QStringLiteral("chain") : QStringLiteral("producer"));
            x.writeAttribute(QStringLiteral("id"), id);
            prop(QStringLiteral("resource"), ps.resource);
            prop(QStringLiteral("mlt_service"), chain ? QStringLiteral("avformat-novalidate") : ps.service);
            props(ps.props);
            for (const FilterSpec &l : ps.links)
                filter(l, QStringLiteral("link"));
            x.writeEndElement();
        }

        QStringList trackIds;
        for (int t = 0; t < p.tracks.size(); ++t) {
            const TrackSpec &ts = p.tracks.at(t);
            const QString plId = t == 0 ? pre + QStringLiteral("background") : QStringLiteral("%1playlist%2").arg(pre).arg(t - 1);
            // Mixes are tractors that must exist before the playlist using them
            QStringList mixIds;
            for (int s = 0; s < ts.segments.size(); ++s) {
                const SegmentSpec &seg = ts.segments.at(s);
                if (seg.type != SegmentSpec::Mix)
                    continue;
                const QString mixId = QStringLiteral("%1mix%2_%3").arg(pre).arg(t).arg(s);
                singleCutPlaylist(mixId + QStringLiteral("_a"), seg.a, ids);
                singleCutPlaylist(mixId + QStringLiteral("_b"), seg.b, ids);
                x.writeStartElement(QStringLiteral("tractor"));
                x.writeAttribute(QStringLiteral("id"), mixId);
                x.writeAttribute(QStringLiteral("in"), QStringLiteral("0"));
                x.writeAttribute(QStringLiteral("out"), QString::number(seg.length - 1));
                props(seg.props);
                for (const char *suffix : {"_a", "_b"}) {
                    x.writeStartElement(QStringLiteral("track"));
                    x.writeAttribute(QStringLiteral("producer"), mixId + QLatin1String(suffix));
                    x.writeEndElement();
                }
                for (const TransitionSpec &tr : seg.transitions)
                    transition(tr);
                x.writeEndElement();
                mixIds << mixId;
            }
            x.writeStartElement(QStringLiteral("playlist"));
            x.writeAttribute(QStringLiteral("id"), plId);
            props(ts.props);
            int mixIndex = 0;
            for (const SegmentSpec &seg : ts.segments) {
                if (seg.type == SegmentSpec::Blank) {
                    x.writeStartElement(QStringLiteral("blank"));
                    x.writeAttribute(QStringLiteral("length"), QString::number(seg.length));
                    x.writeEndElement();
                } else if (seg.type == SegmentSpec::Cut) {
                    entry(seg.cut, ids);
                } else {
                    x.writeStartElement(QStringLiteral("entry"));
                    x.writeAttribute(QStringLiteral("producer"), mixIds.value(mixIndex++));
                    x.writeAttribute(QStringLiteral("in"), QStringLiteral("0"));
                    x.writeAttribute(QStringLiteral("out"), QString::number(seg.length - 1));
                    x.writeEndElement();
                }
            }
            for (const FilterSpec &f : ts.filters)
                filter(f);
            x.writeEndElement();
            trackIds << plId;
        }

        const QString tractorId = top ? QStringLiteral("tractor0") : pre + QStringLiteral("tractor");
        x.writeStartElement(QStringLiteral("tractor"));
        x.writeAttribute(QStringLiteral("id"), tractorId);
        x.writeAttribute(QStringLiteral("in"), QStringLiteral("0"));
        x.writeAttribute(QStringLiteral("out"), QString::number(p.length - 1));
        props(p.props);
        props(rootProps);
        for (int t = 0; t < trackIds.size(); ++t) {
            x.writeStartElement(QStringLiteral("track"));
            x.writeAttribute(QStringLiteral("producer"), trackIds.at(t));
            const QString hide = p.tracks.at(t).hide;
            if (!hide.isEmpty())
                x.writeAttribute(QStringLiteral("hide"), hide);
            x.writeEndElement();
        }
        for (const TransitionSpec &tr : p.transitions)
            transition(tr);
        for (const FilterSpec &f : p.filters)
            filter(f);
        x.writeEndElement();
        return tractorId;
    }

    QXmlStreamWriter &x;
    int counter = 0;
    int depth = 0;
};

} // namespace

QByteArray writeMltXml(const RenderPlan &plan, const QString &title, const Props &extraRootProps)
{
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.setAutoFormattingIndent(1);
    x.writeStartDocument();
    x.writeStartElement(QStringLiteral("mlt"));
    x.writeAttribute(QStringLiteral("LC_NUMERIC"), QStringLiteral("C"));
    x.writeAttribute(QStringLiteral("version"), QStringLiteral("7.0.0"));
    x.writeAttribute(QStringLiteral("title"), title.isEmpty() ? QStringLiteral("Lingmo Studio") : title);
    x.writeAttribute(QStringLiteral("producer"), QStringLiteral("tractor0"));

    const ProjectSettings &s = plan.settings;
    const int g = std::gcd(s.width, s.height);
    x.writeStartElement(QStringLiteral("profile"));
    x.writeAttribute(QStringLiteral("description"), QStringLiteral("%1x%2 %3fps").arg(s.width).arg(s.height).arg(formatNumber(s.fps())));
    x.writeAttribute(QStringLiteral("width"), QString::number(s.width));
    x.writeAttribute(QStringLiteral("height"), QString::number(s.height));
    x.writeAttribute(QStringLiteral("progressive"), QStringLiteral("1"));
    x.writeAttribute(QStringLiteral("sample_aspect_num"), QStringLiteral("1"));
    x.writeAttribute(QStringLiteral("sample_aspect_den"), QStringLiteral("1"));
    x.writeAttribute(QStringLiteral("display_aspect_num"), QString::number(s.width / g));
    x.writeAttribute(QStringLiteral("display_aspect_den"), QString::number(s.height / g));
    x.writeAttribute(QStringLiteral("frame_rate_num"), QString::number(s.fpsNum));
    x.writeAttribute(QStringLiteral("frame_rate_den"), QString::number(s.fpsDen));
    x.writeAttribute(QStringLiteral("colorspace"), QStringLiteral("709"));
    x.writeEndElement();

    Writer w(x);
    w.plan(plan, extraRootProps);
    x.writeEndElement();
    x.writeEndDocument();
    return out;
}

} // namespace Studio
