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
// The model is checked against this computer (whisperhw.h): which one suits it,
// whether it runs on the GPU, how fast, and how to get GPU support.

#pragma once

#include "captions.h"
#include "whisperhw.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QVariantList>
#include <memory>

class QCryptographicHash;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

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
    // "37 % · 2:10 elapsed · ~3:40 left · 18:29 of audio" while running
    Q_PROPERTY(QString timing READ timing NOTIFY timingChanged)
    Q_PROPERTY(double audioSeconds READ audioSeconds NOTIFY timingChanged)
    Q_PROPERTY(double elapsedSeconds READ elapsedSeconds NOTIFY timingChanged)
    Q_PROPERTY(double remainingSeconds READ remainingSeconds NOTIFY timingChanged) // -1 = unknown

    // This computer (whisperhw.h), probed again on refresh()
    Q_PROPERTY(bool probing READ probing NOTIFY hardwareChanged)
    Q_PROPERTY(QString device READ device NOTIFY hardwareChanged)            // "NVIDIA GeForce RTX 2060" or "CPU"
    Q_PROPERTY(bool gpu READ gpu NOTIFY hardwareChanged)                     // whisper runs on the GPU
    Q_PROPERTY(QString runModel READ runModel NOTIFY hardwareChanged)        // the model the next run uses
    Q_PROPERTY(QString summary READ summary NOTIFY hardwareChanged)          // "small-q5_1 · CPU · ~6× real time"
    Q_PROPERTY(QString speedText READ speedText NOTIFY hardwareChanged)
    Q_PROPERTY(bool unsuitable READ unsuitable NOTIFY hardwareChanged)       // runModel is too heavy here
    Q_PROPERTY(QString recommendedModel READ recommendedModel NOTIFY hardwareChanged)
    Q_PROPERTY(QString recommendedSummary READ recommendedSummary NOTIFY hardwareChanged)
    Q_PROPERTY(QString recommendedReason READ recommendedReason NOTIFY hardwareChanged)
    Q_PROPERTY(bool recommendedInstalled READ recommendedInstalled NOTIFY hardwareChanged)
    Q_PROPERTY(QString gpuPackage READ gpuPackage NOTIFY hardwareChanged)    // "ggml-cuda" when the GPU lacks a backend
    Q_PROPERTY(QString gpuHint READ gpuHint NOTIFY hardwareChanged)
    // Per-run choice in the dialogs; "" = the model from Settings
    Q_PROPERTY(QString modelOverride READ modelOverride WRITE setModelOverride NOTIFY engineChanged)
    Q_PROPERTY(QVariantList modelChoices READ modelChoices NOTIFY hardwareChanged)

    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged)
    Q_PROPERTY(QString downloadName READ downloadName NOTIFY downloadChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged) // -1 = unknown
    Q_PROPERTY(QString downloadDetail READ downloadDetail NOTIFY downloadChanged)
    Q_PROPERTY(QString downloadError READ downloadError NOTIFY downloadChanged)

    Q_PROPERTY(bool installing READ installing NOTIFY installChanged)
    Q_PROPERTY(QString installMessage READ installMessage NOTIFY installChanged)
    Q_PROPERTY(bool installFailed READ installFailed NOTIFY installChanged)

public:
    struct Engine {
        QString kind;     // whisper.cpp | faster-whisper
        QString program;
        QString model;    // name
        QString modelFile; // whisper.cpp ggml file
        QString language; // whisper language code or "auto"
        QString configuredModel; // subtitle_model in ai.conf
        bool available() const { return !kind.isEmpty() && (kind != QLatin1String("whisper.cpp") || !modelFile.isEmpty()); }
    };

    explicit Transcriber(QObject *parent = nullptr);

    // Reads ai.conf and looks for the programs and models
    // modelOverride: a downloaded model to use instead of subtitle_model; hw picks
    // a fallback that suits the computer when the configured model is missing
    static Engine detect(QString *problem = nullptr, bool *programFound = nullptr, const QString &modelOverride = QString(),
                         const WhisperHw::Hardware *hw = nullptr);
    static QString modelsDir(); // ~/.local/share/lingmo/whisper-models
    // whisper.cpp: model, file, language, JSON output, progress, plus the options
    // for this computer (WhisperHw::runArguments); faster-whisper: its own
    static QStringList arguments(const Engine &e, const QString &wav, const QString &outBase, const WhisperHw::Hardware &hw);
    // Remaining seconds from the elapsed time and progress; before the first
    // progress, from the audio length and the estimated speed. -1 = unknown
    static double remaining(double elapsedSec, double progress, double audioSec, double estimatedSpeed);
    // "18:29", "1:02:03"
    static QString clockText(double seconds);
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
    QString timing() const;
    double audioSeconds() const { return m_mediaSeconds; }
    double elapsedSeconds() const;
    double remainingSeconds() const;

    bool probing() const { return m_probe != nullptr; }
    const WhisperHw::Hardware &hardware() const { return m_hw; }
    QString device() const;
    bool gpu() const { return m_hw.gpuUsable(); }
    QString runModel() const { return m_engine.model; }
    QString summary() const;
    QString speedText() const;
    bool unsuitable() const;
    QString recommendedModel() const { return m_recommendation.model; }
    QString recommendedSummary() const;
    QString recommendedReason() const { return m_recommendation.reason; }
    bool recommendedInstalled() const;
    QString gpuPackage() const { return m_recommendation.upgradePackage; }
    QString gpuHint() const { return m_recommendation.upgradeHint; }
    QString modelOverride() const { return m_override; }
    void setModelOverride(const QString &name);
    QVariantList modelChoices() const;
    // Tests and screenshots: use this hardware instead of probing
    void setHardware(const WhisperHw::Hardware &hw);

    bool downloading() const { return m_reply != nullptr; }
    QString downloadName() const { return m_downloadName; }
    double downloadProgress() const { return m_downloadProgress; }
    QString downloadDetail() const { return m_downloadDetail; }
    QString downloadError() const { return m_downloadError; }

    bool installing() const { return m_install != nullptr; }
    QString installMessage() const { return m_installMessage; }
    bool installFailed() const { return m_installFailed; }
    const Studio::Captions::Transcript &result() const { return m_result; }

    Q_INVOKABLE void refresh();
    // Transcribes [startMs, startMs + durationMs) of a file (whole file when durationMs <= 0).
    // Word times are in the file's time. Emits finished() or failed().
    void transcribe(const QString &path, qint64 startMs = 0, qint64 durationMs = -1, const QString &task = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openSettings() const; // Lingmo Settings › AI › Transcrição e legendas
    // Writes subtitle_model (as Settings does), downloading the model first when needed
    Q_INVOKABLE void useModel(const QString &name);
    Q_INVOKABLE void useRecommended() { useModel(m_recommendation.model); }
    Q_INVOKABLE void downloadModel(const QString &name);
    Q_INVOKABLE void cancelDownload();
    // Installs the ggml backend for the GPU (pkexec), then probes again
    Q_INVOKABLE void installGpuSupport();

Q_SIGNALS:
    void engineChanged();
    void runningChanged();
    void progressChanged();
    void finished(const QString &task);
    void failed(const QString &task, const QString &message);
    void timingChanged();
    void hardwareChanged();
    void downloadChanged();
    void installChanged();

private:
    void setProgress(double p, const QString &stage);
    void finish(const QString &error);
    void runWhisper();
    QString cacheFile() const;
    void applyHardware(const WhisperHw::Hardware &hw);
    void probeAsync();
    void finishDownload();
    void writeModelSetting(const QString &name);

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
    QElapsedTimer m_clock;
    double m_firstProgressAt = -1; // elapsed seconds when whisper started (-1: not yet)
    QTimer *m_tick = nullptr;

    WhisperHw::Hardware m_hw;
    WhisperHw::Recommendation m_recommendation;
    bool m_fixedHardware = false;
    QPointer<QFutureWatcher<WhisperHw::Hardware>> m_probe;
    QString m_override;

    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile *m_downloadFile = nullptr;
    QCryptographicHash *m_downloadHash = nullptr;
    QString m_downloadName;
    double m_downloadProgress = -1;
    QString m_downloadDetail;
    QString m_downloadError;
    bool m_useAfterDownload = false;
    bool m_downloadCancelled = false;

    QPointer<QProcess> m_install;
    QString m_installMessage;
    bool m_installFailed = false;
};
