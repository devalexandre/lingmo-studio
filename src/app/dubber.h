/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Export › "Dub in another language": the speaker's own voice, cloned, saying
// the translated speech of the timeline. One audio file per language, as long
// as the video, ready for YouTube Studio › Languages › Add dub.
//
//   speech of the timeline (captions, known transcripts, or Whisper)
//     → translation for the ear (the Ollama model of Settings › AI)
//     → a 10-15 s voice reference taken from the video (or a file you choose)
//     → Chatterbox Multilingual speaking each passage (data/dub.py, own virtualenv)
//     → each passage fitted to its place in the timeline (sped up a little at most)
//     → one WAV and one MP3 per language
//
// The engine is large (PyTorch and a 3 GB model): it is installed on demand
// into ~/.local/share/lingmo/dub-venv from the dialog.

#pragma once

#include "dubbing.h"
#include "editor.h"

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;

class Dubber : public QObject
{
    Q_OBJECT
    // the voice engine
    Q_PROPERTY(bool engineChecking READ engineChecking NOTIFY engineChanged)
    Q_PROPERTY(bool engineReady READ engineReady NOTIFY engineChanged)
    Q_PROPERTY(QString engineSummary READ engineSummary NOTIFY engineChanged)
    Q_PROPERTY(bool onGpu READ onGpu NOTIFY engineChanged)
    Q_PROPERTY(bool installing READ installing NOTIFY installChanged)
    Q_PROPERTY(QString installMessage READ installMessage NOTIFY installChanged)
    Q_PROPERTY(bool installFailed READ installFailed NOTIFY installChanged)
    // the job
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged) // 0..1, -1 = unknown
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY stateChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(QStringList targets READ targets WRITE setTargets NOTIFY targetsChanged)
    Q_PROPERTY(QString sourceLanguage READ sourceLanguage NOTIFY targetsChanged)
    Q_PROPERTY(QString referencePath READ referencePath WRITE setReferencePath NOTIFY referenceChanged)
    Q_PROPERTY(QString referenceInfo READ referenceInfo NOTIFY referenceChanged)
    Q_PROPERTY(QString outputFolder READ outputFolder WRITE setOutputFolder NOTIFY outputChanged)
    Q_PROPERTY(QString estimate READ estimate NOTIFY stateChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)

public:
    explicit Dubber(Editor *editor);

    bool engineChecking() const { return m_checking; }
    bool engineReady() const { return m_ready; }
    QString engineSummary() const;
    bool onGpu() const { return m_ready && m_device == QLatin1String("cuda"); }
    bool installing() const { return m_install != nullptr; }
    QString installMessage() const { return m_installMessage; }
    bool installFailed() const { return m_installFailed; }

    bool running() const { return m_running; }
    QString stage() const { return m_stage; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }
    QVariantList languages() const;
    QStringList targets() const { return m_targets; }
    void setTargets(const QStringList &codes);
    QString sourceLanguage() const { return m_sourceLang; }
    QString referencePath() const { return m_referencePath; }
    void setReferencePath(const QString &path);
    QString referenceInfo() const { return m_referenceInfo; }
    QString outputFolder() const;
    void setOutputFolder(const QString &folder);
    QString estimate() const;
    QVariantList results() const { return m_results; }

    Q_INVOKABLE void checkEngine();
    Q_INVOKABLE void installEngine();
    Q_INVOKABLE void cancelInstall();
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void addToTimeline(const QString &code);
    Q_INVOKABLE void openFolder() const;

    // The helper script and the python of its virtualenv (LINGMO_STUDIO_DUB_* override them for tests)
    static QString script();
    static QString python();
    static QString venvDir();

Q_SIGNALS:
    void engineChanged();
    void installChanged();
    void stateChanged();
    void targetsChanged();
    void referenceChanged();
    void outputChanged();
    void resultsChanged();

private:
    void runInstallStep();
    void installFinished(bool ok);
    // job steps
    void transcribeNext();
    void prepare();
    void prepareReference();
    void useReference(const QString &wav, const QString &info);
    void nextLanguage();
    void translateNext();
    void sendTranslation(const QList<int> &ids, bool retry);
    void startSynth();
    void synthFinished(int code);
    void assemble(const QVariantMap &synthResult);
    void encode(const QString &wav, double seconds);
    void finishLanguage(const QVariantMap &result);
    void finishAll();
    void fail(const QString &message);
    void setStage(const QString &stage, double local);
    void addNotice(const QString &text);
    QString workDir() const;
    QString baseName() const;
    QString ollamaModel(QString *problem) const;
    QProcess *startPython(const QStringList &args, const QString &stage);
    static QString lastLines(const QString &text, int n = 6);

    Editor *m_editor;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<QProcess> m_process;
    QPointer<QProcess> m_install;
    QPointer<QProcess> m_probe;

    // engine
    bool m_checking = false;
    bool m_ready = false;
    bool m_v3 = false;
    QString m_device;
    QString m_gpu;
    double m_vram = 0;
    QString m_problem;
    QString m_installMessage;
    bool m_installFailed = false;
    int m_installStep = 0;
    bool m_installPyPi = false;

    // job
    bool m_running = false;
    int m_session = 0;
    QString m_stage;
    double m_progress = -1;
    QString m_error;
    QString m_notice;
    QStringList m_targets;
    QString m_sourceLang;
    QString m_referencePath;
    QString m_referenceInfo;
    QString m_outputFolder;
    QVariantList m_results;

    QList<Editor::SpeechJob> m_jobs;
    int m_jobIndex = 0;
    QList<Studio::Captions::Word> m_words;
    QList<Studio::Dubbing::Sentence> m_source;
    QList<Studio::Dubbing::Sentence> m_work; // the sentences being translated
    QList<Studio::Dubbing::Chunk> m_chunks;
    qint64 m_totalMs = 0;
    QString m_referenceWav;
    QStringList m_queue;
    int m_langTotal = 0;
    int m_langDone = 0;
    const Studio::Dubbing::Language *m_lang = nullptr;
    int m_batchStart = 0;
    int m_batchEnd = 0;
    int m_untranslated = 0;
    bool m_retried = false;
    QString m_model;
    QString m_synthLog;
    QString m_synthBuffer;
    QString m_langDir;
    Studio::Dubbing::FitResult m_fit;
};
