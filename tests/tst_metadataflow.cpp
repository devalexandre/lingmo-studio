/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Export › "Title, description and thumbnail" through the editor controller,
// as the QML dialog drives it: the speech of the timeline → the local AI (a
// fake Ollama here) or the heuristic → frames rendered from the timeline →
// the thumbnail composed over the chosen one. Runs with a scratch HOME.

#include "editor.h"
#include "mediabin.h"
#include "metadatagenerator.h"
#include "testmedia.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>
#include <clocale>

using namespace Studio;

// Answers every POST with one canned HTTP response and remembers the body
class FakeOllama : public QObject
{
public:
    QTcpServer server;
    QByteArray lastBody;
    QByteArray answer;
    int status = 200;
    int requests = 0;

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
                lastBody = buf.mid(head + 4, length);
                buf.clear();
                ++requests;
                s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: "
                         + QByteArray::number(answer.size()) + "\r\nConnection: close\r\n\r\n" + answer);
                s->disconnectFromHost();
            });
        });
    }
    quint16 port() const { return server.serverPort(); }
    void reply(const QJsonObject &content)
    {
        answer = QJsonDocument(QJsonObject{{QStringLiteral("message"),
                                            QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                                        {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact))}}}})
                     .toJson(QJsonDocument::Compact);
    }

private:
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

class TestMetadataFlow : public QObject
{
    Q_OBJECT
    Editor *m_editor = nullptr;
    FakeOllama *m_ollama = nullptr;
    QString m_media;

    MediaBin *bin() const { return static_cast<MediaBin *>(m_editor->mediaBinObject()); }
    MetadataGenerator *gen() const { return static_cast<MetadataGenerator *>(m_editor->metadataObject()); }
    bool runGenerate()
    {
        gen()->generate();
        return QTest::qWaitFor([&] { return !gen()->running(); }, 120000);
    }
    static void setModel(const QString &provider, const QString &model)
    {
        QSettings ai(QStringLiteral("lingmoos"), QStringLiteral("ai"));
        ai.setValue(QStringLiteral("provider"), provider);
        ai.setValue(QStringLiteral("model"), model);
        ai.sync();
    }
    static QList<Captions::Word> speech()
    {
        const QStringList words = QStringLiteral("Hoje vamos montar um servidor Linux do zero. O servidor precisa de rede e de firewall.").split(QLatin1Char(' '));
        QList<Captions::Word> out;
        qint64 t = 100;
        for (const QString &w : words) {
            out.append({t, t + 180, w});
            t += 200;
        }
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (!TestMedia::haveFfmpeg())
            QSKIP("needs ffmpeg");
        m_editor = new Editor;
        m_ollama = new FakeOllama;
        QVERIFY(m_ollama->server.isListening());
        qputenv("OLLAMA_HOST", QByteArray("127.0.0.1:") + QByteArray::number(m_ollama->port()));
    }
    void cleanupTestCase()
    {
        delete m_editor;
        delete m_ollama;
    }

    void emptyTimelineIsRefused()
    {
        QVERIFY(!gen()->running());
        gen()->generate();
        QVERIFY(!gen()->running());
        QVERIFY(!gen()->error().isEmpty());
        QVERIFY(!gen()->hasResult());
        QVERIFY(gen()->thumbnail().isEmpty());
    }

    void withoutAModelItWritesASimpleVersion()
    {
        setModel(QString(), QString());
        const int before = bin()->count();
        m_editor->importPaths({TestMedia::video()});
        QVERIFY(QTest::qWaitFor([&] { return bin()->count() == before + 1; }, 30000));
        m_media = m_editor->media().last().id;
        QVERIFY(!m_editor->appendMedia(m_media).isEmpty());
        m_editor->addMarker(30, QStringLiteral("Rede"));
        m_editor->addMarker(60, QStringLiteral("Firewall"));
        // the speech is known, so nothing needs transcribing
        m_editor->rememberWords(m_media, speech());
        const Metadata::Input in = m_editor->metadataInput();
        QVERIFY2(in.transcript.startsWith(QStringLiteral("Hoje vamos montar")), qPrintable(in.transcript));
        QCOMPARE(in.chapters.size(), 2);
        QVERIFY(in.durationSec > 3.5);

        QVERIFY(runGenerate());
        QVERIFY2(gen()->error().isEmpty(), qPrintable(gen()->error()));
        QVERIFY(gen()->hasResult());
        QVERIFY(!gen()->usedLlm());
        QVERIFY(!gen()->notice().isEmpty());
        QCOMPARE(gen()->title(), QStringLiteral("Hoje vamos montar um servidor Linux do zero"));
        QVERIFY(gen()->description().contains(QStringLiteral("servidor")));
        QVERIFY(gen()->tags().contains(QStringLiteral("servidor")));
        QVERIFY(gen()->thumbnailText().startsWith(QStringLiteral("Servidor "))); // the most repeated word first
        QCOMPARE(m_ollama->requests, 0);

        // frames from the timeline and a thumbnail over the best one
        const QVariantList frames = gen()->frames();
        QVERIFY(!frames.isEmpty() && frames.size() <= 6);
        for (const QVariant &f : frames)
            QVERIFY(QFile::exists(QUrl(f.toMap().value(QStringLiteral("url")).toString()).toLocalFile()));
        QVERIFY(!gen()->thumbnail().isEmpty());
        const QImage thumb(QUrl(gen()->thumbnail()).toLocalFile());
        QCOMPARE(thumb.size(), QSize(1280, 720));
        QCOMPARE(gen()->thumbnailSize(), QSize(1280, 720));
    }

    void thumbnailFollowsTheEdits()
    {
        const QString first = gen()->thumbnail();
        QSignalSpy spy(gen(), &MetadataGenerator::thumbnailChanged);
        gen()->setThumbnailStyle(QStringLiteral("banner"));
        QVERIFY(QTest::qWaitFor([&] { return gen()->thumbnail() != first; }, 5000));
        const QString banner = gen()->thumbnail();
        QVERIFY(QFile::exists(QUrl(banner).toLocalFile()));
        QVERIFY(!QFile::exists(QUrl(first).toLocalFile())); // the old one is cleaned up

        gen()->setThumbnailText(QStringLiteral("Outro título"));
        QVERIFY(QTest::qWaitFor([&] { return gen()->thumbnail() != banner; }, 5000));

        if (gen()->frames().size() > 1) {
            const QString before = gen()->thumbnail();
            gen()->setSelectedFrame(1);
            QCOMPARE(gen()->selectedFrame(), 1);
            QVERIFY(QTest::qWaitFor([&] { return gen()->thumbnail() != before; }, 5000));
        }
        // an unknown style or frame is ignored
        gen()->setThumbnailStyle(QStringLiteral("nope"));
        QCOMPARE(gen()->thumbnailStyle(), QStringLiteral("banner"));
        gen()->setSelectedFrame(99);
        QVERIFY(gen()->selectedFrame() < 6);
    }

    void saves()
    {
        const QString jpg = TestMedia::dir() + QStringLiteral("/metadata-out.jpg");
        QFile::remove(jpg);
        QVERIFY(gen()->saveThumbnail(QUrl::fromLocalFile(jpg)));
        const QImage img(jpg);
        QCOMPARE(img.size(), QSize(1280, 720));
        QVERIFY(QFileInfo(jpg).size() < 2 * 1024 * 1024);

        const QString png = TestMedia::dir() + QStringLiteral("/metadata-out.png");
        QVERIFY(gen()->saveThumbnail(QUrl::fromLocalFile(png)));
        QCOMPARE(QImage(png).size(), QSize(1280, 720));

        gen()->setTitle(QStringLiteral("Meu título"));
        gen()->setTags(QStringLiteral("linux, servidor"));
        const QString txt = TestMedia::dir() + QStringLiteral("/metadata-out.txt");
        QVERIFY(gen()->saveTexts(QUrl::fromLocalFile(txt)));
        QFile f(txt);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.startsWith(QStringLiteral("Meu título\n\n")));
        QVERIFY(text.contains(QStringLiteral("linux, servidor")));
        QVERIFY(gen()->suggestedThumbnailPath().endsWith(QStringLiteral("-thumbnail.jpg")));
        QVERIFY(gen()->suggestedTextPath().endsWith(QStringLiteral("-description.txt")));
    }

    void theLocalAiWritesTheText()
    {
        setModel(QStringLiteral("ollama"), QStringLiteral("fake-model"));
        m_ollama->status = 200;
        m_ollama->reply(QJsonObject{{QStringLiteral("title"), QStringLiteral("Servidor Linux em 5 minutos")},
                                    {QStringLiteral("description"), QStringLiteral("Aprenda a montar um servidor.")},
                                    {QStringLiteral("tags"), QJsonArray{QStringLiteral("linux"), QStringLiteral("servidor"), QStringLiteral("rede")}},
                                    {QStringLiteral("thumbnail_text"), QStringLiteral("SERVIDOR RÁPIDO")}});
        const int requests = m_ollama->requests;
        QVERIFY(runGenerate());
        QCOMPARE(m_ollama->requests, requests + 1);
        QVERIFY2(gen()->error().isEmpty(), qPrintable(gen()->error()));
        QVERIFY2(gen()->usedLlm(), qPrintable(gen()->notice()));
        QCOMPARE(gen()->llmModel(), QStringLiteral("fake-model"));
        QCOMPARE(gen()->title(), QStringLiteral("Servidor Linux em 5 minutos"));
        QCOMPARE(gen()->tags(), QStringLiteral("linux, servidor, rede"));
        QCOMPARE(gen()->thumbnailText(), QStringLiteral("SERVIDOR RÁPIDO"));
        // chapters from the markers and the first hashtags are appended for pasting
        QVERIFY(gen()->description().startsWith(QStringLiteral("Aprenda a montar um servidor.")));
        QVERIFY(gen()->description().endsWith(QStringLiteral("#linux #servidor #rede")));
        QVERIFY(!gen()->thumbnail().isEmpty());

        // the request carried the model and what is said in the video
        const QJsonObject body = QJsonDocument::fromJson(m_ollama->lastBody).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("fake-model"));
        QVERIFY(m_ollama->lastBody.contains("Hoje vamos montar"));
    }

    void anAiThatFailsFallsBackToTheSimpleVersion()
    {
        setModel(QStringLiteral("ollama"), QStringLiteral("fake-model"));
        m_ollama->status = 500;
        m_ollama->answer = "boom";
        QVERIFY(runGenerate());
        QVERIFY(gen()->error().isEmpty());
        QVERIFY(gen()->hasResult());
        QVERIFY(!gen()->usedLlm());
        QVERIFY(gen()->notice().contains(QStringLiteral("fake-model")));
        QVERIFY(!gen()->title().isEmpty());
        QVERIFY(!gen()->thumbnail().isEmpty());

        // an answer that is not JSON
        m_ollama->status = 200;
        m_ollama->answer = QJsonDocument(QJsonObject{{QStringLiteral("message"), QJsonObject{{QStringLiteral("content"), QStringLiteral("não sei")}}}}).toJson(QJsonDocument::Compact);
        QVERIFY(runGenerate());
        QVERIFY(!gen()->usedLlm());
        QVERIFY(!gen()->notice().isEmpty());
        QVERIFY(!gen()->title().isEmpty());
    }

    void otherProvidersAreNotAskedOllama()
    {
        setModel(QStringLiteral("openai"), QStringLiteral("gpt"));
        const int requests = m_ollama->requests;
        QVERIFY(runGenerate());
        QCOMPARE(m_ollama->requests, requests);
        QVERIFY(!gen()->usedLlm());
    }

    void cancelStopsTheRun()
    {
        setModel(QStringLiteral("ollama"), QStringLiteral("fake-model"));
        m_ollama->status = 200;
        m_ollama->reply(QJsonObject{{QStringLiteral("title"), QStringLiteral("X")}});
        gen()->generate();
        QVERIFY(gen()->running());
        gen()->cancel();
        QVERIFY(!gen()->running());
        QCOMPARE(gen()->error(), tr("Cancelled"));
        // nothing from the cancelled run lands afterwards
        QTest::qWait(300);
        QVERIFY(!gen()->running());
        // and a new run works
        QVERIFY(runGenerate());
        QVERIFY(gen()->hasResult());
    }
};

int main(int argc, char **argv)
{
    QByteArray home = QByteArray(TEST_SCRATCH) + "/metadata-home";
    QDir(QString::fromUtf8(home)).removeRecursively();
    QDir().mkpath(QString::fromUtf8(home));
    qputenv("HOME", home);
    qputenv("XDG_CONFIG_HOME", home + "/.config");
    qputenv("XDG_DATA_HOME", home + "/.local/share");
    qputenv("XDG_CACHE_HOME", home + "/.cache");
    qputenv("SDL_AUDIODRIVER", "dummy");
    QGuiApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    app.setOrganizationName(QStringLiteral("lingmoos"));
    app.setApplicationName(QStringLiteral("lingmo-studio-test"));
    TestMetadataFlow t;
    return QTest::qExec(&t, argc, argv);
}

#include "tst_metadataflow.moc"
