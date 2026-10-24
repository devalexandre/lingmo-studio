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

#include "captions.h"
#include "exporter.h"
#include "model.h"
#include "plan.h"
#include "projectio.h"

#include <QHash>
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
class Transcriber;
class ShortsGenerator;
class MetadataGenerator;
class Dubber;
namespace Studio::Metadata {
struct Input;
}
namespace Studio::Reframe {
struct Sample;
}
namespace Studio::Captions {
struct Word;
}

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
    Q_PROPERTY(bool whisperAvailable READ whisperAvailable NOTIFY whisperChanged)
    Q_PROPERTY(QString whisperEngine READ whisperEngine NOTIFY whisperChanged)
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
    Q_PROPERTY(QObject *transcriber READ transcriberObject CONSTANT)
    Q_PROPERTY(QObject *shorts READ shortsObject CONSTANT)
    Q_PROPERTY(QObject *metadata READ metadataObject CONSTANT)
    Q_PROPERTY(QObject *dubber READ dubberObject CONSTANT)
    Q_PROPERTY(QVariantList captionStyles READ captionStyles CONSTANT)
    Q_PROPERTY(QVariantMap captionTrackStyle READ captionTrackStyle NOTIFY documentChanged)
    Q_PROPERTY(bool hasCaptions READ hasCaptions NOTIFY documentChanged)
    Q_PROPERTY(bool captionsBusy READ captionsBusy NOTIFY captionsBusyChanged)
    Q_PROPERTY(QVariantList shortSequences READ shortSequences NOTIFY documentChanged)
    // Captions of the media in the source viewer, when its speech is known (Gerar
    // cortes or automatic captions): an ASS file in file time, drawn over the
    // source by libass like the export draws the caption track ("" = none)
    Q_PROPERTY(QString sourceCaptions READ sourceCaptions NOTIFY sourceCaptionsChanged)
    // width / height of the media in the source viewer (0 = no picture)
    Q_PROPERTY(double sourceAspect READ sourceAspect NOTIFY sourceCaptionsChanged)

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
    Transcriber *transcriber() const { return m_transcriber; }
    QObject *transcriberObject() const;
    QObject *shortsObject() const;
    QObject *metadataObject() const;
    QObject *dubberObject() const;
    QVariantList captionStyles() const;
    QVariantMap captionTrackStyle() const;
    bool hasCaptions() const;
    bool captionsBusy() const { return !m_captionClip.isEmpty(); }
    QVariantList shortSequences() const;
    QString sourceCaptions() const { return m_sourceCaptions; }
    double sourceAspect() const { return m_sourceAspect; }
    // Words of a media file (file time), for the source viewer's captions
    void rememberWords(const QString &mediaId, const QList<Studio::Captions::Word> &words);
    // ---- Publishing metadata (see metadata.h, src/app/metadata.h)
    // What the metadata writer knows about the project: the words spoken on the
    // main timeline (captions, else the transcripts known for its media), the
    // text shown on screen and the markers as chapters.
    Studio::Metadata::Input metadataInput() const;
    // The first clip with speech on the main timeline that has no transcript yet:
    // its media, file and the stretch it plays. False when there is none.
    bool speechSource(QString *mediaId, QString *path, qint64 *startMs, qint64 *lengthMs) const;
    // ---- Dubbing (see dubbing.h, src/app/dubber.h)
    // The words of the main timeline in timeline time: the captions on it, else the
    // transcripts known for the media it plays (clips at normal speed only).
    QList<Studio::Captions::Word> timelineWords() const;
    // Media on the main timeline whose speech is not known yet, one job per file
    struct SpeechJob {
        QString mediaId;
        QString path;
        qint64 startMs = 0;
        qint64 lengthMs = 0;
    };
    QList<SpeechJob> speechJobs() const;
    // The known words of a media file, in file time
    QList<Studio::Captions::Word> mediaWords(const QString &mediaId) const;
    // The media on the timeline with the most known speech: where the voice is taken from
    bool voiceSource(QString *mediaId, QString *path) const;
    // Imports an audio file and lays it from the start on a new audio track with this name
    void placeDubTrack(const QString &path, const QString &name);
    // A frame of the main timeline as the export draws it, `longSide` pixels on its longer
    // side (the project size at most); null when it cannot be rendered
    QImage stillAt(int frame, int longSide) const;
    // Writes an ASS script to the captions cache (named by its content) and returns its path
    static QString cacheCaptions(const QString &ass);

    // ---- Shorts (see shorts.h)
    struct ShortCut {
        qint64 startMs = 0;
        qint64 endMs = 0;
        QString title;
        QString reason;
        double score = 0;
    };
    // One sequence (compound clip) per cut, laid out on the main timeline.
    // Returns {sequence id, video clip id} per cut. styleId empty = no captions.
    QList<QPair<QString, QString>> createShorts(const Studio::MediaItem &media, const QList<ShortCut> &cuts, const QStringList &platforms,
                                                const QString &styleId, const QList<Studio::Captions::Word> &words);
    // The caption track of a cut: the words between startMs and endMs (file
    // time), relative to the cut, laid out for a width x height frame
    static Studio::Track shortCaptionTrack(const QList<Studio::Captions::Word> &words, qint64 startMs, qint64 endMs,
                                           const Studio::Captions::Style &style, int width, int height, double fps, int lengthFrames);
    // Position X keyframes from a subject path (times relative to the clip start)
    void applyReframe(const QString &sequenceId, const QString &clipId, const QList<Studio::Reframe::Sample> &samples);
    // Queues a render of a project (export and batch export)
    void queueRender(const Studio::ProjectData &data, const QString &presetId, const QString &output, bool hardware, int quality, int in, int out);

    void setStatus(const QString &s);
    void setBusy(const QString &task);
    void writePoster();

    // -------- project
    Q_INVOKABLE void newProject(const QString &preset = QStringLiteral("1080p30"));
    Q_INVOKABLE void newFromTemplate(const QString &templateId);
    Q_INVOKABLE bool openProject(const QUrl &url);
    Q_INVOKABLE bool openPath(const QString &path);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QUrl &url);
    // Start screen card of a project file: name, folder, modified, poster
    // (a frame saved with the project), exists
    Q_INVOKABLE QVariantMap projectInfo(const QString &path) const;
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
    // Offset wheel: moves black and white by the same amount per channel by
    // re-balancing lift and gain (offset = gain × lift, slope kept)
    Q_INVOKABLE void setWheelOffset(const QString &clipId, double r, double g, double b);
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

    // -------- captions (word-timed, see captions.h)
    // Transcribes the clip (or the selected/first clip with sound) and puts
    // captions on a caption track with the chosen style preset.
    Q_INVOKABLE void generateCaptions(const QString &clipId = {}, const QString &styleId = QStringLiteral("classic"));
    Q_INVOKABLE void cancelCaptions();
    Q_INVOKABLE QVariantMap captionStylePreset(const QString &id) const;
    // Applies a style (preset map + overrides) to the caption tracks of the open timeline
    Q_INVOKABLE void setCaptionStyle(const QVariantMap &style);
    Q_INVOKABLE bool setCaptionText(const QString &clipId, const QString &text);
    Q_INVOKABLE bool setCaptionWord(const QString &clipId, int index, const QString &text);
    Q_INVOKABLE QString splitCaption(const QString &clipId, int wordIndex);
    Q_INVOKABLE bool mergeCaption(const QString &clipId);
    Q_INVOKABLE bool nudgeCaptionWord(const QString &clipId, int index, int deltaMs);
    Q_INVOKABLE bool setCaptionSpeaker(const QString &clipId, int speaker);
    Q_INVOKABLE bool toggleCaptionEmphasis(const QString &clipId, int index);
    // srt | vtt | ass
    Q_INVOKABLE bool exportCaptions(const QUrl &file, const QString &format = QStringLiteral("srt"));
    Q_INVOKABLE void openTranscriptionSettings();
    // Batch export of the generated cuts: one file per cut and platform.
    // captions: "video" burns them into the picture (the default), "video+srt"
    // also writes a .srt next to each file, "srt" writes only the .srt.
    struct ShortExport {
        QString sequenceId;
        QString platform;
        QString output;
    };
    QList<ShortExport> exportShorts(const QUrl &folder, const QString &captions = QStringLiteral("video"),
                                    const QStringList &sequenceIds = QStringList());
    static QString defaultShortsFolder();

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
    void captionsBusyChanged();
    void whisperChanged();
    void sourceCaptionsChanged();
    void error(const QString &message);

private:
    friend class SnapshotCommand;
    void updateSourceCaptions();
    QHash<QString, QList<Studio::Captions::Word>> m_mediaWords;
    QString m_sourceCaptions;
    double m_sourceAspect = 0;
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
    void captionsTranscribed();
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
    Transcriber *m_transcriber;
    ShortsGenerator *m_shorts;
    MetadataGenerator *m_metadata;
    Dubber *m_dubber;
    QString m_captionClip;  // clip being transcribed for captions
    QString m_captionStyleId;
};
