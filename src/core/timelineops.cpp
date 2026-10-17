/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "timelineops.h"
#include "captions.h"

#include <QCoreApplication>
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

namespace Studio::Ops {

static void fail(QString *error, const QString &msg)
{
    if (error)
        *error = msg;
}

// ---------------------------------------------------------------- keyframes

QVariantMap valuesAt(const QList<Keyframe> &kfs, const QVariantMap &base, int frame)
{
    if (kfs.isEmpty())
        return base;
    if (frame <= kfs.first().frame) {
        QVariantMap v = base;
        v.insert(kfs.first().values);
        return v;
    }
    if (frame >= kfs.last().frame) {
        QVariantMap v = base;
        v.insert(kfs.last().values);
        return v;
    }
    for (int i = 0; i + 1 < kfs.size(); ++i) {
        const Keyframe &a = kfs.at(i);
        const Keyframe &b = kfs.at(i + 1);
        if (frame < a.frame || frame > b.frame)
            continue;
        const double t = b.frame == a.frame ? 0.0 : double(frame - a.frame) / (b.frame - a.frame);
        QVariantMap v = base;
        v.insert(a.values);
        for (auto it = b.values.cbegin(); it != b.values.cend(); ++it) {
            const QVariant av = a.values.value(it.key(), base.value(it.key()));
            bool okA = false, okB = false;
            const double x = av.toDouble(&okA);
            const double y = it.value().toDouble(&okB);
            if (okA && okB && av.typeId() != QMetaType::QString)
                v.insert(it.key(), x + (y - x) * t);
            else
                v.insert(it.key(), t < 1.0 ? av : it.value());
        }
        return v;
    }
    return base;
}

void setKeyframe(QList<Keyframe> &kfs, int frame, const QVariantMap &values)
{
    for (Keyframe &k : kfs) {
        if (k.frame == frame) {
            k.values.insert(values);
            return;
        }
    }
    kfs.append({frame, values});
    std::sort(kfs.begin(), kfs.end(), [](const Keyframe &a, const Keyframe &b) { return a.frame < b.frame; });
}

void shiftKeyframes(QList<Keyframe> &kfs, int delta, int duration, const QVariantMap &base)
{
    if (kfs.isEmpty())
        return;
    const QVariantMap first = valuesAt(kfs, base, -delta);
    const QVariantMap last = valuesAt(kfs, base, duration - 1 - delta);
    bool cutBefore = false, cutAfter = false;
    QList<Keyframe> out;
    for (Keyframe k : std::as_const(kfs)) {
        k.frame += delta;
        if (k.frame < 0)
            cutBefore = true;
        else if (k.frame > duration - 1)
            cutAfter = true;
        else
            out.append(k);
    }
    if (cutBefore && (out.isEmpty() || out.first().frame != 0))
        out.prepend({0, first});
    if (cutAfter && (out.isEmpty() || out.last().frame != duration - 1))
        out.append({std::max(0, duration - 1), last});
    kfs = out;
}

static void shiftClipKeyframes(Clip &c, int delta)
{
    for (Effect &e : c.effects)
        shiftKeyframes(e.keyframes, delta, c.duration(), e.params);
}

static void clampFades(Clip &c)
{
    const int d = c.duration();
    c.fadeIn = std::clamp(c.fadeIn, 0, d);
    c.fadeOut = std::clamp(c.fadeOut, 0, d - c.fadeIn);
}

// ------------------------------------------------------------------- tracks

void sortTrack(Track &t)
{
    std::stable_sort(t.clips.begin(), t.clips.end(), [](const Clip &a, const Clip &b) { return a.position < b.position; });
}

void normalizeTrack(Track &t)
{
    sortTrack(t);
    for (int i = 0; i < t.clips.size(); ++i) {
        Clip &cur = t.clips[i];
        if (cur.position < 0) {
            const int d = -cur.position;
            cur.position = 0;
            cur.in += d;
            shiftClipKeyframes(cur, -d);
        }
        if (i == 0) {
            cur.transitionIn = 0;
            clampFades(cur);
            continue;
        }
        Clip &prev = t.clips[i - 1];
        const int overlap = prev.end() - cur.position;
        if (overlap <= 0) {
            cur.transitionIn = 0;
        } else if (cur.transitionIn > 0) {
            // A transition may not eat into the previous clip's own transition
            const int maxLen = std::min(prev.duration() - prev.transitionIn, cur.duration());
            if (overlap > maxLen) {
                prev.out -= overlap - maxLen;
                cur.transitionIn = maxLen;
            } else {
                cur.transitionIn = overlap;
            }
        } else {
            prev.out -= overlap; // plain overlap: the later clip wins
            cur.transitionIn = 0;
        }
        clampFades(cur);
    }
    t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(), [](const Clip &c) { return c.duration() <= 0; }), t.clips.end());
    for (Clip &c : t.clips)
        clampFades(c);
}

QStringList clearRange(Track &t, int start, int end)
{
    QStringList created;
    if (end <= start)
        return created;
    QList<Clip> result;
    for (const Clip &c : std::as_const(t.clips)) {
        if (c.end() <= start || c.position >= end) {
            result.append(c);
        } else if (c.position >= start && c.end() <= end) {
            // fully covered: dropped
        } else if (c.position < start && c.end() > end) {
            Clip left = c;
            left.out = c.in + (start - c.position) - 1;
            left.fadeOut = 0;
            shiftClipKeyframes(left, 0);
            Clip right = c;
            right.id = newId(QStringLiteral("c"));
            right.position = end;
            right.in = c.in + (end - c.position);
            right.transitionIn = 0;
            right.fadeIn = 0;
            shiftClipKeyframes(right, -(end - c.position));
            created << right.id;
            result << left << right;
        } else if (c.position < start) {
            Clip left = c;
            left.out = c.in + (start - c.position) - 1;
            shiftClipKeyframes(left, 0);
            result << left;
        } else {
            Clip right = c;
            const int d = end - c.position;
            right.position = end;
            right.in += d;
            right.transitionIn = 0;
            shiftClipKeyframes(right, -d);
            result << right;
        }
    }
    t.clips = result;
    normalizeTrack(t);
    return created;
}

void rippleShift(Track &t, int at, int delta)
{
    if (delta == 0)
        return;
    if (delta > 0) {
        for (int i = 0; i < t.clips.size(); ++i) {
            const Clip c = t.clips.at(i);
            if (c.position < at && c.end() > at) {
                Clip left = c;
                left.out = c.in + (at - c.position) - 1;
                left.fadeOut = 0;
                Clip right = c;
                right.id = newId(QStringLiteral("c"));
                right.position = at;
                right.in = c.in + (at - c.position);
                right.transitionIn = 0;
                right.fadeIn = 0;
                shiftClipKeyframes(right, -(at - c.position));
                shiftClipKeyframes(left, 0);
                t.clips[i] = left;
                t.clips.insert(i + 1, right);
                break;
            }
        }
    }
    for (Clip &c : t.clips) {
        if (c.position >= at)
            c.position += delta;
    }
    normalizeTrack(t);
}

static bool compatible(const Track &t, const Clip &c)
{
    if (t.kind == TrackKind::Audio)
        return c.kind == QLatin1String("media");
    return true;
}

bool insertClip(Timeline &tl, int track, Clip clip, bool ripple, QString *error)
{
    if (track < 0 || track >= tl.tracks.size()) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such track"));
        return false;
    }
    Track &t = tl.tracks[track];
    if (t.locked) {
        fail(error, QCoreApplication::translate("TimelineOps", "The track is locked"));
        return false;
    }
    if (!compatible(t, clip)) {
        fail(error, QCoreApplication::translate("TimelineOps", "This clip can't go on an audio track"));
        return false;
    }
    if (clip.duration() <= 0) {
        fail(error, QCoreApplication::translate("TimelineOps", "The clip is empty"));
        return false;
    }
    if (clip.id.isEmpty())
        clip.id = newId(QStringLiteral("c"));
    clip.position = std::max(0, clip.position);
    clip.transitionIn = 0;
    if (ripple)
        rippleShift(t, clip.position, clip.duration());
    else
        clearRange(t, clip.position, clip.end());
    t.clips.append(clip);
    normalizeTrack(t);
    return true;
}

static QList<QPair<int, int>> linkedGroup(const Timeline &tl, const QString &clipId, bool followLinks)
{
    QList<QPair<int, int>> group;
    const auto first = tl.findClip(clipId);
    if (first.first < 0)
        return group;
    group << first;
    const QString link = tl.tracks.at(first.first).clips.at(first.second).linkId;
    if (!followLinks || link.isEmpty())
        return group;
    for (int t = 0; t < tl.tracks.size(); ++t)
        for (int c = 0; c < tl.tracks.at(t).clips.size(); ++c)
            if (tl.tracks.at(t).clips.at(c).linkId == link && QPair<int, int>(t, c) != first)
                group << QPair<int, int>(t, c);
    return group;
}

bool moveClip(Timeline &tl, const QString &clipId, int toTrack, int newPosition, bool ripple, QString *error)
{
    const auto group = linkedGroup(tl, clipId, true);
    if (group.isEmpty()) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such clip"));
        return false;
    }
    if (toTrack < 0 || toTrack >= tl.tracks.size()) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such track"));
        return false;
    }
    const Clip primary = tl.tracks.at(group.first().first).clips.at(group.first().second);
    if (tl.tracks.at(toTrack).kind != tl.tracks.at(group.first().first).kind) {
        fail(error, QCoreApplication::translate("TimelineOps", "Clips can only move between tracks of the same kind"));
        return false;
    }
    int delta = newPosition - primary.position;
    int minPos = INT_MAX;
    for (const auto &g : group) {
        if (tl.tracks.at(g.first).locked || tl.tracks.at(g.first == group.first().first ? toTrack : g.first).locked) {
            fail(error, QCoreApplication::translate("TimelineOps", "The track is locked"));
            return false;
        }
        minPos = std::min(minPos, tl.tracks.at(g.first).clips.at(g.second).position);
    }
    if (minPos + delta < 0)
        delta = -minPos;
    if (delta == 0 && toTrack == group.first().first)
        return false;

    struct Moving { int track; Clip clip; };
    QList<Moving> moving;
    for (const auto &g : group)
        moving.append({g.first, tl.tracks.at(g.first).clips.at(g.second)});
    for (const Moving &m : std::as_const(moving)) {
        Track &t = tl.tracks[m.track];
        for (int i = 0; i < t.clips.size(); ++i) {
            if (t.clips.at(i).id == m.clip.id) {
                t.clips.removeAt(i);
                break;
            }
        }
        if (ripple)
            rippleShift(t, m.clip.end(), -m.clip.duration());
        normalizeTrack(t);
    }
    for (int i = 0; i < moving.size(); ++i) {
        Clip c = moving.at(i).clip;
        const int track = i == 0 ? toTrack : moving.at(i).track;
        c.position += delta;
        c.transitionIn = 0;
        Track &t = tl.tracks[track];
        clearRange(t, c.position, c.end());
        t.clips.append(c);
        normalizeTrack(t);
    }
    return true;
}

static int sourceLimit(const SourceLength &len, const Clip &c)
{
    const int l = len ? len(c) : Unlimited;
    return l < 0 ? INT_MAX / 4 : l;
}

static const Clip *previousClip(const Track &t, const Clip &c)
{
    const Clip *best = nullptr;
    for (const Clip &o : t.clips)
        if (o.id != c.id && o.position < c.position && (!best || o.position > best->position))
            best = &o;
    return best;
}

static const Clip *nextClip(const Track &t, const Clip &c)
{
    const Clip *best = nullptr;
    for (const Clip &o : t.clips)
        if (o.id != c.id && o.position > c.position && (!best || o.position < best->position))
            best = &o;
    return best;
}

bool trimIn(Timeline &tl, const QString &clipId, int newPosition, const SourceLength &len, bool ripple, QString *error)
{
    const auto [ti, ci] = tl.findClip(clipId);
    if (ti < 0) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such clip"));
        return false;
    }
    Track &t = tl.tracks[ti];
    if (t.locked) {
        fail(error, QCoreApplication::translate("TimelineOps", "The track is locked"));
        return false;
    }
    Clip c = t.clips.at(ci);
    c.transitionIn = 0;
    int d = newPosition - c.position;
    d = std::max(d, -c.in);               // source start
    d = std::min(d, c.duration() - 1);    // at least one frame
    if (!ripple) {
        const Clip *prev = previousClip(t, c);
        const int prevEnd = prev ? prev->end() : 0;
        d = std::max(d, prevEnd - c.position);
        d = std::max(d, -c.position);
    }
    if (d == 0)
        return false;
    const int oldEnd = c.end();
    c.in += d;
    shiftClipKeyframes(c, -d);
    if (!ripple)
        c.position += d;
    t.clips[ci] = c;
    if (ripple) {
        for (Clip &o : t.clips)
            if (o.id != c.id && o.position >= oldEnd)
                o.position -= d;
    }
    Q_UNUSED(len)
    normalizeTrack(t);
    return true;
}

bool trimOut(Timeline &tl, const QString &clipId, int newEnd, const SourceLength &len, bool ripple, QString *error)
{
    const auto [ti, ci] = tl.findClip(clipId);
    if (ti < 0) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such clip"));
        return false;
    }
    Track &t = tl.tracks[ti];
    if (t.locked) {
        fail(error, QCoreApplication::translate("TimelineOps", "The track is locked"));
        return false;
    }
    Clip c = t.clips.at(ci);
    int d = newEnd - c.end();
    d = std::min(d, sourceLimit(len, c) - 1 - c.out);
    d = std::max(d, -(c.duration() - 1));
    if (!ripple) {
        const Clip *next = nextClip(t, c);
        if (next)
            d = std::min(d, next->position + next->transitionIn - c.end());
    }
    if (d == 0)
        return false;
    const int oldEnd = c.end();
    c.out += d;
    shiftClipKeyframes(c, 0);
    t.clips[ci] = c;
    if (ripple) {
        for (Clip &o : t.clips)
            if (o.id != c.id && o.position >= oldEnd)
                o.position += d;
    }
    normalizeTrack(t);
    return true;
}

bool roll(Timeline &tl, const QString &clipId, int newBoundary, const SourceLength &len, QString *error)
{
    const auto [ti, ci] = tl.findClip(clipId);
    if (ti < 0) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such clip"));
        return false;
    }
    Track &t = tl.tracks[ti];
    if (t.locked) {
        fail(error, QCoreApplication::translate("TimelineOps", "The track is locked"));
        return false;
    }
    Clip &c = t.clips[ci];
    if (ci + 1 >= t.clips.size() || t.clips.at(ci + 1).position != c.end()) {
        fail(error, QCoreApplication::translate("TimelineOps", "Roll needs two adjacent clips"));
        return false;
    }
    Clip &n = t.clips[ci + 1];
    int d = newBoundary - c.end();
    d = std::min(d, sourceLimit(len, c) - 1 - c.out);
    d = std::min(d, n.duration() - 1 - n.fadeOut);
    d = std::max(d, -(c.duration() - 1));
    d = std::max(d, -n.in);
    if (d == 0)
        return false;
    c.out += d;
    n.position += d;
    n.in += d;
    shiftClipKeyframes(n, -d);
    shiftClipKeyframes(c, 0);
    normalizeTrack(t);
    return true;
}

bool slip(Timeline &tl, const QString &clipId, int delta, const SourceLength &len, QString *error)
{
    const auto [ti, ci] = tl.findClip(clipId);
    if (ti < 0) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such clip"));
        return false;
    }
    if (tl.tracks.at(ti).locked) {
        fail(error, QCoreApplication::translate("TimelineOps", "The track is locked"));
        return false;
    }
    Clip &c = tl.tracks[ti].clips[ci];
    int d = std::max(delta, -c.in);
    d = std::min(d, sourceLimit(len, c) - 1 - c.out);
    if (d == 0)
        return false;
    c.in += d;
    c.out += d;
    return true;
}

bool slide(Timeline &tl, const QString &clipId, int delta, const SourceLength &len, QString *error)
{
    const auto [ti, ci] = tl.findClip(clipId);
    if (ti < 0) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such clip"));
        return false;
    }
    Track &t = tl.tracks[ti];
    if (t.locked) {
        fail(error, QCoreApplication::translate("TimelineOps", "The track is locked"));
        return false;
    }
    Clip c = t.clips.at(ci);
    Clip *prev = ci > 0 && t.clips.at(ci - 1).end() == c.position ? &t.clips[ci - 1] : nullptr;
    Clip *next = ci + 1 < t.clips.size() && t.clips.at(ci + 1).position == c.end() ? &t.clips[ci + 1] : nullptr;
    int d = delta;
    if (prev) {
        d = std::max(d, -(prev->duration() - 1));
        d = std::min(d, sourceLimit(len, *prev) - 1 - prev->out);
    } else {
        const Clip *p = previousClip(t, c);
        d = std::max(d, (p ? p->end() : 0) - c.position);
    }
    if (next) {
        d = std::min(d, next->duration() - 1);
        d = std::max(d, -next->in);
    } else {
        const Clip *n = nextClip(t, c);
        if (n)
            d = std::min(d, n->position - c.end());
    }
    if (d == 0)
        return false;
    if (prev) {
        prev->out += d;
        shiftClipKeyframes(*prev, 0);
    }
    if (next) {
        next->position += d;
        next->in += d;
        shiftClipKeyframes(*next, -d);
    }
    t.clips[ci].position += d;
    normalizeTrack(t);
    return true;
}

// Caption clips: each half keeps the words that start inside it. Both halves
// share the source origin, so the cut is at source frame right.in.
static void splitCaptionWords(Clip &left, Clip &right)
{
    const double fps = Captions::clipFps(left);
    const double cutMs = right.in * 1000.0 / fps;
    QList<Captions::Word> a, b;
    for (const Captions::Word &w : Captions::clipWords(left))
        (w.startMs < cutMs ? a : b).append(w);
    Captions::setClipWords(left, a);
    Captions::setClipWords(right, b);
}

QString split(Timeline &tl, const QString &clipId, int frame, bool followLinks)
{
    const auto [ti, ci] = tl.findClip(clipId);
    if (ti < 0 || tl.tracks.at(ti).locked)
        return {};
    Track &t = tl.tracks[ti];
    const Clip c = t.clips.at(ci);
    if (frame <= c.position + c.transitionIn || frame >= c.end())
        return {};
    const int offset = frame - c.position;
    Clip left = c;
    left.out = c.in + offset - 1;
    left.fadeOut = 0;
    shiftClipKeyframes(left, 0);
    Clip right = c;
    right.id = newId(QStringLiteral("c"));
    right.position = frame;
    right.in = c.in + offset;
    right.transitionIn = 0;
    right.fadeIn = 0;
    shiftClipKeyframes(right, -offset);
    const QString oldLink = c.linkId;
    if (!oldLink.isEmpty())
        right.linkId = newId(QStringLiteral("l"));
    if (Captions::isCaption(c))
        splitCaptionWords(left, right);
    t.clips[ci] = left;
    t.clips.insert(ci + 1, right);
    normalizeTrack(t);

    if (followLinks && !oldLink.isEmpty()) {
        for (int tt = 0; tt < tl.tracks.size(); ++tt) {
            for (int cc = 0; cc < tl.tracks.at(tt).clips.size(); ++cc) {
                const Clip &o = tl.tracks.at(tt).clips.at(cc);
                if (o.linkId == oldLink && o.id != c.id && o.position < frame && o.end() > frame) {
                    const QString partner = split(tl, o.id, frame, false);
                    if (Clip *p = tl.clip(partner))
                        p->linkId = right.linkId;
                    break;
                }
            }
        }
    }
    return right.id;
}

int splitAt(Timeline &tl, int frame, const QStringList &clipIds)
{
    QStringList ids;
    for (const Track &t : std::as_const(tl.tracks)) {
        if (t.locked)
            continue;
        for (const Clip &c : t.clips)
            if (c.position < frame && c.end() > frame && (clipIds.isEmpty() || clipIds.contains(c.id)))
                ids << c.id;
    }
    int n = 0;
    for (const QString &id : std::as_const(ids)) {
        const Clip *c = tl.clip(id);
        if (c && c->position < frame && c->end() > frame && !split(tl, id, frame, true).isEmpty())
            ++n;
    }
    return n;
}

bool removeClip(Timeline &tl, const QString &clipId, bool ripple, bool followLinks)
{
    const auto group = linkedGroup(tl, clipId, followLinks);
    if (group.isEmpty())
        return false;
    for (const auto &g : group)
        if (tl.tracks.at(g.first).locked)
            return false;
    QList<QPair<int, Clip>> removed;
    for (const auto &g : group)
        removed.append({g.first, tl.tracks.at(g.first).clips.at(g.second)});
    for (const auto &r : std::as_const(removed)) {
        Track &t = tl.tracks[r.first];
        for (int i = 0; i < t.clips.size(); ++i) {
            if (t.clips.at(i).id == r.second.id) {
                t.clips.removeAt(i);
                break;
            }
        }
        if (ripple) {
            const int shift = r.second.duration() - r.second.transitionIn;
            for (Clip &o : t.clips)
                if (o.position >= r.second.end() - 0)
                    o.position -= shift;
        }
        normalizeTrack(t);
    }
    return true;
}

bool removeGap(Timeline &tl, int track, int frame)
{
    if (track < 0 || track >= tl.tracks.size() || tl.tracks.at(track).locked)
        return false;
    Track &t = tl.tracks[track];
    int gapStart = 0;
    int gapEnd = INT_MAX;
    for (const Clip &c : std::as_const(t.clips)) {
        if (c.position <= frame && c.end() > frame)
            return false;
        if (c.end() <= frame)
            gapStart = std::max(gapStart, c.end());
        if (c.position > frame)
            gapEnd = std::min(gapEnd, c.position);
    }
    if (gapEnd == INT_MAX || gapEnd <= gapStart)
        return false;
    for (Clip &c : t.clips)
        if (c.position >= gapEnd)
            c.position -= gapEnd - gapStart;
    normalizeTrack(t);
    return true;
}

bool setTransition(Timeline &tl, const QString &clipId, int length, const QString &type, QString *error)
{
    const auto [ti, ci] = tl.findClip(clipId);
    if (ti < 0) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such clip"));
        return false;
    }
    Track &t = tl.tracks[ti];
    if (t.locked) {
        fail(error, QCoreApplication::translate("TimelineOps", "The track is locked"));
        return false;
    }
    if (t.kind == TrackKind::Subtitle) {
        fail(error, QCoreApplication::translate("TimelineOps", "Subtitles have no transitions"));
        return false;
    }
    Clip c = t.clips.at(ci);
    if (ci == 0 || t.clips.at(ci - 1).end() != c.position + c.transitionIn) {
        fail(error, QCoreApplication::translate("TimelineOps", "A transition needs a clip right before this one"));
        return false;
    }
    const Clip &prev = t.clips.at(ci - 1);
    length = std::clamp(length, 0, std::min(prev.duration() - prev.transitionIn, c.duration() - 1));
    const int delta = length - c.transitionIn;
    if (delta != 0) {
        for (Clip &o : t.clips)
            if (o.position >= c.position)
                o.position -= delta;
    }
    t.clips[ci].transitionIn = length;
    if (!type.isEmpty())
        t.clips[ci].transition = type;
    normalizeTrack(t);
    return true;
}

QString freezeFrame(Timeline &tl, const QString &clipId, int frame, int length)
{
    const auto [ti, ci] = tl.findClip(clipId);
    if (ti < 0 || length <= 0 || tl.tracks.at(ti).locked)
        return {};
    const Clip c = tl.tracks.at(ti).clips.at(ci);
    if (frame < c.position || frame >= c.end() || c.kind != QLatin1String("media"))
        return {};
    const int src = c.in + (frame - c.position);
    if (frame > c.position)
        split(tl, clipId, frame, false);
    Track &t = tl.tracks[ti];
    for (Clip &o : t.clips)
        if (o.position >= frame)
            o.position += length;
    Clip f = c;
    f.id = newId(QStringLiteral("c"));
    f.position = frame;
    f.in = src;
    f.out = src + length - 1;
    f.freezeFrame = src;
    f.fadeIn = f.fadeOut = 0;
    f.transitionIn = 0;
    f.linkId.clear();
    f.audioMuted = true;
    f.speed = 1.0;
    f.speedRamp.clear();
    for (Effect &e : f.effects)
        e.keyframes.clear();
    t.clips.append(f);
    normalizeTrack(t);
    return f.id;
}

QString detachAudio(Timeline &tl, const QString &clipId, int audioTrack, QString *error)
{
    const auto [ti, ci] = tl.findClip(clipId);
    if (ti < 0) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such clip"));
        return {};
    }
    if (audioTrack < 0 || audioTrack >= tl.tracks.size() || !tl.tracks.at(audioTrack).isAudio()) {
        fail(error, QCoreApplication::translate("TimelineOps", "Choose an audio track"));
        return {};
    }
    if (!tl.tracks.at(ti).isVideo() || tl.tracks.at(ti).clips.at(ci).audioMuted) {
        fail(error, QCoreApplication::translate("TimelineOps", "This clip has no attached audio"));
        return {};
    }
    if (tl.tracks.at(audioTrack).locked || tl.tracks.at(ti).locked) {
        fail(error, QCoreApplication::translate("TimelineOps", "The track is locked"));
        return {};
    }
    Clip &v = tl.tracks[ti].clips[ci];
    const QString link = v.linkId.isEmpty() ? newId(QStringLiteral("l")) : v.linkId;
    v.linkId = link;
    v.audioMuted = true;
    Clip a = v;
    a.id = newId(QStringLiteral("c"));
    a.audioMuted = false;
    a.transitionIn = 0;
    a.linkId = link;
    Track &t = tl.tracks[audioTrack];
    clearRange(t, a.position, a.end());
    t.clips.append(a);
    normalizeTrack(t);
    return a.id;
}

// ------------------------------------------------------------------ captions

static Clip *captionClip(Timeline &tl, const QString &clipId, QString *error)
{
    const auto [ti, ci] = tl.findClip(clipId);
    if (ti < 0 || !Captions::isCaption(tl.tracks.at(ti).clips.at(ci))) {
        fail(error, QCoreApplication::translate("TimelineOps", "No such caption"));
        return nullptr;
    }
    if (tl.tracks.at(ti).locked) {
        fail(error, QCoreApplication::translate("TimelineOps", "The track is locked"));
        return nullptr;
    }
    return &tl.tracks[ti].clips[ci];
}

bool setCaptionText(Timeline &tl, const QString &clipId, const QString &text, double fps, QString *error)
{
    Clip *c = captionClip(tl, clipId, error);
    if (!c)
        return false;
    const QList<Captions::Word> old = Captions::clipWords(*c);
    const QStringList parts = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        fail(error, QCoreApplication::translate("TimelineOps", "A caption needs some text"));
        return false;
    }
    QList<Captions::Word> words;
    if (parts.size() == old.size()) {
        words = old; // same word count: keep every timing, fix the spelling
        for (int i = 0; i < parts.size(); ++i)
            words[i].text = parts.at(i);
    } else {
        // spread over the spoken span (or the clip when it had no words)
        const double origin = Captions::originMs(*c, fps);
        qint64 start = qint64(c->position * 1000.0 / fps - origin);
        qint64 end = qint64(c->end() * 1000.0 / fps - origin);
        if (!old.isEmpty()) {
            start = std::max(start, old.first().startMs);
            end = std::min(end, old.last().endMs);
            if (end <= start)
                end = start + 1;
        }
        words = Captions::spreadWords(parts.join(QLatin1Char(' ')), start, end);
    }
    if (words == old)
        return false;
    Captions::setClipWords(*c, words);
    return true;
}

bool setCaptionWord(Timeline &tl, const QString &clipId, int index, const QString &text, QString *error)
{
    Clip *c = captionClip(tl, clipId, error);
    if (!c)
        return false;
    QList<Captions::Word> words = Captions::clipWords(*c);
    const QString t = text.simplified();
    if (index < 0 || index >= words.size() || t.isEmpty() || t.contains(QLatin1Char(' ')) || words.at(index).text == t)
        return false;
    words[index].text = t;
    Captions::setClipWords(*c, words);
    return true;
}

bool toggleCaptionEmphasis(Timeline &tl, const QString &clipId, int index, QString *error)
{
    Clip *c = captionClip(tl, clipId, error);
    if (!c)
        return false;
    QList<Captions::Word> words = Captions::clipWords(*c);
    if (index < 0 || index >= words.size())
        return false;
    words[index].emphasis = !words.at(index).emphasis;
    Captions::setClipWords(*c, words);
    return true;
}

QString splitCaption(Timeline &tl, const QString &clipId, int wordIndex, double fps, QString *error)
{
    Clip *c = captionClip(tl, clipId, error);
    if (!c)
        return {};
    const QList<Captions::Word> words = Captions::clipWords(*c);
    if (wordIndex <= 0 || wordIndex >= words.size()) {
        fail(error, QCoreApplication::translate("TimelineOps", "Choose a word in the middle of the caption"));
        return {};
    }
    const double origin = Captions::originMs(*c, fps);
    int frame = int(std::lround((origin + words.at(wordIndex).startMs) * fps / 1000.0));
    frame = std::clamp(frame, c->position + 1, c->end() - 1);
    if (frame <= c->position || frame >= c->end()) {
        fail(error, QCoreApplication::translate("TimelineOps", "The caption is too short to split"));
        return {};
    }
    c->genParams.insert(QStringLiteral("fps"), fps);
    const QString id = clipId;
    const QString right = split(tl, id, frame, false);
    // split() assigns words by start time; make sure the chosen word opens the new cue
    if (Clip *l = tl.clip(id)) {
        if (Clip *r = tl.clip(right)) {
            Captions::setClipWords(*l, words.mid(0, wordIndex));
            Captions::setClipWords(*r, words.mid(wordIndex));
        }
    }
    return right;
}

bool mergeCaptionWithNext(Timeline &tl, const QString &clipId, double fps, QString *error)
{
    Clip *c = captionClip(tl, clipId, error);
    if (!c)
        return false;
    const auto [ti, ci] = tl.findClip(clipId);
    Track &t = tl.tracks[ti];
    if (ci + 1 >= t.clips.size() || !Captions::isCaption(t.clips.at(ci + 1))) {
        fail(error, QCoreApplication::translate("TimelineOps", "There is no caption after this one"));
        return false;
    }
    Clip a = t.clips.at(ci);
    const Clip b = t.clips.at(ci + 1);
    // b's words, moved to a's source origin
    const double shift = Captions::originMs(b, fps) - Captions::originMs(a, fps);
    QList<Captions::Word> words = Captions::clipWords(a);
    for (Captions::Word w : Captions::clipWords(b)) {
        w.startMs = qint64(std::llround(w.startMs + shift));
        w.endMs = qint64(std::llround(w.endMs + shift));
        words.append(w);
    }
    a.out = a.in + (b.end() - a.position) - 1;
    Captions::setClipWords(a, words);
    if (Captions::clipSpeaker(b) != Captions::clipSpeaker(a) && Captions::clipSpeaker(a) == 1)
        a.genParams.remove(QStringLiteral("speaker"));
    t.clips[ci] = a;
    t.clips.removeAt(ci + 1);
    normalizeTrack(t);
    return true;
}

bool nudgeCaptionWord(Timeline &tl, const QString &clipId, int index, qint64 deltaMs, QString *error)
{
    Clip *c = captionClip(tl, clipId, error);
    if (!c)
        return false;
    QList<Captions::Word> words = Captions::clipWords(*c);
    if (index < 0 || index >= words.size() || deltaMs == 0)
        return false;
    Captions::Word &w = words[index];
    const qint64 lo = index > 0 ? words.at(index - 1).startMs + 1 : std::numeric_limits<qint64>::min() / 2;
    const qint64 hi = index + 1 < words.size() ? words.at(index + 1).startMs - 1 : std::numeric_limits<qint64>::max() / 2;
    const qint64 len = w.endMs - w.startMs;
    const qint64 start = std::clamp(w.startMs + deltaMs, lo, hi);
    if (start == w.startMs)
        return false;
    w.startMs = start;
    w.endMs = start + len;
    if (index > 0 && words.at(index - 1).endMs > w.startMs)
        words[index - 1].endMs = w.startMs;
    if (index + 1 < words.size() && w.endMs > words.at(index + 1).startMs)
        w.endMs = words.at(index + 1).startMs;
    Captions::setClipWords(*c, words);
    return true;
}

bool setCaptionSpeaker(Timeline &tl, const QString &clipId, int speaker, QString *error)
{
    Clip *c = captionClip(tl, clipId, error);
    if (!c || (speaker != 1 && speaker != 2) || Captions::clipSpeaker(*c) == speaker)
        return false;
    if (speaker == 2)
        c->genParams.insert(QStringLiteral("speaker"), 2);
    else
        c->genParams.remove(QStringLiteral("speaker"));
    return true;
}

} // namespace Studio::Ops
