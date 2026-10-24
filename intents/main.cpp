// lingmo-studio-intents: the Lingmo Intents of Lingmo Studio
// (studio.cuts.generate, studio.render) on com.lingmo.Studio /Intents.
// Started by D-Bus activation; it only opens Lingmo Studio with the matching
// command-line options (--find-cuts, --export), so the editor itself is unchanged.
// Messages are pt-BR, like the other Lingmo Intents hosts without a window.
#include <LingmoIntents/intentservice.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QMimeDatabase>

using LingmoIntents::IntentCall;
using LingmoIntents::IntentResult;

static QString studio()
{
    const QString env = qEnvironmentVariable("LINGMO_STUDIO_BIN");
    return env.isEmpty() ? QStringLiteral("lingmo-studio") : env;
}

static bool isMedia(const QFileInfo &fi)
{
    const QString mime = QMimeDatabase().mimeTypeForFile(fi).name();
    return mime.startsWith(QLatin1String("video/")) || mime.startsWith(QLatin1String("audio/"))
        || fi.suffix().compare(QLatin1String("mlt"), Qt::CaseInsensitive) == 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("lingmo-studio-intents"));
    LingmoIntents::IntentService intents(QStringLiteral("lingmo-studio"));

    intents.handle(QStringLiteral("studio.cuts.generate"), [](const IntentCall &call) {
        const QFileInfo video(call.params().value(QStringLiteral("video")).toString());
        if (!video.isFile() || !isMedia(video))
            return IntentResult::failed(QStringLiteral("Não é um arquivo de vídeo: %1").arg(video.filePath()));
        QString error;
        if (!LingmoIntents::spawnDetached(studio(), {QStringLiteral("--find-cuts"), video.absoluteFilePath()}, &error))
            return IntentResult::failed(error);
        return IntentResult::ok({{QStringLiteral("video"), video.absoluteFilePath()}});
    });

    intents.handle(QStringLiteral("studio.render"), [](const IntentCall &call) {
        const QFileInfo input(call.params().value(QStringLiteral("input")).toString());
        QString output = call.params().value(QStringLiteral("output")).toString();
        if (!input.isFile() || !isMedia(input))
            return IntentResult::failed(QStringLiteral("Não é um projeto nem um vídeo: %1").arg(input.filePath()));
        if (!output.endsWith(QLatin1String(".mp4"), Qt::CaseInsensitive))
            output += QStringLiteral(".mp4");
        if (QFileInfo::exists(output))
            return IntentResult::failed(QStringLiteral("O arquivo já existe e nunca é substituído: %1").arg(output));
        if (!QFileInfo(QFileInfo(output).absolutePath()).isDir())
            return IntentResult::failed(QStringLiteral("A pasta não existe: %1").arg(QFileInfo(output).absolutePath()));
        QString error;
        if (!LingmoIntents::spawnDetached(studio(), {QStringLiteral("--export"), output, input.absoluteFilePath()}, &error))
            return IntentResult::failed(error);
        return IntentResult::ok({{QStringLiteral("output"), output}});
    });

    if (!intents.publish(LingmoIntents::IntentService::Headless))
        return 1;
    intents.setIdleQuit(20000);
    return app.exec();
}
