/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "editor.h"
#include "aitools.h"
#include "captions.h"
#include "effects.h"
#include "imageproviders.h"
#include "mediabin.h"
#include "mediaprobe.h"
#include "mltbuilder.h"
#include "player.h"
#include "projectio.h"
#include "recorder.h"
#include "renderqueue.h"
#include "platforms.h"
#include "reframe.h"
#include "scopes.h"
#include "shorts.h"
#include "timelineops.h"
#include "timelinemodel.h"
#include "transcriber.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUndoCommand>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>
#include <cmath>

using namespace Studio;

// ------------------------------------------------------------------ undo

class SnapshotCommand : public QUndoCommand
{
public:
    SnapshotCommand(Editor *editor, Document before, Document after, const QString &text, int mergeId, const QString &mergeKey)
        : QUndoCommand(text)
        , m_editor(editor)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_mergeId(mergeId)
        , m_mergeKey(mergeKey)
        , m_time(QDateTime::currentMSecsSinceEpoch())
    {
    }
    void undo() override { m_editor->restoreDocument(m_before); }
    void redo() override { m_editor->restoreDocument(m_after); }
    int id() const override { return m_mergeId; }
    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *o = static_cast<const SnapshotCommand *>(other);
        // slider drags: one undo step per gesture (same key, quick succession)
        if (o->m_mergeKey != m_mergeKey || o->m_time - m_time > 1500)
            return false;
        m_after = o->m_after;
        m_time = o->m_time;
        return true;
    }

private:
    Editor *m_editor;
    Document m_before;
    Document m_after;
    int m_mergeId;
    QString m_mergeKey;
    qint64 m_time;
};

static constexpr int MergeParam = 1;
static constexpr int MergeTrack = 2;
static constexpr int MergeClip = 3;

static QString tr(const char *s)
{
    return QCoreApplication::translate("Editor", s);
}

static const QStringList ColorEffects = {QStringLiteral("color"), QStringLiteral("wheels"), QStringLiteral("curves"),
                                         QStringLiteral("hsl"), QStringLiteral("lut")};

// ------------------------------------------------------------------ setup

static QString sessionId()
{
    static const QString id = QUuid::createUuid().toString(QUuid::Id128).left(10);
    return id;
}

Editor::Editor(QObject *parent)
    : QObject(parent)
{
    initMlt();
    EffectRegistry::setAvailableServices(availableMltServices());

    m_timeline = new TimelineModel(this);
    m_mediaBin = new MediaBin(this);
    m_player = new Player(this);
    m_renderQueue = new RenderQueue(this);
    m_scopes = new Scopes(m_player, this);
    m_recorder = new Recorder(this);
    m_transcriber = new Transcriber(this);
    m_shorts = new ShortsGenerator(this);
    connect(m_transcriber, &Transcriber::engineChanged, this, &Editor::whisperChanged);
    connect(m_transcriber, &Transcriber::finished, this, [this](const QString &task) {
        if (task == QLatin1String("captions"))
            captionsTranscribed();
    });
    connect(m_transcriber, &Transcriber::failed, this, [this](const QString &task, const QString &message) {
        if (task != QLatin1String("captions"))
            return;
        m_captionClip.clear();
        Q_EMIT captionsBusyChanged();
        setStatus(message);
    });

    QSettings s;
    m_recent = s.value(QStringLiteral("recentProjects")).toStringList();
    m_useProxies = s.value(QStringLiteral("useProxies"), true).toBool();
    m_autoProxy = s.value(QStringLiteral("autoProxy"), true).toBool();
    m_darkUi = s.value(QStringLiteral("darkUi"), true).toBool();
    m_player->setPreviewHeight(s.value(QStringLiteral("previewHeight"), 540).toInt());
    connect(m_player, &Player::previewHeightChanged, this, [this] {
        QSettings().setValue(QStringLiteral("previewHeight"), m_player->previewHeight());
        rebuild();
    });

    m_undo.setUndoLimit(300);
    connect(&m_undo, &QUndoStack::indexChanged, this, &Editor::historyChanged);
    connect(&m_undo, &QUndoStack::cleanChanged, this, [this](bool clean) { setDirty(!clean); });

    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(0);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &Editor::rebuild);
    m_autosaveTimer.setInterval(60 * 1000);
    connect(&m_autosaveTimer, &QTimer::timeout, this, &Editor::autosave);
    m_autosaveTimer.start();
    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(5000);
    connect(&m_statusTimer, &QTimer::timeout, this, [this] {
        m_status.clear();
        Q_EMIT statusChanged();
    });
    connect(m_player, &Player::positionChanged, this, [this] {
        if (!m_player->playing())
            Q_EMIT selectedClipChanged();
    });
    connect(m_recorder, &Recorder::finished, this, &Editor::placeRecording);
    connect(m_recorder, &Recorder::failed, this, &Editor::setStatus);
    connect(m_mediaBin, &MediaBin::importFailed, this, [this](const QString &file, const QString &reason) {
        setStatus(tr("Could not import %1: %2").arg(file, reason));
    });
    connect(m_renderQueue, &RenderQueue::jobFinished, this, [this](const QString &out, bool ok) {
        setStatus(ok ? tr("Exported %1").arg(QFileInfo(out).fileName()) : tr("Export of %1 failed").arg(QFileInfo(out).fileName()));
    });

    // the source viewer's captions follow the media it shows and the caption style
    connect(m_player, &Player::modeChanged, this, &Editor::updateSourceCaptions);
    connect(this, &Editor::documentChanged, this, &Editor::updateSourceCaptions);

    checkRecovery();
    newProject(QStringLiteral("1080p30"));
    detectHardware();
}

QString Editor::cacheCaptions(const QString &ass)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/captions");
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("view-%1.ass").arg(QString::fromLatin1(QCryptographicHash::hash(ass.toUtf8(), QCryptographicHash::Sha1).toHex().left(16))));
    if (!QFileInfo::exists(path)) {
        QSaveFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(ass.toUtf8());
            f.commit();
        }
    }
    return path;
}

void Editor::rememberWords(const QString &mediaId, const QList<Captions::Word> &words)
{
    if (mediaId.isEmpty() || words.isEmpty())
        return;
    QList<Captions::Word> &known = m_mediaWords[mediaId];
    if (known.isEmpty()) {
        known = words;
    } else {
        // a stretch transcribed again replaces what was known of it
        const qint64 from = words.first().startMs, to = words.last().endMs;
        known.erase(std::remove_if(known.begin(), known.end(), [&](const Captions::Word &w) { return w.endMs > from && w.startMs < to; }),
                    known.end());
        known.append(words);
        std::sort(known.begin(), known.end(), [](const Captions::Word &a, const Captions::Word &b) { return a.startMs < b.startMs; });
    }
    updateSourceCaptions();
}

void Editor::updateSourceCaptions()
{
    QString file;
    const QString id = m_player->sourceMediaId();
    const MediaItem *m = mediaItem(id);
    const auto words = m_mediaWords.constFind(id);
    if (m && m->hasVideo && m->width > 0 && m->height > 0 && words != m_mediaWords.constEnd() && !words->isEmpty()) {
        // the style of the timeline's captions, else the one chosen in Gerar cortes
        Captions::Style style;
        bool found = false;
        for (const Track &t : currentTimeline().tracks) {
            if (Captions::trackHasCaptions(t)) {
                style = Captions::Style::fromMap(t.captionStyle);
                found = true;
                break;
            }
        }
        if (!found)
            style = Captions::Style::preset(m_shorts->captionStyle());
        style.safeArea = {};
        const QList<Captions::Cue> cues = Captions::buildCues(*words, style.effectiveRules(m->width, m->height));
        file = cacheCaptions(Captions::toAss(cues, style, m->width, m->height, fps()));
    }
    const double aspect = m && m->hasVideo && m->width > 0 && m->height > 0 ? double(m->width) / m->height : 0;
    if (file != m_sourceCaptions || !qFuzzyCompare(1 + aspect, 1 + m_sourceAspect)) {
        m_sourceCaptions = file;
        m_sourceAspect = aspect;
        Q_EMIT sourceCaptionsChanged();
    }
}

Editor::~Editor() = default;

QObject *Editor::timelineObject() const { return m_timeline; }
QObject *Editor::mediaBinObject() const { return m_mediaBin; }
QObject *Editor::playerObject() const { return m_player; }
QObject *Editor::renderQueueObject() const { return m_renderQueue; }
QObject *Editor::scopesObject() const { return m_scopes; }
QObject *Editor::recorderObject() const { return m_recorder; }
QObject *Editor::transcriberObject() const { return m_transcriber; }
QObject *Editor::shortsObject() const { return m_shorts; }

void Editor::setStatus(const QString &s)
{
    m_status = s;
    Q_EMIT statusChanged();
    if (!s.isEmpty())
        m_statusTimer.start();
}

void Editor::setBusy(const QString &task)
{
    if (task == m_busy)
        return;
    m_busy = task;
    Q_EMIT busyChanged();
}

// ------------------------------------------------------------------ document

Timeline &Editor::timelineIn(Document &doc) const
{
    if (!m_sequenceStack.isEmpty())
        for (Sequence &s : doc.sequences)
            if (s.id == m_sequenceStack.last())
                return s.timeline;
    return doc.timeline;
}

const Timeline &Editor::currentTimeline() const
{
    return timelineIn(const_cast<Document &>(m_doc));
}

bool Editor::modify(const QString &text, const EditFn &fn, int mergeId, const QString &mergeKey)
{
    Document d = m_doc;
    Timeline &tl = timelineIn(d);
    QString error;
    if (!fn(tl, d, &error)) {
        if (!error.isEmpty())
            setStatus(error);
        return false;
    }
    if (d == m_doc)
        return false;
    m_undo.push(new SnapshotCommand(this, m_doc, d, text, mergeId, mergeKey));
    return true;
}

void Editor::restoreDocument(const Document &doc)
{
    m_doc = doc;
    // a compound clip being edited may have disappeared (undo)
    while (!m_sequenceStack.isEmpty()) {
        bool found = false;
        for (const Sequence &s : m_doc.sequences)
            found |= s.id == m_sequenceStack.last();
        if (found)
            break;
        m_sequenceStack.removeLast();
        Q_EMIT sequenceChanged();
    }
    refresh();
}

void Editor::refresh()
{
    m_timeline->setTimeline(currentTimeline());
    QStringList kept;
    for (const QString &id : std::as_const(m_selection))
        if (currentTimeline().clip(id))
            kept << id;
    if (kept != m_selection) {
        m_selection = kept;
        Q_EMIT selectionChanged();
    }
    Q_EMIT selectedClipChanged();
    Q_EMIT documentChanged();
    scheduleRebuild();
}

void Editor::scheduleRebuild()
{
    m_rebuildTimer.start();
}

Planner Editor::planner(bool useProxies) const
{
    PlanOptions o;
    o.useProxies = useProxies;
    o.storeModel = false;
    o.scale = m_player->previewScale();
    return Planner(m_settings, m_media, m_doc.sequences, o);
}

void Editor::rebuild()
{
    MediaRegistry::set(m_media, m_settings);
    m_player->setPlan(planner(m_useProxies).plan(currentTimeline()));
}

int Editor::sourceLength(const Clip &clip) const
{
    return Planner(m_settings, m_media, m_doc.sequences).sourceLength(clip);
}

const MediaItem *Editor::mediaItem(const QString &id) const
{
    for (const MediaItem &m : m_media)
        if (m.id == id)
            return &m;
    return nullptr;
}

void Editor::addMedia(const MediaItem &item)
{
    m_media.append(item);
    MediaRegistry::set(m_media, m_settings);
    setDirty(true);
}

void Editor::updateMedia(const MediaItem &item)
{
    for (MediaItem &m : m_media) {
        if (m.id == item.id) {
            m = item;
            break;
        }
    }
    MediaRegistry::set(m_media, m_settings);
    setDirty(true);
    scheduleRebuild();
}

void Editor::removeMediaItem(const QString &id)
{
    m_media.erase(std::remove_if(m_media.begin(), m_media.end(), [&](const MediaItem &m) { return m.id == id; }), m_media.end());
    MediaRegistry::set(m_media, m_settings);
    setDirty(true);
}

ProjectData Editor::projectData() const
{
    ProjectData d;
    d.settings = m_settings;
    d.doc = m_doc;
    d.media = m_media;
    return d;
}

void Editor::setDirty(bool d)
{
    if (d == m_dirty)
        return;
    m_dirty = d;
    Q_EMIT dirtyChanged();
}

QString Editor::projectName() const
{
    return m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).completeBaseName();
}

void Editor::setPath(const QString &path)
{
    m_path = path;
    Q_EMIT projectChanged();
}

QStringList Editor::history() const
{
    QStringList h;
    h << tr("Project opened");
    for (int i = 0; i < m_undo.count(); ++i)
        h << m_undo.text(i);
    return h;
}

void Editor::setUseProxies(bool on)
{
    if (on == m_useProxies)
        return;
    m_useProxies = on;
    QSettings().setValue(QStringLiteral("useProxies"), on);
    Q_EMIT useProxiesChanged();
    scheduleRebuild();
}

void Editor::setAutoProxy(bool on)
{
    if (on == m_autoProxy)
        return;
    m_autoProxy = on;
    QSettings().setValue(QStringLiteral("autoProxy"), on);
    Q_EMIT autoProxyChanged();
}

void Editor::setDarkUi(bool on)
{
    if (on == m_darkUi)
        return;
    m_darkUi = on;
    QSettings().setValue(QStringLiteral("darkUi"), on);
    Q_EMIT darkUiChanged();
}

QVariantList Editor::effectDefs() const
{
    return EffectRegistry::toVariantList();
}

QStringList Editor::transitionTypes() const
{
    return Planner::transitionTypes();
}

QVariantList Editor::exportPresets() const
{
    QVariantList list;
    for (const ExportPreset &p : Exporter::presets())
        list.append(QVariantMap{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}, {QStringLiteral("category"), p.category},
                                {QStringLiteral("extension"), p.extension}, {QStringLiteral("codec"), p.codecFamily},
                                {QStringLiteral("width"), p.width}, {QStringLiteral("height"), p.height},
                                {QStringLiteral("audioOnly"), p.audioOnly}, {QStringLiteral("vertical"), p.vertical}});
    return list;
}

QStringList Editor::projectPresets() const
{
    return ProjectSettings::presetIds();
}

bool Editor::whisperAvailable() const
{
    return m_transcriber->available();
}

QString Editor::whisperEngine() const
{
    return m_transcriber->engine();
}

bool Editor::ollamaAvailable() const
{
    return AiTools::ollamaAvailable();
}

QStringList Editor::sequencePath() const
{
    QStringList p{tr("Main timeline")};
    for (const QString &id : m_sequenceStack)
        for (const Sequence &s : m_doc.sequences)
            if (s.id == id)
                p << s.name;
    return p;
}

int Editor::playhead() const
{
    return m_player->timelinePosition();
}

// ------------------------------------------------------------------ project

struct TemplateDef {
    const char *id;
    const char *name;
    const char *description;
    const char *preset;
    const char *tracks; // display order, comma separated: V/A/S
};
static const TemplateDef Templates[] = {
    {"youtube", QT_TRANSLATE_NOOP("Editor", "YouTube video"), QT_TRANSLATE_NOOP("Editor", "1080p, intro title, music and voice tracks"), "1080p30", "V3,V2,V1,A1,A2"},
    {"shorts", QT_TRANSLATE_NOOP("Editor", "Shorts / Reels / TikTok"), QT_TRANSLATE_NOOP("Editor", "Vertical 9:16 with a subtitle track"), "vertical1080", "S,V2,V1,A1,A2"},
    {"podcast", QT_TRANSLATE_NOOP("Editor", "Video podcast"), QT_TRANSLATE_NOOP("Editor", "Two cameras, three microphones"), "1080p30", "V2,V1,A1,A2,A3"},
    {"film4k", QT_TRANSLATE_NOOP("Editor", "4K film"), QT_TRANSLATE_NOOP("Editor", "UHD with proxies, four audio stems"), "4k30", "V3,V2,V1,A1,A2,A3,A4"},
    {"slideshow", QT_TRANSLATE_NOOP("Editor", "Photo slideshow"), QT_TRANSLATE_NOOP("Editor", "Images with dissolves and music"), "1080p30", "V1,A1"},
};

QVariantList Editor::templates() const
{
    QVariantList list;
    for (const TemplateDef &t : Templates)
        list.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(t.id)},
                                {QStringLiteral("name"), tr(t.name)},
                                {QStringLiteral("description"), tr(t.description)},
                                {QStringLiteral("preset"), QString::fromLatin1(t.preset)}});
    return list;
}

void Editor::loadProject(const ProjectData &data, const QString &path)
{
    m_player->pause();
    m_undo.clear();
    const bool newProfile = !(data.settings == m_settings) || !m_player->duration();
    m_settings = data.settings;
    m_media = data.media;
    m_doc = data.doc;
    m_sequenceStack.clear();
    m_selection.clear();
    MediaRegistry::set(m_media, m_settings);
    if (newProfile || true)
        m_player->setProfile(m_settings);
    m_mediaBin->reset();
    setPath(path);
    refresh();
    rebuild();
    m_player->seek(0);
    m_undo.setClean();
    setDirty(false);
    Q_EMIT settingsChanged();
    Q_EMIT selectionChanged();
    Q_EMIT sequenceChanged();
    Q_EMIT historyChanged();
    QStringList missing;
    for (const MediaItem &m : m_media)
        if (!QFileInfo::exists(m.path))
            missing << m.name;
    if (!missing.isEmpty())
        setStatus(tr("Missing media: %1").arg(missing.join(QStringLiteral(", "))));
    else if (!data.warnings.isEmpty())
        setStatus(data.warnings.join(QLatin1Char(' ')));
}

void Editor::newProject(const QString &preset)
{
    ProjectData d;
    d.settings = ProjectSettings::preset(preset);
    d.doc.timeline = Timeline::defaultTimeline();
    loadProject(d, QString());
}

void Editor::newFromTemplate(const QString &templateId)
{
    for (const TemplateDef &t : Templates) {
        if (templateId != QLatin1String(t.id))
            continue;
        ProjectData d;
        d.settings = ProjectSettings::preset(QString::fromLatin1(t.preset));
        int v = 0, a = 0;
        const QStringList kinds = QString::fromLatin1(t.tracks).split(QLatin1Char(','));
        for (const QString &k : kinds) {
            Track tr;
            tr.id = newId(QStringLiteral("t"));
            if (k.startsWith(QLatin1Char('S'))) {
                tr.kind = TrackKind::Subtitle;
                tr.name = QStringLiteral("S1");
            } else if (k.startsWith(QLatin1Char('V'))) {
                tr.kind = TrackKind::Video;
                tr.name = k;
                ++v;
            } else {
                tr.kind = TrackKind::Audio;
                tr.name = k;
                ++a;
            }
            d.doc.timeline.tracks << tr;
        }
        loadProject(d, QString());
        if (templateId == QLatin1String("youtube"))
            addTitle(tr("My video"), QStringLiteral("center"));
        if (templateId == QLatin1String("film4k"))
            setAutoProxy(true);
        m_undo.clear();
        m_undo.setClean();
        return;
    }
    newProject();
}

bool Editor::openProject(const QUrl &url)
{
    return openPath(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

bool Editor::openPath(const QString &path)
{
    ProjectData data;
    QString error;
    if (!ProjectIO::load(path, &data, &error)) {
        setStatus(tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), error));
        Q_EMIT this->error(error);
        return false;
    }
    loadProject(data, QFileInfo(path).absoluteFilePath());
    addRecent(m_path);
    return true;
}

bool Editor::saveProject()
{
    if (m_path.isEmpty())
        return false;
    QString error;
    if (!ProjectIO::save(m_path, projectData(), &error)) {
        setStatus(tr("Could not save: %1").arg(error));
        Q_EMIT this->error(error);
        return false;
    }
    m_undo.setClean();
    setDirty(false);
    addRecent(m_path);
    QFile::remove(autosavePath());
    QFile::remove(autosavePath() + QStringLiteral(".json"));
    writePoster();
    setStatus(tr("Saved %1").arg(QFileInfo(m_path).fileName()));
    return true;
}

static QString posterPath(const QString &projectPath)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/posters");
    return dir + QLatin1Char('/')
        + QString::fromLatin1(QCryptographicHash::hash(QFileInfo(projectPath).absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(16))
        + QStringLiteral(".jpg");
}

// The frame on screen when the project is saved becomes its card picture
// on the start screen
void Editor::writePoster()
{
    if (m_path.isEmpty())
        return;
    const QImage frame = m_player->currentImage();
    if (frame.isNull() || frame.width() < 8)
        return;
    const QString path = posterPath(m_path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    frame.scaledToWidth(std::min(480, frame.width()), Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32).save(path, "JPG", 85);
}

QVariantMap Editor::projectInfo(const QString &path) const
{
    const QFileInfo fi(path);
    const QString poster = posterPath(path);
    return {{QStringLiteral("name"), fi.completeBaseName()},
            {QStringLiteral("folder"), QDir::toNativeSeparators(fi.absolutePath()).replace(QDir::homePath(), QStringLiteral("~"))},
            {QStringLiteral("modified"), fi.exists() ? QLocale().toString(fi.lastModified(), QLocale::ShortFormat) : QString()},
            {QStringLiteral("exists"), fi.exists()},
            {QStringLiteral("poster"), QFileInfo::exists(poster) ? QUrl::fromLocalFile(poster).toString() : QString()}};
}

bool Editor::saveProjectAs(const QUrl &url)
{
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QLatin1String(".mlt"), Qt::CaseInsensitive))
        path += QStringLiteral(".mlt");
    const QString oldAutosave = autosavePath();
    setPath(path);
    QFile::remove(oldAutosave);
    QFile::remove(oldAutosave + QStringLiteral(".json"));
    return saveProject();
}

void Editor::addRecent(const QString &path)
{
    m_recent.removeAll(path);
    m_recent.prepend(path);
    while (m_recent.size() > 12)
        m_recent.removeLast();
    QSettings().setValue(QStringLiteral("recentProjects"), m_recent);
    Q_EMIT recentChanged();
}

void Editor::clearRecent()
{
    m_recent.clear();
    QSettings().setValue(QStringLiteral("recentProjects"), m_recent);
    Q_EMIT recentChanged();
}

static QString autosaveDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/autosave");
    QDir().mkpath(dir);
    return dir;
}

QString Editor::autosavePath() const
{
    const QString key = m_path.isEmpty() ? QStringLiteral("untitled-") + sessionId() : m_path;
    return QDir(autosaveDir()).filePath(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(16)) + QStringLiteral(".mlt"));
}

void Editor::autosave()
{
    if (!m_dirty)
        return;
    const QString path = autosavePath();
    if (ProjectIO::save(path, projectData())) {
        QFile meta(path + QStringLiteral(".json"));
        if (meta.open(QIODevice::WriteOnly))
            meta.write(QJsonDocument(QJsonObject{{QStringLiteral("project"), m_path},
                                                 {QStringLiteral("time"), QDateTime::currentDateTime().toString(Qt::ISODate)}})
                           .toJson());
    }
}

void Editor::checkRecovery()
{
    QDir dir(autosaveDir());
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.mlt")}, QDir::Files, QDir::Time);
    m_recoveryPath = files.isEmpty() ? QString() : files.first().absoluteFilePath();
    Q_EMIT recoveryChanged();
}

void Editor::recover()
{
    if (m_recoveryPath.isEmpty())
        return;
    ProjectData data;
    QString error;
    const QString file = m_recoveryPath;
    if (!ProjectIO::load(file, &data, &error)) {
        setStatus(tr("The recovery file is damaged: %1").arg(error));
        discardRecovery();
        return;
    }
    QFile meta(file + QStringLiteral(".json"));
    QString original;
    if (meta.open(QIODevice::ReadOnly))
        original = QJsonDocument::fromJson(meta.readAll()).object().value(QStringLiteral("project")).toString();
    loadProject(data, original);
    setDirty(true);
    QFile::remove(file);
    QFile::remove(file + QStringLiteral(".json"));
    m_recoveryPath.clear();
    Q_EMIT recoveryChanged();
    autosave();
    setStatus(tr("Project recovered. Save it to keep the changes."));
}

void Editor::discardRecovery()
{
    QDir dir(autosaveDir());
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files))
        if (fi.absoluteFilePath() != autosavePath() && fi.absoluteFilePath() != autosavePath() + QStringLiteral(".json"))
            QFile::remove(fi.absoluteFilePath());
    m_recoveryPath.clear();
    Q_EMIT recoveryChanged();
}

void Editor::cleanExit()
{
    QFile::remove(autosavePath());
    QFile::remove(autosavePath() + QStringLiteral(".json"));
}

void Editor::setProjectSettings(int width, int height, int fpsNum, int fpsDen)
{
    ProjectSettings s = m_settings;
    s.width = std::clamp(width - width % 2, 16, 8192);
    s.height = std::clamp(height - height % 2, 16, 8192);
    s.fpsNum = std::max(1, fpsNum);
    s.fpsDen = std::max(1, fpsDen);
    if (s == m_settings)
        return;
    // Keep timing in seconds: frame positions scale with the rate
    const double ratio = s.fps() / m_settings.fps();
    if (std::abs(ratio - 1.0) > 1e-6) {
        const double newFps = s.fps();
        auto scale = [ratio, newFps](Timeline &tl) {
            for (Track &t : tl.tracks) {
                for (Clip &c : t.clips) {
                    if (Captions::isCaption(c))
                        c.genParams[QStringLiteral("fps")] = newFps; // words are in ms: only the frame grid changes
                    const int dur = std::max(1, int(std::lround(c.duration() * ratio)));
                    c.position = int(std::lround(c.position * ratio));
                    c.in = int(std::lround(c.in * ratio));
                    c.out = c.in + dur - 1;
                    c.fadeIn = int(std::lround(c.fadeIn * ratio));
                    c.fadeOut = int(std::lround(c.fadeOut * ratio));
                    c.transitionIn = int(std::lround(c.transitionIn * ratio));
                    for (Effect &e : c.effects)
                        for (Keyframe &k : e.keyframes)
                            k.frame = int(std::lround(k.frame * ratio));
                }
                Ops::normalizeTrack(t);
            }
            for (Marker &m : tl.markers)
                m.frame = int(std::lround(m.frame * ratio));
        };
        scale(m_doc.timeline);
        for (Sequence &seq : m_doc.sequences)
            scale(seq.timeline);
        for (MediaItem &m : m_media)
            if (m.kind != QLatin1String("image"))
                m.length = int(std::floor(m.length * ratio));
    }
    m_settings = s;
    m_undo.clear();
    m_player->setProfile(m_settings);
    MediaRegistry::set(m_media, m_settings);
    refresh();
    rebuild();
    setDirty(true);
    Q_EMIT settingsChanged();
}

void Editor::applyProjectPreset(const QString &preset)
{
    const ProjectSettings p = ProjectSettings::preset(preset);
    setProjectSettings(p.width, p.height, p.fpsNum, p.fpsDen);
}

void Editor::importUrls(const QList<QUrl> &urls, int track, int position)
{
    QStringList paths;
    for (const QUrl &u : urls)
        paths << (u.isLocalFile() ? u.toLocalFile() : u.toString());
    importPaths(paths, track, position);
}

void Editor::importPaths(const QStringList &paths, int track, int position)
{
    QStringList media;
    for (const QString &p : paths) {
        if (p.endsWith(QLatin1String(".mlt"), Qt::CaseInsensitive)) {
            openPath(p);
            return;
        }
        if (p.endsWith(QLatin1String(".srt"), Qt::CaseInsensitive)) {
            importSubtitles(QUrl::fromLocalFile(p));
            continue;
        }
        if (QFileInfo(p).isFile())
            media << p;
    }
    if (media.isEmpty())
        return;
    auto next = std::make_shared<int>(position);
    m_mediaBin->importFiles(media, [this, track, next](const MediaItem &m) {
        if (track == -2) {
            appendMedia(m.id);
        } else if (track >= 0) {
            const QString id = addMediaToTimeline(m.id, track, std::max(0, *next));
            if (const Clip *c = currentTimeline().clip(id))
                *next = c->end();
        }
    });
}

// ------------------------------------------------------------------ history / selection

void Editor::undo()
{
    m_undo.undo();
}

void Editor::redo()
{
    m_undo.redo();
}

void Editor::setHistoryIndex(int index)
{
    m_undo.setIndex(std::clamp(index, 0, m_undo.count()));
}

void Editor::select(const QString &clipId, bool add)
{
    QStringList sel = add ? m_selection : QStringList();
    if (!clipId.isEmpty()) {
        if (add && sel.contains(clipId))
            sel.removeAll(clipId);
        else if (!sel.contains(clipId))
            sel << clipId;
    }
    if (sel == m_selection)
        return;
    m_selection = sel;
    Q_EMIT selectionChanged();
    Q_EMIT selectedClipChanged();
}

void Editor::selectAll()
{
    QStringList sel;
    for (const Track &t : currentTimeline().tracks)
        for (const Clip &c : t.clips)
            sel << c.id;
    m_selection = sel;
    Q_EMIT selectionChanged();
    Q_EMIT selectedClipChanged();
}

void Editor::clearSelection()
{
    select(QString());
}

QVariantMap Editor::selectedClip() const
{
    if (m_selection.size() != 1)
        return {};
    return clipInfo(m_selection.first());
}

QVariantMap Editor::clipToMap(const Clip &c, int track) const
{
    const Track &t = currentTimeline().tracks.at(track);
    QVariantMap m{{QStringLiteral("id"), c.id},
                  {QStringLiteral("kind"), c.kind},
                  {QStringLiteral("name"), c.name},
                  {QStringLiteral("mediaId"), c.mediaId},
                  {QStringLiteral("track"), track},
                  {QStringLiteral("trackKind"), t.isAudio() ? QStringLiteral("audio") : t.kind == TrackKind::Subtitle ? QStringLiteral("subtitle") : QStringLiteral("video")},
                  {QStringLiteral("position"), c.position},
                  {QStringLiteral("in"), c.in},
                  {QStringLiteral("out"), c.out},
                  {QStringLiteral("duration"), c.duration()},
                  {QStringLiteral("speed"), c.speed},
                  {QStringLiteral("fadeIn"), c.fadeIn},
                  {QStringLiteral("fadeOut"), c.fadeOut},
                  {QStringLiteral("transitionIn"), c.transitionIn},
                  {QStringLiteral("transition"), c.transition},
                  {QStringLiteral("audioMuted"), c.audioMuted},
                  {QStringLiteral("linked"), !c.linkId.isEmpty()},
                  {QStringLiteral("freezeFrame"), c.freezeFrame},
                  {QStringLiteral("color"), c.genParams.value(QStringLiteral("color"))}};
    bool hasAudio = false, hasVideo = c.kind != QLatin1String("media");
    if (const MediaItem *mi = mediaItem(c.mediaId)) {
        m.insert(QStringLiteral("mediaName"), mi->name);
        m.insert(QStringLiteral("mediaKind"), mi->kind);
        m.insert(QStringLiteral("mediaLength"), mi->length);
        if (c.name.isEmpty())
            m.insert(QStringLiteral("name"), mi->name);
        hasAudio = mi->hasAudio && !c.audioMuted;
        hasVideo = mi->hasVideo;
    }
    if (Captions::isCaption(c)) {
        // words in timeline frames/ms for the caption inspector
        const double origin = Captions::originMs(c, fps());
        QVariantList words;
        for (const Captions::Word &w : Captions::clipWords(c))
            words.append(QVariantMap{{QStringLiteral("text"), w.text},
                                     {QStringLiteral("startMs"), qint64(std::llround(origin + w.startMs))},
                                     {QStringLiteral("endMs"), qint64(std::llround(origin + w.endMs))},
                                     {QStringLiteral("emphasis"), w.emphasis},
                                     {QStringLiteral("visible"), origin + w.endMs > c.position * 1000.0 / fps() && origin + w.startMs < c.end() * 1000.0 / fps()}});
        m.insert(QStringLiteral("words"), words);
        m.insert(QStringLiteral("speaker"), Captions::clipSpeaker(c));
        m.insert(QStringLiteral("text"), Captions::joinWords(Captions::clipWords(c)));
    }
    m.insert(QStringLiteral("hasAudio"), hasAudio && !t.isAudio() ? true : (hasAudio || t.isAudio()));
    m.insert(QStringLiteral("hasVideo"), hasVideo && !t.isAudio());
    QVariantList ramp;
    for (const Keyframe &k : c.speedRamp)
        ramp.append(QVariantMap{{QStringLiteral("frame"), k.frame - c.in}, {QStringLiteral("speed"), k.values.value(QStringLiteral("speed"))}});
    m.insert(QStringLiteral("speedRamp"), ramp);
    QVariantList effects;
    for (int i = 0; i < c.effects.size(); ++i) {
        const Effect &e = c.effects.at(i);
        const EffectDef *def = EffectRegistry::find(e.type);
        QVariantList kfs;
        for (const Keyframe &k : e.keyframes)
            kfs.append(QVariantMap{{QStringLiteral("frame"), k.frame}, {QStringLiteral("values"), k.values}});
        effects.append(QVariantMap{{QStringLiteral("index"), i},
                                   {QStringLiteral("type"), e.type},
                                   {QStringLiteral("name"), def ? def->name : e.type},
                                   {QStringLiteral("category"), def ? def->category : QString()},
                                   {QStringLiteral("audio"), def && !def->video},
                                   {QStringLiteral("enabled"), e.enabled},
                                   {QStringLiteral("params"), e.params},
                                   {QStringLiteral("keyframes"), kfs},
                                   {QStringLiteral("animated"), !e.keyframes.isEmpty()}});
    }
    m.insert(QStringLiteral("effects"), effects);
    return m;
}

QVariantMap Editor::clipInfo(const QString &clipId) const
{
    const auto [t, c] = currentTimeline().findClip(clipId);
    if (t < 0)
        return {};
    return clipToMap(currentTimeline().tracks.at(t).clips.at(c), t);
}

QString Editor::clipAt(int track, int frame) const
{
    if (track < 0 || track >= currentTimeline().tracks.size())
        return {};
    for (const Clip &c : currentTimeline().tracks.at(track).clips)
        if (c.position <= frame && c.end() > frame)
            return c.id;
    return {};
}

// ------------------------------------------------------------------ edit page

int Editor::defaultTrack(TrackKind kind) const
{
    const QList<Track> &tracks = currentTimeline().tracks;
    if (kind == TrackKind::Video) {
        for (int i = tracks.size() - 1; i >= 0; --i)
            if (tracks.at(i).isVideo() && !tracks.at(i).locked)
                return i;
    }
    for (int i = 0; i < tracks.size(); ++i)
        if (tracks.at(i).kind == kind && !tracks.at(i).locked)
            return i;
    return -1;
}

QString Editor::addMediaToTimeline(const QString &mediaId, int track, int position, int in, int out, bool ripple)
{
    const MediaItem *m = mediaItem(mediaId);
    if (!m)
        return {};
    if (track < 0)
        track = defaultTrack(m->hasVideo ? TrackKind::Video : TrackKind::Audio);
    if (track < 0) {
        addTrack(m->hasVideo ? QStringLiteral("video") : QStringLiteral("audio"));
        track = defaultTrack(m->hasVideo ? TrackKind::Video : TrackKind::Audio);
    }
    Clip c;
    c.id = newId(QStringLiteral("c"));
    c.mediaId = mediaId;
    c.position = std::max(0, position);
    const int length = m->kind == QLatin1String("image") ? int(std::lround(fps() * 5)) : m->length;
    c.in = in >= 0 ? in : 0;
    c.out = out >= 0 ? out : length - 1;
    if (m->kind != QLatin1String("image"))
        c.out = std::min(c.out, m->length - 1);
    if (c.out < c.in)
        return {};
    if (currentTimeline().tracks.value(track).isAudio() && !m->hasAudio) {
        setStatus(tr("%1 has no sound").arg(m->name));
        return {};
    }
    if (currentTimeline().tracks.value(track).kind == TrackKind::Subtitle)
        track = defaultTrack(TrackKind::Video);
    if (!modify(tr("Add %1").arg(m->name), [&](Timeline &tl, Document &, QString *err) { return Ops::insertClip(tl, track, c, ripple, err); }))
        return {};
    select(c.id);
    return c.id;
}

QString Editor::appendMedia(const QString &mediaId)
{
    const MediaItem *m = mediaItem(mediaId);
    if (!m)
        return {};
    const int track = defaultTrack(m->hasVideo ? TrackKind::Video : TrackKind::Audio);
    int pos = 0;
    if (track >= 0)
        for (const Clip &c : currentTimeline().tracks.at(track).clips)
            pos = std::max(pos, c.end());
    return addMediaToTimeline(mediaId, track, pos);
}

bool Editor::moveClip(const QString &clipId, int track, int position)
{
    return modify(tr("Move clip"), [&](Timeline &tl, Document &, QString *err) { return Ops::moveClip(tl, clipId, track, position, false, err); });
}

bool Editor::trimClip(const QString &clipId, bool left, int frame, bool ripple)
{
    const auto len = [this](const Clip &c) { return sourceLength(c); };
    return modify(ripple ? tr("Ripple trim") : tr("Trim clip"), [&](Timeline &tl, Document &, QString *err) {
        return left ? Ops::trimIn(tl, clipId, frame, len, ripple, err) : Ops::trimOut(tl, clipId, frame, len, ripple, err);
    });
}

bool Editor::rollEdit(const QString &clipId, int boundary)
{
    const auto len = [this](const Clip &c) { return sourceLength(c); };
    return modify(tr("Roll edit"), [&](Timeline &tl, Document &, QString *err) { return Ops::roll(tl, clipId, boundary, len, err); });
}

bool Editor::slipClip(const QString &clipId, int delta)
{
    const auto len = [this](const Clip &c) { return sourceLength(c); };
    return modify(tr("Slip clip"), [&](Timeline &tl, Document &, QString *err) { return Ops::slip(tl, clipId, delta, len, err); });
}

bool Editor::slideClip(const QString &clipId, int delta)
{
    const auto len = [this](const Clip &c) { return sourceLength(c); };
    return modify(tr("Slide clip"), [&](Timeline &tl, Document &, QString *err) { return Ops::slide(tl, clipId, delta, len, err); });
}

bool Editor::splitClip(const QString &clipId, int frame)
{
    return modify(tr("Split clip"), [&](Timeline &tl, Document &, QString *) { return !Ops::split(tl, clipId, frame).isEmpty(); });
}

int Editor::splitAtPlayhead()
{
    const int frame = playhead();
    QStringList ids;
    for (const QString &id : std::as_const(m_selection))
        if (const Clip *c = currentTimeline().clip(id); c && c->position < frame && c->end() > frame)
            ids << id;
    int n = 0;
    modify(tr("Split at playhead"), [&](Timeline &tl, Document &, QString *) {
        n = Ops::splitAt(tl, frame, ids);
        return n > 0;
    });
    return n;
}

bool Editor::deleteSelected(bool ripple)
{
    if (m_selection.isEmpty())
        return false;
    const QStringList ids = m_selection;
    return modify(ripple ? tr("Ripple delete") : tr("Delete"), [&](Timeline &tl, Document &, QString *) {
        bool any = false;
        for (const QString &id : ids)
            if (tl.clip(id))
                any |= Ops::removeClip(tl, id, ripple);
        return any;
    });
}

bool Editor::removeGap(int track, int frame)
{
    return modify(tr("Close gap"), [&](Timeline &tl, Document &, QString *) { return Ops::removeGap(tl, track, frame); });
}

bool Editor::setTransition(const QString &clipId, int length, const QString &type)
{
    return modify(length > 0 ? tr("Transition") : tr("Remove transition"),
                  [&](Timeline &tl, Document &, QString *err) { return Ops::setTransition(tl, clipId, length, type, err); });
}

bool Editor::addTransitionAtPlayhead(const QString &type, int length)
{
    QString target;
    if (m_selection.size() == 1)
        target = m_selection.first();
    if (target.isEmpty()) {
        const int frame = playhead();
        int best = 1 << 30;
        for (const Track &t : currentTimeline().tracks) {
            if (t.locked || t.kind == TrackKind::Subtitle)
                continue;
            for (int i = 1; i < t.clips.size(); ++i) {
                const int d = std::abs(t.clips.at(i).position + t.clips.at(i).transitionIn - frame);
                if (d < best && d <= 15) {
                    best = d;
                    target = t.clips.at(i).id;
                }
            }
        }
    }
    if (target.isEmpty()) {
        setStatus(tr("Put the playhead on a cut between two clips"));
        return false;
    }
    return setTransition(target, length, type);
}

QString Editor::freezeFrame(int length)
{
    const int frame = playhead();
    if (length <= 0)
        length = framesFromSeconds(2);
    QString target;
    for (const QString &id : std::as_const(m_selection))
        if (const Clip *c = currentTimeline().clip(id); c && c->position <= frame && c->end() > frame)
            target = id;
    if (target.isEmpty()) {
        for (const Track &t : currentTimeline().tracks) {
            if (!t.isVideo() || t.hidden || t.locked)
                continue;
            for (const Clip &c : t.clips)
                if (c.kind == QLatin1String("media") && c.position <= frame && c.end() > frame) {
                    target = c.id;
                    break;
                }
            if (!target.isEmpty())
                break;
        }
    }
    QString id;
    modify(tr("Freeze frame"), [&](Timeline &tl, Document &, QString *) {
        id = Ops::freezeFrame(tl, target, frame, length);
        return !id.isEmpty();
    });
    if (id.isEmpty())
        setStatus(tr("Put the playhead over a video clip"));
    else
        select(id);
    return id;
}

QString Editor::detachAudio(const QString &clipId)
{
    const Clip *c = currentTimeline().clip(clipId);
    if (!c)
        return {};
    const MediaItem *m = mediaItem(c->mediaId);
    if (!m || !m->hasAudio) {
        setStatus(tr("This clip has no sound"));
        return {};
    }
    const int start = c->position, end = c->end();
    int target = -1;
    const QList<Track> &tracks = currentTimeline().tracks;
    for (int i = 0; i < tracks.size() && target < 0; ++i) {
        if (!tracks.at(i).isAudio() || tracks.at(i).locked)
            continue;
        bool free = true;
        for (const Clip &o : tracks.at(i).clips)
            free &= o.end() <= start || o.position >= end;
        if (free)
            target = i;
    }
    QString id;
    modify(tr("Detach audio"), [&](Timeline &tl, Document &, QString *err) {
        int t = target;
        if (t < 0) {
            Track a;
            a.id = newId(QStringLiteral("t"));
            a.kind = TrackKind::Audio;
            a.name = tl.nextTrackName(TrackKind::Audio);
            tl.tracks.append(a);
            t = tl.tracks.size() - 1;
        }
        id = Ops::detachAudio(tl, clipId, t, err);
        return !id.isEmpty();
    });
    return id;
}

static Effect titleEffect(const QString &text, const QString &style, double scale)
{
    Effect e = EffectRegistry::create(QStringLiteral("text"));
    e.params[QStringLiteral("text")] = text;
    auto set = [&](const char *k, const QVariant &v) { e.params[QString::fromLatin1(k)] = v; };
    if (style == QLatin1String("lower-third")) {
        set("size", 56 * scale);
        set("halign", QStringLiteral("left"));
        set("valign", QStringLiteral("middle"));
        set("x", 6.0);
        set("y", 72.0);
        set("w", 55.0);
        set("h", 14.0);
        set("background", QStringLiteral("#b0000000"));
    } else if (style == QLatin1String("subtitle")) {
        set("size", 52 * scale);
        set("weight", QStringLiteral("normal"));
        set("outline", 3.0);
        set("halign", QStringLiteral("center"));
        set("valign", QStringLiteral("bottom"));
        set("x", 8.0);
        set("y", 74.0);
        set("w", 84.0);
        set("h", 18.0);
    } else if (style == QLatin1String("headline")) {
        set("size", 96 * scale);
        set("valign", QStringLiteral("top"));
        set("y", 8.0);
        set("h", 30.0);
    } else if (style == QLatin1String("credits")) {
        set("size", 60 * scale);
        set("weight", QStringLiteral("normal"));
    } else {
        set("size", 120 * scale);
    }
    return e;
}

QString Editor::addTitle(const QString &text, const QString &templateId, int track)
{
    const int frame = playhead();
    const int dur = framesFromSeconds(templateId == QLatin1String("subtitle") ? 2 : 4);
    Clip c;
    c.id = newId(QStringLiteral("c"));
    c.kind = QStringLiteral("title");
    c.position = frame;
    c.in = 0;
    c.out = dur - 1;
    c.fadeIn = c.fadeOut = framesFromSeconds(0.4);
    c.effects << titleEffect(text.isEmpty() ? tr("Title") : text, templateId, m_settings.height / 1080.0);
    bool ok = modify(tr("Add title"), [&](Timeline &tl, Document &, QString *err) {
        int t = track;
        if (t < 0) {
            // the top-most video track that is free over the title's range
            for (int i = 0; i < tl.tracks.size() && t < 0; ++i) {
                const Track &tr2 = tl.tracks.at(i);
                if (!tr2.isVideo() || tr2.locked)
                    continue;
                bool free = true;
                for (const Clip &o : tr2.clips)
                    free &= o.end() <= c.position || o.position >= c.end();
                if (free)
                    t = i;
            }
            if (t < 0) {
                Track v;
                v.id = newId(QStringLiteral("t"));
                v.kind = TrackKind::Video;
                v.name = tl.nextTrackName(TrackKind::Video);
                int at = 0;
                while (at < tl.tracks.size() && tl.tracks.at(at).kind == TrackKind::Subtitle)
                    ++at;
                tl.tracks.insert(at, v);
                t = at;
            }
        }
        return Ops::insertClip(tl, t, c, false, err);
    });
    if (!ok)
        return {};
    select(c.id);
    return c.id;
}

QString Editor::addColorClip(const QString &color, int track)
{
    Clip c;
    c.id = newId(QStringLiteral("c"));
    c.kind = QStringLiteral("color");
    c.position = playhead();
    c.out = framesFromSeconds(4) - 1;
    c.genParams.insert(QStringLiteral("color"), color);
    if (track < 0)
        track = defaultTrack(TrackKind::Video);
    if (!modify(tr("Add color"), [&](Timeline &tl, Document &, QString *err) { return Ops::insertClip(tl, track, c, false, err); }))
        return {};
    select(c.id);
    return c.id;
}

bool Editor::createCompound()
{
    if (m_selection.isEmpty())
        return false;
    const QStringList ids = m_selection;
    QString compoundId;
    const bool ok = modify(tr("Make compound clip"), [&](Timeline &tl, Document &doc, QString *err) {
        int minPos = 1 << 30, maxEnd = 0, topTrack = -1;
        for (const QString &id : ids) {
            const auto [t, c] = tl.findClip(id);
            if (t < 0)
                continue;
            if (tl.tracks.at(t).locked) {
                *err = tr("The track is locked");
                return false;
            }
            const Clip &cl = tl.tracks.at(t).clips.at(c);
            minPos = std::min(minPos, cl.position);
            maxEnd = std::max(maxEnd, cl.end());
            if (tl.tracks.at(t).isVideo() && (topTrack < 0 || t < topTrack))
                topTrack = t;
        }
        if (topTrack < 0) {
            *err = tr("Select at least one video clip");
            return false;
        }
        Sequence seq;
        seq.id = newId(QStringLiteral("s"));
        seq.name = tr("Compound %1").arg(doc.sequences.size() + 1);
        for (const Track &t : tl.tracks) {
            Track copy = t;
            copy.id = newId(QStringLiteral("t"));
            copy.clips.clear();
            copy.locked = false;
            for (const Clip &c : t.clips) {
                if (!ids.contains(c.id))
                    continue;
                Clip moved = c;
                moved.position -= minPos;
                moved.linkId.clear();
                copy.clips << moved;
            }
            Ops::normalizeTrack(copy);
            seq.timeline.tracks << copy;
        }
        for (const QString &id : ids)
            if (tl.clip(id))
                Ops::removeClip(tl, id, false, false);
        Clip comp;
        comp.id = newId(QStringLiteral("c"));
        comp.kind = QStringLiteral("compound");
        comp.mediaId = seq.id;
        comp.name = seq.name;
        comp.position = minPos;
        comp.in = 0;
        comp.out = maxEnd - minPos - 1;
        compoundId = comp.id;
        if (!Ops::insertClip(tl, topTrack, comp, false, err))
            return false;
        doc.sequences.append(seq); // last: tl may live inside doc.sequences
        return true;
    });
    if (ok)
        select(compoundId);
    return ok;
}

bool Editor::openCompound(const QString &clipId)
{
    const Clip *c = currentTimeline().clip(clipId);
    if (!c || c->kind != QLatin1String("compound"))
        return false;
    m_player->pause();
    m_sequenceStack << c->mediaId;
    m_selection.clear();
    Q_EMIT selectionChanged();
    Q_EMIT sequenceChanged();
    refresh();
    m_player->seek(0);
    return true;
}

void Editor::leaveCompound(int depth)
{
    if (depth >= m_sequenceStack.size())
        return;
    m_player->pause();
    // A nested timeline may have grown or shrunk: refit its compound clips
    while (m_sequenceStack.size() > depth)
        m_sequenceStack.removeLast();
    Document d = m_doc;
    auto refit = [&](Timeline &tl) {
        for (Track &t : tl.tracks) {
            for (Clip &c : t.clips) {
                if (c.kind != QLatin1String("compound"))
                    continue;
                for (const Sequence &s : d.sequences)
                    if (s.id == c.mediaId)
                        c.out = std::min(c.out, std::max(c.in, s.timeline.duration() - 1));
            }
            Ops::normalizeTrack(t);
        }
    };
    refit(d.timeline);
    if (!(d == m_doc))
        m_undo.push(new SnapshotCommand(this, m_doc, d, tr("Update compound clip"), -1, {}));
    m_selection.clear();
    Q_EMIT selectionChanged();
    Q_EMIT sequenceChanged();
    refresh();
}

void Editor::addTrack(const QString &kind)
{
    modify(tr("Add track"), [&](Timeline &tl, Document &, QString *) {
        Track t;
        t.id = newId(QStringLiteral("t"));
        if (kind == QLatin1String("audio")) {
            t.kind = TrackKind::Audio;
            t.name = tl.nextTrackName(TrackKind::Audio);
            tl.tracks.append(t);
        } else if (kind == QLatin1String("subtitle")) {
            for (const Track &o : tl.tracks)
                if (o.kind == TrackKind::Subtitle)
                    return false;
            t.kind = TrackKind::Subtitle;
            t.name = QStringLiteral("S1");
            tl.tracks.prepend(t);
        } else {
            t.kind = TrackKind::Video;
            t.name = tl.nextTrackName(TrackKind::Video);
            int at = 0;
            while (at < tl.tracks.size() && tl.tracks.at(at).kind == TrackKind::Subtitle)
                ++at;
            tl.tracks.insert(at, t);
        }
        return true;
    });
}

void Editor::removeTrack(int index)
{
    modify(tr("Remove track"), [&](Timeline &tl, Document &, QString *err) {
        if (index < 0 || index >= tl.tracks.size())
            return false;
        int sameKind = 0;
        for (const Track &t : tl.tracks)
            sameKind += t.kind == tl.tracks.at(index).kind;
        if (sameKind <= 1 && tl.tracks.at(index).kind != TrackKind::Subtitle) {
            *err = tr("The timeline needs at least one track of each kind");
            return false;
        }
        tl.tracks.removeAt(index);
        return true;
    });
}

void Editor::setTrackProperty(int index, const QString &name, const QVariant &value)
{
    const bool drag = name == QLatin1String("volume") || name == QLatin1String("pan");
    modify(tr("Track %1").arg(name), [&](Timeline &tl, Document &, QString *) {
        if (index < 0 || index >= tl.tracks.size())
            return false;
        Track &t = tl.tracks[index];
        if (name == QLatin1String("name"))
            t.name = value.toString();
        else if (name == QLatin1String("muted"))
            t.muted = value.toBool();
        else if (name == QLatin1String("hidden"))
            t.hidden = value.toBool();
        else if (name == QLatin1String("locked"))
            t.locked = value.toBool();
        else if (name == QLatin1String("solo"))
            t.solo = value.toBool();
        else if (name == QLatin1String("volume"))
            t.volumeDb = std::clamp(value.toDouble(), -60.0, 12.0);
        else if (name == QLatin1String("pan"))
            t.pan = std::clamp(value.toDouble(), -1.0, 1.0);
        else
            return false;
        return true;
    }, drag ? MergeTrack : -1, QStringLiteral("%1/%2").arg(index).arg(name));
}

void Editor::addMarker(int frame, const QString &name, const QString &color)
{
    modify(tr("Add marker"), [&](Timeline &tl, Document &, QString *) {
        for (const Marker &m : tl.markers)
            if (m.frame == frame)
                return false;
        tl.markers.append({frame, name.isEmpty() ? tr("Marker %1").arg(tl.markers.size() + 1) : name, color});
        std::sort(tl.markers.begin(), tl.markers.end(), [](const Marker &a, const Marker &b) { return a.frame < b.frame; });
        return true;
    });
}

void Editor::removeMarker(int index)
{
    modify(tr("Remove marker"), [&](Timeline &tl, Document &, QString *) {
        if (index < 0 || index >= tl.markers.size())
            return false;
        tl.markers.removeAt(index);
        return true;
    });
}

void Editor::renameMarker(int index, const QString &name)
{
    modify(tr("Rename marker"), [&](Timeline &tl, Document &, QString *) {
        if (index < 0 || index >= tl.markers.size())
            return false;
        tl.markers[index].name = name;
        return true;
    });
}

int Editor::nextEdit(int frame, bool forward) const
{
    QList<int> points{0, currentTimeline().duration()};
    for (const Track &t : currentTimeline().tracks)
        for (const Clip &c : t.clips)
            points << c.position << c.end();
    for (const Marker &m : currentTimeline().markers)
        points << m.frame;
    std::sort(points.begin(), points.end());
    if (forward) {
        for (int p : points)
            if (p > frame)
                return p;
        return frame;
    }
    for (auto it = points.crbegin(); it != points.crend(); ++it)
        if (*it < frame)
            return *it;
    return 0;
}

int Editor::snapFrame(int frame, int tolerance, const QString &ignoreClip) const
{
    QList<int> points{0, playhead()};
    for (const Track &t : currentTimeline().tracks)
        for (const Clip &c : t.clips)
            if (c.id != ignoreClip)
                points << c.position << c.end();
    for (const Marker &m : currentTimeline().markers)
        points << m.frame;
    int best = frame, bestD = tolerance + 1;
    for (int p : points) {
        const int d = std::abs(p - frame);
        if (d < bestD) {
            bestD = d;
            best = p;
        }
    }
    return best;
}

// ------------------------------------------------------------------ clip properties & effects

void Editor::setClipProperty(const QString &clipId, const QString &key, const QVariant &value)
{
    const bool drag = key == QLatin1String("fadeIn") || key == QLatin1String("fadeOut");
    modify(tr("Clip %1").arg(key), [&](Timeline &tl, Document &, QString *err) {
        const auto [ti, ci] = tl.findClip(clipId);
        if (ti < 0)
            return false;
        if (tl.tracks.at(ti).locked) {
            *err = tr("The track is locked");
            return false;
        }
        Clip &c = tl.tracks[ti].clips[ci];
        if (key == QLatin1String("name")) {
            c.name = value.toString();
        } else if (key == QLatin1String("fadeIn")) {
            c.fadeIn = std::clamp(value.toInt(), 0, c.duration());
        } else if (key == QLatin1String("fadeOut")) {
            c.fadeOut = std::clamp(value.toInt(), 0, c.duration());
        } else if (key == QLatin1String("transition")) {
            c.transition = value.toString();
        } else if (key == QLatin1String("audioMuted")) {
            c.audioMuted = value.toBool();
        } else if (key == QLatin1String("color")) {
            c.genParams.insert(QStringLiteral("color"), value.toString());
        } else if (key == QLatin1String("unlink")) {
            const QString link = c.linkId;
            for (Track &t : tl.tracks)
                for (Clip &o : t.clips)
                    if (!link.isEmpty() && o.linkId == link)
                        o.linkId.clear();
        } else if (key == QLatin1String("speed")) {
            const double s = value.toDouble();
            if (std::abs(s) < 0.05 || std::abs(s) > 20 || c.kind != QLatin1String("media")) {
                *err = tr("Speed must be between 0.05× and 20×");
                return false;
            }
            const double ratio = c.speed / s;
            c.in = int(std::lround(c.in * ratio));
            const int dur = std::max(1, int(std::lround(c.duration() * ratio)));
            c.out = c.in + dur - 1;
            c.speed = s;
            c.speedRamp.clear();
            const int limit = sourceLength(c);
            if (limit > 0 && c.out >= limit) {
                c.out = limit - 1;
                c.in = std::min(c.in, c.out);
            }
            for (Effect &e : c.effects)
                Ops::shiftKeyframes(e.keyframes, 0, c.duration(), e.params);
            Ops::normalizeTrack(tl.tracks[ti]);
        } else {
            return false;
        }
        return true;
    }, drag ? MergeClip : -1, clipId + key);
}

void Editor::addEffect(const QString &clipId, const QString &type)
{
    const EffectDef *def = EffectRegistry::find(type);
    if (!def)
        return;
    modify(tr("Add %1").arg(def->name), [&](Timeline &tl, Document &, QString *err) {
        Clip *c = tl.clip(clipId);
        if (!c)
            return false;
        if (!def->video && (c->kind == QLatin1String("title") || c->kind == QLatin1String("color"))) {
            *err = tr("This clip has no sound");
            return false;
        }
        c->effects.append(EffectRegistry::create(type));
        return true;
    });
}

void Editor::addEffectToSelection(const QString &type)
{
    if (m_selection.isEmpty()) {
        setStatus(tr("Select a clip first"));
        return;
    }
    const EffectDef *def = EffectRegistry::find(type);
    if (!def)
        return;
    const QStringList ids = m_selection;
    modify(tr("Add %1").arg(def->name), [&](Timeline &tl, Document &, QString *) {
        bool any = false;
        for (const QString &id : ids)
            if (Clip *c = tl.clip(id)) {
                c->effects.append(EffectRegistry::create(type));
                any = true;
            }
        return any;
    });
}

void Editor::removeEffect(const QString &clipId, int index)
{
    modify(tr("Remove effect"), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(clipId);
        if (!c || index < 0 || index >= c->effects.size())
            return false;
        c->effects.removeAt(index);
        return true;
    });
}

void Editor::moveEffect(const QString &clipId, int from, int to)
{
    modify(tr("Reorder effects"), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(clipId);
        if (!c || from < 0 || to < 0 || from >= c->effects.size() || to >= c->effects.size() || from == to)
            return false;
        c->effects.move(from, to);
        return true;
    });
}

void Editor::setEffectEnabled(const QString &clipId, int index, bool enabled)
{
    modify(enabled ? tr("Enable effect") : tr("Bypass effect"), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(clipId);
        if (!c || index < 0 || index >= c->effects.size())
            return false;
        c->effects[index].enabled = enabled;
        return true;
    });
}

static bool isAnimatable(const QString &type, const QString &key)
{
    if (const EffectDef *d = EffectRegistry::find(type))
        for (const ParamDef &p : d->params)
            if (p.id == key)
                return p.animatable;
    return false;
}

void Editor::setEffectParam(const QString &clipId, int index, const QString &key, const QVariant &value)
{
    const int frame = playhead();
    modify(tr("Change %1").arg(key), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(clipId);
        if (!c || index < 0 || index >= c->effects.size())
            return false;
        Effect &e = c->effects[index];
        if (!e.keyframes.isEmpty() && isAnimatable(e.type, key)) {
            const int rel = std::clamp(frame - c->position, 0, c->duration() - 1);
            QVariantMap values = Ops::valuesAt(e.keyframes, e.params, rel);
            values.insert(key, value);
            QVariantMap animated;
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                if (isAnimatable(e.type, it.key()))
                    animated.insert(it.key(), it.value());
            Ops::setKeyframe(e.keyframes, rel, animated);
        } else {
            e.params.insert(key, value);
        }
        return true;
    }, MergeParam, QStringLiteral("%1/%2/%3").arg(clipId).arg(index).arg(key));
}

QVariantMap Editor::effectValuesAt(const QString &clipId, int index, int timelineFrame) const
{
    const Clip *c = currentTimeline().clip(clipId);
    if (!c || index < 0 || index >= c->effects.size())
        return {};
    const Effect &e = c->effects.at(index);
    return Ops::valuesAt(e.keyframes, e.params, std::clamp(timelineFrame - c->position, 0, c->duration() - 1));
}

bool Editor::hasKeyframeAt(const QString &clipId, int index, int timelineFrame) const
{
    const Clip *c = currentTimeline().clip(clipId);
    if (!c || index < 0 || index >= c->effects.size())
        return false;
    const int rel = timelineFrame - c->position;
    for (const Keyframe &k : c->effects.at(index).keyframes)
        if (k.frame == rel)
            return true;
    return false;
}

void Editor::toggleKeyframe(const QString &clipId, int index, int timelineFrame)
{
    modify(tr("Keyframe"), [&](Timeline &tl, Document &, QString *err) {
        Clip *c = tl.clip(clipId);
        if (!c || index < 0 || index >= c->effects.size())
            return false;
        Effect &e = c->effects[index];
        const int rel = timelineFrame - c->position;
        if (rel < 0 || rel >= c->duration()) {
            *err = tr("Move the playhead over the clip");
            return false;
        }
        for (int i = 0; i < e.keyframes.size(); ++i) {
            if (e.keyframes.at(i).frame == rel) {
                const QVariantMap values = e.keyframes.at(i).values;
                e.keyframes.removeAt(i);
                if (e.keyframes.isEmpty())
                    e.params.insert(values); // keep the look when the last keyframe goes
                return true;
            }
        }
        QVariantMap animated;
        const QVariantMap now = Ops::valuesAt(e.keyframes, e.params, rel);
        for (auto it = now.cbegin(); it != now.cend(); ++it)
            if (isAnimatable(e.type, it.key()))
                animated.insert(it.key(), it.value());
        if (animated.isEmpty()) {
            *err = tr("This effect can't be animated");
            return false;
        }
        Ops::setKeyframe(e.keyframes, rel, animated);
        return true;
    });
}

void Editor::moveKeyframe(const QString &clipId, int index, int keyframe, int newRelFrame)
{
    modify(tr("Move keyframe"), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(clipId);
        if (!c || index < 0 || index >= c->effects.size())
            return false;
        QList<Keyframe> &kfs = c->effects[index].keyframes;
        if (keyframe < 0 || keyframe >= kfs.size())
            return false;
        newRelFrame = std::clamp(newRelFrame, 0, c->duration() - 1);
        for (int i = 0; i < kfs.size(); ++i)
            if (i != keyframe && kfs.at(i).frame == newRelFrame)
                return false;
        kfs[keyframe].frame = newRelFrame;
        std::sort(kfs.begin(), kfs.end(), [](const Keyframe &a, const Keyframe &b) { return a.frame < b.frame; });
        return true;
    }, MergeParam, QStringLiteral("kf/%1/%2/%3").arg(clipId).arg(index).arg(keyframe));
}

void Editor::setKeyframeValue(const QString &clipId, int index, int keyframe, const QString &key, const QVariant &value)
{
    modify(tr("Keyframe value"), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(clipId);
        if (!c || index < 0 || index >= c->effects.size())
            return false;
        QList<Keyframe> &kfs = c->effects[index].keyframes;
        if (keyframe < 0 || keyframe >= kfs.size())
            return false;
        kfs[keyframe].values.insert(key, value);
        return true;
    }, MergeParam, QStringLiteral("kfv/%1/%2/%3/%4").arg(clipId).arg(index).arg(keyframe).arg(key));
}

void Editor::removeKeyframe(const QString &clipId, int index, int keyframe)
{
    modify(tr("Remove keyframe"), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(clipId);
        if (!c || index < 0 || index >= c->effects.size())
            return false;
        Effect &e = c->effects[index];
        if (keyframe < 0 || keyframe >= e.keyframes.size())
            return false;
        const QVariantMap values = e.keyframes.at(keyframe).values;
        e.keyframes.removeAt(keyframe);
        if (e.keyframes.isEmpty())
            e.params.insert(values);
        return true;
    });
}

void Editor::setSpeedRamp(const QString &clipId, const QVariantList &points)
{
    modify(points.isEmpty() ? tr("Remove speed ramp") : tr("Speed ramp"), [&](Timeline &tl, Document &, QString *err) {
        Clip *c = tl.clip(clipId);
        if (!c || c->kind != QLatin1String("media")) {
            *err = tr("Speed ramps work on video and audio clips");
            return false;
        }
        QList<Keyframe> ramp;
        for (const QVariant &v : points) {
            const QVariantMap m = v.toMap();
            Ops::setKeyframe(ramp, c->in + m.value(QStringLiteral("frame")).toInt(),
                             {{QStringLiteral("speed"), std::clamp(m.value(QStringLiteral("speed")).toDouble(), 0.05, 20.0)}});
        }
        if (!ramp.isEmpty() && std::abs(c->speed - 1.0) > 1e-6) {
            // ramps are measured in source frames: drop the constant speed first
            c->in = int(std::lround(c->in * c->speed));
            c->out = c->in + c->duration() - 1;
            c->speed = 1.0;
        }
        c->speedRamp = ramp;
        return true;
    });
}

void Editor::applyPictureInPicture(const QString &corner)
{
    if (m_selection.size() != 1) {
        setStatus(tr("Select the clip to show small"));
        return;
    }
    const QString id = m_selection.first();
    modify(tr("Picture in picture"), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(id);
        if (!c)
            return false;
        Effect *tf = nullptr;
        for (Effect &e : c->effects)
            if (e.type == QLatin1String("transform"))
                tf = &e;
        if (!tf) {
            c->effects.append(EffectRegistry::create(QStringLiteral("transform")));
            tf = &c->effects.last();
        }
        tf->keyframes.clear();
        const double s = 0.32, W = m_settings.width, H = m_settings.height, margin = H * 0.04;
        const double dx = W / 2 - W * s / 2 - margin, dy = H / 2 - H * s / 2 - margin;
        tf->params[QStringLiteral("scale")] = s * 100;
        tf->params[QStringLiteral("x")] = corner.contains(QLatin1String("left")) ? -dx : dx;
        tf->params[QStringLiteral("y")] = corner.contains(QLatin1String("bottom")) ? dy : -dy;
        return true;
    });
}

// ------------------------------------------------------------------ color page

void Editor::copyGrade(const QString &clipId)
{
    const Clip *c = currentTimeline().clip(clipId);
    if (!c)
        return;
    m_grade.clear();
    for (const Effect &e : c->effects)
        if (ColorEffects.contains(e.type))
            m_grade << e;
    if (m_grade.isEmpty())
        setStatus(tr("This clip has no grade to copy"));
    else
        setStatus(tr("Grade copied"));
    Q_EMIT gradeChanged();
}

void Editor::pasteGrade()
{
    if (m_grade.isEmpty() || m_selection.isEmpty())
        return;
    const QStringList ids = m_selection;
    modify(tr("Paste grade"), [&](Timeline &tl, Document &, QString *) {
        bool any = false;
        for (const QString &id : ids) {
            Clip *c = tl.clip(id);
            if (!c || c->kind == QLatin1String("title"))
                continue;
            c->effects.erase(std::remove_if(c->effects.begin(), c->effects.end(), [](const Effect &e) { return ColorEffects.contains(e.type); }),
                             c->effects.end());
            c->effects.append(m_grade);
            any = true;
        }
        return any;
    });
}

void Editor::resetGrade(const QString &clipId)
{
    modify(tr("Reset grade"), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(clipId);
        if (!c)
            return false;
        const auto before = c->effects.size();
        c->effects.erase(std::remove_if(c->effects.begin(), c->effects.end(), [](const Effect &e) { return ColorEffects.contains(e.type); }),
                         c->effects.end());
        return c->effects.size() != before;
    });
}

void Editor::setWheel(const QString &clipId, const QString &range, double r, double g, double b)
{
    modify(tr("Color wheel"), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(clipId);
        if (!c)
            return false;
        Effect *w = nullptr;
        for (Effect &e : c->effects)
            if (e.type == QLatin1String("wheels"))
                w = &e;
        if (!w) {
            c->effects.append(EffectRegistry::create(QStringLiteral("wheels")));
            w = &c->effects.last();
        }
        w->params[range + QStringLiteral("_r")] = r;
        w->params[range + QStringLiteral("_g")] = g;
        w->params[range + QStringLiteral("_b")] = b;
        return true;
    }, MergeParam, clipId + QStringLiteral("/wheel/") + range);
}

void Editor::setWheelOffset(const QString &clipId, double r, double g, double b)
{
    modify(tr("Color wheel"), [&](Timeline &tl, Document &, QString *) {
        Clip *c = tl.clip(clipId);
        if (!c)
            return false;
        Effect *w = nullptr;
        for (Effect &e : c->effects)
            if (e.type == QLatin1String("wheels"))
                w = &e;
        if (!w) {
            c->effects.append(EffectRegistry::create(QStringLiteral("wheels")));
            w = &c->effects.last();
        }
        // y = gain * (x + lift * (1 - x)): slope s = gain * (1 - lift), offset d = gain * lift.
        // A new offset d' keeps the slope: gain' = s + d', lift' = d' / gain'.
        const double offsets[3] = {r, g, b};
        const char *channels[3] = {"r", "g", "b"};
        for (int i = 0; i < 3; ++i) {
            const QString lk = QStringLiteral("lift_") + QLatin1String(channels[i]);
            const QString gk = QStringLiteral("gain_") + QLatin1String(channels[i]);
            const double lift = w->params.value(lk, 0.0).toDouble();
            const double gain = w->params.value(gk, 1.0).toDouble();
            const double slope = std::max(0.05, gain * (1.0 - lift));
            const double newGain = std::clamp(slope + offsets[i], 0.05, 4.0);
            const double newLift = std::clamp((newGain - slope) / newGain, -0.5, 0.5);
            w->params[lk] = newLift;
            w->params[gk] = newGain;
        }
        return true;
    }, MergeParam, clipId + QStringLiteral("/wheel/offset"));
}

QImage Editor::beforeAfterFrame(bool before, int width) const
{
    Document d = m_doc;
    Timeline &tl = timelineIn(d);
    if (before) {
        for (Track &t : tl.tracks)
            for (Clip &c : t.clips)
                if (m_selection.isEmpty() || m_selection.contains(c.id))
                    for (Effect &e : c.effects)
                        if (ColorEffects.contains(e.type))
                            e.enabled = false;
    }
    PlanOptions o;
    o.useProxies = m_useProxies;
    o.storeModel = false;
    o.scale = m_player->previewScale();
    Planner p(m_settings, m_media, d.sequences, o);
    return m_player->renderStill(p.plan(tl), m_player->position(), width);
}

// ------------------------------------------------------------------ audio page

void Editor::runTool(const QString &task, const QString &program, const QStringList &args, std::function<void(int, const QString &)> done)
{
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    p->setProcessEnvironment(env);
    setBusy(task);
    connect(p, &QProcess::finished, this, [this, p, done](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromUtf8(p->readAll());
        p->deleteLater();
        setBusy(QString());
        done(st == QProcess::NormalExit ? code : -1, out);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, done, program](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        p->deleteLater();
        setBusy(QString());
        setStatus(tr("%1 is not installed").arg(program));
        done(-1, QString());
    });
    p->start(program, args);
}

static QStringList rangeArgs(const Clip &c, double fps)
{
    return {QStringLiteral("-ss"), QString::number(c.in / fps, 'f', 3), QStringLiteral("-t"), QString::number(c.duration() / fps, 'f', 3)};
}

void Editor::normalizeLoudness(const QString &clipId, double targetLufs)
{
    const Clip *c = currentTimeline().clip(clipId);
    const MediaItem *m = c ? mediaItem(c->mediaId) : nullptr;
    if (!m || !m->hasAudio) {
        setStatus(tr("Select a clip with sound"));
        return;
    }
    // measure only the part of the source the clip plays
    QStringList args = AiTools::loudnessArguments(m->path);
    const int input = args.indexOf(QStringLiteral("-i"));
    const QStringList range = rangeArgs(*c, fps() / (std::abs(c->speed) > 0 ? std::abs(c->speed) : 1.0));
    for (int i = range.size() - 1; i >= 0; --i)
        args.insert(input, range.at(i));
    const QString id = clipId;
    runTool(tr("Measuring loudness…"), QStringLiteral("ffmpeg"), args, [this, id, targetLufs](int, const QString &out) {
        const double lufs = AiTools::parseIntegratedLoudness(out);
        if (std::isnan(lufs)) {
            setStatus(tr("Could not measure the loudness"));
            return;
        }
        const double gain = std::clamp(targetLufs - lufs, -30.0, 30.0);
        modify(tr("Normalize loudness"), [&](Timeline &tl, Document &, QString *) {
            Clip *cl = tl.clip(id);
            if (!cl)
                return false;
            Effect *vol = nullptr;
            for (Effect &e : cl->effects)
                if (e.type == QLatin1String("volume"))
                    vol = &e;
            if (!vol) {
                cl->effects.append(EffectRegistry::create(QStringLiteral("volume")));
                vol = &cl->effects.last();
            }
            vol->keyframes.clear();
            vol->params[QStringLiteral("gain")] = std::round(gain * 10) / 10;
            return true;
        });
        setStatus(tr("Loudness was %1 LUFS, gain %2 dB").arg(lufs, 0, 'f', 1).arg(gain, 0, 'f', 1));
    });
}

void Editor::placeRecording(const QString &path)
{
    const int frame = playhead();
    m_mediaBin->importFiles({path}, [this, frame](const MediaItem &m) {
        int target = -1;
        const QList<Track> &tracks = currentTimeline().tracks;
        for (int i = 0; i < tracks.size() && target < 0; ++i) {
            if (!tracks.at(i).isAudio() || tracks.at(i).locked)
                continue;
            bool free = true;
            for (const Clip &o : tracks.at(i).clips)
                free &= o.end() <= frame || o.position >= frame + m.length;
            if (free)
                target = i;
        }
        if (target < 0) {
            addTrack(QStringLiteral("audio"));
            target = currentTimeline().tracks.size() - 1;
        }
        addMediaToTimeline(m.id, target, frame);
    });
}

// ------------------------------------------------------------------ AI / analysis

void Editor::autoSubtitles(const QString &clipId, const QString &language)
{
    Q_UNUSED(language) // Settings › AI › Transcrição e legendas decides
    generateCaptions(clipId, m_captionStyleId.isEmpty() ? QStringLiteral("classic") : m_captionStyleId);
}

// ------------------------------------------------------------------ captions

// The caption track of a timeline: the first subtitle track with captions,
// else the first one without legacy title subtitles, else a new one on top.
static int captionTrack(Timeline &tl, const QVariantMap &style)
{
    for (int i = 0; i < tl.tracks.size(); ++i)
        if (Captions::trackHasCaptions(tl.tracks.at(i)))
            return i;
    for (int i = 0; i < tl.tracks.size(); ++i)
        if (tl.tracks.at(i).kind == TrackKind::Subtitle && tl.tracks.at(i).clips.isEmpty()) {
            if (tl.tracks.at(i).captionStyle.isEmpty())
                tl.tracks[i].captionStyle = style;
            return i;
        }
    Track s;
    s.id = newId(QStringLiteral("t"));
    s.kind = TrackKind::Subtitle;
    int n = 0;
    for (const Track &t : tl.tracks)
        n += t.kind == TrackKind::Subtitle;
    s.name = QStringLiteral("S%1").arg(n + 1);
    s.captionStyle = style;
    tl.tracks.prepend(s);
    return 0;
}

QVariantList Editor::captionStyles() const
{
    QVariantList list;
    for (const Captions::Style &s : Captions::Style::presets()) {
        QVariantMap m = s.toMap();
        m.insert(QStringLiteral("name"), Captions::Style::presetName(s.id));
        list.append(m);
    }
    return list;
}

QVariantMap Editor::captionStylePreset(const QString &id) const
{
    QVariantMap m = Captions::Style::preset(id).toMap();
    m.insert(QStringLiteral("name"), Captions::Style::presetName(id));
    return m;
}

QVariantMap Editor::captionTrackStyle() const
{
    for (const Track &t : currentTimeline().tracks)
        if (Captions::trackHasCaptions(t)) {
            QVariantMap m = Captions::Style::fromMap(t.captionStyle).toMap();
            m.insert(QStringLiteral("name"), Captions::Style::presetName(m.value(QStringLiteral("id")).toString()));
            return m;
        }
    return captionStylePreset(m_captionStyleId.isEmpty() ? QStringLiteral("classic") : m_captionStyleId);
}

bool Editor::hasCaptions() const
{
    for (const Track &t : currentTimeline().tracks)
        if (Captions::trackHasCaptions(t))
            return true;
    return false;
}

// A style for this project: vertical projects keep captions out of the
// platform interface (TikTok's layout covers the most common case)
static Captions::Style styleForProject(const QString &id, const ProjectSettings &settings)
{
    Captions::Style st = Captions::Style::preset(id);
    if (settings.height > settings.width) {
        const Platform p = Platforms::find(QStringLiteral("tiktok"));
        if (p.valid())
            st.safeArea = p.safeArea;
    }
    return st;
}

void Editor::generateCaptions(const QString &clipId, const QString &styleId)
{
    if (!m_captionClip.isEmpty())
        return;
    m_transcriber->refresh();
    if (!m_transcriber->available()) {
        setStatus(m_transcriber->problem());
        return;
    }
    // the clip asked for, else the selection, else the first clip with sound
    QString id = clipId;
    if (id.isEmpty() && !m_selection.isEmpty())
        id = m_selection.first();
    auto speechClip = [this](const QString &cid) {
        const Clip *c = currentTimeline().clip(cid);
        const MediaItem *m = c ? mediaItem(c->mediaId) : nullptr;
        return c && m && m->hasAudio && c->kind == QLatin1String("media");
    };
    if (!speechClip(id)) {
        id.clear();
        for (const Track &t : currentTimeline().tracks)
            for (const Clip &c : t.clips)
                if (id.isEmpty() && speechClip(c.id))
                    id = c.id;
    }
    if (id.isEmpty()) {
        setStatus(tr("Select a clip with speech"));
        return;
    }
    const Clip *c = currentTimeline().clip(id);
    if (std::abs(c->speed - 1.0) > 1e-6 || !c->speedRamp.isEmpty()) {
        setStatus(tr("Captions need the clip at normal speed"));
        return;
    }
    const MediaItem *m = mediaItem(c->mediaId);
    m_captionClip = id;
    m_captionStyleId = Captions::Style::presetIds().contains(styleId) ? styleId : QStringLiteral("classic");
    Q_EMIT captionsBusyChanged();
    // only what the clip plays (with a little margin for words cut at the edges)
    const qint64 start = std::max<qint64>(0, qint64(c->in * 1000.0 / fps()) - 500);
    const qint64 length = qint64(c->duration() * 1000.0 / fps()) + 1000;
    m_transcriber->transcribe(m->path, start, length, QStringLiteral("captions"));
}

void Editor::cancelCaptions()
{
    if (m_captionClip.isEmpty())
        return;
    m_transcriber->cancel();
}

void Editor::captionsTranscribed()
{
    const QString id = m_captionClip;
    m_captionClip.clear();
    Q_EMIT captionsBusyChanged();
    const Captions::Transcript t = m_transcriber->result();
    const Clip *c0 = currentTimeline().clip(id);
    if (!c0) {
        setStatus(tr("The clip was removed"));
        return;
    }
    rememberWords(c0->mediaId, t.words);
    if (const MediaItem *mi = mediaItem(c0->mediaId)) {
        // spoken-word search uses the transcript
        QList<AiTools::SubtitleCue> cues;
        for (const Captions::Cue &cue : Captions::buildCues(t.words, Captions::Rules()))
            cues.append({cue.startMs, cue.endMs, cue.lines.join(QLatin1Char(' '))});
        MediaItem copy = *mi;
        copy.transcript = AiTools::writeSrt(cues);
        updateMedia(copy);
    }
    const Captions::Style style = styleForProject(m_captionStyleId, m_settings);
    int added = 0;
    modify(tr("Automatic captions"), [&](Timeline &tl, Document &, QString *) {
        const Clip *cl = tl.clip(id);
        if (!cl)
            return false;
        const Clip clip = *cl;
        const double f = fps();
        // file time → timeline time
        const double offset = (clip.position - clip.in) * 1000.0 / f;
        const qint64 from = qint64(clip.position * 1000.0 / f), to = qint64(clip.end() * 1000.0 / f);
        QList<Captions::Word> words;
        for (Captions::Word w : t.words) {
            w.startMs = qint64(std::llround(w.startMs + offset));
            w.endMs = qint64(std::llround(w.endMs + offset));
            const qint64 mid = (w.startMs + w.endMs) / 2;
            if (mid < from || mid >= to)
                continue;
            w.startMs = std::max(w.startMs, from);
            w.endMs = std::min(w.endMs, to);
            words.append(w);
        }
        if (words.isEmpty())
            return false;
        const int st = captionTrack(tl, style.toMap());
        Track &track = tl.tracks[st];
        // replace earlier captions of this stretch
        Ops::clearRange(track, clip.position, clip.end());
        QList<Captions::Cue> cues = Captions::buildCues(words, Captions::Style::fromMap(track.captionStyle).effectiveRules(m_settings.width, m_settings.height));
        for (Captions::Cue &cue : cues)
            cue.endMs = std::min(cue.endMs, to);
        for (const Captions::Cue &cue : std::as_const(cues)) {
            Clip cc = Captions::makeClip(cue, f);
            if (cc.position >= clip.end())
                continue;
            cc.out = std::min(cc.out, cc.in + (clip.end() - cc.position) - 1);
            track.clips.append(cc);
            ++added;
        }
        Ops::sortTrack(track);
        Ops::normalizeTrack(track);
        return added > 0;
    });
    setStatus(added ? tr("%n caption(s) added", nullptr, added) : tr("No speech was found in this clip"));
}

void Editor::setCaptionStyle(const QVariantMap &styleMap)
{
    const QVariantMap style = Captions::Style::fromMap(styleMap).toMap();
    m_captionStyleId = style.value(QStringLiteral("id")).toString();
    modify(tr("Caption style"), [&](Timeline &tl, Document &, QString *) {
        bool any = false;
        for (Track &t : tl.tracks)
            if (Captions::trackHasCaptions(t) && t.captionStyle != style) {
                t.captionStyle = style;
                any = true;
            }
        return any;
    }, MergeTrack, QStringLiteral("captionstyle"));
}

bool Editor::setCaptionText(const QString &clipId, const QString &text)
{
    return modify(tr("Edit caption"), [&](Timeline &tl, Document &, QString *err) { return Ops::setCaptionText(tl, clipId, text, fps(), err); },
                  MergeClip, QStringLiteral("captiontext:") + clipId);
}

bool Editor::setCaptionWord(const QString &clipId, int index, const QString &text)
{
    return modify(tr("Edit caption"), [&](Timeline &tl, Document &, QString *err) { return Ops::setCaptionWord(tl, clipId, index, text, err); });
}

QString Editor::splitCaption(const QString &clipId, int wordIndex)
{
    QString right;
    modify(tr("Split caption"), [&](Timeline &tl, Document &, QString *err) {
        right = Ops::splitCaption(tl, clipId, wordIndex, fps(), err);
        return !right.isEmpty();
    });
    return right;
}

bool Editor::mergeCaption(const QString &clipId)
{
    return modify(tr("Merge captions"), [&](Timeline &tl, Document &, QString *err) { return Ops::mergeCaptionWithNext(tl, clipId, fps(), err); });
}

bool Editor::nudgeCaptionWord(const QString &clipId, int index, int deltaMs)
{
    return modify(tr("Move word"), [&](Timeline &tl, Document &, QString *err) { return Ops::nudgeCaptionWord(tl, clipId, index, deltaMs, err); },
                  MergeClip, QStringLiteral("nudge:%1:%2").arg(clipId).arg(index));
}

bool Editor::setCaptionSpeaker(const QString &clipId, int speaker)
{
    return modify(tr("Caption speaker"), [&](Timeline &tl, Document &, QString *err) { return Ops::setCaptionSpeaker(tl, clipId, speaker, err); });
}

bool Editor::toggleCaptionEmphasis(const QString &clipId, int index)
{
    return modify(tr("Caption keyword"), [&](Timeline &tl, Document &, QString *err) { return Ops::toggleCaptionEmphasis(tl, clipId, index, err); });
}

static QList<Captions::Cue> timelineCues(const Timeline &tl, const ProjectSettings &s, Captions::Style *styleOut = nullptr)
{
    for (const Track &t : tl.tracks) {
        if (!Captions::trackHasCaptions(t))
            continue;
        const Captions::Style style = Captions::Style::fromMap(t.captionStyle);
        if (styleOut)
            *styleOut = style;
        return Captions::trackCues(t, s.fps(), style, s.width, s.height);
    }
    return {};
}

bool Editor::exportCaptions(const QUrl &file, const QString &format)
{
    Captions::Style style;
    const QList<Captions::Cue> cues = timelineCues(currentTimeline(), m_settings, &style);
    if (cues.isEmpty())
        return exportSubtitles(file); // legacy title subtitles
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const QString fmt = format.toLower();
    if (QFileInfo(path).suffix().isEmpty())
        path += QLatin1Char('.') + fmt;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        setStatus(tr("Could not write %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    const QString text = fmt == QLatin1String("vtt") ? Captions::toVtt(cues)
        : fmt == QLatin1String("ass")                ? Captions::toAss(cues, style, m_settings.width, m_settings.height, fps())
                                                     : Captions::toSrt(cues);
    f.write(text.toUtf8());
    setStatus(tr("Captions saved"));
    return true;
}

void Editor::openTranscriptionSettings()
{
    m_transcriber->openSettings();
}

void Editor::importSubtitles(const QUrl &srt)
{
    QFile f(srt.isLocalFile() ? srt.toLocalFile() : srt.toString());
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(tr("Could not read the subtitles"));
        return;
    }
    const QList<AiTools::SubtitleCue> cues = AiTools::parseSrt(QString::fromUtf8(f.readAll()));
    const Captions::Style style = styleForProject(m_captionStyleId.isEmpty() ? QStringLiteral("classic") : m_captionStyleId, m_settings);
    modify(tr("Import subtitles"), [&](Timeline &tl, Document &, QString *) {
        const int st = captionTrack(tl, style.toMap());
        for (const AiTools::SubtitleCue &cue : cues) {
            // no word times in SRT: words are spread over the cue, the cue keeps its exact times
            Captions::Cue c;
            c.startMs = cue.startMs;
            c.endMs = cue.endMs;
            c.words = Captions::spreadWords(cue.text, cue.startMs, cue.endMs);
            if (c.words.isEmpty())
                continue;
            Clip cc = Captions::makeClip(c, fps());
            Ops::clearRange(tl.tracks[st], cc.position, cc.end());
            tl.tracks[st].clips.append(cc);
            Ops::sortTrack(tl.tracks[st]);
        }
        Ops::normalizeTrack(tl.tracks[st]);
        return !cues.isEmpty();
    });
}

bool Editor::exportSubtitles(const QUrl &srt)
{
    QList<AiTools::SubtitleCue> cues;
    for (const Captions::Cue &c : timelineCues(currentTimeline(), m_settings))
        cues.append({c.startMs, c.endMs, c.text()});
    for (const Track &t : currentTimeline().tracks) {
        if (t.kind != TrackKind::Subtitle || !cues.isEmpty())
            continue;
        for (const Clip &c : t.clips)
            for (const Effect &e : c.effects)
                if (e.type == QLatin1String("text"))
                    cues.append({qint64(std::llround(c.position / fps() * 1000)), qint64(std::llround(c.end() / fps() * 1000)),
                                 e.params.value(QStringLiteral("text")).toString()});
    }
    if (cues.isEmpty()) {
        setStatus(tr("There are no subtitles to export"));
        return false;
    }
    QFile f(srt.isLocalFile() ? srt.toLocalFile() : srt.toString());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(AiTools::writeSrt(cues).toUtf8());
    setStatus(tr("Subtitles saved"));
    return true;
}

// ------------------------------------------------------------------ shorts

QVariantList Editor::shortSequences() const
{
    QVariantList list;
    for (const Sequence &s : m_doc.sequences) {
        if (s.info.isEmpty())
            continue;
        QVariantMap m = s.info;
        m.insert(QStringLiteral("id"), s.id);
        m.insert(QStringLiteral("name"), s.name);
        m.insert(QStringLiteral("duration"), s.timeline.duration());
        list.append(m);
    }
    return list;
}

Track Editor::shortCaptionTrack(const QList<Captions::Word> &words, qint64 startMs, qint64 endMs, const Captions::Style &style, int width,
                                int height, double fps, int lengthFrames)
{
    QList<Captions::Word> local;
    for (Captions::Word w : words) {
        const qint64 mid = (w.startMs + w.endMs) / 2; // words cut by an edge belong to the side with most of them
        if (mid < startMs || mid >= endMs)
            continue;
        w.startMs = std::max<qint64>(0, w.startMs - startMs);
        w.endMs = std::min<qint64>(endMs - startMs, w.endMs - startMs);
        local.append(w);
    }
    Track sub;
    sub.id = newId(QStringLiteral("t"));
    sub.kind = TrackKind::Subtitle;
    sub.name = QStringLiteral("S1");
    sub.captionStyle = style.toMap();
    for (const Captions::Cue &cue : Captions::buildCues(local, style.effectiveRules(width, height))) {
        Clip cc = Captions::makeClip(cue, fps);
        if (cc.position >= lengthFrames)
            continue;
        cc.out = std::min(cc.out, cc.in + (lengthFrames - cc.position) - 1);
        sub.clips << cc;
    }
    Ops::normalizeTrack(sub);
    return sub;
}

QList<QPair<QString, QString>> Editor::createShorts(const MediaItem &media, const QList<ShortCut> &cuts, const QStringList &platforms,
                                                    const QString &styleId, const QList<Captions::Word> &words)
{
    QList<QPair<QString, QString>> made;
    const Platform platform = Platforms::primary(platforms);
    if (!platform.valid() || cuts.isEmpty())
        return made;
    leaveCompound(0);
    // A project in the platform's size: a new one when this one already has other work
    const bool sizeDiffers = m_settings.width != platform.width || m_settings.height != platform.height;
    if (sizeDiffers && currentTimeline().duration() > 1) {
        const QList<MediaItem> keep = m_media;
        newProject(QStringLiteral("vertical1080"));
        for (const MediaItem &m : keep)
            addMedia(m);
    }
    if (sizeDiffers || std::abs(fps() - platform.fps) > 1e-6)
        setProjectSettings(platform.width, platform.height, platform.fps, 1);
    if (!mediaItem(media.id))
        addMedia(media);
    m_mediaBin->reset(); // the media pool lists what the new project kept
    const MediaItem *m = mediaItem(media.id);
    const double f = fps();
    const int W = m_settings.width, H = m_settings.height;
    const double scale = m->hasVideo ? Reframe::fillScale(m->width, m->height, W, H) : 100.0;

    modify(tr("Generate cuts"), [&](Timeline &tl, Document &doc, QString *) {
        int v1 = -1, cursor = tl.duration();
        for (int i = tl.tracks.size() - 1; i >= 0; --i)
            if (tl.tracks.at(i).isVideo()) {
                v1 = i; // the bottom video track
                break;
            }
        if (v1 < 0)
            return false;
        int n = 0;
        for (const Sequence &s : doc.sequences)
            n += !s.info.isEmpty();
        for (const ShortCut &cut : cuts) {
            ++n;
            Sequence seq;
            seq.id = newId(QStringLiteral("seq"));
            seq.name = QStringLiteral("%1. %2").arg(n).arg(cut.title.isEmpty() ? tr("Cut") : cut.title);
            seq.timeline = Timeline::defaultTimeline();
            int sv1 = -1;
            for (int i = seq.timeline.tracks.size() - 1; i >= 0; --i)
                if (seq.timeline.tracks.at(i).isVideo()) {
                    sv1 = i;
                    break;
                }
            Clip c;
            c.id = newId(QStringLiteral("c"));
            c.mediaId = m->id;
            c.in = std::max(0, int(std::lround(cut.startMs * f / 1000.0)));
            c.out = std::min(m->length - 1, int(std::lround(cut.endMs * f / 1000.0)) - 1);
            if (c.out <= c.in)
                continue;
            c.position = 0;
            if (m->hasVideo && scale > 101) {
                Effect tf = EffectRegistry::create(QStringLiteral("transform"));
                tf.params[QStringLiteral("scale")] = std::round(scale * 10) / 10;
                tf.params[QStringLiteral("x")] = 0.0; // centred until the subject tracker answers
                c.effects << tf;
            }
            if (!Ops::insertClip(seq.timeline, sv1, c, false))
                continue;
            if (!styleId.isEmpty()) {
                Captions::Style style = Captions::Style::preset(styleId);
                style.safeArea = platform.safeArea;
                seq.timeline.tracks.prepend(shortCaptionTrack(words, cut.startMs, cut.endMs, style, W, H, f, c.duration()));
            }
            seq.info = {{QStringLiteral("platforms"), platforms},
                        {QStringLiteral("title"), cut.title},
                        {QStringLiteral("reason"), cut.reason},
                        {QStringLiteral("score"), std::round(cut.score)},
                        {QStringLiteral("mediaId"), m->id},
                        {QStringLiteral("startMs"), cut.startMs},
                        {QStringLiteral("endMs"), cut.endMs},
                        {QStringLiteral("index"), n}};
            doc.sequences.append(seq);
            // the cuts one after another on the main timeline, with a marker each
            Clip compound;
            compound.id = newId(QStringLiteral("c"));
            compound.kind = QStringLiteral("compound");
            compound.mediaId = seq.id;
            compound.name = seq.name;
            compound.position = cursor;
            compound.in = 0;
            compound.out = seq.timeline.duration() - 1;
            if (!Ops::insertClip(tl, v1, compound, false)) {
                doc.sequences.removeLast();
                continue;
            }
            tl.markers.append({cursor, cut.title, QStringLiteral("orange")});
            cursor += compound.duration();
            made.append({seq.id, c.id});
        }
        return !made.isEmpty();
    });
    setStatus(tr("%n cut(s) created: double-click one on the timeline to edit it", nullptr, int(made.size())));
    return made;
}

void Editor::applyReframe(const QString &sequenceId, const QString &clipId, const QList<Reframe::Sample> &samples)
{
    if (samples.isEmpty())
        return;
    const QList<Reframe::Sample> path = Reframe::smooth(samples);
    modify(tr("Follow the subject"), [&](Timeline &, Document &doc, QString *) {
        for (Sequence &s : doc.sequences) {
            if (s.id != sequenceId)
                continue;
            Clip *c = s.timeline.clip(clipId);
            const MediaItem *m = c ? mediaItem(c->mediaId) : nullptr;
            if (!c || !m || m->width <= 0)
                return false;
            Effect *tf = nullptr;
            for (Effect &e : c->effects)
                if (e.type == QLatin1String("transform"))
                    tf = &e;
            if (!tf)
                return false;
            QVariantList stored;
            for (const Reframe::Sample &p : path)
                stored.append(QVariantList{std::round(p.t * 1000) / 1000, std::round(p.x * 10000) / 10000});
            c->genParams[QStringLiteral("reframePath")] = stored; // re-used for other aspect ratios at export
            tf->keyframes = Reframe::keyframes(path, 0, c->duration(), fps(), m->width, m->height, m_settings.width, m_settings.height, tf->params);
            if (tf->keyframes.size() == 1) { // a still subject: no animation
                tf->params = tf->keyframes.first().values;
                tf->keyframes.clear();
            }
            return true;
        }
        return false;
    });
}

static QString slug(const QString &s)
{
    QString d = s.normalized(QString::NormalizationForm_D);
    d.remove(QRegularExpression(QStringLiteral("\\p{Mn}")));
    d = d.toLower();
    d.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    d = d.left(40);
    while (d.endsWith(QLatin1Char('-')))
        d.chop(1);
    while (d.startsWith(QLatin1Char('-')))
        d.remove(0, 1);
    return d.isEmpty() ? QStringLiteral("corte") : d;
}

QString Editor::defaultShortsFolder()
{
    QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (movies.isEmpty())
        movies = QDir::homePath();
    return QDir(movies).filePath(tr("Cuts"));
}

QList<Editor::ShortExport> Editor::exportShorts(const QUrl &folder, const QString &captions, const QStringList &sequenceIds)
{
    QList<ShortExport> queuedFiles;
    QString dir = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (dir.isEmpty())
        dir = defaultShortsFolder();
    QDir().mkpath(dir);
    const bool burn = captions != QLatin1String("srt");
    const bool sidecar = captions == QLatin1String("srt") || captions == QLatin1String("video+srt");
    int queued = 0;
    for (const Sequence &seq : std::as_const(m_doc.sequences)) {
        if (seq.info.isEmpty() || (!sequenceIds.isEmpty() && !sequenceIds.contains(seq.id)))
            continue;
        const QStringList platforms = seq.info.value(QStringLiteral("platforms")).toStringList();
        const int index = seq.info.value(QStringLiteral("index")).toInt();
        for (const QString &pid : platforms) {
            const Platform p = Platforms::find(pid);
            if (!p.valid())
                continue;
            ProjectData d;
            d.settings = m_settings;
            d.media = m_media;
            d.doc.sequences = m_doc.sequences;
            d.doc.timeline = seq.timeline;
            const bool sameSize = p.width == m_settings.width && p.height == m_settings.height;
            d.settings.width = p.width;
            d.settings.height = p.height;
            for (Track &t : d.doc.timeline.tracks) {
                if (Captions::trackHasCaptions(t)) {
                    Captions::Style st = Captions::Style::fromMap(t.captionStyle);
                    st.safeArea = p.safeArea; // each platform's interface
                    t.captionStyle = st.toMap();
                }
                if (sameSize || !t.isVideo())
                    continue;
                // another aspect ratio: fill it again, following the stored subject path
                for (Clip &c : t.clips) {
                    const MediaItem *m = mediaItem(c.mediaId);
                    if (!m || m->width <= 0)
                        continue;
                    for (Effect &e : c.effects) {
                        if (e.type != QLatin1String("transform"))
                            continue;
                        e.params[QStringLiteral("scale")] = std::round(Reframe::fillScale(m->width, m->height, p.width, p.height) * 10) / 10;
                        e.params[QStringLiteral("x")] = 0.0;
                        QList<Reframe::Sample> path;
                        for (const QVariant &v : c.genParams.value(QStringLiteral("reframePath")).toList())
                            path.append({v.toList().value(0).toDouble(), v.toList().value(1).toDouble(), 1});
                        e.keyframes = Reframe::keyframes(path, 0, c.duration(), fps(), m->width, m->height, p.width, p.height, e.params);
                        if (e.keyframes.size() <= 1) {
                            if (!e.keyframes.isEmpty())
                                e.params = e.keyframes.first().values;
                            e.keyframes.clear();
                        }
                    }
                }
            }
            const QString base = QDir(dir).filePath(QStringLiteral("%1-%2-%3").arg(index, 2, 10, QLatin1Char('0')).arg(slug(seq.info.value(QStringLiteral("title")).toString()), p.id));
            const ExportPreset preset = Exporter::preset(p.exportPreset);
            // The cues before the caption tracks may be taken out of the render
            const QList<Captions::Cue> cues = sidecar ? timelineCues(d.doc.timeline, d.settings) : QList<Captions::Cue>();
            if (!burn) {
                // only the .srt: the picture without captions
                for (Track &t : d.doc.timeline.tracks)
                    if (Captions::trackHasCaptions(t))
                        t.hidden = true;
            }
            const QString output = base + QLatin1Char('.') + preset.extension;
            queueRender(d, preset.id, output, true, -1, -1, -1);
            queuedFiles.append({seq.id, p.id, output});
            if (sidecar && !cues.isEmpty()) {
                QFile srt(base + QStringLiteral(".srt"));
                if (srt.open(QIODevice::WriteOnly))
                    srt.write(Captions::toSrt(cues).toUtf8());
            }
            ++queued;
        }
    }
    setStatus(queued ? tr("%n export(s) added to the render queue", nullptr, queued) : tr("There are no generated cuts to export"));
    return queuedFiles;
}

void Editor::splitOnSilence(const QString &clipId, double noiseDb, double minSeconds, bool removeSilence)
{
    const Clip *c = currentTimeline().clip(clipId);
    const MediaItem *m = c ? mediaItem(c->mediaId) : nullptr;
    if (!m || !m->hasAudio || std::abs(c->speed - 1.0) > 1e-6 || !c->speedRamp.isEmpty()) {
        setStatus(tr("Select a clip with sound at normal speed"));
        return;
    }
    const QString id = clipId;
    runTool(tr("Finding silence…"), QStringLiteral("ffmpeg"), AiTools::silenceArguments(m->path, noiseDb, minSeconds),
            [this, id, removeSilence](int, const QString &out) {
                const QList<AiTools::Interval> gaps = AiTools::parseSilence(out);
                int removed = 0;
                modify(tr("Cut silence"), [&](Timeline &tl, Document &, QString *) {
                    const Clip *cl = tl.clip(id);
                    if (!cl)
                        return false;
                    const Clip clip = *cl;
                    // last gap first, so earlier positions stay valid while rippling
                    for (auto it = gaps.crbegin(); it != gaps.crend(); ++it) {
                        const int s = std::max(clip.in, int(std::lround(it->start * fps())));
                        const int e = std::min(clip.out + 1, int(std::lround(it->end * fps())));
                        if (e - s < 2)
                            continue;
                        const int ts = clip.position + (s - clip.in);
                        const int te = clip.position + (e - clip.in);
                        Ops::splitAt(tl, te);
                        Ops::splitAt(tl, ts);
                        if (removeSilence) {
                            const auto [ti, ci] = tl.findClip(id);
                            Q_UNUSED(ci)
                            if (ti >= 0)
                                for (const Clip &o : tl.tracks.at(ti).clips)
                                    if (o.position == ts && o.mediaId == clip.mediaId) {
                                        Ops::removeClip(tl, o.id, true);
                                        ++removed;
                                        break;
                                    }
                        } else {
                            ++removed;
                        }
                    }
                    return removed > 0;
                });
                setStatus(removed ? tr("%n silent part(s) handled", nullptr, removed) : tr("No silence found"));
            });
}

void Editor::splitOnScenes(const QString &clipId, double threshold)
{
    const Clip *c = currentTimeline().clip(clipId);
    const MediaItem *m = c ? mediaItem(c->mediaId) : nullptr;
    if (!m || !m->hasVideo || std::abs(c->speed - 1.0) > 1e-6) {
        setStatus(tr("Select a video clip at normal speed"));
        return;
    }
    const QString id = clipId;
    runTool(tr("Finding scene cuts…"), QStringLiteral("ffmpeg"), AiTools::sceneArguments(m->path, threshold), [this, id](int, const QString &out) {
        const QList<double> cuts = AiTools::parseScenes(out);
        int n = 0;
        modify(tr("Split at scene cuts"), [&](Timeline &tl, Document &, QString *) {
            const Clip *cl = tl.clip(id);
            if (!cl)
                return false;
            const Clip clip = *cl;
            for (double t : cuts) {
                const int src = int(std::lround(t * fps()));
                if (src <= clip.in || src > clip.out)
                    continue;
                const int frame = clip.position + (src - clip.in);
                for (const Track &tr2 : tl.tracks)
                    for (const Clip &o : tr2.clips)
                        if (o.mediaId == clip.mediaId && o.position < frame && o.end() > frame) {
                            if (!Ops::split(tl, o.id, frame).isEmpty())
                                ++n;
                            goto next;
                        }
            next:;
            }
            return n > 0;
        });
        setStatus(n ? tr("%n scene cut(s) found", nullptr, n) : tr("No scene cuts found"));
    });
}

void Editor::smartReframe(const QString &aspect)
{
    const QStringList parts = aspect.split(QLatin1Char(':'));
    const double aw = parts.value(0).toDouble(), ah = parts.value(1).toDouble();
    if (aw <= 0 || ah <= 0)
        return;
    // Keep the short side at the current resolution
    const int shortSide = std::min(m_settings.width, m_settings.height);
    int w, h;
    if (aw < ah) {
        w = shortSide;
        h = int(std::lround(shortSide * ah / aw));
    } else {
        h = shortSide;
        w = int(std::lround(shortSide * aw / ah));
    }
    setProjectSettings(w, h, m_settings.fpsNum, m_settings.fpsDen);
    const double W = m_settings.width, H = m_settings.height;
    auto reframe = [&](Timeline &tl) {
        bool any = false;
        for (Track &t : tl.tracks) {
            if (!t.isVideo())
                continue;
            for (Clip &c : t.clips) {
                const MediaItem *m = mediaItem(c.mediaId);
                if (c.kind != QLatin1String("media") || !m || m->width <= 0 || m->height <= 0)
                    continue;
                const double fit = std::min(W / m->width, H / m->height);
                const double fill = std::max(W / m->width, H / m->height);
                const double scale = fill / fit * 100.0;
                if (scale < 101)
                    continue;
                Effect *tf = nullptr;
                for (Effect &e : c.effects)
                    if (e.type == QLatin1String("transform"))
                        tf = &e;
                if (!tf) {
                    c.effects.prepend(EffectRegistry::create(QStringLiteral("transform")));
                    tf = &c.effects.first();
                }
                tf->params[QStringLiteral("scale")] = std::round(scale * 10) / 10;
                tf->params[QStringLiteral("x")] = 0.0; // centered: keyframe X to follow the subject
                tf->params[QStringLiteral("y")] = 0.0;
                any = true;
            }
        }
        return any;
    };
    modify(tr("Reframe to %1").arg(aspect), [&](Timeline &tl, Document &, QString *) { return reframe(tl); });
    setStatus(tr("Reframed to %1: clips fill the frame, centered. Keyframe Position X to follow the action.").arg(aspect));
}

void Editor::searchTranscripts(const QString &query)
{
    m_searchResults.clear();
    for (const MediaItem &m : m_media) {
        if (m.transcript.isEmpty())
            continue;
        for (const AiTools::SubtitleCue &cue : AiTools::search(AiTools::parseSrt(m.transcript), query))
            m_searchResults.append(QVariantMap{{QStringLiteral("mediaId"), m.id},
                                               {QStringLiteral("name"), m.name},
                                               {QStringLiteral("frame"), int(std::lround(cue.startMs / 1000.0 * fps()))},
                                               {QStringLiteral("text"), cue.text},
                                               {QStringLiteral("timeline"), false}});
    }
    // subtitles already on the timeline
    for (const Track &t : currentTimeline().tracks)
        for (const Clip &c : t.clips) {
            if (Captions::isCaption(c)) {
                const QString text = Captions::joinWords(Captions::clipWords(c));
                if (!AiTools::search({{0, 0, text}}, query).isEmpty())
                    m_searchResults.append(QVariantMap{{QStringLiteral("mediaId"), QString()},
                                                       {QStringLiteral("name"), t.name},
                                                       {QStringLiteral("frame"), c.position},
                                                       {QStringLiteral("text"), text},
                                                       {QStringLiteral("timeline"), true}});
                continue;
            }
            for (const Effect &e : c.effects)
                if (e.type == QLatin1String("text")) {
                    const QList<AiTools::SubtitleCue> hit = AiTools::search({{0, 0, e.params.value(QStringLiteral("text")).toString()}}, query);
                    if (!hit.isEmpty())
                        m_searchResults.append(QVariantMap{{QStringLiteral("mediaId"), QString()},
                                                           {QStringLiteral("name"), t.name},
                                                           {QStringLiteral("frame"), c.position},
                                                           {QStringLiteral("text"), hit.first().text},
                                                           {QStringLiteral("timeline"), true}});
                }
        }
    Q_EMIT searchResultsChanged();
}

// ------------------------------------------------------------------ export

void Editor::detectHardware()
{
    auto *watcher = new QFutureWatcher<HwEncoders>(this);
    connect(watcher, &QFutureWatcher<HwEncoders>::finished, this, [this, watcher] {
        m_hw = watcher->result();
        watcher->deleteLater();
        Q_EMIT hwEncodersChanged();
    });
    watcher->setFuture(QtConcurrent::run([] { return Exporter::detectHardware(); }));
}

QString Editor::suggestedExportPath(const QString &presetId) const
{
    const ExportPreset p = Exporter::preset(presetId);
    QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) : QFileInfo(m_path).absolutePath();
    if (dir.isEmpty())
        dir = QDir::homePath();
    return QDir(dir).filePath(projectName() + QLatin1Char('.') + p.extension);
}

void Editor::queueExport(const QString &presetId, const QUrl &output, bool hardware, int quality, bool inOutOnly)
{
    QString out = output.isLocalFile() ? output.toLocalFile() : output.toString();
    const ExportPreset preset = Exporter::preset(presetId);
    if (out.isEmpty())
        out = suggestedExportPath(presetId);
    if (QFileInfo(out).suffix().isEmpty())
        out += QLatin1Char('.') + preset.extension;
    int in = -1, outFrame = -1;
    if (inOutOnly && m_player->markIn() >= 0) {
        in = m_player->markIn();
        outFrame = m_player->markOut() >= 0 ? m_player->markOut() : -1;
    }
    queueRender(projectData(), presetId, out, hardware, quality, in, outFrame);
    setStatus(tr("Added to the render queue"));
}

void Editor::queueRender(const ProjectData &data, const QString &presetId, const QString &out, bool hardware, int quality, int in, int outFrame)
{
    const ExportPreset preset = Exporter::preset(presetId);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/renders");
    QDir().mkpath(dir);
    static int serial = 0;
    const QString project = QDir(dir).filePath(QStringLiteral("render-%1-%2.mlt").arg(QDateTime::currentMSecsSinceEpoch()).arg(++serial));
    QString error;
    if (!ProjectIO::save(project, data, &error)) {
        setStatus(tr("Could not prepare the render: %1").arg(error));
        return;
    }
    Exporter::Settings s;
    s.presetId = presetId;
    s.output = out;
    s.hardware = hardware;
    s.quality = quality;
    s.threads = std::clamp(QThread::idealThreadCount() / 2, 1, 8);
    s.in = in;
    s.out = outFrame;
    RenderQueue::Job job;
    job.name = QFileInfo(out).fileName();
    job.output = out;
    job.preset = preset.name;
    job.projectFile = project;
    job.args = Exporter::meltArguments(project, s, m_hw, data.settings, &job.encoder);
    if (job.encoder.endsWith(QLatin1String("_nvenc")) || job.encoder.endsWith(QLatin1String("_vaapi"))) {
        Exporter::Settings sw = s;
        sw.hardware = false;
        job.fallbackArgs = Exporter::meltArguments(project, sw, m_hw, data.settings, &job.fallbackEncoder);
    }
    m_renderQueue->add(job);
}

// ------------------------------------------------------------------ helpers

QString Editor::timecode(int frame) const
{
    const int f = std::max(0, frame);
    const int rate = std::max(1, int(std::lround(fps())));
    const int ff = f % rate;
    const int secs = f / rate;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(secs / 3600, 2, 10, QLatin1Char('0'))
        .arg((secs / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(secs % 60, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

int Editor::framesFromSeconds(double seconds) const
{
    return std::max(1, int(std::lround(seconds * fps())));
}

QString Editor::formatDuration(int frames) const
{
    const int secs = int(frames / fps());
    if (secs >= 3600)
        return QStringLiteral("%1:%2:%3").arg(secs / 3600).arg((secs / 60) % 60, 2, 10, QLatin1Char('0')).arg(secs % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0'));
}

QStringList Editor::mediaNameFilters() const
{
    return MediaProbe::nameFilters();
}
