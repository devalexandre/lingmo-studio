/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Pure editing operations on Studio::Timeline. No MLT, no Qt objects: they
// are unit tested directly and wrapped in undo commands by the app.
//
// Rules shared by every operation:
//  - clips on a track never overlap, except a clip with transitionIn = L that
//    overlaps the previous clip by exactly L frames (a transition);
//  - "overwrite" placement clears the destination range (cutting, trimming or
//    removing what was there), "ripple" placement pushes later clips right;
//  - move, split and delete follow linked clips (detached audio); trims do not,
//    which is how J and L cuts are made.

#pragma once

#include "model.h"

#include <functional>

namespace Studio::Ops {

// Length in source frames of what a clip plays (media length at the clip's
// speed). Negative = unlimited (titles, colors, images).
using SourceLength = std::function<int(const Clip &)>;

inline constexpr int Unlimited = -1;

void sortTrack(Track &t);
// Re-establishes the invariants after an edit (transition lengths, overlaps).
void normalizeTrack(Track &t);

// Overwrites [start, end) on a track. Returns ids of clips created by cuts.
QStringList clearRange(Track &t, int start, int end);
// Pushes every clip starting at/after `at` by `delta` frames (a clip spanning `at` is split first when delta > 0).
void rippleShift(Track &t, int at, int delta);

bool insertClip(Timeline &tl, int track, Clip clip, bool ripple, QString *error = nullptr);
bool moveClip(Timeline &tl, const QString &clipId, int toTrack, int newPosition, bool ripple = false, QString *error = nullptr);
bool trimIn(Timeline &tl, const QString &clipId, int newPosition, const SourceLength &len, bool ripple = false, QString *error = nullptr);
bool trimOut(Timeline &tl, const QString &clipId, int newEnd, const SourceLength &len, bool ripple = false, QString *error = nullptr);
// Roll: moves the edit point between clipId and the clip right after it.
bool roll(Timeline &tl, const QString &clipId, int newBoundary, const SourceLength &len, QString *error = nullptr);
// Slip: changes what part of the source plays, keeping position and length.
bool slip(Timeline &tl, const QString &clipId, int delta, const SourceLength &len, QString *error = nullptr);
// Slide: moves the clip between its neighbours, trimming them to keep the gap closed.
bool slide(Timeline &tl, const QString &clipId, int delta, const SourceLength &len, QString *error = nullptr);
// Razor: splits at a timeline frame strictly inside the clip. Returns the new right clip id.
QString split(Timeline &tl, const QString &clipId, int frame, bool followLinks = true);
// Splits every unlocked track at frame (or only clipIds when not empty). Returns count.
int splitAt(Timeline &tl, int frame, const QStringList &clipIds = {});
bool removeClip(Timeline &tl, const QString &clipId, bool ripple, bool followLinks = true);
// Deletes the empty gap containing `frame` on a track.
bool removeGap(Timeline &tl, int track, int frame);
// Overlaps clipId with the previous clip by `length` frames (0 removes); later clips ripple.
bool setTransition(Timeline &tl, const QString &clipId, int length, const QString &type, QString *error = nullptr);
// Holds the frame at timeline `frame` for `length` frames, rippling the rest of the track.
QString freezeFrame(Timeline &tl, const QString &clipId, int frame, int length);
// Moves the audio of a video clip to an audio track (linked), muting it on the video clip.
QString detachAudio(Timeline &tl, const QString &clipId, int audioTrack, QString *error = nullptr);

// Captions (clips of kind "caption", see captions.h). Times keep in sync:
// words stay where they were spoken; fps converts frames and ms.
// New text with the same number of words keeps every timing; otherwise the
// words are spread over the spoken span.
bool setCaptionText(Timeline &tl, const QString &clipId, const QString &text, double fps, QString *error = nullptr);
bool setCaptionWord(Timeline &tl, const QString &clipId, int index, const QString &text, QString *error = nullptr);
bool toggleCaptionEmphasis(Timeline &tl, const QString &clipId, int index, QString *error = nullptr);
// Splits before word `wordIndex` (at its start frame). Returns the new clip id.
QString splitCaption(Timeline &tl, const QString &clipId, int wordIndex, double fps, QString *error = nullptr);
// Joins the caption with the next one on the track (gap included)
bool mergeCaptionWithNext(Timeline &tl, const QString &clipId, double fps, QString *error = nullptr);
// Moves one word's timing (between its neighbours)
bool nudgeCaptionWord(Timeline &tl, const QString &clipId, int index, qint64 deltaMs, QString *error = nullptr);
bool setCaptionSpeaker(Timeline &tl, const QString &clipId, int speaker, QString *error = nullptr);

// Keyframe helpers (linear interpolation of numeric values)
QVariantMap valuesAt(const QList<Keyframe> &kfs, const QVariantMap &base, int frame);
void setKeyframe(QList<Keyframe> &kfs, int frame, const QVariantMap &values);
void shiftKeyframes(QList<Keyframe> &kfs, int delta, int duration, const QVariantMap &base);

} // namespace Studio::Ops
