/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "editor.h"
#include "imageproviders.h"
#include "player.h"
#include "captionoverlay.h"
#include "videoitem.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>
#include <clocale>

// The Lingmo platform theme doesn't always hand the icon theme over at startup:
// use the one set in Lingmo Settings. Studio's workspace is always charcoal,
// so prefer the dark variant of the theme (light icons on dark panels).
static void applyIconTheme(bool darkUi)
{
    auto exists = [](const QString &name) {
        return !name.isEmpty()
            && !QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("icons/%1/index.theme").arg(name)).isEmpty();
    };
    QString current = QIcon::themeName();
    if (current.isEmpty() || current == QLatin1String("hicolor")) {
        QSettings theme(QStringLiteral("lingmoos"), QStringLiteral("theme"));
        const bool dark = darkUi || theme.value(QStringLiteral("DarkMode"), false).toBool();
        const QStringList candidates = {
            theme.value(dark ? QStringLiteral("DarkIconTheme") : QStringLiteral("IconTheme")).toString(),
            dark ? QStringLiteral("Lingmo-dark") : QStringLiteral("Lingmo"),
            dark ? QStringLiteral("Crule-dark") : QStringLiteral("Crule"),
            dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze"),
            QStringLiteral("breeze"),
        };
        for (const QString &name : candidates) {
            if (exists(name)) {
                current = name;
                break;
            }
        }
    }
    if (darkUi && !current.endsWith(QLatin1String("-dark"), Qt::CaseInsensitive)) {
        for (const QString &variant : {current + QStringLiteral("-dark"), current + QStringLiteral("-Dark")})
            if (exists(variant)) {
                current = variant;
                break;
            }
    }
    QIcon::setThemeName(current);
    QIcon::setFallbackThemeName(QStringLiteral("breeze-dark"));
}

static void installTranslator(QCoreApplication &app, QTranslator &translator)
{
    for (const QString &dir : {app.applicationDirPath() + QStringLiteral("/../share/lingmo-studio/translations"),
                               app.applicationDirPath(), QStringLiteral(TRANSLATIONS_DIR)}) {
        if (translator.load(QLocale(), QStringLiteral("lingmo-studio"), QStringLiteral("_"), dir)) {
            app.installTranslator(&translator);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // MLT and ffmpeg parse numbers ("1.5") with strtod: the C locale is required
    std::setlocale(LC_NUMERIC, "C");
    app.setOrganizationName(QStringLiteral("lingmoos"));
    app.setApplicationName(QStringLiteral("lingmo-studio"));
    app.setApplicationDisplayName(QStringLiteral("Lingmo Studio"));
    app.setApplicationVersion(QStringLiteral(LINGMO_STUDIO_VERSION));
    app.setDesktopFileName(QStringLiteral("lingmo-studio"));

    QTranslator translator;
    installTranslator(app, translator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lingmo Studio, video editor (MLT)"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption page(QStringLiteral("page"), QStringLiteral("Open on a page: media, cut, edit, effects, color, audio, deliver, shorts, captions"), QStringLiteral("page"));
    QCommandLineOption quitAfter(QStringLiteral("quit-after"), QStringLiteral("Quit after N seconds (tests)"), QStringLiteral("seconds"));
    QCommandLineOption play(QStringLiteral("play"), QStringLiteral("Start playing after loading"));
    QCommandLineOption seek(QStringLiteral("seek"), QStringLiteral("Move the playhead to a frame after loading"), QStringLiteral("frame"));
    QCommandLineOption select(QStringLiteral("select-first"), QStringLiteral("Select the first clip after loading (tests)"));
    QCommandLineOption exportTo(QStringLiteral("export"), QStringLiteral("Queue an export of the loaded project to FILE (MP4 preset)"), QStringLiteral("file"));
    QCommandLineOption findCuts(QStringLiteral("find-cuts"), QStringLiteral("Look for Shorts/Reels cuts in the first imported video (tests)"));
    parser.addOptions({page, quitAfter, play, seek, select, exportTo, findCuts});
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Project (.mlt) or media files to open"));
    parser.process(app);

    applyIconTheme(true);
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("lingmo-studio"), QIcon::fromTheme(QStringLiteral("applications-multimedia"))));

    qmlRegisterType<VideoItem>("Lingmo.Studio", 1, 0, "VideoView");
    qmlRegisterType<CaptionOverlay>("Lingmo.Studio", 1, 0, "CaptionOverlay");

    Editor editor;
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumb"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("wave"), new WaveformProvider);
    engine.addImageProvider(QStringLiteral("scope"), new ScopeProvider);
    engine.addImageProvider(QStringLiteral("captionpreview"), new CaptionPreviewProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("editor"), &editor);
    engine.rootContext()->setContextProperty(QStringLiteral("startPage"), parser.value(page));
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // "Open with" from Files/Photos: projects open, media lands on the timeline
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty()) {
        QStringList absolute;
        for (const QString &f : files)
            absolute << QFileInfo(f).absoluteFilePath();
        editor.importPaths(absolute, -2);
    }
    if (parser.isSet(seek) || parser.isSet(play) || parser.isSet(select) || parser.isSet(exportTo) || parser.isSet(findCuts)) {
        // after imports (probing runs in the background)
        auto *poll = new QTimer(&app);
        poll->setInterval(200);
        QObject::connect(poll, &QTimer::timeout, &app, [&, poll] {
            auto *bin = editor.mediaBinObject();
            if (bin->property("importing").toInt() > 0)
                return;
            poll->stop();
            QTimer::singleShot(300, &app, [&] {
                if (parser.isSet(select)) {
                    for (const auto &t : editor.currentTimeline().tracks)
                        if (!t.clips.isEmpty()) {
                            editor.select(t.clips.first().id);
                            break;
                        }
                }
                if (parser.isSet(seek))
                    editor.player()->seek(parser.value(seek).toInt());
                if (parser.isSet(play))
                    editor.player()->play();
                if (parser.isSet(findCuts) && !editor.media().isEmpty()) {
                    auto *shorts = editor.shortsObject();
                    shorts->setProperty("mediaId", editor.media().first().id);
                    QMetaObject::invokeMethod(shorts, "analyze");
                }
                if (parser.isSet(exportTo))
                    editor.queueExport(QStringLiteral("mp4-h264"), QUrl::fromLocalFile(QFileInfo(parser.value(exportTo)).absoluteFilePath()), true, -1, false);
            });
        });
        poll->start();
    }
    if (parser.isSet(quitAfter)) {
        QTimer::singleShot(int(parser.value(quitAfter).toDouble() * 1000), &app, [&editor] {
            editor.cleanExit();
            QCoreApplication::quit();
        });
    }
    const int code = app.exec();
    return code;
}
