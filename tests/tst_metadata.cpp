/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Publishing metadata (title, description, tags, chapters) and thumbnails
// (frame scoring, headline layout, composing).

#include "metadata.h"
#include "thumbnail.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRandomGenerator>
#include <QTest>

using namespace Studio;

static QByteArray ollamaReply(const QString &content)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("message"), QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), content}}}})
        .toJson(QJsonDocument::Compact);
}

class TestMetadata : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void condenseKeepsStartMiddleAndEnd()
    {
        QString text;
        for (int i = 0; i < 2000; ++i)
            text += QStringLiteral("palavra%1 ").arg(i);
        const QString c = Metadata::condense(text, 1000);
        QVERIFY(c.size() <= 1000);
        QVERIFY(c.startsWith(QStringLiteral("palavra0 ")));
        QVERIFY(c.endsWith(QStringLiteral("palavra1999")));
        QVERIFY(c.contains(QStringLiteral("[…]")));
        QCOMPARE(Metadata::condense(QStringLiteral("curto  texto"), 1000), QStringLiteral("curto texto"));
    }

    void requestAsksForJsonInTheRightLanguage()
    {
        Metadata::Input in;
        in.transcript = QStringLiteral("Hoje vamos falar sobre café.");
        in.projectName = QStringLiteral("Café");
        in.language = QStringLiteral("pt");
        in.durationSec = 125;
        in.chapters = {{0, QStringLiteral("Início")}, {60, QStringLiteral("Preparo")}};
        const QJsonObject body = QJsonDocument::fromJson(Metadata::llmRequest(QStringLiteral("qwen3:8b"), in)).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("qwen3:8b"));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), false);
        QVERIFY(body.contains(QStringLiteral("think")));
        QCOMPARE(body.value(QStringLiteral("think")).toBool(), false);
        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        QVERIFY(messages.at(0).toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("Brazilian Portuguese")));
        const QString user = messages.at(1).toObject().value(QStringLiteral("content")).toString();
        QVERIFY(user.contains(QStringLiteral("Hoje vamos falar sobre café.")));
        QVERIFY(user.contains(QStringLiteral("2:05")));
        QVERIFY(user.contains(QStringLiteral("1:00 Preparo")));
        const QJsonObject props = body.value(QStringLiteral("format")).toObject().value(QStringLiteral("properties")).toObject();
        for (const char *key : {"title", "description", "tags", "thumbnail_text"})
            QVERIFY2(props.contains(QLatin1String(key)), key);

        in.transcript.clear();
        const QJsonObject empty = QJsonDocument::fromJson(Metadata::llmRequest(QStringLiteral("m"), in)).object();
        QVERIFY(empty.value(QStringLiteral("messages")).toArray().at(1).toObject().value(QStringLiteral("content")).toString().contains(QStringLiteral("no transcript")));
    }

    void parsesTheAnswer()
    {
        const QString answer = QStringLiteral("<think>hmm</think>```json\n{\"title\": \"  \\\"Como fazer café perfeito\\\"  \", "
                                              "\"description\": \"Primeiro parágrafo.\\n\\n\\n\\nSegundo.\", "
                                              "\"tags\": [\"#café\", \"Café\", \"barista\", \"  \"], \"thumbnail_text\": \"CAFÉ PERFEITO\"}\n```");
        const Metadata::Result r = Metadata::parseLlmReply(ollamaReply(answer));
        QVERIFY(r.ok);
        QCOMPARE(r.title, QStringLiteral("Como fazer café perfeito"));
        QCOMPARE(r.description, QStringLiteral("Primeiro parágrafo.\n\nSegundo."));
        QCOMPARE(r.tags, QStringList({QStringLiteral("café"), QStringLiteral("barista")})); // duplicate and blank dropped, # removed
        QCOMPARE(r.thumbnailText, QStringLiteral("CAFÉ PERFEITO"));

        // tags as one string, thumbnail key in camelCase
        const Metadata::Result s = Metadata::parseLlmReply(
            ollamaReply(QStringLiteral("{\"title\":\"T\",\"description\":\"d\",\"tags\":\"a, b; c\",\"thumbnailText\":\"X\"}")));
        QVERIFY(s.ok);
        QCOMPARE(s.tags, QStringList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
        QCOMPARE(s.thumbnailText, QStringLiteral("X"));
    }

    void rejectsAnswersWithoutATitle()
    {
        QVERIFY(!Metadata::parseLlmReply(ollamaReply(QStringLiteral("Desculpe, não posso."))).ok);
        QVERIFY(!Metadata::parseLlmReply(ollamaReply(QStringLiteral("{\"description\": \"x\"}"))).ok);
        QVERIFY(!Metadata::parseLlmReply(ollamaReply(QStringLiteral("{\"title\": \"  \"}"))).ok);
        QVERIFY(!Metadata::parseLlmReply(QByteArray()).ok);
    }

    void longTitlesAreCutAtAWord()
    {
        const QString t = Metadata::cleanTitle(QStringLiteral("um dois três quatro cinco seis sete oito nove dez onze doze treze"), 30);
        QVERIFY(t.size() <= 30);
        QVERIFY(!t.endsWith(QLatin1Char(' ')));
        QVERIFY(QStringLiteral("um dois três quatro cinco seis sete oito nove dez onze doze treze").startsWith(t));
    }

    void heuristicFromTheTranscript()
    {
        Metadata::Input in;
        in.transcript = QStringLiteral("Hoje vou mostrar como montar um servidor Linux do zero. Primeiro o servidor precisa de uma instalação limpa. "
                                       "Depois configuramos a rede do servidor e o firewall. O Linux é ótimo para isso.");
        const Metadata::Result r = Metadata::heuristic(in);
        QVERIFY(r.ok);
        QCOMPARE(r.title, QStringLiteral("Hoje vou mostrar como montar um servidor Linux do zero"));
        QVERIFY(r.description.startsWith(QStringLiteral("Hoje vou mostrar")));
        QVERIFY(r.tags.contains(QStringLiteral("servidor")));
        QVERIFY(r.tags.contains(QStringLiteral("linux")));
        QVERIFY(!r.tags.contains(QStringLiteral("para")));
        QCOMPARE(r.thumbnailText, QStringLiteral("Servidor Linux")); // the two most repeated words

        Metadata::Input none;
        none.projectName = QStringLiteral("Meu vídeo");
        QCOMPARE(Metadata::heuristic(none).title, QStringLiteral("Meu vídeo"));
        none.onScreenText = {QStringLiteral("Receita de bolo")};
        QCOMPARE(Metadata::heuristic(none).title, QStringLiteral("Receita de bolo"));
        QVERIFY(!Metadata::heuristic(Metadata::Input()).ok);
    }

    void timestamps()
    {
        QCOMPARE(Metadata::timestamp(0), QStringLiteral("0:00"));
        QCOMPARE(Metadata::timestamp(65), QStringLiteral("1:05"));
        QCOMPARE(Metadata::timestamp(3723), QStringLiteral("1:02:03"));
        QCOMPARE(Metadata::timestamp(-4), QStringLiteral("0:00"));
    }

    void chaptersFollowYouTubeRules()
    {
        // an intro is added when the first marker is later than 0:00
        QCOMPARE(Metadata::chaptersBlock({{30, QStringLiteral("Preparo")}, {95, QStringLiteral("Servir")}}, QStringLiteral("Intro")),
                 QStringLiteral("0:00 Intro\n0:30 Preparo\n1:35 Servir"));
        // a first marker at the start is the first chapter
        QCOMPARE(Metadata::chaptersBlock({{1, QStringLiteral("A")}, {40, QStringLiteral("B")}, {100, QStringLiteral("C")}}, QStringLiteral("Intro")),
                 QStringLiteral("0:00 A\n0:40 B\n1:40 C"));
        // fewer than three chapters, or chapters under 10 s: none
        QCOMPARE(Metadata::chaptersBlock({{40, QStringLiteral("B")}}, QStringLiteral("Intro")), QString());
        QCOMPARE(Metadata::chaptersBlock({{20, QStringLiteral("B")}, {25, QStringLiteral("C")}}, QStringLiteral("Intro")), QString());
        // unnamed markers are not chapters; order does not matter
        QCOMPARE(Metadata::chaptersBlock({{90, QStringLiteral("Fim")}, {20, QString()}, {45, QStringLiteral("Meio")}}, QStringLiteral("Início")),
                 QStringLiteral("0:00 Início\n0:45 Meio\n1:30 Fim"));
    }

    void descriptionIsReadyToPaste()
    {
        Metadata::Result r;
        r.description = QStringLiteral("Resumo.");
        r.tags = {QStringLiteral("café gelado"), QStringLiteral("receita"), QStringLiteral("barista"), QStringLiteral("verão")};
        const QString d = Metadata::composeDescription(r, {{30, QStringLiteral("Preparo")}, {95, QStringLiteral("Servir")}}, QStringLiteral("Intro"));
        QCOMPARE(d, QStringLiteral("Resumo.\n\n0:00 Intro\n0:30 Preparo\n1:35 Servir\n\n#cafégelado #receita #barista"));
        QCOMPARE(Metadata::composeDescription(Metadata::Result(), {}, QStringLiteral("Intro")), QString());
    }

    // ------------------------------------------------------------- thumbnails

    static QImage noise(int w, int h)
    {
        QImage img(w, h, QImage::Format_RGB32);
        QRandomGenerator rng(7);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                img.setPixel(x, y, qRgb(int(rng.bounded(256)), int(rng.bounded(256)), int(rng.bounded(256))));
        return img;
    }

    void scoresPreferDetailedFramesOverBlankOnes()
    {
        QImage black(320, 180, QImage::Format_RGB32);
        black.fill(Qt::black);
        QImage white(320, 180, QImage::Format_RGB32);
        white.fill(Qt::white);
        QImage grey(320, 180, QImage::Format_RGB32);
        grey.fill(QColor(120, 120, 120));
        QImage scene(320, 180, QImage::Format_RGB32);
        {
            QPainter p(&scene);
            p.fillRect(scene.rect(), QColor(40, 90, 160));
            p.fillRect(QRect(40, 30, 120, 90), QColor(230, 170, 40));
            p.fillRect(QRect(180, 60, 100, 100), QColor(200, 50, 60));
        }
        QVERIFY(Thumbnail::frameScore(black) < 0.15);
        QVERIFY(Thumbnail::frameScore(white) < 0.2);
        QVERIFY(Thumbnail::frameScore(grey) < 0.3);
        QVERIFY(Thumbnail::frameScore(scene) > Thumbnail::frameScore(black));
        QVERIFY(Thumbnail::frameScore(scene) > Thumbnail::frameScore(grey));
        QVERIFY(Thumbnail::frameScore(scene) > 0.4);
        QCOMPARE(Thumbnail::frameScore(QImage()), 0.0);
        QVERIFY(Thumbnail::frameScore(noise(320, 180)) <= 1.0);
    }

    void pickingKeepsFramesApart()
    {
        // best first; 1 and 3 are too close to the best (2) to be kept at a gap of 2
        const QList<int> picks = Thumbnail::pickDiverse({0.1, 0.8, 0.9, 0.7, 0.2, 0.6}, 3, 2);
        QCOMPARE(picks, QList<int>({2, 5, 0}));
        QCOMPARE(Thumbnail::pickDiverse({0.5, 0.4}, 5, 1), QList<int>({0, 1}));
        QVERIFY(Thumbnail::pickDiverse({}, 3, 1).isEmpty());
    }

    void wrapsTheHeadline()
    {
        QFont f(QStringLiteral("Sans Serif"));
        f.setPixelSize(40);
        const QFontMetrics fm(f);
        const int w = fm.horizontalAdvance(QStringLiteral("CAFÉ PERFEITO")) / 2 + 20;
        const QStringList lines = Thumbnail::wrapText(QStringLiteral("CAFÉ PERFEITO EM CASA"), f, w, 4);
        QVERIFY(lines.size() >= 2);
        QCOMPARE(lines.join(QLatin1Char(' ')), QStringLiteral("CAFÉ PERFEITO EM CASA"));
        // more lines than allowed: empty, so the caller tries a smaller size
        QVERIFY(Thumbnail::wrapText(QStringLiteral("UM DOIS TRÊS QUATRO CINCO SEIS"), f, 10, 2).isEmpty());
        // a word wider than the box still gets its own line
        QCOMPARE(Thumbnail::wrapText(QStringLiteral("SUPERCALIFRAGILISTICO"), f, 10, 2), QStringList({QStringLiteral("SUPERCALIFRAGILISTICO")}));
    }

    void composesEveryStyleAtTheRequestedSize()
    {
        const QImage frame = noise(640, 360);
        QImage previous;
        for (const QString &style : Thumbnail::styleIds()) {
            Thumbnail::Options o;
            o.text = QStringLiteral("Café perfeito em casa");
            o.style = style;
            const QImage out = Thumbnail::compose(frame, o);
            QCOMPARE(out.size(), QSize(1280, 720));
            QVERIFY2(out != previous, qPrintable(style));
            previous = out;

            Thumbnail::Options none = o;
            none.text.clear();
            QVERIFY2(Thumbnail::compose(frame, none) != out, qPrintable(style)); // the headline is drawn

            Thumbnail::Options tall = o;
            tall.size = QSize(720, 1280);
            QCOMPARE(Thumbnail::compose(frame, tall).size(), QSize(720, 1280));
        }
        // no frame: a plain background, no crash
        Thumbnail::Options o;
        o.text = QStringLiteral("Sem imagem");
        QCOMPARE(Thumbnail::compose(QImage(), o).size(), QSize(1280, 720));
    }

    void headlineAccentAppearsInBold()
    {
        QImage flat(640, 360, QImage::Format_RGB32);
        flat.fill(QColor(30, 30, 30));
        Thumbnail::Options o;
        o.text = QStringLiteral("MUITO GRANDE TITULO FORTE");
        o.style = QStringLiteral("bold");
        o.accent = QColor(255, 0, 255);
        const QImage out = Thumbnail::compose(flat, o);
        bool accent = false, white = false;
        for (int y = 0; y < out.height() && !(accent && white); ++y)
            for (int x = 0; x < out.width(); ++x) {
                const QRgb c = out.pixel(x, y);
                accent |= c == qRgb(255, 0, 255);
                white |= c == qRgb(255, 255, 255);
            }
        QVERIFY(white);
        QVERIFY(accent); // the last line of a multi-line headline
    }
};

QTEST_MAIN(TestMetadata)
#include "tst_metadata.moc"
