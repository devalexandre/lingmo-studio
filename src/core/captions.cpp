/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "captions.h"

#include <QCoreApplication>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Studio::Captions {

// ================================================================ words

static const QRegularExpression &spaces()
{
    static const QRegularExpression re(QStringLiteral("\\s+"));
    return re;
}

QString joinWords(const QList<Word> &words)
{
    QStringList parts;
    parts.reserve(words.size());
    for (const Word &w : words)
        parts << w.text;
    return parts.join(QLatin1Char(' '));
}

QList<Word> spreadWords(const QString &text, qint64 startMs, qint64 endMs)
{
    const QStringList parts = text.simplified().split(spaces(), Qt::SkipEmptyParts);
    QList<Word> words;
    if (parts.isEmpty())
        return words;
    qint64 total = 0;
    for (const QString &p : parts)
        total += p.size() + 1;
    const double span = std::max<qint64>(1, endMs - startMs);
    qint64 acc = 0;
    for (const QString &p : parts) {
        Word w;
        w.text = p;
        w.startMs = startMs + qint64(std::llround(span * acc / total));
        acc += p.size() + 1;
        w.endMs = startMs + qint64(std::llround(span * acc / total));
        words.append(w);
    }
    words.last().endMs = endMs;
    return words;
}

static bool isPunctuationOnly(const QString &s)
{
    for (const QChar c : s)
        if (c.isLetterOrNumber())
            return false;
    return !s.isEmpty();
}

// Sorted, no zero-length words, punctuation glued to the previous word
static QList<Word> normalizeWords(QList<Word> in)
{
    QList<Word> out;
    for (Word w : in) {
        w.text = w.text.trimmed();
        if (w.text.isEmpty())
            continue;
        if (isPunctuationOnly(w.text) && !out.isEmpty()) {
            out.last().text += w.text;
            out.last().endMs = std::max(out.last().endMs, w.endMs);
            continue;
        }
        if (!out.isEmpty() && w.startMs < out.last().startMs)
            w.startMs = out.last().startMs;
        out.append(w);
    }
    for (int i = 0; i < out.size(); ++i) {
        Word &w = out[i];
        const qint64 next = i + 1 < out.size() ? out.at(i + 1).startMs : std::numeric_limits<qint64>::max();
        if (w.endMs <= w.startMs)
            w.endMs = std::min(w.startMs + 120, std::max(w.startMs + 1, next));
        if (w.endMs > next && next > w.startMs)
            w.endMs = next;
    }
    return out;
}

// whisper.cpp's DTW times trail the start of the sound by ~140 ms (measured
// on Portuguese speech with known onsets, large-v3-turbo and small: median
// +149 ms, +133 ms with background noise); words start this much earlier
static constexpr qint64 DtwLeadMs = 140;

static QString wordKey(const QString &text)
{
    QString k;
    for (const QChar ch : text)
        if (ch.isLetterOrNumber())
            k += ch.toLower();
    return k;
}

QList<Word> collapseRepeats(const QList<Word> &words)
{
    QStringList keys;
    keys.reserve(words.size());
    for (const Word &w : words)
        keys << wordKey(w.text);
    QList<bool> keep(words.size(), true);
    int i = 0;
    while (i < words.size()) {
        bool collapsed = false;
        for (int n = 1; n <= 12 && !collapsed; ++n) { // the shortest period first
            if (i + 2 * n > words.size())
                continue;
            // the phrase keys[i, i+n) and how many times it follows itself
            bool hasText = false;
            for (int k = 0; k < n; ++k)
                hasText |= !keys.at(i + k).isEmpty();
            if (!hasText)
                continue;
            int copies = 1;
            for (int j = i + n; j + n <= words.size(); j += n) {
                bool same = true;
                for (int k = 0; k < n && same; ++k)
                    same = keys.at(j + k) == keys.at(i + k);
                if (!same)
                    break;
                ++copies;
            }
            if (copies >= (n == 1 ? 4 : 3)) {
                for (int j = i + n; j < i + copies * n; ++j)
                    keep[j] = false;
                i += copies * n;
                collapsed = true;
            }
        }
        if (!collapsed)
            ++i;
    }
    QList<Word> out;
    for (int j = 0; j < words.size(); ++j)
        if (keep.at(j))
            out.append(words.at(j));
    return out;
}

static double repetitionOfKeys(const QStringList &keys)
{
    if (keys.size() < 6)
        return 0;
    QSet<QString> seen;
    int repeated = 0, total = 0;
    for (int i = 0; i + 2 < keys.size(); ++i) {
        const QString g = keys.at(i) + QLatin1Char(' ') + keys.at(i + 1) + QLatin1Char(' ') + keys.at(i + 2);
        ++total;
        if (seen.contains(g))
            ++repeated;
        else
            seen.insert(g);
    }
    return total ? double(repeated) / total : 0;
}

double repetition(const QList<Word> &words)
{
    QStringList keys;
    for (const Word &w : words) {
        const QString k = wordKey(w.text);
        if (!k.isEmpty())
            keys << k;
    }
    return repetitionOfKeys(keys);
}

double repetition(const QString &text)
{
    QStringList keys;
    for (const QString &w : text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        const QString k = wordKey(w);
        if (!k.isEmpty())
            keys << k;
    }
    return repetitionOfKeys(keys);
}

Transcript parseWhisperCppJson(const QByteArray &json)
{
    Transcript t;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    t.language = root.value(QStringLiteral("result")).toObject().value(QStringLiteral("language")).toString();
    QList<Word> all;
    for (const QJsonValue &sv : root.value(QStringLiteral("transcription")).toArray()) {
        const QJsonObject seg = sv.toObject();
        const QJsonObject segOff = seg.value(QStringLiteral("offsets")).toObject();
        const qint64 segFrom = segOff.value(QStringLiteral("from")).toInteger();
        const qint64 segTo = segOff.value(QStringLiteral("to")).toInteger();
        const QString segText = seg.value(QStringLiteral("text")).toString();
        const QJsonArray tokens = seg.value(QStringLiteral("tokens")).toArray();
        // With -dtw every token has a time from the cross-attention alignment
        // (t_dtw, centiseconds): far steadier than the offsets, which whisper.cpp
        // often spreads evenly over the segment
        bool dtw = false;
        for (const QJsonValue &tv : tokens)
            dtw |= tv.toObject().value(QStringLiteral("t_dtw")).toInteger(-1) >= 0;
        QList<Word> words;
        for (const QJsonValue &tv : tokens) {
            const QJsonObject tok = tv.toObject();
            const QString text = tok.value(QStringLiteral("text")).toString();
            if (text.isEmpty() || (text.startsWith(QLatin1String("[_")) && text.endsWith(QLatin1Char(']'))))
                continue;
            const QJsonObject off = tok.value(QStringLiteral("offsets")).toObject();
            qint64 from = off.value(QStringLiteral("from")).toInteger();
            qint64 to = off.value(QStringLiteral("to")).toInteger();
            if (dtw) {
                const qint64 t = tok.value(QStringLiteral("t_dtw")).toInteger(-1);
                if (t < 0)
                    continue;
                from = to = std::max<qint64>(0, t * 10 - DtwLeadMs);
            }
            const bool starts = text.at(0).isSpace() || words.isEmpty();
            if (starts && !(isPunctuationOnly(text.trimmed()) && !words.isEmpty())) {
                words.append({from, to, text.trimmed()});
            } else {
                words.last().text += text.trimmed();
                words.last().endMs = std::max(words.last().endMs, to);
            }
        }
        if (dtw) {
            // a word lasts until the next one starts (within the segment)
            for (int i = 0; i < words.size(); ++i) {
                const qint64 next = i + 1 < words.size() ? words.at(i + 1).startMs : std::max(words.at(i).startMs + 300, std::min(segTo, words.at(i).startMs + 900));
                words[i].endMs = std::max(words.at(i).startMs + 60, std::min(next, words.at(i).startMs + 1500));
            }
        }
        // Spelling from the segment text when it agrees (tokens can split UTF-8 sequences)
        const QStringList segWords = segText.simplified().split(spaces(), Qt::SkipEmptyParts);
        QList<Word> merged;
        for (const Word &w : words) {
            if (!merged.isEmpty() && isPunctuationOnly(w.text)) {
                merged.last().text += w.text;
                merged.last().endMs = std::max(merged.last().endMs, w.endMs);
            } else {
                merged.append(w);
            }
        }
        if (merged.size() == segWords.size()) {
            for (int i = 0; i < merged.size(); ++i)
                merged[i].text = segWords.at(i);
        } else if (merged.isEmpty() && !segWords.isEmpty()) {
            merged = spreadWords(segText, segFrom, segTo); // -ml 1 output or no tokens
        }
        for (Word &w : merged)
            w.text.remove(QChar::ReplacementCharacter);
        all += merged;
    }
    t.words = collapseRepeats(normalizeWords(all));
    return t;
}

Transcript parseFasterWhisperJson(const QByteArray &json)
{
    Transcript t;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    t.language = root.value(QStringLiteral("language")).toString();
    QList<Word> all;
    for (const QJsonValue &sv : root.value(QStringLiteral("segments")).toArray()) {
        const QJsonObject seg = sv.toObject();
        const QJsonArray words = seg.value(QStringLiteral("words")).toArray();
        if (words.isEmpty()) {
            all += spreadWords(seg.value(QStringLiteral("text")).toString(), qint64(seg.value(QStringLiteral("start")).toDouble() * 1000),
                               qint64(seg.value(QStringLiteral("end")).toDouble() * 1000));
            continue;
        }
        for (const QJsonValue &wv : words) {
            const QJsonObject w = wv.toObject();
            all.append({qint64(std::llround(w.value(QStringLiteral("start")).toDouble() * 1000)),
                        qint64(std::llround(w.value(QStringLiteral("end")).toDouble() * 1000)),
                        w.value(QStringLiteral("word")).toString().trimmed()});
        }
    }
    t.words = collapseRepeats(normalizeWords(all));
    return t;
}

Transcript parseTranscript(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.contains(QStringLiteral("transcription")))
        return parseWhisperCppJson(json);
    if (root.contains(QStringLiteral("segments")))
        return parseFasterWhisperJson(json);
    if (root.contains(QStringLiteral("words")))
        return transcriptFromJson(json);
    return {};
}

QByteArray transcriptToJson(const Transcript &t)
{
    QJsonArray words;
    for (const Word &w : t.words)
        words.append(QJsonArray{w.startMs, w.endMs, w.text});
    return QJsonDocument(QJsonObject{{QStringLiteral("language"), t.language}, {QStringLiteral("words"), words}}).toJson(QJsonDocument::Compact);
}

Transcript transcriptFromJson(const QByteArray &json)
{
    Transcript t;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    t.language = root.value(QStringLiteral("language")).toString();
    for (const QJsonValue &v : root.value(QStringLiteral("words")).toArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() >= 3)
            t.words.append({a.at(0).toInteger(), a.at(1).toInteger(), a.at(2).toString()});
    }
    return t;
}

Transcript fromSubtitleCues(const QList<QPair<QPair<qint64, qint64>, QString>> &cues)
{
    Transcript t;
    for (const auto &c : cues)
        t.words += spreadWords(c.second, c.first.first, c.first.second);
    t.words = normalizeWords(t.words);
    return t;
}

// ================================================================ rules / line breaking

QVariantMap Rules::toMap() const
{
    return {{QStringLiteral("maxCharsPerLine"), maxCharsPerLine}, {QStringLiteral("maxLines"), maxLines},
            {QStringLiteral("maxCps"), maxCps},                   {QStringLiteral("minDurationMs"), minDurationMs},
            {QStringLiteral("maxDurationMs"), maxDurationMs},     {QStringLiteral("maxGapMs"), maxGapMs},
            {QStringLiteral("minGapMs"), minGapMs},               {QStringLiteral("maxWords"), maxWords}};
}

Rules Rules::fromMap(const QVariantMap &m)
{
    return fromMap(m, Rules());
}

Rules Rules::fromMap(const QVariantMap &m, const Rules &d)
{
    Rules r;
    r.maxCharsPerLine = std::clamp(m.value(QStringLiteral("maxCharsPerLine"), d.maxCharsPerLine).toInt(), 0, 80);
    r.maxLines = std::clamp(m.value(QStringLiteral("maxLines"), d.maxLines).toInt(), 1, 2);
    r.maxCps = std::clamp(m.value(QStringLiteral("maxCps"), d.maxCps).toDouble(), 5.0, 40.0);
    r.minDurationMs = std::clamp(m.value(QStringLiteral("minDurationMs"), d.minDurationMs).toInt(), 100, 5000);
    r.maxDurationMs = std::clamp(m.value(QStringLiteral("maxDurationMs"), d.maxDurationMs).toInt(), r.minDurationMs, 20000);
    r.maxGapMs = std::clamp(m.value(QStringLiteral("maxGapMs"), d.maxGapMs).toInt(), 100, 10000);
    r.minGapMs = std::clamp(m.value(QStringLiteral("minGapMs"), d.minGapMs).toInt(), 0, 1000);
    r.maxWords = std::clamp(m.value(QStringLiteral("maxWords"), d.maxWords).toInt(), 0, 50);
    return r;
}

static QString bare(const QString &word)
{
    QString s;
    for (const QChar c : word)
        if (c.isLetterOrNumber() || c == QLatin1Char('\''))
            s += c.toLower();
    return s;
}

// Words a line should not end with (articles, prepositions, contractions) — pt and en
static bool isWeak(const QString &word)
{
    static const QSet<QString> weak = {
        QStringLiteral("o"), QStringLiteral("a"), QStringLiteral("os"), QStringLiteral("as"), QStringLiteral("um"), QStringLiteral("uma"),
        QStringLiteral("uns"), QStringLiteral("umas"), QStringLiteral("de"), QStringLiteral("do"), QStringLiteral("da"), QStringLiteral("dos"),
        QStringLiteral("das"), QStringLiteral("em"), QStringLiteral("no"), QStringLiteral("na"), QStringLiteral("nos"), QStringLiteral("nas"),
        QStringLiteral("ao"), QStringLiteral("aos"), QStringLiteral("à"), QStringLiteral("às"), QStringLiteral("por"), QStringLiteral("pelo"),
        QStringLiteral("pela"), QStringLiteral("com"), QStringLiteral("para"), QStringLiteral("pra"), QStringLiteral("pro"), QStringLiteral("sem"),
        QStringLiteral("e"), QStringLiteral("ou"), QStringLiteral("que"), QStringLiteral("se"), QStringLiteral("mais"), QStringLiteral("muito"),
        QStringLiteral("meu"), QStringLiteral("minha"), QStringLiteral("seu"), QStringLiteral("sua"), QStringLiteral("the"), QStringLiteral("an"),
        QStringLiteral("of"), QStringLiteral("to"), QStringLiteral("in"), QStringLiteral("on"), QStringLiteral("at"), QStringLiteral("for"),
        QStringLiteral("with"), QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("my"), QStringLiteral("your"), QStringLiteral("el"),
        QStringLiteral("la"), QStringLiteral("los"), QStringLiteral("las"), QStringLiteral("del"), QStringLiteral("en"), QStringLiteral("y")};
    if (word.isEmpty() || !word.at(word.size() - 1).isLetterOrNumber())
        return false; // "de," is a phrase end
    return weak.contains(bare(word));
}

// Words a new line (or cue) may start with: conjunctions and relative pronouns
static bool startsClause(const QString &word)
{
    static const QSet<QString> conj = {
        QStringLiteral("e"), QStringLiteral("mas"), QStringLiteral("ou"), QStringLiteral("que"), QStringLiteral("porque"), QStringLiteral("quando"),
        QStringLiteral("se"), QStringLiteral("então"), QStringLiteral("como"), QStringLiteral("onde"), QStringLiteral("enquanto"), QStringLiteral("pois"),
        QStringLiteral("and"), QStringLiteral("but"), QStringLiteral("or"), QStringLiteral("because"), QStringLiteral("when"), QStringLiteral("so"),
        QStringLiteral("which"), QStringLiteral("that"), QStringLiteral("if"), QStringLiteral("pero"), QStringLiteral("porque"), QStringLiteral("cuando")};
    return conj.contains(bare(word));
}

static bool endsSentence(const QString &word)
{
    if (word.isEmpty())
        return false;
    QString w = word;
    while (!w.isEmpty() && (w.endsWith(QLatin1Char('"')) || w.endsWith(QLatin1Char(')')) || w.endsWith(QChar(0x201D))))
        w.chop(1);
    return w.endsWith(QLatin1Char('.')) || w.endsWith(QLatin1Char('?')) || w.endsWith(QLatin1Char('!')) || w.endsWith(QChar(0x2026));
}

static bool endsClause(const QString &word)
{
    return word.endsWith(QLatin1Char(',')) || word.endsWith(QLatin1Char(';')) || word.endsWith(QLatin1Char(':'))
        || word.endsWith(QChar(0x2014));
}

static int joinedLength(const QList<Word> &words, int from, int to) // [from, to)
{
    int n = 0;
    for (int i = from; i < to; ++i)
        n += words.at(i).text.size() + (i > from ? 1 : 0);
    return n;
}

static QString joinRange(const QList<Word> &words, int from, int to)
{
    QStringList parts;
    for (int i = from; i < to; ++i)
        parts << words.at(i).text;
    return parts.join(QLatin1Char(' '));
}

QStringList breakLines(const QList<Word> &words, int maxChars, int maxLines)
{
    const int n = words.size();
    if (n == 0)
        return {};
    maxChars = std::max(1, maxChars);
    const int total = joinedLength(words, 0, n);
    if (total <= maxChars || n == 1)
        return {joinRange(words, 0, n)};
    // Two words a bit over the limit read better as one line (shrunk to fit by toAss)
    // than as two lines of one word each
    if (n == 2 && total * 100 <= maxChars * 135)
        return {joinRange(words, 0, n)};
    if (maxLines < 2)
        return {};
    int best = -1;
    double bestCost = std::numeric_limits<double>::max();
    for (int k = 1; k < n; ++k) {
        const int l1 = joinedLength(words, 0, k);
        const int l2 = joinedLength(words, k, n);
        if (l1 > maxChars || l2 > maxChars)
            continue;
        double cost = std::abs(l1 - l2);
        if (n >= 3 && (k == 1 || k == n - 1))
            cost += 1000; // a lone word on a line
        const QString &last = words.at(k - 1).text;
        if (endsSentence(last))
            cost -= 15;
        else if (endsClause(last))
            cost -= 20;
        if (isWeak(last))
            cost += 15;
        if (startsClause(words.at(k).text))
            cost -= 6;
        if (l1 > l2)
            cost += 2; // slight preference for a wider bottom line
        if (cost < bestCost) {
            bestCost = cost;
            best = k;
        }
    }
    // n >= 3 and every split leaves a lone word: it doesn't fit as two lines
    if (best < 0 || bestCost >= 900)
        return {};
    return {joinRange(words, 0, best), joinRange(words, best, n)};
}

QStringList wrapLines(const QList<Word> &words, int maxChars)
{
    QStringList lines;
    QString line;
    for (const Word &w : words) {
        if (!line.isEmpty() && line.size() + 1 + w.text.size() > maxChars) {
            lines << line;
            line.clear();
        }
        line += (line.isEmpty() ? QString() : QStringLiteral(" ")) + w.text;
    }
    if (!line.isEmpty())
        lines << line;
    return lines;
}

int Cue::characters() const
{
    int n = 0;
    for (const QString &l : lines)
        n += l.size();
    return n;
}

QList<Cue> buildCues(const QList<Word> &input, const Rules &rules)
{
    const QList<Word> words = normalizeWords(input);
    const int maxChars = rules.maxCharsPerLine > 0 ? rules.maxCharsPerLine : 32;
    const int capacity = maxChars * rules.maxLines;
    QList<QList<Word>> groups;
    QList<Word> cur;

    auto fits = [&](const QList<Word> &g) {
        if (g.size() <= 1)
            return true;
        if (rules.maxWords > 0 && g.size() > rules.maxWords)
            return false;
        if (g.last().endMs - g.first().startMs > rules.maxDurationMs)
            return false;
        return !breakLines(g, maxChars, rules.maxLines).isEmpty();
    };
    auto flush = [&]() {
        if (!cur.isEmpty())
            groups.append(cur);
        cur.clear();
    };

    for (const Word &w : words) {
        if (!cur.isEmpty()) {
            const qint64 gap = w.startMs - cur.last().endMs;
            QList<Word> trial = cur;
            trial.append(w);
            if (gap > rules.maxGapMs) {
                flush();
            } else if (!fits(trial)) {
                // Don't end a cue on an article/preposition: carry it over
                QList<Word> carry;
                while (cur.size() > 1 && carry.size() < 2 && isWeak(cur.last().text))
                    carry.prepend(cur.takeLast());
                flush();
                cur = carry;
                QList<Word> again = cur;
                again.append(w);
                if (!fits(again))
                    flush();
            }
        }
        cur.append(w);
        const int chars = joinedLength(cur, 0, cur.size());
        if (endsSentence(w.text))
            flush();
        else if (endsClause(w.text) && chars * 10 >= capacity * 6)
            flush();
    }
    flush();

    // No orphan cues: a lone word joins its neighbour, or takes a word from it
    for (int i = 0; i < groups.size(); ++i) {
        if (groups.at(i).size() != 1 || groups.size() < 2)
            continue;
        const bool hasPrev = i > 0 && !endsSentence(groups.at(i - 1).last().text)
            && groups.at(i).first().startMs - groups.at(i - 1).last().endMs <= rules.maxGapMs;
        const bool hasNext = i + 1 < groups.size() && !endsSentence(groups.at(i).last().text)
            && groups.at(i + 1).first().startMs - groups.at(i).last().endMs <= rules.maxGapMs;
        if (hasPrev) {
            QList<Word> joined = groups.at(i - 1) + groups.at(i);
            if (fits(joined)) {
                groups[i - 1] = joined;
                groups.removeAt(i);
                --i;
                continue;
            }
            if (groups.at(i - 1).size() >= 3) {
                groups[i].prepend(groups[i - 1].takeLast());
                continue;
            }
        }
        if (hasNext) {
            QList<Word> joined = groups.at(i) + groups.at(i + 1);
            if (fits(joined)) {
                groups[i] = joined;
                groups.removeAt(i + 1);
                continue;
            }
            if (groups.at(i + 1).size() >= 3)
                groups[i].append(groups[i + 1].takeFirst());
        }
    }

    QList<Cue> cues;
    for (const QList<Word> &g : std::as_const(groups)) {
        if (g.isEmpty())
            continue;
        Cue c;
        c.words = g;
        c.startMs = g.first().startMs;
        c.endMs = std::max(g.last().endMs, c.startMs + 1);
        c.lines = breakLines(g, maxChars, rules.maxLines);
        if (c.lines.isEmpty())
            c.lines = {joinWords(g)};
        cues.append(c);
    }
    applyTiming(cues, rules);
    return cues;
}

void applyTiming(QList<Cue> &cues, const Rules &rules)
{
    for (int i = 0; i < cues.size(); ++i) {
        Cue &c = cues[i];
        const qint64 speechEnd = c.endMs;
        const qint64 next = i + 1 < cues.size() ? cues.at(i + 1).startMs : std::numeric_limits<qint64>::max() / 2;
        const qint64 cpsEnd = c.startMs + qint64(std::ceil(c.characters() * 1000.0 / rules.maxCps));
        qint64 wanted = std::max({speechEnd, c.startMs + rules.minDurationMs, cpsEnd});
        wanted = std::min(wanted, std::max(speechEnd, c.startMs + rules.maxDurationMs));
        const qint64 limit = next - rules.minGapMs;
        if (wanted > limit)
            wanted = std::max(std::min(speechEnd, next), std::min(wanted, limit));
        c.endMs = std::max(c.startMs + 1, wanted);
    }
}

// ================================================================ styles

QVariantMap Style::toMap() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("effect"), effect},
            {QStringLiteral("font"), font},
            {QStringLiteral("size"), size},
            {QStringLiteral("weight"), weight},
            {QStringLiteral("uppercase"), uppercase},
            {QStringLiteral("textColor"), textColor},
            {QStringLiteral("activeColor"), activeColor},
            {QStringLiteral("outlineColor"), outlineColor},
            {QStringLiteral("boxColor"), boxColor},
            {QStringLiteral("speaker2Color"), speaker2Color},
            {QStringLiteral("outline"), outline},
            {QStringLiteral("shadow"), shadow},
            {QStringLiteral("position"), position},
            {QStringLiteral("offset"), offset},
            {QStringLiteral("wordsPerGroup"), wordsPerGroup},
            {QStringLiteral("safeTop"), safeArea.top},
            {QStringLiteral("safeBottom"), safeArea.bottom},
            {QStringLiteral("safeLeft"), safeArea.left},
            {QStringLiteral("safeRight"), safeArea.right},
            {QStringLiteral("rules"), rules.toMap()}};
}

Style Style::fromMap(const QVariantMap &m)
{
    Style s = preset(m.value(QStringLiteral("id"), QStringLiteral("classic")).toString());
    auto str = [&](const char *k, QString &v) {
        const QString key = QLatin1String(k);
        if (m.contains(key))
            v = m.value(key).toString();
    };
    auto num = [&](const char *k, double &v, double lo, double hi) {
        const QString key = QLatin1String(k);
        if (m.contains(key))
            v = std::clamp(m.value(key).toDouble(), lo, hi);
    };
    str("effect", s.effect);
    str("font", s.font);
    str("textColor", s.textColor);
    str("activeColor", s.activeColor);
    str("outlineColor", s.outlineColor);
    str("boxColor", s.boxColor);
    str("speaker2Color", s.speaker2Color);
    str("position", s.position);
    num("size", s.size, 1, 30);
    num("outline", s.outline, 0, 40);
    num("shadow", s.shadow, 0, 20);
    num("offset", s.offset, -40, 40);
    num("safeTop", s.safeArea.top, 0, 0.45);
    num("safeBottom", s.safeArea.bottom, 0, 0.45);
    num("safeLeft", s.safeArea.left, 0, 0.45);
    num("safeRight", s.safeArea.right, 0, 0.45);
    if (m.contains(QStringLiteral("weight")))
        s.weight = std::clamp(m.value(QStringLiteral("weight")).toInt(), 100, 900);
    if (m.contains(QStringLiteral("uppercase")))
        s.uppercase = m.value(QStringLiteral("uppercase")).toBool();
    if (m.contains(QStringLiteral("wordsPerGroup")))
        s.wordsPerGroup = std::clamp(m.value(QStringLiteral("wordsPerGroup")).toInt(), 1, 4);
    if (m.contains(QStringLiteral("rules")))
        s.rules = Rules::fromMap(m.value(QStringLiteral("rules")).toMap(), s.rules);
    return s;
}

QStringList Style::presetIds()
{
    return {QStringLiteral("classic"), QStringLiteral("box"),     QStringLiteral("karaoke"),
            QStringLiteral("highlight"), QStringLiteral("word"),  QStringLiteral("podcast"),
            QStringLiteral("neon"),    QStringLiteral("contrast"), QStringLiteral("twocolor")};
}

QString Style::presetName(const QString &id)
{
    if (id == QLatin1String("classic"))
        return QCoreApplication::translate("Captions", "Classic");
    if (id == QLatin1String("box"))
        return QCoreApplication::translate("Captions", "Box");
    if (id == QLatin1String("karaoke"))
        return QCoreApplication::translate("Captions", "Karaoke");
    if (id == QLatin1String("highlight"))
        return QCoreApplication::translate("Captions", "Highlight");
    if (id == QLatin1String("word"))
        return QCoreApplication::translate("Captions", "Word by word");
    if (id == QLatin1String("podcast"))
        return QCoreApplication::translate("Captions", "Bold podcast style");
    if (id == QLatin1String("neon"))
        return QCoreApplication::translate("Captions", "Neon glow");
    if (id == QLatin1String("contrast"))
        return QCoreApplication::translate("Captions", "High contrast (accessible)");
    if (id == QLatin1String("twocolor"))
        return QCoreApplication::translate("Captions", "Two colors (speakers)");
    return id;
}

Style Style::preset(const QString &id)
{
    Style s;
    s.id = id;
    if (id == QLatin1String("box")) {
        s.effect = QStringLiteral("box");
        s.weight = 600;
        s.size = 6;
        s.boxColor = QStringLiteral("#b3000000");
        s.outline = 12; // box padding
    } else if (id == QLatin1String("karaoke")) {
        s.effect = QStringLiteral("karaoke");
        s.weight = 800;
        s.size = 7;
        s.textColor = QStringLiteral("#ffffff");  // not yet spoken
        s.activeColor = QStringLiteral("#ffd60a"); // spoken (sweep fill)
        s.outline = 5;
    } else if (id == QLatin1String("highlight")) {
        s.effect = QStringLiteral("highlight");
        s.weight = 800;
        s.size = 7;
        s.activeColor = QStringLiteral("#7c4dff"); // pill behind the current word
        s.outline = 4;
    } else if (id == QLatin1String("word")) {
        s.effect = QStringLiteral("word");
        s.weight = 900;
        s.size = 10;
        s.uppercase = true;
        s.outline = 7;
        s.shadow = 2;
        s.position = QStringLiteral("middle");
        s.offset = 12;
        s.wordsPerGroup = 1;
        s.rules.maxLines = 1;
        s.rules.maxWords = 4;
    } else if (id == QLatin1String("podcast")) {
        s.effect = QStringLiteral("podcast");
        s.weight = 900;
        s.size = 7.5;
        s.uppercase = true;
        s.activeColor = QStringLiteral("#ffd60a"); // keywords
        s.outline = 7;
        s.shadow = 3;
        s.position = QStringLiteral("middle");
        s.offset = 15;
    } else if (id == QLatin1String("neon")) {
        s.effect = QStringLiteral("neon");
        s.weight = 700;
        s.size = 7;
        s.textColor = QStringLiteral("#f2feff");
        s.outlineColor = QStringLiteral("#00e5ff"); // glow
        s.outline = 3;
    } else if (id == QLatin1String("contrast")) {
        s.effect = QStringLiteral("box");
        s.weight = 700;
        s.size = 8.5;
        s.textColor = QStringLiteral("#ffff00");
        s.boxColor = QStringLiteral("#ff000000");
        s.outline = 14;
        s.rules.maxCps = 15;
    } else if (id == QLatin1String("twocolor")) {
        s.effect = QStringLiteral("twocolor");
        s.textColor = QStringLiteral("#ffd60a");     // speaker 1
        s.speaker2Color = QStringLiteral("#5ac8fa"); // speaker 2
    } else {
        s.id = QStringLiteral("classic");
        s.shadow = 1;
    }
    return s;
}

QList<Style> Style::presets()
{
    QList<Style> list;
    for (const QString &id : presetIds())
        list.append(preset(id));
    return list;
}

int Style::charsPerLine(int frameWidth, int frameHeight) const
{
    if (rules.maxCharsPerLine > 0)
        return rules.maxCharsPerLine;
    const double fontPx = size / 100.0 * std::min(frameWidth, frameHeight);
    const double usable = frameWidth * (1.0 - safeArea.left - safeArea.right) * 0.9;
    const double perChar = fontPx * (uppercase ? 0.66 : 0.55) * (weight >= 800 ? 1.06 : 1.0);
    return std::clamp(int(usable / std::max(1.0, perChar)), 8, 48);
}

Rules Style::effectiveRules(int frameWidth, int frameHeight) const
{
    Rules r = rules;
    r.maxCharsPerLine = charsPerLine(frameWidth, frameHeight);
    return r;
}

// ================================================================ ASS

QString assTime(qint64 ms)
{
    const qint64 cs = std::max<qint64>(0, ms) / 10;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(cs / 360000)
        .arg((cs / 6000) % 60, 2, 10, QLatin1Char('0'))
        .arg((cs / 100) % 60, 2, 10, QLatin1Char('0'))
        .arg(cs % 100, 2, 10, QLatin1Char('0'));
}

QString assColor(const QString &color)
{
    const QColor c(color);
    if (!c.isValid())
        return QStringLiteral("&H00FFFFFF");
    return QStringLiteral("&H%1%2%3%4")
        .arg(255 - c.alpha(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

// Override-tag colour (no alpha): &HBBGGRR&
static QString tagColor(const QString &color)
{
    const QColor c(color);
    return QStringLiteral("&H%1%2%3&").arg(c.blue(), 2, 16, QLatin1Char('0')).arg(c.green(), 2, 16, QLatin1Char('0')).arg(c.red(), 2, 16, QLatin1Char('0')).toUpper();
}

static QString escapeAss(QString s)
{
    s.replace(QLatin1Char('\\'), QChar(0xFF3C));
    s.replace(QLatin1Char('{'), QChar(0xFF5B));
    s.replace(QLatin1Char('}'), QChar(0xFF5D));
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return s;
}

namespace {

struct AssContext {
    const Style &style;
    int width;
    int height;
    double fps;
    double scale; // px for a 1080 short side → frame px
    QLocale locale;

    // Frame-accurate time in centiseconds: snap to the nearest frame, then floor
    qint64 cs(qint64 ms) const
    {
        if (fps <= 0)
            return ms / 10;
        const double frame = std::round(ms * fps / 1000.0);
        const double t = frame * 1000.0 / fps;
        return qint64(std::floor(t / 10.0 + 1e-6));
    }
    QString time(qint64 centis) const { return assTime(centis * 10); }
    QString word(const QString &w) const { return escapeAss(style.uppercase ? locale.toUpper(w) : w); }
};

// Words of a cue with the separator that follows each (" " or "\N")
QList<QPair<QString, QString>> layout(const Cue &cue, const AssContext &ctx)
{
    QList<QPair<QString, QString>> out;
    QList<int> breaks; // word index after which a line ends
    int count = 0;
    for (int l = 0; l + 1 < cue.lines.size(); ++l) {
        count += cue.lines.at(l).split(QLatin1Char(' '), Qt::SkipEmptyParts).size();
        breaks << count - 1;
    }
    for (int i = 0; i < cue.words.size(); ++i)
        out.append({ctx.word(cue.words.at(i).text), i + 1 == cue.words.size() ? QString() : breaks.contains(i) ? QStringLiteral("\\N") : QStringLiteral(" ")});
    return out;
}

QString plainText(const QList<QPair<QString, QString>> &l)
{
    QString s;
    for (const auto &p : l)
        s += p.first + p.second;
    return s;
}

QString dialogue(int layer, qint64 startCs, qint64 endCs, const QString &style, const QString &text, const AssContext &ctx)
{
    return QStringLiteral("Dialogue: %1,%2,%3,%4,,0,0,0,,%5\n").arg(layer).arg(ctx.time(startCs), ctx.time(endCs), style, text);
}

} // namespace

QString toAss(const QList<Cue> &cues, const Style &style, int width, int height, double fps)
{
    const int shortSide = std::max(1, std::min(width, height));
    AssContext ctx{style, width, height, fps, shortSide / 1080.0, QLocale(QLocale::Portuguese, QLocale::Brazil)};
    const double fontPx = style.size / 100.0 * shortSide;
    const double outline = style.outline * ctx.scale;
    const double shadow = style.shadow * ctx.scale;
    const int alignment = style.position == QLatin1String("top") ? 8 : style.position == QLatin1String("middle") ? 5 : 2;
    const int marginL = int(std::lround(width * (style.safeArea.left + 0.04)));
    const int marginR = int(std::lround(width * (style.safeArea.right + 0.04)));
    int marginV = 0;
    if (alignment == 2)
        marginV = int(std::lround(height * (style.safeArea.bottom + 0.03 + style.offset / 100.0)));
    else if (alignment == 8)
        marginV = int(std::lround(height * (style.safeArea.top + 0.03 + style.offset / 100.0)));
    const bool bold = style.weight >= 600;
    const QString weightTag = (style.weight != 400 && style.weight != 700) ? QStringLiteral("\\b%1").arg(style.weight) : QString();

    auto styleLine = [&](const QString &name, const QString &primary, const QString &secondary, const QString &outlineCol,
                         const QString &back, int borderStyle, double outlineW, double shadowW) {
        return QStringLiteral("Style: %1,%2,%3,%4,%5,%6,%7,%8,0,0,0,100,100,0,0,%9,%10,%11,%12,%13,%14,%15,1\n")
            .arg(name, style.font)
            .arg(int(std::lround(fontPx)))
            .arg(assColor(primary), assColor(secondary), assColor(outlineCol), assColor(back))
            .arg(bold ? -1 : 0)
            .arg(borderStyle)
            .arg(QString::number(outlineW, 'f', 1), QString::number(shadowW, 'f', 1))
            .arg(alignment)
            .arg(marginL)
            .arg(marginR)
            .arg(marginV);
    };

    QString out;
    out += QStringLiteral("[Script Info]\n; Lingmo Studio captions (style: %1)\nScriptType: v4.00+\nPlayResX: %2\nPlayResY: %3\n"
                          "WrapStyle: 2\nScaledBorderAndShadow: yes\nYCbCr Matrix: None\n\n")
               .arg(style.id)
               .arg(width)
               .arg(height);
    out += QStringLiteral("[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
                          "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n");
    const QString shadowCol = QStringLiteral("#80000000");
    const QString e = style.effect;
    if (e == QLatin1String("box")) {
        out += styleLine(QStringLiteral("Main"), style.textColor, style.textColor, style.boxColor, QStringLiteral("#00000000"), 3, outline, 0);
    } else if (e == QLatin1String("karaoke")) {
        // \kf fills from SecondaryColour (not yet spoken) to PrimaryColour (spoken)
        out += styleLine(QStringLiteral("Main"), style.activeColor, style.textColor, style.outlineColor, shadowCol, 1, outline, shadow);
    } else if (e == QLatin1String("neon")) {
        out += styleLine(QStringLiteral("Main"), style.textColor, style.textColor, style.outlineColor, QStringLiteral("#00000000"), 1, std::max(1.0, outline * 0.5), 0);
        out += styleLine(QStringLiteral("Glow"), style.outlineColor, style.outlineColor, style.outlineColor, QStringLiteral("#00000000"), 1, outline * 2.5, 0);
    } else {
        out += styleLine(QStringLiteral("Main"), style.textColor, style.textColor, style.outlineColor, shadowCol, 1, outline, shadow);
    }
    if (e == QLatin1String("highlight"))
        out += styleLine(QStringLiteral("Pill"), style.textColor, style.textColor, style.activeColor, QStringLiteral("#00000000"), 3, std::max(4.0, outline * 2.2), 0);
    if (e == QLatin1String("twocolor"))
        out += styleLine(QStringLiteral("Speaker2"), style.speaker2Color, style.speaker2Color, style.outlineColor, shadowCol, 1, outline, shadow);
    out += QStringLiteral("\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");

    // Width a line may use, and a pessimistic width per character of the heavy caption fonts
    const double available = std::max(1, width - marginL - marginR);
    const double charPx = fontPx * (style.uppercase ? 0.66 : 0.58);
    for (const Cue &cue : cues) {
        if (cue.words.isEmpty())
            continue;
        const qint64 start = ctx.cs(cue.startMs);
        const qint64 end = ctx.cs(cue.endMs);
        if (end <= start)
            continue;
        // WrapStyle 2 never wraps: shrink a cue whose longest line would leave the safe area
        int longest = 0;
        for (const QString &l : cue.lines)
            longest = std::max<int>(longest, l.size());
        const int fit = std::clamp(int(available / std::max(1.0, longest * charPx) * 100), 60, 100);
        auto pct = [fit](int v) { return v * fit / 100; };
        const QString fitTag = fit < 100 ? QStringLiteral("\\fscx%1\\fscy%1").arg(fit) : QString();
        const QString prefix = (weightTag.isEmpty() && fitTag.isEmpty()) ? QString() : QStringLiteral("{%1%2}").arg(weightTag, fitTag);
        const auto words = layout(cue, ctx);
        auto wordStart = [&](int i) { return std::clamp(ctx.cs(cue.words.at(i).startMs), start, end); };
        auto wordEnd = [&](int i) { return std::clamp(ctx.cs(cue.words.at(i).endMs), start, end); };

        if (e == QLatin1String("karaoke")) {
            QString text = prefix;
            qint64 t = start;
            for (int i = 0; i < words.size(); ++i) {
                const qint64 ws = std::max(t, wordStart(i));
                if (ws > t)
                    text += QStringLiteral("{\\k%1}").arg(ws - t);
                const qint64 we = std::max(ws + 1, wordEnd(i));
                text += QStringLiteral("{\\kf%1}").arg(we - ws) + words.at(i).first + words.at(i).second;
                t = we;
            }
            out += dialogue(0, start, end, QStringLiteral("Main"), text, ctx);
        } else if (e == QLatin1String("highlight")) {
            out += dialogue(0, start, end, QStringLiteral("Main"), prefix + plainText(words), ctx);
            for (int i = 0; i < words.size(); ++i) {
                const qint64 s = i == 0 ? start : wordStart(i);
                const qint64 en = i + 1 < words.size() ? wordStart(i + 1) : end;
                if (en <= s)
                    continue;
                QString text = prefix;
                for (int j = 0; j < words.size(); ++j) {
                    if (j == i)
                        text += QStringLiteral("{\\alpha&H00&}") + words.at(j).first + QStringLiteral("{\\alpha&HFF&}") + words.at(j).second;
                    else
                        text += QStringLiteral("{\\alpha&HFF&}") + words.at(j).first + words.at(j).second;
                }
                out += dialogue(1, s, en, QStringLiteral("Pill"), text, ctx);
            }
        } else if (e == QLatin1String("word")) {
            const int n = std::max(1, style.wordsPerGroup);
            for (int g = 0; g < words.size(); g += n) {
                const qint64 s = g == 0 ? start : wordStart(g);
                const qint64 en = g + n < words.size() ? wordStart(g + n) : end;
                if (en <= s)
                    continue;
                QStringList group;
                for (int j = g; j < std::min<int>(g + n, words.size()); ++j)
                    group << words.at(j).first;
                out += dialogue(0, s, en, QStringLiteral("Main"),
                                QStringLiteral("{%1\\fscx%2\\fscy%2\\t(0,90,\\fscx%3\\fscy%3)\\t(90,150,\\fscx%4\\fscy%4)}")
                                        .arg(weightTag).arg(pct(70)).arg(pct(108)).arg(fit)
                                    + group.join(QLatin1Char(' ')),
                                ctx);
            }
        } else if (e == QLatin1String("podcast")) {
            // keywords: marked words, otherwise the longest content word of each line
            QSet<int> keys;
            for (int i = 0; i < cue.words.size(); ++i)
                if (cue.words.at(i).emphasis)
                    keys.insert(i);
            if (keys.isEmpty()) {
                int best = -1, bestLen = 4;
                for (int i = 0; i < cue.words.size(); ++i) {
                    const int len = bare(cue.words.at(i).text).size();
                    if (len > bestLen && !isWeak(cue.words.at(i).text)) {
                        best = i;
                        bestLen = len;
                    }
                }
                if (best >= 0)
                    keys.insert(best);
            }
            QString text = QStringLiteral("{%1\\fscx%2\\fscy%2\\t(0,100,\\fscx%3\\fscy%3)}").arg(weightTag).arg(pct(92)).arg(fit);
            for (int i = 0; i < words.size(); ++i) {
                if (keys.contains(i))
                    text += QStringLiteral("{\\1c%1}").arg(tagColor(style.activeColor)) + words.at(i).first + QStringLiteral("{\\1c%1}").arg(tagColor(style.textColor));
                else
                    text += words.at(i).first;
                text += words.at(i).second;
            }
            out += dialogue(0, start, end, QStringLiteral("Main"), text, ctx);
        } else if (e == QLatin1String("neon")) {
            out += dialogue(0, start, end, QStringLiteral("Glow"), QStringLiteral("{%1%2\\blur%3}").arg(weightTag, fitTag).arg(std::max(2.0, 6 * ctx.scale), 0, 'f', 1) + plainText(words), ctx);
            out += dialogue(1, start, end, QStringLiteral("Main"), QStringLiteral("{%1%2\\blur1}").arg(weightTag, fitTag) + plainText(words), ctx);
        } else if (e == QLatin1String("twocolor")) {
            out += dialogue(0, start, end, cue.speaker == 2 ? QStringLiteral("Speaker2") : QStringLiteral("Main"), prefix + plainText(words), ctx);
        } else {
            out += dialogue(0, start, end, QStringLiteral("Main"), prefix + plainText(words), ctx);
        }
    }
    return out;
}

static QString srtTime(qint64 ms, QChar sep)
{
    ms = std::max<qint64>(0, ms);
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(sep)
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString toSrt(const QList<Cue> &cues)
{
    QString out;
    int n = 0;
    for (const Cue &c : cues)
        out += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(++n).arg(srtTime(c.startMs, QLatin1Char(',')), srtTime(c.endMs, QLatin1Char(',')), c.text());
    return out;
}

QString toVtt(const QList<Cue> &cues)
{
    QString out = QStringLiteral("WEBVTT\n\n");
    for (const Cue &c : cues) {
        QString text = c.text();
        text.replace(QLatin1String("-->"), QStringLiteral("->"));
        out += QStringLiteral("%1 --> %2\n%3\n\n").arg(srtTime(c.startMs, QLatin1Char('.')), srtTime(c.endMs, QLatin1Char('.')), text);
    }
    return out;
}

// ================================================================ caption clips

bool isCaption(const Clip &c)
{
    return c.kind == ClipKind;
}

QList<Word> clipWords(const Clip &c)
{
    QList<Word> words;
    for (const QVariant &v : c.genParams.value(QStringLiteral("words")).toList()) {
        const QVariantMap m = v.toMap();
        Word w;
        w.startMs = m.value(QStringLiteral("s")).toLongLong();
        w.endMs = m.value(QStringLiteral("e")).toLongLong();
        w.text = m.value(QStringLiteral("t")).toString();
        w.emphasis = m.value(QStringLiteral("k")).toBool();
        words.append(w);
    }
    return words;
}

void setClipWords(Clip &c, const QList<Word> &words)
{
    QVariantList list;
    for (const Word &w : words) {
        QVariantMap m{{QStringLiteral("s"), w.startMs}, {QStringLiteral("e"), w.endMs}, {QStringLiteral("t"), w.text}};
        if (w.emphasis)
            m.insert(QStringLiteral("k"), true);
        list.append(m);
    }
    c.genParams.insert(QStringLiteral("words"), list);
    c.name = joinWords(words);
}

int clipSpeaker(const Clip &c)
{
    return c.genParams.value(QStringLiteral("speaker"), 1).toInt() == 2 ? 2 : 1;
}

double clipFps(const Clip &c)
{
    const double f = c.genParams.value(QStringLiteral("fps")).toDouble();
    return f > 0 ? f : 30.0;
}

double originMs(const Clip &c, double fps)
{
    return (c.position - c.in) * 1000.0 / (fps > 0 ? fps : 30.0);
}

Clip makeClip(const Cue &cue, double fps)
{
    Clip c;
    c.id = newId(QStringLiteral("c"));
    c.kind = ClipKind;
    const int start = int(std::lround(cue.startMs * fps / 1000.0));
    const int end = std::max(start + 1, int(std::lround(cue.endMs * fps / 1000.0)));
    c.position = start;
    c.in = start; // source origin = timeline 0: word times are timeline ms
    c.out = end - 1;
    setClipWords(c, cue.words);
    c.genParams.insert(QStringLiteral("fps"), fps);
    if (cue.speaker == 2)
        c.genParams.insert(QStringLiteral("speaker"), 2);
    return c;
}

bool trackHasCaptions(const Track &t)
{
    if (t.kind != TrackKind::Subtitle)
        return false;
    for (const Clip &c : t.clips)
        if (isCaption(c))
            return true;
    return false;
}

QList<Cue> trackCues(const Track &t, double fps, const Style &style, int width, int height)
{
    QList<Cue> cues;
    const Rules rules = style.effectiveRules(width, height);
    for (const Clip &c : t.clips) {
        if (!isCaption(c))
            continue;
        const double origin = originMs(c, fps);
        Cue cue;
        cue.startMs = qint64(std::llround(c.position * 1000.0 / fps));
        cue.endMs = qint64(std::llround(c.end() * 1000.0 / fps));
        cue.speaker = clipSpeaker(c);
        for (Word w : clipWords(c)) {
            w.startMs = qint64(std::llround(origin + w.startMs));
            w.endMs = qint64(std::llround(origin + w.endMs));
            if (w.endMs <= cue.startMs || w.startMs >= cue.endMs)
                continue;
            w.startMs = std::max(w.startMs, cue.startMs);
            w.endMs = std::min(w.endMs, cue.endMs);
            cue.words.append(w);
        }
        if (cue.words.isEmpty())
            continue;
        cue.lines = breakLines(cue.words, rules.maxCharsPerLine, rules.maxLines);
        if (cue.lines.isEmpty()) // edited text longer than the rules allow: wrap rather than lose words
            cue.lines = wrapLines(cue.words, rules.maxCharsPerLine);
        cues.append(cue);
    }
    return cues;
}

} // namespace Studio::Captions
