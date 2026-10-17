/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Developer tool: writes a demo project (.mlt) from media files, exercising
// transitions, titles, grading, keyframes, speed, subtitles and markers.
// Usage: demo_project OUT.mlt VIDEO1 VIDEO2 VIDEO3 MUSIC

#include "effects.h"
#include "mediaprobe.h"
#include "projectio.h"
#include "timelineops.h"

#include <QGuiApplication>
#include <QTextStream>
#include <clocale>

using namespace Studio;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    const QStringList args = app.arguments();
    if (args.size() < 6) {
        QTextStream(stderr) << "usage: demo_project OUT.mlt VIDEO1 VIDEO2 VIDEO3 MUSIC\n";
        return 2;
    }
    ProjectData d;
    d.settings = ProjectSettings::preset(QStringLiteral("1080p30"));
    QList<MediaItem> m;
    for (int i = 2; i < 6; ++i) {
        MediaItem item;
        item.id = QStringLiteral("m%1").arg(i - 1);
        if (!MediaProbe::probe(args.at(i), d.settings, &item)) {
            QTextStream(stderr) << "cannot read " << args.at(i) << "\n";
            return 1;
        }
        m << item;
    }
    d.media = m;
    Timeline &tl = d.doc.timeline;
    tl = Timeline::defaultTimeline(); // V2 V1 A1 A2
    auto clip = [](const MediaItem &mi, int pos, int in, int out) {
        Clip c;
        c.id = newId(QStringLiteral("c"));
        c.mediaId = mi.id;
        c.position = pos;
        c.in = in;
        c.out = out;
        return c;
    };
    Clip a = clip(m[0], 0, 0, 149);
    a.fadeIn = 15;
    Ops::insertClip(tl, 1, a, false);
    Clip b = clip(m[1], 150, 0, 149);
    Effect wheels = EffectRegistry::create(QStringLiteral("wheels"));
    wheels.params[QStringLiteral("gain_r")] = 1.15;
    wheels.params[QStringLiteral("gain_b")] = 0.9;
    wheels.params[QStringLiteral("lift_b")] = 0.04;
    b.effects << wheels;
    Effect curves = EffectRegistry::create(QStringLiteral("curves"));
    curves.params[QStringLiteral("master")] = QStringLiteral("0/0 0.25/0.18 0.75/0.85 1/1");
    b.effects << curves;
    Effect color = EffectRegistry::create(QStringLiteral("color"));
    color.params[QStringLiteral("saturation")] = 1.3;
    b.effects << color;
    Ops::insertClip(tl, 1, b, false);
    Ops::setTransition(tl, b.id, 20, QStringLiteral("wipe-left"));
    Clip c = clip(m[2], 280, 0, 59);
    c.speed = 2.0;
    c.out = 59;
    Ops::insertClip(tl, 1, c, false);
    Ops::setTransition(tl, c.id, 15, QStringLiteral("dissolve"));

    // picture in picture with keyframed position on V2
    Clip pip = clip(m[0], 200, 60, 149);
    Effect tf = EffectRegistry::create(QStringLiteral("transform"));
    Ops::setKeyframe(tf.keyframes, 0, {{QStringLiteral("x"), 600.0}, {QStringLiteral("y"), -300.0}, {QStringLiteral("scale"), 30.0}, {QStringLiteral("rotation"), 0.0}, {QStringLiteral("opacity"), 0.0}});
    Ops::setKeyframe(tf.keyframes, 20, {{QStringLiteral("x"), 600.0}, {QStringLiteral("y"), -300.0}, {QStringLiteral("scale"), 30.0}, {QStringLiteral("rotation"), 0.0}, {QStringLiteral("opacity"), 100.0}});
    Ops::setKeyframe(tf.keyframes, 89, {{QStringLiteral("x"), 500.0}, {QStringLiteral("y"), 250.0}, {QStringLiteral("scale"), 36.0}, {QStringLiteral("rotation"), 8.0}, {QStringLiteral("opacity"), 100.0}});
    pip.effects << tf;
    Ops::insertClip(tl, 0, pip, false);

    Clip title;
    title.id = newId(QStringLiteral("c"));
    title.kind = QStringLiteral("title");
    title.position = 20;
    title.out = 99;
    title.fadeIn = title.fadeOut = 12;
    Effect text = EffectRegistry::create(QStringLiteral("text"));
    text.params[QStringLiteral("text")] = QStringLiteral("Lingmo Studio");
    text.params[QStringLiteral("size")] = 130;
    text.params[QStringLiteral("outline")] = 4.0;
    title.effects << text;
    Ops::insertClip(tl, 0, title, false);

    Clip music = clip(m[3], 0, 0, 299);
    Effect vol = EffectRegistry::create(QStringLiteral("volume"));
    Ops::setKeyframe(vol.keyframes, 0, {{QStringLiteral("gain"), -24.0}});
    Ops::setKeyframe(vol.keyframes, 60, {{QStringLiteral("gain"), -6.0}});
    Ops::setKeyframe(vol.keyframes, 299, {{QStringLiteral("gain"), -30.0}});
    music.effects << vol;
    Effect eq = EffectRegistry::create(QStringLiteral("eq"));
    eq.params[QStringLiteral("low")] = 4.0;
    music.effects << eq;
    Ops::insertClip(tl, 2, music, false);
    tl.tracks[2].volumeDb = -2;

    Track subs;
    subs.id = newId(QStringLiteral("t"));
    subs.kind = TrackKind::Subtitle;
    subs.name = QStringLiteral("S1");
    tl.tracks.prepend(subs);
    const QStringList lines = {QStringLiteral("Edição com MLT"), QStringLiteral("Cor, efeitos e áudio"), QStringLiteral("Exportação com GPU")};
    for (int i = 0; i < lines.size(); ++i) {
        Clip s;
        s.id = newId(QStringLiteral("c"));
        s.kind = QStringLiteral("title");
        s.position = 30 + i * 90;
        s.out = 69;
        Effect st = EffectRegistry::create(QStringLiteral("text"));
        st.params[QStringLiteral("text")] = lines.at(i);
        st.params[QStringLiteral("size")] = 52;
        st.params[QStringLiteral("weight")] = QStringLiteral("normal");
        st.params[QStringLiteral("outline")] = 3.0;
        st.params[QStringLiteral("valign")] = QStringLiteral("bottom");
        st.params[QStringLiteral("y")] = 74.0;
        st.params[QStringLiteral("h")] = 18.0;
        s.effects << st;
        Ops::insertClip(tl, 0, s, false);
    }
    tl.markers << Marker{150, QStringLiteral("Bars"), QStringLiteral("orange")} << Marker{280, QStringLiteral("Fast"), QStringLiteral("green")};

    QString error;
    if (!ProjectIO::save(args.at(1), d, &error)) {
        QTextStream(stderr) << error << "\n";
        return 1;
    }
    QTextStream(stdout) << "wrote " << args.at(1) << " (" << tl.duration() << " frames)\n";
    return 0;
}
