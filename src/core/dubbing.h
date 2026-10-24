/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Dubbing a single-speaker video into another language, in the speaker's own
// cloned voice. The pure parts live here; the engine (Chatterbox through
// data/dub.py) and the editor are driven from src/app/dubber.cpp.
//
//  1. sentences from the word-timed transcript (timeline time);
//  2. a translation written for the ear (Ollama), sized to the time each
//     sentence takes, so the dub does not need squeezing;
//  3. chunks: neighbouring sentences spoken in one go, because longer
//     passages carry the intonation better than separate short sentences;
//  4. a voice reference: the cleanest, most continuous 10-15 seconds;
//  5. a fit: each spoken chunk starts where the original did and is sped up a
//     little when it runs long, never beyond what still sounds natural.

#pragma once

#include "captions.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

namespace Studio::Dubbing {

struct Language {
    QString code; // Chatterbox language_id
    QString name;
    QString native;
    double charsPerSecond = 14; // typical natural speech rate, for sizing the translation
};

// The 23 languages Chatterbox Multilingual speaks
const QList<Language> &languages();
const Language *find(const QString &code);
// "pt-BR", "PT", "iw" → "pt", "pt", "he"; empty when it is not a language code
QString normalizeCode(const QString &code);

struct Sentence {
    int id = 0;
    qint64 startMs = 0;
    qint64 endMs = 0;
    int words = 0;
    QString text;
    QString translation;
    double seconds() const { return (endMs - startMs) / 1000.0; }
};

// Sentences of the transcript; long ones are split at a pause or comma so
// that none runs past ~30 words or ~14 s.
QList<Sentence> sentences(const QList<Captions::Word> &words);

// Neighbouring sentences spoken together
struct Chunk {
    int firstSentence = 0;
    int lastSentence = 0; // inclusive
    qint64 startMs = 0;
    qint64 endMs = 0;
    qint64 slotEndMs = 0; // where the next chunk starts (or the end of the video)
    QString id() const { return QStringLiteral("c%1").arg(firstSentence); }
};
QList<Chunk> chunks(const QList<Sentence> &sentences, qint64 totalMs, qint64 maxSpanMs = 10000, qint64 maxGapMs = 700, int maxWords = 36);
// The translations of a chunk, joined
QString chunkText(const QList<Sentence> &sentences, const Chunk &chunk);

// ---- translation (Ollama /api/chat)

// How many characters a translation should have to be spoken in `seconds`
int targetChars(double seconds, const Language &target);
// Body of the request for `batch` (ids refer to Sentence::id). `context` holds the
// sentences just before, already translated, so tone and terms stay consistent.
QByteArray translateRequest(const QString &model, const QList<Sentence> &batch, const QList<Sentence> &context, const Language &target,
                            const QString &sourceName);
// id → translation from the reply; tolerates <think> blocks, code fences and an
// array at the top level. Only ids in `expected` are kept; empty texts are dropped.
QMap<int, QString> parseTranslation(const QByteArray &reply, const QList<int> &expected);

// ---- the voice

struct Window {
    qint64 startMs = 0;
    qint64 endMs = 0;
    double score = 0;
    bool valid() const { return endMs > startMs; }
    qint64 length() const { return endMs - startMs; }
};
// The passage of (file-time) words that makes the best voice reference: 9-15 s of
// continuous speech, ending on a sentence end. Not valid with under ~4 s of speech.
Window referenceWindow(const QList<Captions::Word> &words, qint64 minMs = 9000, qint64 maxMs = 15000);

// ---- fitting the spoken chunks onto the timeline

struct FitInput {
    QString id;
    double startSec = 0;   // where the original sentence began
    double slotEndSec = 0; // where the next one begins
    double naturalSec = 0; // how long the synthesised speech is
};
struct Placement {
    QString id;
    double startSec = 0;
    double ratio = 1.0;   // duration multiplier (below 1 = faster)
    double seconds = 0;   // duration after the stretch
    double driftSec = 0;  // how late it starts compared to the original
};
struct FitResult {
    QList<Placement> items;
    int sped = 0;               // chunks that were sped up
    double maxSpeedup = 1.0;    // the largest speed-up used (1.0 = none)
    double maxDriftSec = 0;
    double overflowEndSec = 0;  // speech that would run past the end of the video
};
// Every chunk starts at its original time (or right after the previous one),
// keeps its natural speed when it fits its slot, and is sped up by at most
// `maxSpeedup` when it does not. Later chunks absorb the rest as drift, which
// the pauses of the original win back.
FitResult fit(const QList<FitInput> &inputs, double totalSec, double maxSpeedup = 1.3, double gapSec = 0.06);

} // namespace Studio::Dubbing
