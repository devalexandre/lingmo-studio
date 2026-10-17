/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Editor: the one controller QML talks to. It owns the document (timeline +
// nested sequences), the media list, project settings and the undo stack.
// Every edit goes through modify(): copy the document, run a pure operation
// from timelineops.h, push a snapshot command. Undo/redo restore snapshots.

#pragma once

#include "exporter.h"
#include "model.h"
#include "plan.h"
#include "projectio.h"

#include <QImage>

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUndoStack>
#include <QVariant>
#include <functional>

class TimelineModel;
class MediaBin;
class Player;
class RenderQueue;
class Scopes;
class Recorder;

class Editor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(int projectWidth READ projectWidth NOTIFY settingsChanged)
    Q_PROPERTY(int projectHeight READ projectHeight NOTIFY settingsChanged)
    Q_PROPERTY(double fps READ fps NOTIFY settingsChanged)
    Q_PROPERTY(QString presetId READ presetId NOTIFY settingsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(QStringList history READ history NOTIFY historyChanged)
    Q_PROPERTY(int historyIndex READ historyIndex NOTIFY historyChanged)
    Q_PROPERTY(QStringList recentProjects READ recentProjects NOTIFY recentChanged)
    Q_PROPERTY(QString recoveryPath READ recoveryPath NOTIFY recoveryChanged)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedClip READ selectedClip NOTIFY selectedClipChanged)
    Q_PROPERTY(bool useProxies READ useProxies WRITE setUseProxies NOTIFY useProxiesChanged)
    Q_PROPERTY(bool autoProxy READ autoProxy WRITE setAutoProxy NOTIFY autoProxyChanged)
    Q_PROPERTY(QVariantList effectDefs READ effectDefs CONSTANT)
    Q_PROPERTY(QStringList transitionTypes READ transitionTypes CONSTANT)
    Q_PROPERTY(QVariantList templates READ templates CONSTANT)
    Q_PROPERTY(QVariantList exportPresets READ exportPresets CONSTANT)
    Q_PROPERTY(QStringList projectPresets READ projectPresets CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString busyTask READ busyTask NOTIFY busyChanged)
    Q_PROPERTY(bool whisperAvailable READ whisperAvailable CONSTANT)
    Q_PROPERTY(QString whisperEngine READ whisperEngine CONSTANT)
    Q_PROPERTY(bool ollamaAvailable READ ollamaAvailable CONSTANT)
    Q_PROPERTY(QStringList hwEncoders READ hwEncoders NOTIFY hwEncodersChanged)
    Q_PROPERTY(bool hwDetected READ hwDetected NOTIFY hwEncodersChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)
    Q_PROPERTY(bool hasGrade READ hasGrade NOTIFY gradeChanged)
    Q_PROPERTY(QStringList sequencePath READ sequencePath NOTIFY sequenceChanged)
    Q_PROPERTY(bool darkUi READ darkUi WRITE setDarkUi NOTIFY darkUiChanged)
    Q_PROPERTY(QObject *timeline READ timelineObject CONSTANT)
    Q_PROPERTY(QObject *mediaBin READ mediaBinObject CONSTANT)
    Q_PROPERTY(QObject *player READ playerObject CONSTANT)
    Q_PROPERTY(QObject *renderQueue READ renderQueueObject CONSTANT)
    Q_PROPERTY(QObject *scopes READ scopesObject CONSTANT)
    Q_PROPERTY(QObject *recorder READ recorderObject CONSTANT)

public:
    explicit Editor(QObject *parent = nullptr);
    ~Editor() override;

    // -------- state for C++ collaborators
    const Studio::Document &document() const { return m_doc; }
    const Studio::Timeline &currentTimeline() const;
    const QList<Studio::MediaItem> &media() const { return m_media; }
    const Studio::ProjectSettings &settings() const { return m_settings; }
    const Studio::MediaItem *mediaItem(const QString &id) const;
    void addMedia(const Studio::MediaItem &item);
    void updateMedia(const Studio::MediaItem &item);
    void removeMediaItem(const QString &id);
    Studio::ProjectData projectData() const;
    void restoreDocument(const Studio::Document &doc);
    int sourceLength(const Studio::Clip &clip) const;

    QString projectPath() const { return m_path; }
    QString projectName() const;
    bool dirty() const { return m_dirty; }
    int projectWidth() const { return m_settings.width; }
    int projectHeight() const { return m_settings.height; }
    double fps() const { return m_settings.fps(); }
    QString presetId() const { return m_settings.presetId(); }
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    QString undoText() const { return m_undo.undoText(); }
    QString redoText() const { return m_undo.redoText(); }
    QStringList history() const;
    int historyIndex() const { return m_undo.index(); }
    QStringList recentProjects() const { return m_recent; }
    QString recoveryPath() const { return m_recoveryPath; }
    QStringList selection() const { return m_selection; }
    QVariantMap selectedClip() const;
    bool useProxies() const { return m_useProxies; }
    void setUseProxies(bool on);
    bool autoProxy() const { return m_autoProxy; }
    void setAutoProxy(bool on);
    QVariantList effectDefs() const;
    QStringList transitionTypes() const;
    QVariantList templates() const;
    QVariantList exportPresets() const;
    QStringList projectPresets() const;
    QString status() const { return m_status; }
    QString busyTask() const { return m_busy; }
    bool whisperAvailable() const;
    QString whisperEngine() const;
    bool ollamaAvailable() const;
    QStringList hwEncoders() const { return m_hw.available; }
    bool hwDetected() const { return m_hw.detected; }
    const Studio::HwEncoders &hardware() const { return m_hw; }
    QVariantList searchResults() const { return m_searchResults; }
    bool hasGrade() const { return !m_grade.isEmpty(); }
    QStringList sequencePath() const;
    bool darkUi() const { return m_darkUi; }
    void setDarkUi(bool on);

    QObject *timelineObject() const;
    QObject *mediaBinObject() const;
    QObject *playerObject() const;
    QObject *renderQueueObject() const;
    QObject *scopesObject() const;
    QObject *recorderObject() const;
    Player *player() const { return m_player; }

    void setStatus(const QString &s);
    void setBusy(const QString &task);

    // -------- project
    Q_INVOKABLE void newProject(const QString &preset = QStringLiteral("1080p30"));
    Q_INVOKABLE void newFromTemplate(const QString &templateId);
    Q_INVOKABLE bool openProject(const QUrl &url);
    Q_INVOKABLE bool openPath(const QString &path);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QUrl &url);
    Q_INVOKABLE void recover();
    Q_INVOKABLE void discardRecovery();
    Q_INVOKABLE void setProjectSettings(int width, int height, int fpsNum, int fpsDen);
    Q_INVOKABLE void applyProjectPreset(const QString &preset);
    Q_INVOKABLE void importUrls(const QList<QUrl> &urls, int track = -1, int position = -1);
    Q_INVOKABLE void importPaths(const QStringList &paths, int track = -1, int position = -1);
    Q_INVOKABLE void clearRecent();
    Q_INVOKABLE void cleanExit();
    Q_INVOKABLE void autosave(); // also runs every minute while there are unsaved changes

    // -------- history
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void setHistoryIndex(int index);

    // -------- selection
    Q_INVOKABLE void select(const QString &clipId, bool add = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QVariantMap clipInfo(const QString &clipId) const;
    Q_INVOKABLE QString clipAt(int track, int frame) const;

    // -------- edit page
    Q_INVOKABLE QString addMediaToTimeline(const QString &mediaId, int track, int position, int in = -1, int out = -1, bool ripple = false);
    Q_INVOKABLE QString appendMedia(const QString &mediaId);
    Q_INVOKABLE bool moveClip(const QString &clipId, int track, int position);
    Q_INVOKABLE bool trimClip(const QString &clipId, bool left, int frame, bool ripple = false);
    Q_INVOKABLE bool rollEdit(const QString &clipId, int boundary);
    Q_INVOKABLE bool slipClip(const QString &clipId, int delta);
    Q_INVOKABLE bool slideClip(const QString &clipId, int delta);
    Q_INVOKABLE bool splitClip(const QString &clipId, int frame);
    Q_INVOKABLE int splitAtPlayhead();
    Q_INVOKABLE bool deleteSelected(bool ripple);
    Q_INVOKABLE bool removeGap(int track, int frame);
    Q_INVOKABLE bool setTransition(const QString &clipId, int length, const QString &type);
    Q_INVOKABLE bool addTransitionAtPlayhead(const QString &type = QStringLiteral("dissolve"), int length = 15);
    Q_INVOKABLE QString freezeFrame(int length = -1);
    Q_INVOKABLE QString detachAudio(const QString &clipId);
    Q_INVOKABLE QString addTitle(const QString &text = {}, const QString &templateId = QStringLiteral("center"), int track = -1);
    Q_INVOKABLE QString addColorClip(const QString &color, int track = -1);
    Q_INVOKABLE bool createCompound();
    Q_INVOKABLE bool openCompound(const QString &clipId);
    Q_INVOKABLE void leaveCompound(int depth = 0);
    Q_INVOKABLE void addTrack(const QString &kind);
    Q_INVOKABLE void removeTrack(int index);
    Q_INVOKABLE void setTrackProperty(int index, const QString &name, const QVariant &value);
    Q_INVOKABLE void addMarker(int frame, const QString &name = {}, const QString &color = QStringLiteral("blue"));
    Q_INVOKABLE void removeMarker(int index);
    Q_INVOKABLE void renameMarker(int index, const QString &name);
    Q_INVOKABLE int nextEdit(int frame, bool forward) const;
    Q_INVOKABLE int snapFrame(int frame, int tolerance, const QString &ignoreClip = {}) const;

    // -------- clip properties, effects, keyframes
    Q_INVOKABLE void setClipProperty(const QString &clipId, const QString &key, const QVariant &value);
    Q_INVOKABLE void addEffect(const QString &clipId, const QString &type);
    Q_INVOKABLE void addEffectToSelection(const QString &type);
    Q_INVOKABLE void removeEffect(const QString &clipId, int index);
    Q_INVOKABLE void moveEffect(const QString &clipId, int from, int to);
    Q_INVOKABLE void setEffectEnabled(const QString &clipId, int index, bool enabled);
    Q_INVOKABLE void setEffectParam(const QString &clipId, int index, const QString &key, const QVariant &value);
    Q_INVOKABLE QVariantMap effectValuesAt(const QString &clipId, int index, int timelineFrame) const;
    Q_INVOKABLE bool hasKeyframeAt(const QString &clipId, int index, int timelineFrame) const;
    Q_INVOKABLE void toggleKeyframe(const QString &clipId, int index, int timelineFrame);
    Q_INVOKABLE void moveKeyframe(const QString &clipId, int index, int keyframe, int newRelFrame);
    Q_INVOKABLE void setKeyframeValue(const QString &clipId, int index, int keyframe, const QString &key, const QVariant &value);
    Q_INVOKABLE void removeKeyframe(const QString &clipId, int index, int keyframe);
    Q_INVOKABLE void setSpeedRamp(const QString &clipId, const QVariantList &points); // [{frame (rel), speed}]
    Q_INVOKABLE void applyPictureInPicture(const QString &corner = QStringLiteral("top-right"));

    // -------- color page
    Q_INVOKABLE void copyGrade(const QString &clipId);
    Q_INVOKABLE void pasteGrade();
    Q_INVOKABLE void resetGrade(const QString &clipId);
    Q_INVOKABLE void setWheel(const QString &clipId, const QString &range, double r, double g, double b);
    Q_INVOKABLE QImage beforeAfterFrame(bool before, int width) const;

    // -------- audio page
    Q_INVOKABLE void normalizeLoudness(const QString &clipId, double targetLufs = -14);
    Q_INVOKABLE void placeRecording(const QString &path);

    // -------- AI / analysis (local only)
    Q_INVOKABLE void autoSubtitles(const QString &clipId, const QString &language = {});
    Q_INVOKABLE void importSubtitles(const QUrl &srt);
    Q_INVOKABLE bool exportSubtitles(const QUrl &srt);
    Q_INVOKABLE void splitOnSilence(const QString &clipId, double noiseDb = -35, double minSeconds = 0.6, bool removeSilence = true);
    Q_INVOKABLE void splitOnScenes(const QString &clipId, double threshold = 0.35);
    Q_INVOKABLE void smartReframe(const QString &aspect = QStringLiteral("9:16"));
    Q_INVOKABLE void searchTranscripts(const QString &query);

    // -------- export
    Q_INVOKABLE void queueExport(const QString &presetId, const QUrl &output, bool hardware, int quality, bool inOutOnly);
    Q_INVOKABLE QString suggestedExportPath(const QString &presetId) const;
    Q_INVOKABLE void detectHardware();

    // -------- helpers for QML
    Q_INVOKABLE QString timecode(int frame) const;
    Q_INVOKABLE int framesFromSeconds(double seconds) const;
    Q_INVOKABLE QString formatDuration(int frames) const;
    Q_INVOKABLE QStringList mediaNameFilters() const;
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }

Q_SIGNALS:
    void projectChanged();
    void dirtyChanged();
    void settingsChanged();
    void historyChanged();
    void recentChanged();
    void recoveryChanged();
    void selectionChanged();
    void selectedClipChanged();
    void useProxiesChanged();
    void autoProxyChanged();
    void statusChanged();
    void busyChanged();
    void hwEncodersChanged();
    void searchResultsChanged();
    void gradeChanged();
    void sequenceChanged();
    void darkUiChanged();
    void documentChanged();
    void error(const QString &message);

private:
    friend class SnapshotCommand;
    using EditFn = std::function<bool(Studio::Timeline &, Studio::Document &, QString *)>;
    bool modify(const QString &text, const EditFn &fn, int mergeId = -1, const QString &mergeKey = {});
    Studio::Timeline &timelineIn(Studio::Document &doc) const;
    void refresh();
    void scheduleRebuild();
    void rebuild();
    void setDirty(bool d);
    void setPath(const QString &path);
    void addRecent(const QString &path);
    void loadProject(const Studio::ProjectData &data, const QString &path);
    QString autosavePath() const;
    void checkRecovery();
    int playhead() const;
    int defaultTrack(Studio::TrackKind kind) const;
    QVariantMap clipToMap(const Studio::Clip &clip, int track) const;
    Studio::Planner planner(bool useProxies) const;
    void runTool(const QString &task, const QString &program, const QStringList &args, std::function<void(int, const QString &)> done);

    Studio::Document m_doc;
    QList<Studio::MediaItem> m_media;
    Studio::ProjectSettings m_settings;
    QStringList m_sequenceStack; // ids of open compound clips' sequences
    QUndoStack m_undo;
    QString m_path;
    bool m_dirty = false;
    int m_cleanIndex = 0;
    QStringList m_recent;
    QString m_recoveryPath;
    QStringList m_selection;
    bool m_useProxies = true;
    bool m_autoProxy = true;
    bool m_darkUi = true;
    QString m_status;
    QString m_busy;
    Studio::HwEncoders m_hw;
    QVariantList m_searchResults;
    QList<Studio::Effect> m_grade;
    QTimer m_rebuildTimer;
    QTimer m_autosaveTimer;
    QTimer m_statusTimer;

    TimelineModel *m_timeline;
    MediaBin *m_mediaBin;
    Player *m_player;
    RenderQueue *m_renderQueue;
    Scopes *m_scopes;
    Recorder *m_recorder;
};
