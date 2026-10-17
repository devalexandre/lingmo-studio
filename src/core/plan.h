/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// RenderPlan: an MLT-shaped description of a timeline (producers, playlists of
// cuts/blanks/mixes, tractor transitions) computed from Studio::Timeline.
// The same plan is turned into MLT XML (mltxml.h: project files, export) and
// into live Mlt:: objects (mltbuilder.h: preview), so both always agree.

#pragma once

#include "model.h"

#include <QHash>
#include <memory>

namespace Studio {

using Props = QList<QPair<QString, QString>>;

struct FilterSpec {
    QString service;
    Props props;
    int in = -1; // -1: not set
    int out = -1;
};

struct RenderPlan;

struct ProducerSpec {
    QString key;      // cache key, unique per distinct producer
    QString service;  // avformat | qimage | color | timewarp | tractor (nested) | chain
    QString resource;
    Props props;
    QList<FilterSpec> links;            // chain links (timeremap)
    std::shared_ptr<RenderPlan> nested; // service tractor
};

struct CutSpec {
    int producer = -1; // index in RenderPlan::producers
    int in = 0;
    int out = 0;
    QList<FilterSpec> filters;
    Props props;
};

struct TransitionSpec {
    QString service;
    int aTrack = 0;
    int bTrack = 1;
    int in = -1;
    int out = -1;
    Props props;
};

struct SegmentSpec {
    enum Type { Blank, Cut, Mix } type = Blank;
    int length = 0;
    CutSpec cut;                       // Cut
    CutSpec a, b;                      // Mix: outgoing tail, incoming head
    QList<TransitionSpec> transitions; // Mix
    Props props;                       // Mix
};

struct TrackSpec {
    QList<SegmentSpec> segments;
    Props props;
    QString hide; // "", "video", "audio", "both"
    QList<FilterSpec> filters;
};

struct RenderPlan {
    ProjectSettings settings;
    QList<ProducerSpec> producers;
    QList<TrackSpec> tracks; // MLT order, 0 = black background
    QList<TransitionSpec> transitions;
    QList<FilterSpec> filters;
    Props props;
    int length = 1;

    int producerIndex(const QString &key) const;
};

struct PlanOptions {
    bool useProxies = false;
    bool storeModel = true; // write studio:* properties (project files)
    int imageLength = 150;
    // Preview: render at this fraction of the project size (MLT profile and
    // pixel-valued effect parameters are scaled together)
    double scale = 1.0;
    // Where generated caption (.ass) files go; empty = cache/captions
    QString captionDir;
};

class Planner
{
public:
    Planner(const ProjectSettings &settings, const QList<MediaItem> &media, const QList<Sequence> &sequences, PlanOptions options = {});

    RenderPlan plan(const Timeline &timeline, const QList<Marker> &markers = {}) const;
    // Source length of a clip in its own (speed-adjusted) frames; -1 = unlimited
    int sourceLength(const Clip &clip) const;

    // Luma resource for a transition type ("" = dissolve)
    static QString lumaFor(const QString &type, bool *invert = nullptr);
    static QStringList transitionTypes();

private:
    int producerFor(RenderPlan &plan, const Clip &clip, int depth) const;
    QList<FilterSpec> clipFilters(const Clip &clip, const Track &track, int depth) const;
    void planTrack(RenderPlan &plan, const Track &track, bool soloActive, int depth) const;
    RenderPlan planInternal(const Timeline &timeline, int depth) const;
    void captionFilter(RenderPlan &plan, const Track &track) const;
    QString captionFile(const QString &ass) const;

    ProjectSettings m_settings;
    int m_fullWidth = 1920; // project size before the preview scale
    int m_fullHeight = 1080;
    QHash<QString, MediaItem> m_media;
    QHash<QString, Sequence> m_sequences;
    PlanOptions m_options;
};

QString mltColor(const QString &qmlColor); // "#rrggbb" / "#aarrggbb" -> "#aarrggbb"
QString formatNumber(double v);

} // namespace Studio
