/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Local analysis tools. Everything runs on this machine with tools the user
// already installed; nothing is downloaded and nothing leaves the computer.
//  - speech to text: whisper.cpp (whisper-cli) or faster-whisper
//    (whisper-ctranslate2), only when installed (and a model is present);
//  - silence / scene-cut detection and loudness: ffmpeg filters;
//  - subtitle files: SRT read/write.

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Studio::AiTools {

struct SubtitleCue {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
    bool operator==(const SubtitleCue &o) const { return startMs == o.startMs && endMs == o.endMs && text == o.text; }
};

struct Interval {
    double start = 0; // seconds
    double end = 0;
};

struct WhisperEngine {
    QString kind;    // "whisper.cpp" | "faster-whisper" | ""
    QString program;
    QString model;   // whisper.cpp model file
    bool available() const { return !kind.isEmpty(); }
};

WhisperEngine findWhisper();
bool ollamaAvailable();

// Arguments to transcribe `wav` (16 kHz mono) into `outBase`.srt
QStringList whisperArguments(const WhisperEngine &engine, const QString &wav, const QString &outBase, const QString &language);
QStringList extractSpeechAudioArguments(const QString &input, const QString &wav);

QList<SubtitleCue> parseSrt(const QString &text);
QString writeSrt(const QList<SubtitleCue> &cues);

// ffmpeg command lines and parsers (stderr output)
QStringList silenceArguments(const QString &input, double noiseDb = -35, double minSeconds = 0.6);
QList<Interval> parseSilence(const QString &ffmpegOutput);
QStringList sceneArguments(const QString &input, double threshold = 0.35);
QList<double> parseScenes(const QString &ffmpegOutput);
QStringList loudnessArguments(const QString &input);
// Integrated loudness (LUFS) from the ebur128 summary; NaN if missing
double parseIntegratedLoudness(const QString &ffmpegOutput);

// Finds cues containing all words of `query` (case/accent-insensitive)
QList<SubtitleCue> search(const QList<SubtitleCue> &cues, const QString &query);

} // namespace Studio::AiTools
