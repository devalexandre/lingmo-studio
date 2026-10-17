/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Local speech to text with word timestamps, in the background with progress
// and cancel. Engine, model and language come from Lingmo Settings › AI ›
// "Transcrição e legendas" (ai.conf: subtitle_backend, subtitle_model,
// subtitle_language); models live in ~/.local/share/lingmo/whisper-models.
//  - whisper.cpp:   whisper-cli -m ggml-<model>.bin -ojf -pp (tokens → words)
//  - faster-whisper: whisper-ctranslate2 --word_timestamps True --output_format json
// Results are cached per media file, model and language.

#pragma once

#include "captions.h"

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>

class Transcriber : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY engineChanged)
    Q_PROPERTY(QString engine READ engine NOTIFY engineChanged)        // "whisper.cpp" | "faster-whisper" | ""
    Q_PROPERTY(QString model READ model NOTIFY engineChanged)          // "base", "large-v3-turbo"...
    Q_PROPERTY(QString language READ language NOTIFY engineChanged)    // "pt", "auto"...
    Q_PROPERTY(bool engineInstalled READ engineInstalled NOTIFY engineChanged) // a program, maybe without a model
    Q_PROPERTY(QString problem READ problem NOTIFY engineChanged)      // why it isn't available (translated)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)  // 0..1, -1 = unknown
    Q_PROPERTY(QString stage READ stage NOTIFY progressChanged)

public:
    struct Engine {
        QString kind;     // whisper.cpp | faster-whisper
        QString program;
        QString model;    // name
        QString modelFile; // whisper.cpp ggml file
        QString language; // whisper language code or "auto"
        bool available() const { return !kind.isEmpty() && (kind != QLatin1String("whisper.cpp") || !modelFile.isEmpty()); }
    };

    explicit Transcriber(QObject *parent = nullptr);

    // Reads ai.conf and looks for the programs and models
    static Engine detect(QString *problem = nullptr, bool *programFound = nullptr);
    static QString modelsDir(); // ~/.local/share/lingmo/whisper-models
    static QStringList arguments(const Engine &e, const QString &wav, const QString &outBase, int threads);
    // "progress = 37%" (whisper.cpp -pp) or a segment end time (faster-whisper)
    static double parseProgress(const QString &line, double durationSec);

    bool available() const { return m_engine.available(); }
    QString engine() const { return m_engine.kind; }
    QString model() const { return m_engine.model; }
    QString language() const { return m_engine.language; }
    bool engineInstalled() const { return m_programFound; }
    QString problem() const { return m_problem; }
    bool running() const { return !m_task.isEmpty(); }
    double progress() const { return m_progress; }
    QString stage() const { return m_stage; }
    const Studio::Captions::Transcript &result() const { return m_result; }

    Q_INVOKABLE void refresh();
    // Transcribes [startMs, startMs + durationMs) of a file (whole file when durationMs <= 0).
    // Word times are in the file's time. Emits finished() or failed().
    void transcribe(const QString &path, qint64 startMs = 0, qint64 durationMs = -1, const QString &task = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openSettings() const; // Lingmo Settings › AI › Transcrição e legendas

Q_SIGNALS:
    void engineChanged();
    void runningChanged();
    void progressChanged();
    void finished(const QString &task);
    void failed(const QString &task, const QString &message);

private:
    void setProgress(double p, const QString &stage);
    void finish(const QString &error);
    void runWhisper();
    QString cacheFile() const;

    Engine m_engine;
    QString m_problem;
    bool m_programFound = false;
    QString m_task;
    QString m_path;
    qint64 m_start = 0;
    qint64 m_duration = -1;
    double m_mediaSeconds = 0;
    double m_progress = -1;
    QString m_stage;
    QPointer<QProcess> m_process;
    std::unique_ptr<QTemporaryDir> m_dir;
    Studio::Captions::Transcript m_result;
    QString m_log;
};
