/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "highlights.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace Studio::Highlights {

using Captions::Word;

static bool sentenceEnd(const QString &w)
{
    QString s = w;
    while (!s.isEmpty() && (s.endsWith(QLatin1Char('"')) || s.endsWith(QLatin1Char(')')) || s.endsWith(QChar(0x201D))))
        s.chop(1);
    return s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char('?')) || s.endsWith(QLatin1Char('!')) || s.endsWith(QChar(0x2026));
}

QList<Sentence> sentences(const QList<Word> &words, int maxGapMs)
{
    QList<Sentence> list;
    int first = 0;
    for (int i = 0; i < words.size(); ++i) {
        const bool last = i + 1 == words.size();
        const bool pause = !last && words.at(i + 1).startMs - words.at(i).endMs > maxGapMs;
        if (sentenceEnd(words.at(i).text) || pause || last) {
            Sentence s;
            s.firstWord = first;
            s.lastWord = i;
            s.startMs = words.at(first).startMs;
            s.endMs = words.at(i).endMs;
            s.text = Captions::joinWords(words.mid(first, i - first + 1));
            list.append(s);
            first = i + 1;
        }
    }
    return list;
}

QVariantMap Candidate::toMap() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("startMs"), startMs},
            {QStringLiteral("endMs"), endMs},
            {QStringLiteral("seconds"), seconds()},
            {QStringLiteral("text"), text},
            {QStringLiteral("heuristic"), heuristic},
            {QStringLiteral("llmScore"), llmScore},
            {QStringLiteral("score"), score},
            {QStringLiteral("title"), title},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("platforms"), platforms}};
}

static constexpr qint64 LeadMs = 250;  // before the first word
static constexpr qint64 TailMs = 400;  // after the last word

QList<Candidate> candidates(const QList<Sentence> &sents, const QList<Word> &words, int minSec, int maxSec, qint64 mediaMs)
{
    QList<Candidate> out;
    const qint64 minMs = qint64(minSec) * 1000;
    const qint64 maxMs = qint64(maxSec) * 1000;
    auto clampEnd = [&](qint64 e) { return mediaMs > 0 ? std::min(e, mediaMs) : e; };

    // Over-long sentences are cut at word boundaries first
    QList<Sentence> units;
    for (const Sentence &s : sents) {
        if (s.endMs - s.startMs <= maxMs - LeadMs - TailMs) {
            units.append(s);
            continue;
        }
        int first = s.firstWord;
        for (int i = s.firstWord; i <= s.lastWord; ++i) {
            const bool cut = words.at(i).endMs - words.at(first).startMs >= (maxMs - LeadMs - TailMs) * 0.9 || i == s.lastWord;
            if (cut) {
                Sentence part;
                part.firstWord = first;
                part.lastWord = i;
                part.startMs = words.at(first).startMs;
                part.endMs = words.at(i).endMs;
                part.text = Captions::joinWords(words.mid(first, i - first + 1));
                units.append(part);
                first = i + 1;
            }
        }
    }

    int n = 0;
    for (int a = 0; a < units.size(); ++a) {
        for (int b = a; b < units.size(); ++b) {
            const qint64 start = std::max<qint64>(0, units.at(a).startMs - LeadMs);
            const qint64 end = clampEnd(units.at(b).endMs + TailMs);
            const qint64 len = end - start;
            if (len > maxMs)
                break;
            if (len < minMs)
                continue;
            Candidate c;
            c.id = QStringLiteral("cand%1").arg(++n);
            c.startMs = start;
            c.endMs = end;
            c.firstSentence = a;
            c.lastSentence = b;
            QStringList texts;
            for (int k = a; k <= b; ++k)
                texts << units.at(k).text;
            c.text = texts.join(QLatin1Char(' '));
            out.append(c);
        }
    }
    return out;
}

static double clamp01(double v)
{
    return std::clamp(v, 0.0, 1.0);
}

ScoreParts scoreParts(const Candidate &c, const QList<Word> &words, const Features &f, double idealSec)
{
    ScoreParts p;
    const double secs = std::max(0.5, c.seconds());
    // speech density: ~2.6 words/s is lively speech
    int count = 0;
    QString lastWord;
    for (const Word &w : words) {
        if (w.startMs >= c.startMs && w.endMs <= c.endMs) {
            ++count;
            lastWord = w.text;
        }
    }
    p.density = clamp01(count / secs / 2.6);

    // energy: momentary loudness inside vs. the whole media
    if (!f.loudness.isEmpty()) {
        double all = 0, inside = 0;
        int nAll = 0, nIn = 0;
        for (const LoudnessPoint &l : f.loudness) {
            if (l.lufs < -70)
                continue;
            all += l.lufs;
            ++nAll;
            if (l.t * 1000 >= c.startMs && l.t * 1000 <= c.endMs) {
                inside += l.lufs;
                ++nIn;
            }
        }
        if (nAll > 0 && nIn > 0)
            p.energy = clamp01(0.5 + (inside / nIn - all / nAll) / 8.0); // ±4 LU spans the range
        else
            p.energy = 0.3;
    } else {
        p.energy = 0.5;
    }

    // scene changes: a few per 10 s keep it visual; none is fine for talking heads
    int cuts = 0;
    for (double t : f.scenes)
        if (t * 1000 > c.startMs && t * 1000 < c.endMs)
            ++cuts;
    p.scenes = clamp01(cuts / (secs / 10.0) / 3.0);

    // long silences inside the clip
    double silent = 0;
    for (const AiTools::Interval &s : f.silences) {
        const double a = std::max(s.start * 1000, double(c.startMs + LeadMs));
        const double b = std::min(s.end * 1000, double(c.endMs - TailMs));
        if (b - a > 700)
            silent += (b - a) / 1000.0;
    }
    for (int i = 1; i < words.size(); ++i) {
        const Word &prev = words.at(i - 1);
        const Word &w = words.at(i);
        if (prev.endMs >= c.startMs && w.startMs <= c.endMs && w.startMs - prev.endMs > 1500)
            silent += (w.startMs - prev.endMs) / 1000.0 * 0.5;
    }
    p.pauses = clamp01(silent / secs * 4);

    // hook: an opening question or a hook phrase
    static const QRegularExpression hookRe(QStringLiteral("^(como|por que|porque|o que|qual|quem|quando|você|voce|nunca|ninguém|segredo|imagina|olha|sabia|"
                                                          "how|why|what|you|never|secret|imagine|did you)\\b"),
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    const QString text = c.text.trimmed();
    const int stop = text.indexOf(QRegularExpression(QStringLiteral("[.!?\u2026]")));
    const QString firstSentence = stop >= 0 ? text.left(stop + 1) : text;
    if (firstSentence.endsWith(QLatin1Char('?')))
        p.hook = 1;
    else if (hookRe.match(text).hasMatch())
        p.hook = 0.7;
    if (c.text.contains(QRegularExpression(QStringLiteral("\\d"))))
        p.hook = std::max(p.hook, 0.4); // numbers/lists hold attention

    p.repetition = Captions::repetition(c.text);
    p.complete = sentenceEnd(lastWord) ? 1.0 : 0.3;
    const double ideal = idealSec > 0 ? idealSec : secs;
    p.fit = clamp01(1.0 - std::abs(secs - ideal) / std::max(ideal, 1.0));
    return p;
}

double heuristicScore(const Candidate &c, const QList<Word> &words, const Features &f, double idealSec)
{
    const ScoreParts p = scoreParts(c, words, f, idealSec);
    const double s = 0.26 * p.density + 0.22 * p.energy + 0.10 * p.scenes + 0.14 * p.hook + 0.12 * p.complete + 0.16 * p.fit - 0.30 * p.pauses;
    return penaliseRepetition(std::clamp(s * 100.0, 0.0, 100.0), c.text);
}

double penaliseRepetition(double score, const QString &text)
{
    const double r = Captions::repetition(text);
    return score * (1.0 - std::clamp((r - 0.15) * 1.5, 0.0, 0.9));
}

QList<Candidate> pickBest(QList<Candidate> list, int count, double maxOverlap)
{
    std::stable_sort(list.begin(), list.end(), [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    QList<Candidate> chosen;
    for (const Candidate &c : std::as_const(list)) {
        if (count > 0 && chosen.size() >= count)
            break;
        bool clash = false;
        for (const Candidate &o : std::as_const(chosen)) {
            const qint64 overlap = std::min(c.endMs, o.endMs) - std::max(c.startMs, o.startMs);
            const qint64 shorter = std::min(c.endMs - c.startMs, o.endMs - o.startMs);
            if (overlap > 0 && overlap > maxOverlap * shorter)
                clash = true;
        }
        if (!clash)
            chosen.append(c);
    }
    std::sort(chosen.begin(), chosen.end(), [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    return chosen;
}

double combine(double heuristic, double llm)
{
    if (llm < 0)
        return heuristic;
    return 0.65 * llm + 0.35 * heuristic;
}

QByteArray llmRequest(const QString &model, const Candidate &c, const QString &platformName, const QString &language)
{
    const bool pt = language.isEmpty() || language.startsWith(QLatin1String("pt"));
    const QString system = QStringLiteral(
        "You pick highlights from long videos for short vertical clips (%1). Rate how well the transcript excerpt works as a "
        "stand-alone short: a strong hook in the first seconds, one complete idea, emotion or surprise, value for the viewer, "
        "and an ending that does not cut a thought. Answer ONLY with JSON: "
        "{\"score\": <integer 0-100>, \"title\": \"<catchy hook title, max 60 characters, in %2>\", \"reason\": \"<one short sentence in %2>\"}")
                               .arg(platformName.isEmpty() ? QStringLiteral("TikTok, Reels, Shorts") : platformName,
                                    pt ? QStringLiteral("Brazilian Portuguese") : QStringLiteral("the language of the transcript"));
    const QString user = QStringLiteral("Excerpt (%1 s):\n\"\"\"\n%2\n\"\"\"").arg(qRound(c.seconds())).arg(c.text.left(4000));
    QJsonObject format{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"),
                        QJsonObject{{QStringLiteral("score"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
                                    {QStringLiteral("title"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                    {QStringLiteral("reason"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                       {QStringLiteral("required"), QJsonArray{QStringLiteral("score"), QStringLiteral("title"), QStringLiteral("reason")}}};
    const QJsonObject body{{QStringLiteral("model"), model},
                           {QStringLiteral("stream"), false},
                           {QStringLiteral("think"), false},
                           {QStringLiteral("format"), format},
                           {QStringLiteral("options"), QJsonObject{{QStringLiteral("temperature"), 0.2}, {QStringLiteral("num_predict"), 200}}},
                           {QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}},
                                                                   QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), user}}}}};
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

LlmScore parseLlmReply(const QByteArray &reply)
{
    LlmScore r;
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
    const int a = content.indexOf(QLatin1Char('{'));
    const int b = content.lastIndexOf(QLatin1Char('}'));
    if (a < 0 || b <= a)
        return r;
    const QJsonObject o = QJsonDocument::fromJson(content.mid(a, b - a + 1).toUtf8()).object();
    if (o.isEmpty() || !o.contains(QStringLiteral("score")))
        return r;
    const QJsonValue sv = o.value(QStringLiteral("score"));
    double score = sv.isString() ? sv.toString().toDouble() : sv.toDouble(-1);
    if (score < 0)
        return r;
    if (score <= 1.0 && score != std::floor(score))
        score *= 100; // 0..1
    else if (score <= 10)
        score *= 10; // 0..10
    r.score = std::clamp(score, 0.0, 100.0);
    r.title = o.value(QStringLiteral("title")).toString().simplified().left(80);
    r.reason = o.value(QStringLiteral("reason")).toString().simplified().left(240);
    r.ok = true;
    return r;
}

QStringList loudnessCurveArguments(const QString &input)
{
    return {QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-i"), input, QStringLiteral("-vn"),
            QStringLiteral("-af"), QStringLiteral("aresample=16000,ebur128=framelog=verbose"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};
}

QList<LoudnessPoint> parseLoudnessCurve(const QString &out)
{
    QList<LoudnessPoint> pts;
    static const QRegularExpression re(QStringLiteral("t:\\s*([\\d.]+)\\s.*?M:\\s*(-?[\\d.]+|-inf|nan)"));
    auto it = re.globalMatch(out);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString v = m.captured(2);
        pts.append({m.captured(1).toDouble(), (v == QLatin1String("-inf") || v == QLatin1String("nan")) ? -120.0 : v.toDouble()});
    }
    return pts;
}

} // namespace Studio::Highlights
