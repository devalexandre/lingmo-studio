/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "dubbing.h"
#include "highlights.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

using namespace Studio::Captions;

namespace Studio::Dubbing {

// ------------------------------------------------------------------ languages

const QList<Language> &languages()
{
    static const QList<Language> list = {
        {QStringLiteral("ar"), QStringLiteral("Arabic"), QStringLiteral("العربية"), 13},
        {QStringLiteral("da"), QStringLiteral("Danish"), QStringLiteral("Dansk"), 14},
        {QStringLiteral("de"), QStringLiteral("German"), QStringLiteral("Deutsch"), 14},
        {QStringLiteral("el"), QStringLiteral("Greek"), QStringLiteral("Ελληνικά"), 14},
        {QStringLiteral("en"), QStringLiteral("English"), QStringLiteral("English"), 15},
        {QStringLiteral("es"), QStringLiteral("Spanish"), QStringLiteral("Español"), 16},
        {QStringLiteral("fi"), QStringLiteral("Finnish"), QStringLiteral("Suomi"), 13},
        {QStringLiteral("fr"), QStringLiteral("French"), QStringLiteral("Français"), 15},
        {QStringLiteral("he"), QStringLiteral("Hebrew"), QStringLiteral("עברית"), 13},
        {QStringLiteral("hi"), QStringLiteral("Hindi"), QStringLiteral("हिन्दी"), 14},
        {QStringLiteral("it"), QStringLiteral("Italian"), QStringLiteral("Italiano"), 15},
        {QStringLiteral("ja"), QStringLiteral("Japanese"), QStringLiteral("日本語"), 8},
        {QStringLiteral("ko"), QStringLiteral("Korean"), QStringLiteral("한국어"), 8},
        {QStringLiteral("ms"), QStringLiteral("Malay"), QStringLiteral("Bahasa Melayu"), 15},
        {QStringLiteral("nl"), QStringLiteral("Dutch"), QStringLiteral("Nederlands"), 14},
        {QStringLiteral("no"), QStringLiteral("Norwegian"), QStringLiteral("Norsk"), 14},
        {QStringLiteral("pl"), QStringLiteral("Polish"), QStringLiteral("Polski"), 14},
        {QStringLiteral("pt"), QStringLiteral("Portuguese"), QStringLiteral("Português"), 15},
        {QStringLiteral("ru"), QStringLiteral("Russian"), QStringLiteral("Русский"), 14},
        {QStringLiteral("sv"), QStringLiteral("Swedish"), QStringLiteral("Svenska"), 14},
        {QStringLiteral("sw"), QStringLiteral("Swahili"), QStringLiteral("Kiswahili"), 14},
        {QStringLiteral("tr"), QStringLiteral("Turkish"), QStringLiteral("Türkçe"), 13},
        {QStringLiteral("zh"), QStringLiteral("Chinese"), QStringLiteral("中文"), 5.5},
    };
    return list;
}

const Language *find(const QString &code)
{
    const QString c = normalizeCode(code);
    for (const Language &l : languages())
        if (l.code == c)
            return &l;
    return nullptr;
}

QString normalizeCode(const QString &code)
{
    QString c = code.trimmed().toLower();
    c = c.section(QRegularExpression(QStringLiteral("[-_]")), 0, 0);
    if (c == QLatin1String("iw"))
        return QStringLiteral("he");
    if (c == QLatin1String("nb") || c == QLatin1String("nn"))
        return QStringLiteral("no");
    return c.size() >= 2 && c.size() <= 3 ? c : QString();
}

// ------------------------------------------------------------------ sentences

static bool endsSentence(const QString &text)
{
    QString t = text.trimmed();
    while (!t.isEmpty() && QStringLiteral("\"'”’»)]").contains(t.back()))
        t.chop(1);
    return t.endsWith(QLatin1Char('.')) || t.endsWith(QLatin1Char('!')) || t.endsWith(QLatin1Char('?')) || t.endsWith(QChar(0x2026))
        || t.endsWith(QChar(0x3002)) || t.endsWith(QChar(0xFF01)) || t.endsWith(QChar(0xFF1F));
}

static bool softBreak(const QString &text)
{
    const QString t = text.trimmed();
    return t.endsWith(QLatin1Char(',')) || t.endsWith(QLatin1Char(';')) || t.endsWith(QLatin1Char(':')) || t.endsWith(QChar(0x2013))
        || t.endsWith(QChar(0x2014)) || t.endsWith(QChar(0xFF0C));
}

// Splits [first, last] until no piece is longer than ~30 words or ~14 s
static void splitRange(const QList<Word> &w, int first, int last, QList<QPair<int, int>> &out)
{
    const int n = last - first + 1;
    const qint64 span = w.at(last).endMs - w.at(first).startMs;
    if ((n <= 30 && span <= 14000) || n < 8) {
        out.append({first, last});
        return;
    }
    int best = first + n / 2;
    double bestScore = -1e9;
    for (int i = first + 3; i <= last - 4; ++i) {
        const double gap = (w.at(i + 1).startMs - w.at(i).endMs) / 1000.0;
        const double middle = std::abs(double(i - first + 1) / n - 0.5);
        const double score = gap * 2.0 + (softBreak(w.at(i).text) ? 1.0 : 0.0) - middle * 2.0;
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    splitRange(w, first, best, out);
    splitRange(w, best + 1, last, out);
}

QList<Sentence> sentences(const QList<Word> &words)
{
    QList<Sentence> out;
    for (const Highlights::Sentence &s : Highlights::sentences(words, 700)) {
        QList<QPair<int, int>> parts;
        splitRange(words, s.firstWord, s.lastWord, parts);
        for (const auto &p : parts) {
            Sentence x;
            x.id = int(out.size());
            x.startMs = words.at(p.first).startMs;
            x.endMs = words.at(p.second).endMs;
            x.words = p.second - p.first + 1;
            x.text = joinWords(words.mid(p.first, p.second - p.first + 1)).simplified();
            if (!x.text.isEmpty())
                out.append(x);
        }
    }
    for (int i = 0; i < out.size(); ++i)
        out[i].id = i;
    return out;
}

QList<Chunk> chunks(const QList<Sentence> &s, qint64 totalMs, qint64 maxSpanMs, qint64 maxGapMs, int maxWords)
{
    QList<Chunk> out;
    int words = 0;
    for (int i = 0; i < s.size(); ++i) {
        const Sentence &x = s.at(i);
        const bool join = !out.isEmpty() && x.startMs - s.at(i - 1).endMs <= maxGapMs && x.endMs - out.last().startMs <= maxSpanMs
            && words + x.words <= maxWords;
        if (join) {
            out.last().lastSentence = i;
            out.last().endMs = x.endMs;
            words += x.words;
        } else {
            Chunk c;
            c.firstSentence = c.lastSentence = i;
            c.startMs = x.startMs;
            c.endMs = x.endMs;
            out.append(c);
            words = x.words;
        }
    }
    for (int i = 0; i < out.size(); ++i)
        out[i].slotEndMs = i + 1 < out.size() ? out.at(i + 1).startMs : std::max(totalMs, out.at(i).endMs);
    return out;
}

QString chunkText(const QList<Sentence> &s, const Chunk &c)
{
    QStringList parts;
    for (int i = c.firstSentence; i <= c.lastSentence && i < s.size(); ++i)
        if (!s.at(i).translation.isEmpty())
            parts << s.at(i).translation;
    return parts.join(QLatin1Char(' ')).simplified();
}

// ------------------------------------------------------------------ translation

int targetChars(double seconds, const Language &target)
{
    return std::max(8, int(std::lround(seconds * target.charsPerSecond)));
}

QByteArray translateRequest(const QString &model, const QList<Sentence> &batch, const QList<Sentence> &context, const Language &target,
                            const QString &sourceName)
{
    const QString system = QStringLiteral(
        "You are a professional dubbing translator. The sentences below are spoken by ONE person in a video, in %1. "
        "Translate them into %2 for a voice-over that must sound like the same person speaking naturally in %2.\n"
        "Rules:\n"
        "- Write natural, fluent, conversational spoken %2, in the speaker's tone, humour and register. Never translate word for word; "
        "say it the way a native speaker would say it out loud.\n"
        "- Keep the meaning. Do not add, drop or explain ideas.\n"
        "- Each sentence has a time slot. Make each translation about `target_chars` characters long (within 20%%); when in doubt, be "
        "shorter, never longer.\n"
        "- Write numbers, symbols, currencies, units, dates, abbreviations and acronyms the way they are SPOKEN in %2, so a "
        "text-to-speech voice reads them correctly. Keep brand, product and person names.\n"
        "- Keep the continuity with the previous sentences (same terms, same way of addressing the viewer).\n"
        "- No notes, no quotes around the text, no emoji, no hashtags.\n"
        "Answer ONLY with JSON: {\"translations\": [{\"id\": <id>, \"text\": \"<translation>\"}]} with every id exactly once.")
                               .arg(sourceName.isEmpty() ? QStringLiteral("the original language") : sourceName, target.name);

    QJsonArray ctx;
    for (const Sentence &c : context)
        if (!c.translation.isEmpty())
            ctx.append(QJsonObject{{QStringLiteral("text"), c.text}, {QStringLiteral("translation"), c.translation}});
    QJsonArray items;
    for (const Sentence &s : batch)
        items.append(QJsonObject{{QStringLiteral("id"), s.id},
                                 {QStringLiteral("text"), s.text},
                                 {QStringLiteral("seconds"), std::round(s.seconds() * 10) / 10.0},
                                 {QStringLiteral("target_chars"), targetChars(s.seconds(), target)}});
    QJsonObject user{{QStringLiteral("previous_sentences"), ctx}, {QStringLiteral("translate"), items}};

    const QJsonObject idSchema{{QStringLiteral("type"), QStringLiteral("integer")}};
    const QJsonObject textSchema{{QStringLiteral("type"), QStringLiteral("string")}};
    const QJsonObject itemSchema{{QStringLiteral("type"), QStringLiteral("object")},
                                 {QStringLiteral("properties"), QJsonObject{{QStringLiteral("id"), idSchema}, {QStringLiteral("text"), textSchema}}},
                                 {QStringLiteral("required"), QJsonArray{QStringLiteral("id"), QStringLiteral("text")}}};
    const QJsonObject format{{QStringLiteral("type"), QStringLiteral("object")},
                             {QStringLiteral("properties"),
                              QJsonObject{{QStringLiteral("translations"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), itemSchema}}}}},
                             {QStringLiteral("required"), QJsonArray{QStringLiteral("translations")}}};
    int chars = 0;
    for (const Sentence &s : batch)
        chars += targetChars(s.seconds(), target);
    const QJsonObject body{
        {QStringLiteral("model"), model},
        {QStringLiteral("stream"), false},
        {QStringLiteral("think"), false},
        {QStringLiteral("format"), format},
        {QStringLiteral("options"), QJsonObject{{QStringLiteral("temperature"), 0.3}, {QStringLiteral("num_predict"), 400 + chars * 3}}},
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}},
                    QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(user).toJson(QJsonDocument::Compact))}}}}};
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QMap<int, QString> parseTranslation(const QByteArray &reply, const QList<int> &expected)
{
    QMap<int, QString> out;
    QString content;
    const QJsonDocument doc = QJsonDocument::fromJson(reply);
    if (doc.isObject() && doc.object().contains(QStringLiteral("message")))
        content = doc.object().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
    else if (doc.isObject() && doc.object().contains(QStringLiteral("response")))
        content = doc.object().value(QStringLiteral("response")).toString();
    else
        content = QString::fromUtf8(reply);
    static const QRegularExpression think(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
    content.remove(think);

    QJsonArray array;
    const int ob = content.indexOf(QLatin1Char('{')), oe = content.lastIndexOf(QLatin1Char('}'));
    const int ab = content.indexOf(QLatin1Char('[')), ae = content.lastIndexOf(QLatin1Char(']'));
    if (ob >= 0 && oe > ob && (ab < 0 || ob < ab)) {
        array = QJsonDocument::fromJson(content.mid(ob, oe - ob + 1).toUtf8()).object().value(QStringLiteral("translations")).toArray();
    } else if (ab >= 0 && ae > ab) {
        array = QJsonDocument::fromJson(content.mid(ab, ae - ab + 1).toUtf8()).array();
    }
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        const QJsonValue idv = o.value(QStringLiteral("id"));
        const int id = idv.isString() ? idv.toString().toInt() : idv.toInt(-1);
        if (!expected.contains(id))
            continue;
        QString text = o.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            text = o.value(QStringLiteral("translation")).toString();
        text = text.simplified();
        static const QString quotes = QStringLiteral("\"“”«» ");
        while (!text.isEmpty() && quotes.contains(text.front()) && text.front() != QLatin1Char(' '))
            text.remove(0, 1);
        while (!text.isEmpty() && quotes.contains(text.back()) && text.back() != QLatin1Char(' '))
            text.chop(1);
        text = text.simplified();
        if (!text.isEmpty() && !out.contains(id))
            out.insert(id, text);
    }
    return out;
}

// ------------------------------------------------------------------ the voice

Window referenceWindow(const QList<Word> &w, qint64 minMs, qint64 maxMs)
{
    Window best;
    best.score = -1;
    const int n = int(w.size());
    if (n < 4)
        return {};
    constexpr qint64 RunGap = 900; // a longer pause breaks the passage
    auto startOk = [&](int i) { return i == 0 || endsSentence(w.at(i - 1).text) || w.at(i).startMs - w.at(i - 1).endMs > 500; };
    auto endOk = [&](int e) { return e + 1 == n || endsSentence(w.at(e).text) || w.at(e + 1).startMs - w.at(e).endMs > 400; };

    Window longest;
    for (int s = 0; s < n; ++s) {
        if (!startOk(s))
            continue;
        for (int e = s; e < n; ++e) {
            if (e > s && w.at(e).startMs - w.at(e - 1).endMs > RunGap)
                break;
            const qint64 dur = w.at(e).endMs - w.at(s).startMs;
            if (dur > maxMs)
                break;
            if (dur > longest.length() && endOk(e))
                longest = {w.at(s).startMs, w.at(e).endMs, 0};
            if (dur < minMs || !endOk(e))
                continue;
            const double wps = (e - s + 1) / (dur / 1000.0);
            const double lengthTerm = std::max(0.0, 1.0 - std::abs(dur - 11500.0) / 6000.0);
            const double paceTerm = std::max(0.0, 1.0 - std::abs(wps - 2.7) / 2.5);
            double score = lengthTerm + paceTerm + (endsSentence(w.at(e).text) ? 0.3 : 0.0) + (w.at(s).startMs >= 1500 ? 0.2 : 0.0);
            if (score > best.score)
                best = {w.at(s).startMs, w.at(e).endMs, score};
        }
    }
    if (best.valid())
        return best;
    return longest.length() >= 3500 ? longest : Window();
}

// ------------------------------------------------------------------ fit

FitResult fit(const QList<FitInput> &in, double totalSec, double maxSpeedup, double gapSec)
{
    FitResult r;
    maxSpeedup = std::max(1.0, maxSpeedup);
    const double minRatio = 1.0 / maxSpeedup;
    double cursor = 0;
    for (const FitInput &x : in) {
        Placement p;
        p.id = x.id;
        p.startSec = std::max(x.startSec, cursor);
        p.driftSec = p.startSec - x.startSec;
        const double natural = std::max(0.0, x.naturalSec);
        const double available = x.slotEndSec - p.startSec - gapSec;
        p.ratio = 1.0;
        if (natural > available + 0.02 && natural > 0.05) {
            const double wanted = available > 0 ? available / natural : 0.0;
            p.ratio = std::clamp(wanted, minRatio, 1.0);
        }
        p.seconds = natural * p.ratio;
        if (p.ratio < 0.995) {
            ++r.sped;
            r.maxSpeedup = std::max(r.maxSpeedup, 1.0 / p.ratio);
        }
        r.maxDriftSec = std::max(r.maxDriftSec, p.driftSec);
        r.overflowEndSec = std::max(r.overflowEndSec, p.startSec + p.seconds - totalSec);
        cursor = p.startSec + p.seconds + gapSec;
        r.items.append(p);
    }
    return r;
}

} // namespace Studio::Dubbing
