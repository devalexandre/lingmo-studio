/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Timeline editing operations: insert/overwrite, move, trim, roll, slip, slide,
// split, ripple delete, transitions, freeze frames, detached audio, keyframes,
// and undo through value snapshots.

#include "timelineops.h"

#include <QTest>

using namespace Studio;

class TestTimeline : public QObject
{
    Q_OBJECT

private:
    static Clip media(const QString &id, int position, int in, int out)
    {
        Clip c;
        c.id = id;
        c.kind = QStringLiteral("media");
        c.mediaId = QStringLiteral("m1");
        c.position = position;
        c.in = in;
        c.out = out;
        return c;
    }
    static Ops::SourceLength len(int frames = 300)
    {
        return [frames](const Clip &) { return frames; };
    }
    static Timeline base()
    {
        Timeline tl = Timeline::defaultTimeline(); // V2, V1, A1, A2
        return tl;
    }
    static QString layout(const Track &t)
    {
        QStringList s;
        for (const Clip &c : t.clips)
            s << QStringLiteral("%1@%2[%3-%4]").arg(c.id).arg(c.position).arg(c.in).arg(c.out);
        return s.join(QLatin1Char(' '));
    }

private Q_SLOTS:
    void insertOverwriteAndRipple()
    {
        Timeline tl = base();
        QVERIFY(Ops::insertClip(tl, 1, media("a", 0, 0, 99), false));
        QVERIFY(Ops::insertClip(tl, 1, media("b", 100, 0, 49), false));
        // overwrite the middle of a: a is cut in two
        QVERIFY(Ops::insertClip(tl, 1, media("c", 20, 200, 229), false));
        const Track &t = tl.tracks.at(1);
        QCOMPARE(t.clips.size(), 4);
        QCOMPARE(t.clips.at(0).id, QStringLiteral("a"));
        QCOMPARE(t.clips.at(0).out, 19);
        QCOMPARE(t.clips.at(1).id, QStringLiteral("c"));
        QCOMPARE(t.clips.at(2).position, 50);
        QCOMPARE(t.clips.at(2).in, 50);
        QCOMPARE(t.clips.at(3).id, QStringLiteral("b"));
        QCOMPARE(tl.duration(), 150);

        // ripple insert pushes later clips
        QVERIFY(Ops::insertClip(tl, 1, media("d", 50, 0, 9), true));
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 110);
        QCOMPARE(tl.duration(), 160);
    }

    void audioTrackRejectsTitles()
    {
        Timeline tl = base();
        Clip title = media("t", 0, 0, 49);
        title.kind = QStringLiteral("title");
        QString error;
        QVERIFY(!Ops::insertClip(tl, 2, title, false, &error));
        QVERIFY(!error.isEmpty());
    }

    void splitAndUndoSnapshot()
    {
        Timeline tl = base();
        Ops::insertClip(tl, 1, media("a", 10, 0, 99), false);
        Effect fx;
        fx.type = QStringLiteral("transform");
        fx.params = {{QStringLiteral("opacity"), 100.0}};
        fx.keyframes = {{0, {{QStringLiteral("opacity"), 0.0}}}, {99, {{QStringLiteral("opacity"), 100.0}}}};
        tl.clip(QStringLiteral("a"))->effects << fx;
        tl.clip(QStringLiteral("a"))->fadeIn = 10;
        tl.clip(QStringLiteral("a"))->fadeOut = 10;

        const Timeline before = tl;
        const QString right = Ops::split(tl, QStringLiteral("a"), 60);
        QVERIFY(!right.isEmpty());
        const Clip *l = tl.clip(QStringLiteral("a"));
        const Clip *r = tl.clip(right);
        QCOMPARE(l->out, 49);
        QCOMPARE(r->position, 60);
        QCOMPARE(r->in, 50);
        QCOMPARE(r->out, 99);
        QCOMPARE(l->fadeIn, 10);
        QCOMPARE(l->fadeOut, 0);
        QCOMPARE(r->fadeIn, 0);
        QCOMPARE(r->fadeOut, 10);
        // keyframes: left ends at the interpolated value, right starts from it
        QCOMPARE(l->effects.first().keyframes.last().frame, 49);
        QVERIFY(qAbs(l->effects.first().keyframes.last().values.value(QStringLiteral("opacity")).toDouble() - 49.4949) < 0.01);
        QCOMPARE(r->effects.first().keyframes.first().frame, 0);
        QCOMPARE(r->effects.first().keyframes.last().frame, 49);

        // undo = restore the snapshot
        tl = before;
        QCOMPARE(tl.tracks.at(1).clips.size(), 1);
        QVERIFY(tl == before);

        // split outside the clip does nothing
        QVERIFY(Ops::split(tl, QStringLiteral("a"), 10).isEmpty());
        QVERIFY(Ops::split(tl, QStringLiteral("a"), 110).isEmpty());
    }

    void splitAtPlayheadAllTracks()
    {
        Timeline tl = base();
        Ops::insertClip(tl, 0, media("v2", 0, 0, 99), false);
        Ops::insertClip(tl, 1, media("v1", 0, 0, 99), false);
        Ops::insertClip(tl, 2, media("a1", 50, 0, 99), false);
        tl.tracks[0].locked = true;
        QCOMPARE(Ops::splitAt(tl, 70), 2);
        QCOMPARE(tl.tracks.at(0).clips.size(), 1);
        QCOMPARE(tl.tracks.at(1).clips.size(), 2);
        QCOMPARE(tl.tracks.at(2).clips.size(), 2);
    }

    void trimEdges()
    {
        Timeline tl = base();
        Ops::insertClip(tl, 1, media("a", 0, 10, 109), false);
        Ops::insertClip(tl, 1, media("b", 150, 0, 99), false);
        // trim in past the source start: already at in = 0, nothing changes
        QVERIFY(!Ops::trimIn(tl, QStringLiteral("b"), 100, len(), false));
        QCOMPARE(tl.clip(QStringLiteral("b"))->in, 0);
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 150);
        // move b's in point to 160
        QVERIFY(Ops::trimIn(tl, QStringLiteral("b"), 160, len(), false));
        QCOMPARE(tl.clip(QStringLiteral("b"))->in, 10);
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 160);
        // trim out of a can't run into b (b starts at 160)
        QVERIFY(Ops::trimOut(tl, QStringLiteral("a"), 400, len(), false));
        QCOMPARE(tl.clip(QStringLiteral("a"))->end(), 160);
        // source length limit (300 frames)
        Timeline t2 = base();
        Ops::insertClip(t2, 1, media("x", 0, 250, 279), false);
        Ops::trimOut(t2, QStringLiteral("x"), 1000, len(300), false);
        QCOMPARE(t2.clip(QStringLiteral("x"))->out, 299);
        // ripple trim out shortens and pulls later clips
        QVERIFY(Ops::trimOut(tl, QStringLiteral("a"), 120, len(), true));
        QCOMPARE(tl.clip(QStringLiteral("a"))->end(), 120);
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 120);
    }

    void rollSlipSlide()
    {
        Timeline tl = base();
        Ops::insertClip(tl, 1, media("a", 0, 50, 149), false);  // 100 frames
        Ops::insertClip(tl, 1, media("b", 100, 50, 149), false);
        Ops::insertClip(tl, 1, media("c", 200, 50, 149), false);
        QVERIFY(Ops::roll(tl, QStringLiteral("a"), 110, len()));
        QCOMPARE(tl.clip(QStringLiteral("a"))->out, 159);
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 110);
        QCOMPARE(tl.clip(QStringLiteral("b"))->in, 60);
        QCOMPARE(tl.duration(), 300);

        QVERIFY(Ops::slip(tl, QStringLiteral("b"), -20, len()));
        QCOMPARE(tl.clip(QStringLiteral("b"))->in, 40);
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 110);

        QVERIFY(Ops::slide(tl, QStringLiteral("b"), 10, len()));
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 120);
        QCOMPARE(tl.clip(QStringLiteral("a"))->end(), 120);
        QCOMPARE(tl.clip(QStringLiteral("c"))->position, 210);
        QCOMPARE(tl.clip(QStringLiteral("c"))->in, 60);
        QCOMPARE(tl.duration(), 300);
    }

    void moveAndOverwrite()
    {
        Timeline tl = base();
        Ops::insertClip(tl, 1, media("a", 0, 0, 99), false);
        Ops::insertClip(tl, 1, media("b", 100, 0, 99), false);
        QVERIFY(Ops::moveClip(tl, QStringLiteral("b"), 0, 50));
        QCOMPARE(tl.tracks.at(0).clips.size(), 1);
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 50);
        // moving onto a: a is trimmed
        QVERIFY(Ops::moveClip(tl, QStringLiteral("b"), 1, 50));
        QCOMPARE(tl.clip(QStringLiteral("a"))->out, 49);
        // video clip can't move to an audio track
        QVERIFY(!Ops::moveClip(tl, QStringLiteral("b"), 2, 0));
        // negative positions clamp to 0
        QVERIFY(Ops::moveClip(tl, QStringLiteral("a"), 0, -40));
        QCOMPARE(tl.clip(QStringLiteral("a"))->position, 0);
    }

    void rippleDeleteAndGap()
    {
        Timeline tl = base();
        Ops::insertClip(tl, 1, media("a", 0, 0, 49), false);
        Ops::insertClip(tl, 1, media("b", 50, 0, 49), false);
        Ops::insertClip(tl, 1, media("c", 130, 0, 49), false);
        QVERIFY(Ops::removeClip(tl, QStringLiteral("b"), true));
        QCOMPARE(tl.clip(QStringLiteral("c"))->position, 80);
        QVERIFY(Ops::removeGap(tl, 1, 60));
        QCOMPARE(tl.clip(QStringLiteral("c"))->position, 50);
        QVERIFY(!Ops::removeGap(tl, 1, 10)); // inside a clip
        QVERIFY(Ops::removeClip(tl, QStringLiteral("a"), false));
        QCOMPARE(tl.clip(QStringLiteral("c"))->position, 50);
    }

    void transitions()
    {
        Timeline tl = base();
        Ops::insertClip(tl, 1, media("a", 0, 0, 99), false);
        Ops::insertClip(tl, 1, media("b", 100, 0, 99), false);
        Ops::insertClip(tl, 1, media("c", 200, 0, 99), false);
        QVERIFY(Ops::setTransition(tl, QStringLiteral("b"), 20, QStringLiteral("wipe-left")));
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 80);
        QCOMPARE(tl.clip(QStringLiteral("b"))->transitionIn, 20);
        QCOMPARE(tl.clip(QStringLiteral("c"))->position, 180);
        QCOMPARE(tl.duration(), 280);
        // change length, then remove
        QVERIFY(Ops::setTransition(tl, QStringLiteral("b"), 30, {}));
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 70);
        QCOMPARE(tl.clip(QStringLiteral("b"))->transition, QStringLiteral("wipe-left"));
        QVERIFY(Ops::setTransition(tl, QStringLiteral("b"), 0, {}));
        QCOMPARE(tl.clip(QStringLiteral("b"))->position, 100);
        QCOMPARE(tl.clip(QStringLiteral("b"))->transitionIn, 0);
        // the first clip has nothing to transition from
        QVERIFY(!Ops::setTransition(tl, QStringLiteral("a"), 10, {}));
        // moving a transitioned clip away drops the transition
        Ops::setTransition(tl, QStringLiteral("b"), 20, {});
        Ops::moveClip(tl, QStringLiteral("b"), 0, 0);
        QCOMPARE(tl.clip(QStringLiteral("b"))->transitionIn, 0);
    }

    void freezeAndDetach()
    {
        Timeline tl = base();
        Ops::insertClip(tl, 1, media("a", 0, 0, 99), false);
        const QString f = Ops::freezeFrame(tl, QStringLiteral("a"), 40, 30);
        QVERIFY(!f.isEmpty());
        QCOMPARE(tl.clip(f)->freezeFrame, 40);
        QCOMPARE(tl.clip(f)->position, 40);
        QCOMPARE(tl.clip(f)->duration(), 30);
        QCOMPARE(tl.duration(), 130);

        Timeline t2 = base();
        Ops::insertClip(t2, 1, media("v", 10, 0, 99), false);
        const QString a = Ops::detachAudio(t2, QStringLiteral("v"), 2);
        QVERIFY(!a.isEmpty());
        QVERIFY(t2.clip(QStringLiteral("v"))->audioMuted);
        QCOMPARE(t2.clip(a)->linkId, t2.clip(QStringLiteral("v"))->linkId);
        // linked: move together
        QVERIFY(Ops::moveClip(t2, QStringLiteral("v"), 1, 30));
        QCOMPARE(t2.clip(a)->position, 30);
        // L cut: trim audio independently
        QVERIFY(Ops::trimOut(t2, a, t2.clip(a)->end() - 20, len()));
        QCOMPARE(t2.clip(a)->end(), 110);
        QCOMPARE(t2.clip(QStringLiteral("v"))->end(), 130);
        // split follows the link
        const QString right = Ops::split(t2, QStringLiteral("v"), 60);
        QVERIFY(!right.isEmpty());
        QCOMPARE(t2.tracks.at(2).clips.size(), 2);
        QCOMPARE(t2.tracks.at(2).clips.at(1).linkId, t2.clip(right)->linkId);
        // delete follows the link
        QVERIFY(Ops::removeClip(t2, right, false));
        QCOMPARE(t2.tracks.at(2).clips.size(), 1);
    }

    void keyframeInterpolation()
    {
        QList<Keyframe> kfs;
        Ops::setKeyframe(kfs, 10, {{QStringLiteral("v"), 0.0}});
        Ops::setKeyframe(kfs, 0, {{QStringLiteral("v"), 100.0}});
        Ops::setKeyframe(kfs, 20, {{QStringLiteral("v"), 50.0}});
        QCOMPARE(kfs.first().frame, 0);
        QCOMPARE(Ops::valuesAt(kfs, {}, 5).value(QStringLiteral("v")).toDouble(), 50.0);
        QCOMPARE(Ops::valuesAt(kfs, {}, 15).value(QStringLiteral("v")).toDouble(), 25.0);
        QCOMPARE(Ops::valuesAt(kfs, {}, 99).value(QStringLiteral("v")).toDouble(), 50.0);
    }
};

QTEST_GUILESS_MAIN(TestTimeline)
#include "tst_timeline.moc"
