/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// "Gerar cortes": a long video in, ready-to-edit vertical clips out.
//  1. transcribe with word timestamps (Transcriber);
//  2. silence, scene cuts and loudness with ffmpeg;
//  3. candidates on sentence boundaries sized for the platforms, scored by a
//     heuristic and — when Ollama runs with a model chosen in Settings › AI —
//     by the local LLM (score, hook title, reason);
//  4. the user picks and trims; each pick becomes a sequence (compound clip)
//     with the video reframed to the platform, optionally with captions;
//  5. subject tracking (OpenCV, optional) bakes Position X keyframes;
//  6. batch export, one file per cut and platform.

#pragma once

#include "highlights.h"

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QVariant>
#include <functional>

class Editor;
class QNetworkAccessManager;
class QNetworkReply;

class ShortsGenerator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList platforms READ platforms CONSTANT)
    Q_PROPERTY(QStringList selectedPlatforms READ selectedPlatforms WRITE setSelectedPlatforms NOTIFY optionsChanged)
    Q_PROPERTY(QString mediaId READ mediaId WRITE setMediaId NOTIFY optionsChanged)
    Q_PROPERTY(int count READ count WRITE setCount NOTIFY optionsChanged)
    Q_PROPERTY(bool captions READ captions WRITE setCaptions NOTIFY optionsChanged)
    Q_PROPERTY(QString captionStyle READ captionStyle WRITE setCaptionStyle NOTIFY optionsChanged)
    Q_PROPERTY(bool reframe READ reframe WRITE setReframe NOTIFY optionsChanged)
    Q_PROPERTY(int minSeconds READ minSeconds NOTIFY optionsChanged)
    Q_PROPERTY(int maxSeconds READ maxSeconds NOTIFY optionsChanged)
    Q_PROPERTY(bool mixedAspects READ mixedAspects NOTIFY optionsChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY candidatesChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY candidatesChanged)
    Q_PROPERTY(bool usedLlm READ usedLlm NOTIFY candidatesChanged)
    Q_PROPERTY(QString llmModel READ llmModel NOTIFY stateChanged)
    Q_PROPERTY(bool opencvAvailable READ opencvAvailable NOTIFY toolsChanged)
    Q_PROPERTY(bool tracking READ tracking NOTIFY trackingChanged)
    Q_PROPERTY(int cutsMade READ cutsMade NOTIFY cutsChanged)

public:
    explicit ShortsGenerator(Editor *editor);
    ~ShortsGenerator() override;

    QVariantList platforms() const;
    QStringList selectedPlatforms() const { return m_platforms; }
    void setSelectedPlatforms(const QStringList &ids);
    QString mediaId() const { return m_mediaId; }
    void setMediaId(const QString &id);
    int count() const { return m_count; }
    void setCount(int n);
    bool captions() const { return m_captions; }
    void setCaptions(bool on);
    QString captionStyle() const { return m_style; }
    void setCaptionStyle(const QString &id);
    bool reframe() const { return m_reframe; }
    void setReframe(bool on);
    int minSeconds() const;
    int maxSeconds() const;
    bool mixedAspects() const;
    bool running() const { return m_running; }
    QString stage() const { return m_stage; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    QVariantList candidates() const;
    int selectedCount() const;
    bool usedLlm() const { return m_usedLlm; }
    QString llmModel() const { return m_llmModel; }
    bool opencvAvailable() const { return m_opencv; }
    bool tracking() const { return !m_trackQueue.isEmpty() || m_trackProcess; }
    int cutsMade() const { return m_cutsMade; }

    Q_INVOKABLE void togglePlatform(const QString &id);
    Q_INVOKABLE void analyze();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void setSelected(const QString &id, bool on);
    // Moves one edge of a candidate by deltaMs (snaps to the nearest word edge)
    Q_INVOKABLE void trim(const QString &id, bool start, int deltaMs);
    Q_INVOKABLE void preview(const QString &id);
    Q_INVOKABLE void stopPreview();
    // Creates a sequence per selected candidate; returns how many
    Q_INVOKABLE int createCuts();
    Q_INVOKABLE void exportCuts(const QUrl &folder);
    Q_INVOKABLE QString formatTime(qint64 ms) const;
    // Media with speech in the project: [{id, name, duration, video}]
    Q_INVOKABLE QVariantList mediaChoices() const;
    Q_INVOKABLE void clear();

    // Tests: inject a transcript and analysis features instead of running tools
    void setTestData(const Studio::Captions::Transcript &t, const Studio::Highlights::Features &f);

Q_SIGNALS:
    void optionsChanged();
    void stateChanged();
    void candidatesChanged();
    void toolsChanged();
    void trackingChanged();
    void cutsChanged();
    void analysisFinished();

private:
    void setState(const QString &stage, double progress);
    void fail(const QString &message);
    void runAnalysisTools();
    void runFfmpeg(const QStringList &args, std::function<void(const QString &)> done);
    void buildCandidates();
    void scoreWithLlm();
    void scoreNext();
    void finishAnalysis();
    void trackNext();
    const Studio::MediaItem *media() const;

    Editor *m_editor;
    QStringList m_platforms;
    QString m_mediaId;
    int m_count = 5;
    bool m_captions = true;
    QString m_style = QStringLiteral("karaoke");
    bool m_reframe = true;

    bool m_running = false;
    QString m_stage;
    double m_progress = -1;
    QString m_error;
    Studio::Captions::Transcript m_transcript;
    Studio::Highlights::Features m_features;
    QList<Studio::Highlights::Candidate> m_pool;
    QList<Studio::Highlights::Candidate> m_candidates;
    QSet<QString> m_unselected;
    bool m_usedLlm = false;
    QString m_llmModel;
    int m_scoreIndex = 0;
    bool m_testData = false;

    QPointer<QProcess> m_process;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    bool m_opencv = false;

    struct TrackJob {
        QString sequenceId;
        QString clipId;
        QString path;
        double startSec = 0;
        double durationSec = 0;
    };
    QList<TrackJob> m_trackQueue;
    QPointer<QProcess> m_trackProcess;
    QPointer<QProcess> m_probe;
    int m_cutsMade = 0;
};
