/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Export › "Dub in another language" through the editor controller, as the QML
// dialog drives it: speech of the timeline → translation (a fake Ollama) →
// voice reference → the voice engine (fake_dub.py, same protocol as dub.py) →
// fit → WAV and MP3 per language. With STUDIO_DUB_REAL_PYTHON pointing at the
// dubbing virtualenv's python, the real Chatterbox engine runs instead.

#include "dubber.h"
#include "editor.h"
#include "mediabin.h"
#include "testmedia.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <clocale>

using namespace Studio;

// An Ollama that translates: "<lang> N" for every id asked, as long as target_chars
class FakeOllama : public QObject
{
public:
    QTcpServer server;
    int requests = 0;
    int status = 200;
    QString lastSystem;
    QList<QJsonObject> bodies;
    bool dropSecond = false; // answers only the first id of a batch (to test the retry)
    QMap<int, QString> fixed; // translations to give instead of the generated ones

    FakeOllama()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *s = server.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, this, [this, s] {
                QByteArray &buf = m_buffers[s];
                buf += s->readAll();
                const int head = buf.indexOf("\r\n\r\n");
                if (head < 0)
                    return;
                int length = 0;
                for (const QByteArray &line : buf.left(head).split('\n'))
                    if (line.toLower().startsWith("content-length:"))
                        length = line.mid(15).trimmed().toInt();
                if (buf.size() < head + 4 + length)
                    return;
                const QByteArray body = buf.mid(head + 4, length);
                buf.clear();
                ++requests;
                const QByteArray answer = status == 200 ? reply(body) : QByteArray("boom");
                s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: "
                         + QByteArray::number(answer.size()) + "\r\nConnection: close\r\n\r\n" + answer);
                s->disconnectFromHost();
            });
        });
    }
    quint16 port() const { return server.serverPort(); }

private:
    QByteArray reply(const QByteArray &requestBody)
    {
        const QJsonObject req = QJsonDocument::fromJson(requestBody).object();
        bodies.append(req);
        const QJsonArray messages = req.value(QStringLiteral("messages")).toArray();
        lastSystem = messages.at(0).toObject().value(QStringLiteral("content")).toString();
        const QJsonObject user = QJsonDocument::fromJson(messages.at(1).toObject().value(QStringLiteral("content")).toString().toUtf8()).object();
        QString lang = QStringLiteral("xx");
        const int at = lastSystem.indexOf(QStringLiteral("into "));
        if (at >= 0)
            lang = lastSystem.mid(at + 5, lastSystem.indexOf(QLatin1Char(' '), at + 5) - at - 5);
        QJsonArray out;
        bool first = true;
        for (const QJsonValue &v : user.value(QStringLiteral("translate")).toArray()) {
            if (dropSecond && !first)
                continue;
            first = false;
            const QJsonObject o = v.toObject();
            const int id = o.value(QStringLiteral("id")).toInt();
            if (fixed.contains(id)) {
                out.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("text"), fixed.value(id)}});
                continue;
            }
            out.append(QJsonObject{{QStringLiteral("id"), o.value(QStringLiteral("id")).toInt()},
                                   {QStringLiteral("text"), QStringLiteral("%1 %2 %3.").arg(lang).arg(o.value(QStringLiteral("id")).toInt()).arg(QString(o.value(QStringLiteral("target_chars")).toInt() / 2, QLatin1Char('a')))}});
        }
        const QString content = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("translations"), out}}).toJson(QJsonDocument::Compact));
        return QJsonDocument(QJsonObject{{QStringLiteral("message"), QJsonObject{{QStringLiteral("content"), content}}}}).toJson(QJsonDocument::Compact);
    }
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

class TestDubFlow : public QObject
{
    Q_OBJECT
    Editor *m_editor = nullptr;
    FakeOllama *m_ollama = nullptr;
    QString m_media;
    QString m_out;

    MediaBin *bin() const { return static_cast<MediaBin *>(m_editor->mediaBinObject()); }
    Dubber *dub() const { return static_cast<Dubber *>(m_editor->dubberObject()); }
    bool runDub(int timeoutMs = 180000)
    {
        dub()->start();
        return QTest::qWaitFor([&] { return !dub()->running(); }, timeoutMs);
    }
    static void setModel(const QString &provider, const QString &model)
    {
        QSettings ai(QStringLiteral("lingmoos"), QStringLiteral("ai"));
        ai.setValue(QStringLiteral("provider"), provider);
        ai.setValue(QStringLiteral("model"), model);
        ai.sync();
    }
    double timelineSeconds() const { return m_editor->document().timeline.duration() / m_editor->fps(); }
    static QList<Captions::Word> speech()
    {
        // 3.6 s of speech: two sentences (the test video is 4 s long)
        const QString text = QStringLiteral("Hello everyone and welcome to my channel. Today we talk about servers at home, step by step.");
        QList<Captions::Word> out;
        qint64 t = 150;
        for (const QString &w : text.split(QLatin1Char(' '))) {
            out.append({t, t + 195, w});
            t += 215;
        }
        return out;
    }
    static double duration(const QString &file)
    {
        QProcess p;
        p.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                                           QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"), file});
        p.waitForFinished(20000);
        return p.readAllStandardOutput().trimmed().toDouble();
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (!TestMedia::haveFfmpeg())
            QSKIP("needs ffmpeg");
        m_ollama = new FakeOllama;
        QVERIFY(m_ollama->server.isListening());
        qputenv("OLLAMA_HOST", QByteArray("127.0.0.1:") + QByteArray::number(m_ollama->port()));
        m_out = TestMedia::dir() + QStringLiteral("/dub-out");
        QDir(m_out).removeRecursively();
        m_editor = new Editor;
        QVERIFY(QTest::qWaitFor([&] { return !dub()->engineChecking(); }, 60000));
    }
    void cleanupTestCase()
    {
        delete m_editor;
        delete m_ollama;
    }

    void theEngineIsDetected()
    {
        QVERIFY2(dub()->engineReady(), qPrintable(dub()->engineSummary()));
        QVERIFY(!dub()->engineSummary().isEmpty());
        QVERIFY(!dub()->languages().isEmpty());
        QCOMPARE(dub()->languages().size(), 23);
    }

    void refusesWithoutWhatItNeeds()
    {
        setModel(QString(), QString());
        dub()->setTargets({QStringLiteral("en")});
        dub()->start();
        QVERIFY(!dub()->running());
        QVERIFY2(dub()->error().contains(QStringLiteral("model")), qPrintable(dub()->error()));

        setModel(QStringLiteral("ollama"), QStringLiteral("fake"));
        dub()->setTargets({});
        dub()->start();
        QVERIFY(!dub()->error().isEmpty());
        QVERIFY(!dub()->running());

        dub()->setTargets({QStringLiteral("en")});
        dub()->start(); // nothing on the timeline
        QVERIFY(!dub()->running());
        QVERIFY(dub()->error().contains(QStringLiteral("timeline")));

        // targets are cleaned: unknown codes and repeats dropped, region suffixes folded
        dub()->setTargets({QStringLiteral("EN"), QStringLiteral("en"), QStringLiteral("xx"), QStringLiteral("pt-BR")});
        QCOMPARE(dub()->targets(), QStringList({QStringLiteral("en"), QStringLiteral("pt")}));
    }

    void dubsIntoTwoLanguages()
    {
        const int before = bin()->count();
        m_editor->importPaths({TestMedia::video()});
        QVERIFY(QTest::qWaitFor([&] { return bin()->count() == before + 1; }, 30000));
        m_media = m_editor->media().last().id;
        QVERIFY(!m_editor->appendMedia(m_media).isEmpty());
        m_editor->rememberWords(m_media, speech());
        // the speech is known: nothing to transcribe, and it lands on the timeline
        QVERIFY(m_editor->speechJobs().isEmpty());
        const QList<Captions::Word> onTimeline = m_editor->timelineWords();
        QCOMPARE(onTimeline.size(), speech().size());
        QCOMPARE(onTimeline.first().startMs, qint64(150));

        setModel(QStringLiteral("ollama"), QStringLiteral("fake"));
        dub()->setOutputFolder(m_out);
        dub()->setTargets({QStringLiteral("es"), QStringLiteral("fr")});
        m_ollama->requests = 0;
        QVERIFY(runDub());
        QVERIFY2(dub()->error().isEmpty(), qPrintable(dub()->error()));

        const QVariantList results = dub()->results();
        QCOMPARE(results.size(), 2);
        QCOMPARE(results.at(0).toMap().value(QStringLiteral("code")).toString(), QStringLiteral("es"));
        QCOMPARE(results.at(1).toMap().value(QStringLiteral("code")).toString(), QStringLiteral("fr"));
        for (const QVariant &v : results) {
            const QVariantMap r = v.toMap();
            const QString wav = r.value(QStringLiteral("wav")).toString();
            QVERIFY2(QFile::exists(wav), qPrintable(wav));
            QCOMPARE(QFileInfo(wav).dir().absolutePath(), QFileInfo(m_out).absoluteFilePath());
            QVERIFY(wav.endsWith(QStringLiteral("-%1.wav").arg(r.value(QStringLiteral("code")).toString())));
            // as long as the video (4 s), 48 kHz stereo
            QVERIFY2(std::abs(duration(wav) - timelineSeconds()) < 0.15, qPrintable(QString::number(duration(wav))));
            const QString mp3 = r.value(QStringLiteral("mp3")).toString();
            if (!mp3.isEmpty()) {
                QVERIFY(QFile::exists(mp3));
                QVERIFY(std::abs(duration(mp3) - duration(wav)) < 0.2);
            }
        }
        // one translation request per language (two sentences fit in one batch)
        QCOMPARE(m_ollama->requests, 2);
        QVERIFY(m_ollama->lastSystem.contains(QStringLiteral("into French")));
        QCOMPARE(dub()->progress(), 1.0);
        QVERIFY(dub()->stage().isEmpty());

        // what the engine was asked to say: the translations, in the cloned voice
        QFile jobFile(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/dub/work/es/job-seen.json"));
        if (jobFile.exists() && jobFile.open(QIODevice::ReadOnly)) {
            const QJsonObject job = QJsonDocument::fromJson(jobFile.readAll()).object();
            QCOMPARE(job.value(QStringLiteral("language")).toString(), QStringLiteral("es"));
            QVERIFY(QFile::exists(job.value(QStringLiteral("reference")).toString()));
            QCOMPARE(job.value(QStringLiteral("cfg_weight")).toDouble(), 0.0);
            const QJsonArray segs = job.value(QStringLiteral("segments")).toArray();
            QVERIFY(!segs.isEmpty());
            QVERIFY(segs.at(0).toObject().value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Spanish ")));
        }
        QVERIFY(!dub()->referenceInfo().isEmpty());
    }

    void aMissingAnswerIsAskedAgain()
    {
        m_ollama->dropSecond = true;
        m_ollama->requests = 0;
        dub()->setTargets({QStringLiteral("de")});
        QVERIFY(runDub());
        m_ollama->dropSecond = false;
        QVERIFY2(dub()->error().isEmpty(), qPrintable(dub()->error()));
        QCOMPARE(dub()->results().size(), 1);
        // 1st request: both sentences, only one answered; 2nd: just the other one
        QCOMPARE(m_ollama->requests, 2);
        const QJsonArray second = QJsonDocument::fromJson(m_ollama->bodies.last().value(QStringLiteral("messages")).toArray().at(1).toObject().value(QStringLiteral("content")).toString().toUtf8())
                                      .object().value(QStringLiteral("translate")).toArray();
        QCOMPARE(second.size(), 1);
        QVERIFY(dub()->notice().isEmpty()); // the retry worked: nothing to report
    }

    void aTranslationThatRunsLongIsSpedUpAndReported()
    {
        // the "voice" speaks at 8 characters a second, far slower than the text allows
        qputenv("FAKE_DUB_CPS", "5");
        dub()->setTargets({QStringLiteral("it")});
        QVERIFY(runDub());
        qunsetenv("FAKE_DUB_CPS");
        QVERIFY2(dub()->error().isEmpty(), qPrintable(dub()->error()));
        const QVariantMap r = dub()->results().first().toMap();
        QVERIFY(r.value(QStringLiteral("sped")).toInt() >= 1);
        QVERIFY(r.value(QStringLiteral("maxSpeedup")).toDouble() > 1.1);
        QVERIFY(!dub()->notice().isEmpty());
        QVERIFY(dub()->notice().contains(QStringLiteral("Italian")));
        QVERIFY(std::abs(duration(r.value(QStringLiteral("wav")).toString()) - timelineSeconds()) < 0.15);
    }

    void theSourceLanguageIsSkipped()
    {
        m_editor->transcriber(); // its configured language is "auto": the video's language is unknown here
        dub()->setTargets({QStringLiteral("en")});
        QVERIFY(runDub());
        QCOMPARE(dub()->results().size(), 1); // nothing to skip when the language is unknown
    }

    void aDeadModelServerStopsTheRun()
    {
        m_ollama->status = 500;
        dub()->setTargets({QStringLiteral("es")});
        QVERIFY(runDub());
        m_ollama->status = 200;
        QVERIFY(!dub()->error().isEmpty());
        QVERIFY(dub()->error().contains(QStringLiteral("fake")));
        QVERIFY(!dub()->running());
    }

    void aFailingEngineIsReported()
    {
        qputenv("FAKE_DUB_FAIL", "1");
        dub()->setTargets({QStringLiteral("es")});
        QVERIFY(runDub());
        qunsetenv("FAKE_DUB_FAIL");
        QVERIFY(dub()->error().contains(QStringLiteral("boom")));
        QVERIFY(!dub()->running());
    }

    void cancelStopsEverything()
    {
        qputenv("FAKE_DUB_SLOW", "2");
        dub()->setTargets({QStringLiteral("es"), QStringLiteral("fr")});
        dub()->start();
        QVERIFY(dub()->running());
        QVERIFY(QTest::qWaitFor([&] { return dub()->stage().contains(QStringLiteral("voice model")); }, 30000));
        dub()->cancel();
        qunsetenv("FAKE_DUB_SLOW");
        QVERIFY(!dub()->running());
        QCOMPARE(dub()->error(), tr("Cancelled"));
        QTest::qWait(500);
        QVERIFY(!dub()->running()); // nothing resumes by itself
        QVERIFY(dub()->results().isEmpty());
        // and it runs again afterwards
        dub()->setTargets({QStringLiteral("es")});
        QVERIFY(runDub());
        QCOMPARE(dub()->results().size(), 1);
    }

    // The installer, with a stand-in for uv (no download): environment, packages, model, then a check
    void installsTheEngine()
    {
        const QString bin = TestMedia::dir() + QStringLiteral("/stubbin");
        QDir(bin).removeRecursively();
        QDir().mkpath(bin);
        const QString log = bin + QStringLiteral("/uv.log");
        QFile stub(bin + QStringLiteral("/uv"));
        QVERIFY(stub.open(QIODevice::WriteOnly));
        const QByteArray script = R"SH(#!/bin/sh
echo "$@" >> LOGPATH
if [ -n "$UV_STUB_FAIL" ]; then echo 'no network' >&2; exit 1; fi
case "$1" in
  venv) mkdir -p "$4/bin"; ln -sf /usr/bin/python3 "$4/bin/python"; echo 'Using CPython 3.11'; exit 0;;
  pip) case "$*" in *git+*) if [ -n "$UV_STUB_NOGIT" ]; then echo 'git failed' >&2; exit 1; fi;; esac; echo 'Installed 120 packages'; exit 0;;
esac
)SH";
        QByteArray body = script;
        body.replace("LOGPATH", log.toUtf8());
        stub.write(body);
        stub.setPermissions(stub.permissions() | QFileDevice::ExeOwner);
        stub.close();
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", bin.toUtf8() + ":" + oldPath);
        auto lines = [&] {
            QFile f(log);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts) : QStringList();
        };
        auto install = [&] {
            dub()->installEngine();
            QVERIFY(dub()->installing());
            QVERIFY(QTest::qWaitFor([&] { return !dub()->installing(); }, 60000));
        };

        // a normal install: the pinned GitHub build with V3
        install();
        QVERIFY2(!dub()->installFailed(), qPrintable(dub()->installMessage()));
        QVERIFY(dub()->installMessage().contains(QStringLiteral("installed")));
        QVERIFY(QTest::qWaitFor([&] { return !dub()->engineChecking(); }, 20000));
        QVERIFY(dub()->engineReady());
        QStringList l = lines();
        QCOMPARE(l.size(), 2);
        QVERIFY(l.at(0).startsWith(QStringLiteral("venv --python 3.11 ")));
        QVERIFY(l.at(0).endsWith(QStringLiteral("/lingmo/dub-venv")));
        QVERIFY(l.at(1).startsWith(QStringLiteral("pip install --python ")));
        QVERIFY(l.at(1).contains(QStringLiteral("chatterbox-tts @ git+https://github.com/resemble-ai/chatterbox.git@")));
        QVERIFY(l.at(1).contains(QStringLiteral("setuptools<81")));
        QVERIFY(QFile::exists(Dubber::venvDir() + QStringLiteral("/bin/python")));

        // GitHub unreachable: falls back to the stable PyPI release (V2) and still finishes
        QFile::remove(log);
        qputenv("UV_STUB_NOGIT", "1");
        install();
        qunsetenv("UV_STUB_NOGIT");
        QVERIFY2(!dub()->installFailed(), qPrintable(dub()->installMessage()));
        l = lines();
        QCOMPARE(l.size(), 2); // the venv already existed: the failed git attempt, then PyPI
        QVERIFY(l.at(0).contains(QStringLiteral("git+")));
        QVERIFY(!l.at(1).contains(QStringLiteral("git+")));
        QVERIFY(l.at(1).contains(QStringLiteral("chatterbox-tts setuptools<81")));

        // nothing works: reported, not installed, and the message says why
        QFile::remove(log);
        qputenv("UV_STUB_FAIL", "1");
        install();
        qunsetenv("UV_STUB_FAIL");
        QVERIFY(dub()->installFailed());
        QVERIFY2(dub()->installMessage().contains(QStringLiteral("no network")), qPrintable(dub()->installMessage()));
        qputenv("PATH", oldPath);
    }

    // The real engine (Chatterbox) on a real voice: STUDIO_DUB_REAL=<python of the dubbing virtualenv>,
    // STUDIO_DUB_REAL_MEDIA=<a video with one person speaking Portuguese from ~13 s on>. Slow on a CPU.
    void realEngine()
    {
        const QString media = qEnvironmentVariable("STUDIO_DUB_REAL_MEDIA");
        if (qEnvironmentVariableIsEmpty("STUDIO_DUB_REAL") || media.isEmpty())
            QSKIP("set STUDIO_DUB_REAL and STUDIO_DUB_REAL_MEDIA");
        QVERIFY2(dub()->engineReady(), qPrintable(dub()->engineSummary()));
        qInfo() << "engine:" << dub()->engineSummary();
        m_editor->newProject();
        const int before = bin()->count();
        m_editor->importPaths({media});
        QVERIFY(QTest::qWaitFor([&] { return bin()->count() == before + 1; }, 60000));
        const QString id = m_editor->media().last().id;
        const QString clip = m_editor->appendMedia(id);
        QVERIFY(!clip.isEmpty());
        QVERIFY(m_editor->trimClip(clip, false, 40 * 30 - 1)); // the first 40 s
        // three sentences, ~11 s of speech from 13.4 s (the words only place the sentences)
        const QStringList sentences = {QStringLiteral("Olá pessoal, tudo bem com vocês?"), QStringLiteral("Hoje eu vou mostrar como funciona esse sistema novo."),
                                       QStringLiteral("Vamos começar agora mesmo, é bem simples.")};
        QList<Captions::Word> words;
        qint64 t = 13400;
        for (const QString &s : sentences)
            for (const QString &w : s.split(QLatin1Char(' '))) {
                words.append({t, t + 440, w});
                t += 500;
            }
        m_editor->rememberWords(id, words);
        m_ollama->fixed = {{0, QStringLiteral("Hello everyone, how are you all doing?")},
                           {1, QStringLiteral("Today I am going to show you how this new system works.")},
                           {2, QStringLiteral("Let's get started right now, it is really simple.")}};
        setModel(QStringLiteral("ollama"), QStringLiteral("fake"));
        dub()->setOutputFolder(TestMedia::dir() + QStringLiteral("/dub-real"));
        dub()->setTargets({QStringLiteral("en")});
        QElapsedTimer clock;
        clock.start();
        QVERIFY(runDub(40 * 60 * 1000));
        qInfo() << "dubbed in" << clock.elapsed() / 1000 << "s; notice:" << dub()->notice() << "reference:" << dub()->referenceInfo();
        QVERIFY2(dub()->error().isEmpty(), qPrintable(dub()->error()));
        const QVariantMap r = dub()->results().first().toMap();
        qInfo() << r;
        QVERIFY(QFile::exists(r.value(QStringLiteral("wav")).toString()));
        QVERIFY(std::abs(duration(r.value(QStringLiteral("wav")).toString()) - 40.0) < 0.2);
    }

    void aDubCanBePutOnTheTimeline()
    {
        const int tracks = int(m_editor->currentTimeline().tracks.size());
        dub()->addToTimeline(QStringLiteral("es"));
        QVERIFY(QTest::qWaitFor([&] { return m_editor->currentTimeline().tracks.size() == tracks + 1; }, 20000));
        QVERIFY(QTest::qWaitFor([&] { return !m_editor->currentTimeline().tracks.last().clips.isEmpty(); }, 20000));
        const Track &t = m_editor->currentTimeline().tracks.last();
        QVERIFY(t.isAudio());
        QVERIFY2(t.name.contains(QStringLiteral("Spanish")), qPrintable(t.name));
        QCOMPARE(t.clips.first().position, 0);
        // an unknown language does nothing
        dub()->addToTimeline(QStringLiteral("zh"));
        QTest::qWait(200);
        QCOMPARE(int(m_editor->currentTimeline().tracks.size()), tracks + 1);
    }
};

int main(int argc, char **argv)
{
    QByteArray home = QByteArray(TEST_SCRATCH) + "/dub-home";
    QDir(QString::fromUtf8(home)).removeRecursively();
    QDir().mkpath(QString::fromUtf8(home));
    qputenv("HOME", home);
    qputenv("XDG_CONFIG_HOME", home + "/.config");
    qputenv("XDG_DATA_HOME", home + "/.local/share");
    qputenv("XDG_CACHE_HOME", home + "/.cache");
    qputenv("SDL_AUDIODRIVER", "dummy");
    if (qEnvironmentVariableIsEmpty("STUDIO_DUB_REAL")) {
        qputenv("LINGMO_STUDIO_DUB_PYTHON", QByteArray("/usr/bin/python3"));
        qputenv("LINGMO_STUDIO_DUB_SCRIPT", QByteArray(FAKE_DUB));
    } else {
        qputenv("LINGMO_STUDIO_DUB_PYTHON", qgetenv("STUDIO_DUB_REAL"));
        qputenv("LINGMO_STUDIO_DUB_SCRIPT", QByteArray(REAL_DUB));
        // the scratch HOME hides the model caches: point at the real ones
        const QByteArray realHome = qgetenv("STUDIO_DUB_REAL_HOME");
        qputenv("HF_HOME", realHome + "/.cache/huggingface");
        qputenv("TORCH_HOME", realHome + "/.cache/torch");
        qputenv("PKUSEG_HOME", realHome + "/.pkuseg");
        qputenv("HF_HUB_OFFLINE", "1");
    }
    QGuiApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    app.setOrganizationName(QStringLiteral("lingmoos"));
    app.setApplicationName(QStringLiteral("lingmo-studio-test"));
    TestDubFlow t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_dubflow.moc"
