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
//  6. batch export, one file per cut and platform, captions burned into the
//     picture (a .srt next to it only when asked for), with per-cut progress.
// The preview plays the source between the cut's edges with its captions on
// top, rendered by libass from the same ASS the export uses (previewCaptions).

#pragma once

#include "highlights.h"

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QSize>
#include <QUrl>
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
    // Export of the generated cuts
    Q_PROPERTY(QString exportFolder READ exportFolder WRITE setExportFolder NOTIFY exportChanged)
    Q_PROPERTY(QUrl exportFolderUrl READ exportFolderUrl NOTIFY exportChanged)
    // "video" (burned in, the default), "video+srt" or "srt"; back to "video" for each export
    Q_PROPERTY(QString captionOutput READ captionOutput WRITE setCaptionOutput NOTIFY exportChanged)
    // Per generated cut: {id, name, duration, platforms, state, progress, files: [{platform, name, output, state, progress}]}
    Q_PROPERTY(QVariantList cuts READ cuts NOTIFY exportChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)          // queued or rendering (or waiting for face tracking)
    Q_PROPERTY(bool exportDone READ exportDone NOTIFY exportChanged)        // the last export finished
    Q_PROPERTY(int exportFailed READ exportFailed NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QString exportSummary READ exportSummary NOTIFY exportChanged)
    // The captions of the cut being previewed: an ASS file in source time, the
    // platform's frame size (the overlay and the video fill that shape)
    Q_PROPERTY(QString previewCaptions READ previewCaptions NOTIFY previewChanged)
    Q_PROPERTY(QSize previewSize READ previewSize NOTIFY previewChanged)

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
    QString exportFolder() const { return m_exportFolder; }
    void setExportFolder(const QString &folder);
    QUrl exportFolderUrl() const;
    QString captionOutput() const { return m_captionOutput; }
    void setCaptionOutput(const QString &mode);
    QVariantList cuts() const;
    bool exporting() const;
    bool exportDone() const;
    int exportFailed() const;
    double exportProgress() const;
    QString exportSummary() const;
    QString previewCaptions() const { return m_previewAss; }
    QSize previewSize() const { return m_previewSize; }
    // The captions a cut of [startMs, endMs) gets, as an ASS script in the
    // source's time (what the preview draws over the source); "" = none
    QString previewCaptionsFor(qint64 startMs, qint64 endMs) const;
    const Studio::Captions::Transcript &transcript() const { return m_transcript; }
    // The cut's words, relative to its start, as createShorts lays them out
    static QList<Studio::Captions::Word> wordsInRange(const QList<Studio::Captions::Word> &words, qint64 startMs, qint64 endMs);

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
    // Queues the generated cuts (creating the selected ones first when none
    // exist yet); waits for face tracking. Empty folder = exportFolder.
    Q_INVOKABLE void exportCuts(const QUrl &folder = QUrl(), const QString &captions = QStringLiteral("video"));
    Q_INVOKABLE void openExportFolder() const;
    // One generated cut: export it alone, or play it (the source between its
    // edges with its caption track drawn on top)
    Q_INVOKABLE void exportCut(const QString &sequenceId, const QString &captions = QStringLiteral("video"));
    Q_INVOKABLE void previewCut(const QString &sequenceId);
    // Opens a generated cut's sequence on the timeline
    Q_INVOKABLE bool editCut(const QString &sequenceId);
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
    void exportChanged();
    void previewChanged();

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
    void startExport();
    void syncExports();
    void setPreviewCaptions(const Studio::Highlights::Candidate *c);

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

    struct ExportJob {
        QString sequenceId;
        QString platform;
        QString output;
        QString state = QStringLiteral("queued");
        int progress = 0;
    };
    QList<ExportJob> m_exports; // the last batch
    QStringList m_exportOnly;   // the sequences of the pending export ("" = all)
    QString m_trackingSequence; // the cut whose faces are being followed
    QString m_exportFolder;
    QString m_captionOutput = QStringLiteral("video");
    bool m_exportPending = false; // waits for face tracking
    QString m_pendingCaptions;
    QString m_previewAss;
    QSize m_previewSize{1080, 1920};
};
