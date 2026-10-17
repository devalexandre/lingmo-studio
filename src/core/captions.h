/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Captions synced to speech, made to be read.
//
//  - Transcripts with word timestamps from whisper.cpp (whisper-cli -ojf) or
//    faster-whisper (whisper-ctranslate2 --word_timestamps True);
//  - cue building with readability rules (characters per line, lines, CPS,
//    duration limits, line breaks at phrase boundaries, no orphan words);
//  - caption styles (Clássica, Caixa, Karaokê, Destaque, Palavra a palavra,
//    Podcast, Neon, Alto contraste, Duas cores);
//  - ASS generation: one renderer (libass, through MLT's avfilter.ass) for
//    preview and export; SRT/VTT/ASS sidecars.
//
// In the project, a caption is a clip of kind "caption" on a subtitle track.
// Its words live in genParams["words"] with times in ms relative to the clip's
// source origin (timeline frame position - in), so moving the clip moves the
// words and trimming an edge hides/reveals words without losing them.
// The track's look is Track::captionStyle (a Style map).

#pragma once

#include "model.h"
#include "platforms.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace Studio::Captions {

struct Word {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    bool emphasis = false; // keyword (Podcast style colour)
    bool operator==(const Word &o) const { return startMs == o.startMs && endMs == o.endMs && text == o.text && emphasis == o.emphasis; }
};

struct Transcript {
    QString language;
    QList<Word> words;
    bool isEmpty() const { return words.isEmpty(); }
};

// ---------------------------------------------------------------- transcripts

// whisper.cpp `whisper-cli -ojf` (full JSON with token offsets). Tokens are
// merged into words (a token starting with a space starts a word); the
// segment text is used for the spelling when it splits into the same words.
// Also reads `-ml 1` output (one word per segment) when tokens are missing.
Transcript parseWhisperCppJson(const QByteArray &json);
// faster-whisper / whisper-ctranslate2 `--word_timestamps True --output_format json`
Transcript parseFasterWhisperJson(const QByteArray &json);
// Either of the two (by shape)
Transcript parseTranscript(const QByteArray &json);
// Our own cache format
QByteArray transcriptToJson(const Transcript &t);
Transcript transcriptFromJson(const QByteArray &json);
// SRT cues → words spread over each cue by length (imports without word times)
Transcript fromSubtitleCues(const QList<QPair<QPair<qint64, qint64>, QString>> &cues);
// Splits text into words and spreads them over [startMs, endMs] by length
QList<Word> spreadWords(const QString &text, qint64 startMs, qint64 endMs);
QString joinWords(const QList<Word> &words);

// ---------------------------------------------------------------- cues

struct Rules {
    int maxCharsPerLine = 0;     // 0 = from the frame width and font size
    int maxLines = 2;            // 1 or 2
    double maxCps = 17;          // characters per second
    int minDurationMs = 833;     // 5/6 s
    int maxDurationMs = 6000;
    int maxGapMs = 1200;         // a pause this long ends a cue
    int minGapMs = 80;           // between consecutive cues
    int maxWords = 0;            // 0 = no limit
    QVariantMap toMap() const;
    static Rules fromMap(const QVariantMap &m, const Rules &defaults);
    static Rules fromMap(const QVariantMap &m);
    bool operator==(const Rules &o) const { return toMap() == o.toMap(); }
};

struct Cue {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QList<Word> words;
    QStringList lines; // the text as displayed, broken into lines
    int speaker = 1;
    QString text() const { return lines.join(QLatin1Char('\n')); }
    int characters() const; // displayed characters (spaces included, line breaks not)
};

// Groups words into cues following the rules. Words must be sorted.
QList<Cue> buildCues(const QList<Word> &words, const Rules &rules);
// Breaks words into at most maxLines lines of maxChars, at the best phrase
// boundary and never leaving a lone word on a line when another split fits.
// Returns an empty list when the words do not fit.
QStringList breakLines(const QList<Word> &words, int maxChars, int maxLines);
// Greedy wrap (as many lines as needed): for text longer than the rules allow
QStringList wrapLines(const QList<Word> &words, int maxChars);
// Fixes the display times: minimum duration and CPS by extending into the
// following pause, never overlapping the next cue.
void applyTiming(QList<Cue> &cues, const Rules &rules);

// ---------------------------------------------------------------- styles

struct Style {
    QString id = QStringLiteral("classic"); // preset it started from
    QString effect = QStringLiteral("none"); // none | box | karaoke | highlight | word | podcast | neon | twocolor
    QString font = QStringLiteral("Inter");
    double size = 6.5;          // % of the frame's short side
    int weight = 700;
    bool uppercase = false;
    QString textColor = QStringLiteral("#ffffff");
    QString activeColor = QStringLiteral("#ffd60a");  // karaoke sung / current word / keywords
    QString outlineColor = QStringLiteral("#000000");
    QString boxColor = QStringLiteral("#99000000");   // #aarrggbb
    QString speaker2Color = QStringLiteral("#5ac8fa");
    double outline = 4;         // px for a 1080 short side
    double shadow = 0;
    QString position = QStringLiteral("bottom"); // bottom | middle | top
    double offset = 0;          // extra % of the frame height away from the edge
    int wordsPerGroup = 1;      // "word" effect
    SafeArea safeArea;          // platform interface to avoid
    Rules rules;

    QVariantMap toMap() const;
    static Style fromMap(const QVariantMap &m);
    static QList<Style> presets();
    static Style preset(const QString &id);
    static QStringList presetIds();
    static QString presetName(const QString &id); // translated
    // Characters per line when rules.maxCharsPerLine is 0
    int charsPerLine(int frameWidth, int frameHeight) const;
    Rules effectiveRules(int frameWidth, int frameHeight) const;
};

// ---------------------------------------------------------------- ASS / sidecars

// Times are snapped to frames (and floored to centiseconds) so the first and
// last frame of every event match the timeline exactly.
QString toAss(const QList<Cue> &cues, const Style &style, int width, int height, double fps);
QString toSrt(const QList<Cue> &cues);
QString toVtt(const QList<Cue> &cues);
QString assTime(qint64 ms);
// ASS colour &HAABBGGRR from "#rrggbb" / "#aarrggbb" (alpha inverted)
QString assColor(const QString &color);

// ---------------------------------------------------------------- caption clips

inline const QString ClipKind = QStringLiteral("caption");
bool isCaption(const Clip &c);
QList<Word> clipWords(const Clip &c);              // relative to the source origin
void setClipWords(Clip &c, const QList<Word> &w);  // also refreshes the clip name
int clipSpeaker(const Clip &c);
// Frame rate the clip was made with (genParams "fps"; 30 when unknown)
double clipFps(const Clip &c);
// A caption clip for a cue (timeline ms): position = in = cue start frame
Clip makeClip(const Cue &cue, double fps);
// Ms of the clip's source origin on the timeline
double originMs(const Clip &c, double fps);
// Cues of a subtitle track in timeline ms, clipped to each clip's range;
// lines are rebuilt with the style's rules for that frame size.
QList<Cue> trackCues(const Track &t, double fps, const Style &style, int width, int height);
bool trackHasCaptions(const Track &t);

} // namespace Studio::Captions
