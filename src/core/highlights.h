/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Finding highlights in a long video for Shorts/Reels:
//  1. sentences from a word-timed transcript;
//  2. candidate segments made of whole sentences, sized for the platform;
//  3. a score: the local LLM (Ollama, when present) or a heuristic from
//     audio energy, speech density, scene changes and pauses;
//  4. the best non-overlapping candidates.

#pragma once

#include "aitools.h"
#include "captions.h"

#include <QList>
#include <QString>
#include <QVariantMap>

namespace Studio::Highlights {

struct Sentence {
    qint64 startMs = 0;
    qint64 endMs = 0;
    int firstWord = 0;
    int lastWord = 0; // inclusive
    QString text;
};

// Sentence ends: . ? ! … or a pause longer than maxGapMs
QList<Sentence> sentences(const QList<Captions::Word> &words, int maxGapMs = 1500);

struct LoudnessPoint {
    double t = 0;    // seconds
    double lufs = 0; // momentary loudness
};

struct Features {
    QList<AiTools::Interval> silences;
    QList<double> scenes;
    QList<LoudnessPoint> loudness;
};

struct Candidate {
    QString id;
    qint64 startMs = 0;
    qint64 endMs = 0;
    int firstSentence = 0;
    int lastSentence = 0;
    QString text;
    double heuristic = 0; // 0..100
    double llmScore = -1; // 0..100, -1 = not scored
    double score = 0;     // what is shown and sorted by
    QString title;
    QString reason;
    QStringList platforms;

    double seconds() const { return (endMs - startMs) / 1000.0; }
    QVariantMap toMap() const;
};

// Every run of consecutive sentences lasting between minSec and maxSec
// (a little padding is added around the speech). Sentences longer than
// maxSec on their own are split at the word nearest the limit.
QList<Candidate> candidates(const QList<Sentence> &sentences, const QList<Captions::Word> &words, int minSec, int maxSec,
                            qint64 mediaMs = -1);

struct ScoreParts {
    double density = 0;  // words per second, normalised
    double energy = 0;   // loudness above the media's average
    double scenes = 0;   // visual changes
    double pauses = 0;   // penalty for long silences inside
    double hook = 0;     // opening line is a question / hook phrase
    double complete = 0; // ends on a sentence end
    double fit = 0;      // close to the ideal length
    double repetition = 0; // the same phrase over and over (a transcription loop)
};
ScoreParts scoreParts(const Candidate &c, const QList<Captions::Word> &words, const Features &f, double idealSec);
double heuristicScore(const Candidate &c, const QList<Captions::Word> &words, const Features &f, double idealSec);

// Highest score first, no two overlapping by more than `maxOverlap` (0..1 of the shorter)
QList<Candidate> pickBest(QList<Candidate> list, int count, double maxOverlap = 0.25);

// Combined score when the LLM answered
double combine(double heuristic, double llm);
// A score scaled down for a repetitive text (Captions::repetition): normal
// speech repeats a few word trigrams, a whisper loop almost all of them
double penaliseRepetition(double score, const QString &text);

// ---- Ollama
// Body for POST /api/chat (stream off, think off, JSON answer)
QByteArray llmRequest(const QString &model, const Candidate &c, const QString &platformName, const QString &language);
struct LlmScore {
    bool ok = false;
    double score = 0; // 0..100
    QString title;
    QString reason;
};
// Reads the /api/chat reply (or a bare content string); tolerates <think>
// blocks, code fences and scores on a 0..10 scale.
LlmScore parseLlmReply(const QByteArray &reply);

// ---- audio energy
// ffmpeg -af ebur128=framelog=verbose: momentary loudness every 100 ms
QStringList loudnessCurveArguments(const QString &input);
QList<LoudnessPoint> parseLoudnessCurve(const QString &ffmpegOutput);

} // namespace Studio::Highlights
