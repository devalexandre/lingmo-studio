/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Publishing metadata of a video: title, description, tags and the short
// headline for the thumbnail. The text comes from the local LLM (Ollama, the
// model chosen in Settings › AI) reading the transcript; without it a
// heuristic builds a plain version from the transcript. Pure functions: the
// network and the editor live in src/app/metadata.cpp.

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace Studio::Metadata {

struct Chapter {
    int seconds = 0;
    QString title;
};

struct Input {
    QString transcript;       // what is said (may be empty)
    QString projectName;
    QStringList onScreenText; // titles and lower thirds on the timeline
    QList<Chapter> chapters;  // timeline markers
    QString language;         // whisper code ("pt", "en"…) or empty
    double durationSec = 0;
};

struct Result {
    bool ok = false;
    QString title;
    QString description;
    QStringList tags;
    QString thumbnailText; // 2-4 words
};

// The transcript cut down to `maxChars`: the start, the middle and the end,
// so a long video still shows its whole arc to the model.
QString condense(const QString &text, int maxChars);

// Body for POST /api/chat (stream off, think off, JSON answer)
QByteArray llmRequest(const QString &model, const Input &in);
// Reads the /api/chat reply (or a bare content string); tolerates <think>
// blocks and code fences. ok is false without a title.
Result parseLlmReply(const QByteArray &reply);

// What can be said without a model: the title from the project name or the
// first sentence, a summary from the opening sentences, tags from the most
// frequent words.
Result heuristic(const Input &in);

// "0:00", "12:05", "1:02:03"
QString timestamp(int seconds);
// YouTube chapters ("0:00 Intro" first line); empty when there are fewer than
// three or they are too close together for YouTube to accept them.
QString chaptersBlock(const QList<Chapter> &chapters, const QString &firstTitle);
// description + chapters + the first hashtags: ready to paste into the upload form
QString composeDescription(const Result &r, const QList<Chapter> &chapters, const QString &firstChapterTitle);

// Normalises what the model wrote: one line, no quotes, bounded length
QString cleanTitle(const QString &s, int maxChars = 100);
QStringList cleanTags(const QStringList &tags, int maxCount = 12);

} // namespace Studio::Metadata
