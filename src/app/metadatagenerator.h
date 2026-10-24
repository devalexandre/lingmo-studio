/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Export › "Title, description and thumbnail". One button:
//   1. the words spoken on the timeline (captions, known transcripts, or a new
//      Whisper run on the first clip with speech);
//   2. title, description, tags and a thumbnail headline from the local LLM
//      chosen in Settings › AI (Ollama) — a plain heuristic without it;
//   3. candidate frames rendered from the timeline, scored (thumbnail.h), and
//      the headline composed over the chosen one.
// Everything stays editable; the thumbnail is recomposed as the text, style
// or frame change.

#pragma once

#include "metadata.h"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class Editor;
class QNetworkAccessManager;
class QNetworkReply;

class MetadataGenerator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged) // 0..1
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY stateChanged) // why the result is simpler than it could be
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)
    Q_PROPERTY(bool usedLlm READ usedLlm NOTIFY resultChanged)
    Q_PROPERTY(QString llmModel READ llmModel NOTIFY resultChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY resultChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY resultChanged)
    Q_PROPERTY(QString tags READ tags WRITE setTags NOTIFY resultChanged) // comma separated
    Q_PROPERTY(QString thumbnailText READ thumbnailText WRITE setThumbnailText NOTIFY thumbnailChanged)
    Q_PROPERTY(QString thumbnailStyle READ thumbnailStyle WRITE setThumbnailStyle NOTIFY thumbnailChanged)
    Q_PROPERTY(QVariantList styles READ styles CONSTANT)
    Q_PROPERTY(QVariantList frames READ frames NOTIFY framesChanged) // [{url, frame, time}]
    Q_PROPERTY(int selectedFrame READ selectedFrame WRITE setSelectedFrame NOTIFY thumbnailChanged)
    Q_PROPERTY(QString thumbnail READ thumbnail NOTIFY thumbnailChanged) // file URL of the composed image ("" = none)
    Q_PROPERTY(QSize thumbnailSize READ thumbnailSize NOTIFY thumbnailChanged)

public:
    explicit MetadataGenerator(Editor *editor);

    bool running() const { return m_running; }
    QString stage() const { return m_stage; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }
    bool hasResult() const { return m_hasResult; }
    bool usedLlm() const { return m_usedLlm; }
    QString llmModel() const { return m_llmModel; }
    QString title() const { return m_title; }
    void setTitle(const QString &t);
    QString description() const { return m_description; }
    void setDescription(const QString &d);
    QString tags() const { return m_tags; }
    void setTags(const QString &t);
    QString thumbnailText() const { return m_thumbText; }
    void setThumbnailText(const QString &t);
    QString thumbnailStyle() const { return m_style; }
    void setThumbnailStyle(const QString &s);
    QVariantList styles() const;
    QVariantList frames() const { return m_frames; }
    int selectedFrame() const { return m_selected; }
    void setSelectedFrame(int i);
    QString thumbnail() const;
    QSize thumbnailSize() const { return m_composed.size(); }

    Q_INVOKABLE void generate();
    Q_INVOKABLE void cancel();
    // Next to the video's default export path
    Q_INVOKABLE QString suggestedThumbnailPath() const;
    Q_INVOKABLE QString suggestedTextPath() const;
    Q_INVOKABLE bool saveThumbnail(const QUrl &file);
    // Title, description and tags as a text file
    Q_INVOKABLE bool saveTexts(const QUrl &file);
    Q_INVOKABLE void copyToClipboard(const QString &text) const;

Q_SIGNALS:
    void stateChanged();
    void resultChanged();
    void thumbnailChanged();
    void framesChanged();

private:
    void askModel();
    void applyText(Studio::Metadata::Result r, bool fromLlm);
    void fallbackText(const QString &why);
    void startFrames();
    void nextFrame();
    void finishFrames();
    void finish();
    void fail(const QString &message);
    void setStage(const QString &stage, double progress);
    void scheduleCompose();
    void composeNow();
    QString workDir() const;
    QString baseName() const;
    QString resolveLanguage() const;

    Editor *m_editor;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_composeTimer;

    bool m_running = false;
    bool m_waitingTranscript = false;
    int m_session = 0;
    QString m_stage;
    double m_progress = -1;
    QString m_error;
    QString m_notice;

    Studio::Metadata::Input m_input;
    QString m_mediaId;
    QString m_language;

    bool m_hasResult = false;
    bool m_usedLlm = false;
    QString m_llmModel;
    QString m_title;
    QString m_description;
    QString m_tags;
    QString m_thumbText;
    QString m_style = QStringLiteral("bold");

    QList<int> m_positions;
    QList<QImage> m_images; // candidates while scanning, then the ones kept
    QList<double> m_scores;
    QList<int> m_framePositions; // timeline frame of each kept image
    int m_index = 0;
    QVariantList m_frames;
    int m_selected = 0;
    QImage m_composed;
    QString m_composedPath;
    int m_serial = 0;
};
