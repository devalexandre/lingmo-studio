/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Plain value types describing a Lingmo Studio project. They are the single
// source of truth for editing: the MLT graph is rebuilt from them (see plan.h),
// and undo/redo stores snapshots of them (copy-on-write, cheap).

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace Studio {

struct ProjectSettings {
    int width = 1920;
    int height = 1080;
    int fpsNum = 30;
    int fpsDen = 1;
    int sampleRate = 48000;
    int channels = 2;

    double fps() const { return fpsDen > 0 ? double(fpsNum) / fpsDen : 30.0; }
    bool operator==(const ProjectSettings &o) const
    {
        return width == o.width && height == o.height && fpsNum == o.fpsNum && fpsDen == o.fpsDen
            && sampleRate == o.sampleRate && channels == o.channels;
    }

    // Named presets: "1080p30", "1080p60", "4k30", "4k60", "720p30", "vertical1080" (9:16), "square1080"
    static QStringList presetIds();
    static ProjectSettings preset(const QString &id);
    QString presetId() const; // empty when custom
};

// A keyframe stores every animatable value of an effect at a frame relative to
// the clip start (frame 0 = first frame of the clip on the timeline).
struct Keyframe {
    int frame = 0;
    QVariantMap values;
    bool operator==(const Keyframe &o) const { return frame == o.frame && values == o.values; }
};

struct Effect {
    QString type;            // registry id, see effects.h ("transform", "color", "volume"...)
    bool enabled = true;
    QVariantMap params;      // static values (also the base when not animated)
    QList<Keyframe> keyframes; // sorted by frame; empty = not animated

    bool operator==(const Effect &o) const
    {
        return type == o.type && enabled == o.enabled && params == o.params && keyframes == o.keyframes;
    }
    QJsonObject toJson() const;
    static Effect fromJson(const QJsonObject &o);
};

enum class TrackKind { Video, Audio, Subtitle };

struct Clip {
    QString id;
    QString kind = QStringLiteral("media"); // media | title | color | compound
    QString mediaId;                        // MediaItem id (kind media) or sequence id (compound)
    QString name;
    int position = 0;   // timeline frame of the first frame
    int in = 0;         // first source frame (in frames of the speed-adjusted source)
    int out = 0;        // last source frame, inclusive
    double speed = 1.0; // constant speed (timewarp); negative = reverse
    QList<Keyframe> speedRamp; // values {"speed": x}; non-empty overrides speed (timeremap)
    int freezeFrame = -1;      // >= 0: source frame held for the whole clip
    int fadeIn = 0;
    int fadeOut = 0;
    int transitionIn = 0;      // frames overlapping the previous clip on the same track
    QString transition = QStringLiteral("dissolve");
    bool audioMuted = false;   // video clip whose audio was detached
    QString linkId;            // clips moved/trimmed together (detached A/V)
    QList<Effect> effects;
    QVariantMap genParams;     // kind color: {"color"}; title text lives in a "text" effect

    int duration() const { return out - in + 1; }
    int end() const { return position + duration(); } // exclusive
    bool operator==(const Clip &o) const;

    QJsonObject toJson() const; // everything but position/in/out (those live in the MLT structure)
    static Clip fromJson(const QJsonObject &o);
    QJsonObject toFullJson() const;
    static Clip fromFullJson(const QJsonObject &o);
};

struct Track {
    QString id;
    TrackKind kind = TrackKind::Video;
    QString name;
    bool muted = false;
    bool hidden = false;
    bool locked = false;
    bool solo = false;
    double volumeDb = 0.0; // mixer fader
    double pan = 0.0;      // -1 left .. 1 right
    QList<Clip> clips;     // sorted by position

    bool isVideo() const { return kind == TrackKind::Video; }
    bool isAudio() const { return kind == TrackKind::Audio; }
    bool operator==(const Track &o) const;
};

struct Marker {
    int frame = 0;
    QString name;
    QString color = QStringLiteral("blue"); // Theme color name
    bool operator==(const Marker &o) const { return frame == o.frame && name == o.name && color == o.color; }
};

// Tracks are kept in display order: video tracks top (highest) to bottom,
// then audio tracks, then the subtitle track. The MLT order is derived from it.
struct Timeline {
    QList<Track> tracks;
    QList<Marker> markers;

    int duration() const;
    int indexOfTrack(const QString &id) const;
    // Returns {track index, clip index} or {-1,-1}
    QPair<int, int> findClip(const QString &clipId) const;
    const Clip *clip(const QString &clipId) const;
    Clip *clip(const QString &clipId);
    bool operator==(const Timeline &o) const { return tracks == o.tracks && markers == o.markers; }

    static Timeline defaultTimeline(); // V2, V1, A1, A2
    QString nextTrackName(TrackKind kind) const;
};

// A nested sequence (compound clip). Its own timeline renders as one clip.
struct Sequence {
    QString id;
    QString name;
    Timeline timeline;
    bool operator==(const Sequence &o) const { return id == o.id && name == o.name && timeline == o.timeline; }
};

struct MediaItem {
    QString id;
    QString path;
    QString name;
    QString kind;        // video | audio | image
    int length = 0;      // frames at project fps (images: default still duration)
    int width = 0;
    int height = 0;
    double fps = 0;
    QString videoCodec;
    QString audioCodec;
    int audioChannels = 0;
    int sampleRate = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    QString proxyPath;   // set when a proxy exists
    QString transcript;  // SRT text from auto-subtitles (AI search)

    bool needsProxy() const { return kind == QLatin1String("video") && (width >= 3840 || height >= 2160); }
    QJsonObject toJson() const;
    static MediaItem fromJson(const QJsonObject &o);
    bool operator==(const MediaItem &o) const { return toJson() == o.toJson(); }
};

// Everything that undo/redo snapshots.
struct Document {
    Timeline timeline;
    QList<Sequence> sequences;
    bool operator==(const Document &o) const { return timeline == o.timeline && sequences == o.sequences; }
};

QString newId(const QString &prefix);

QJsonArray keyframesToJson(const QList<Keyframe> &kfs);
QList<Keyframe> keyframesFromJson(const QJsonArray &a);

} // namespace Studio
