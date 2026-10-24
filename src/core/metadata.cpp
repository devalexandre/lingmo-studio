/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "metadata.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace Studio::Metadata {

QString condense(const QString &text, int maxChars)
{
    const QString t = text.simplified();
    if (t.size() <= maxChars || maxChars < 40)
        return t.left(maxChars);
    const int head = maxChars / 2, mid = maxChars / 4, tail = maxChars - head - mid - 12;
    const int midStart = (t.size() - mid) / 2;
    return t.left(head) + QStringLiteral(" […] ") + t.mid(midStart, mid) + QStringLiteral(" […] ") + t.right(tail);
}

QString cleanTitle(const QString &s, int maxChars)
{
    QString t = s.simplified();
    static const QString quotes = QStringLiteral("\"'`“”‘’«»*# ");
    while (!t.isEmpty() && quotes.contains(t.front()))
        t.remove(0, 1);
    while (!t.isEmpty() && quotes.contains(t.back()))
        t.chop(1);
    if (t.size() > maxChars) {
        t = t.left(maxChars);
        const int space = t.lastIndexOf(QLatin1Char(' '));
        if (space > maxChars / 2)
            t = t.left(space);
    }
    return t;
}

QStringList cleanTags(const QStringList &tags, int maxCount)
{
    QStringList out;
    QSet<QString> seen;
    for (const QString &raw : tags) {
        QString t = raw.simplified();
        while (t.startsWith(QLatin1Char('#')))
            t.remove(0, 1);
        t = t.simplified();
        if (t.isEmpty() || t.size() > 30 || seen.contains(t.toLower()))
            continue;
        seen.insert(t.toLower());
        out << t;
        if (out.size() >= maxCount)
            break;
    }
    return out;
}

// ------------------------------------------------------------------ LLM

QByteArray llmRequest(const QString &model, const Input &in)
{
    const bool pt = in.language.startsWith(QLatin1String("pt"));
    const QString lang = pt ? QStringLiteral("Brazilian Portuguese") : QStringLiteral("the language of the transcript");
    const QString system = QStringLiteral(
        "You write the publishing text of a video from its transcript, for YouTube. Be accurate: only say what the "
        "video is about, never invent facts. Answer ONLY with JSON: "
        "{\"title\": \"<clear, curiosity-driven title, max 70 characters, no clickbait lies, no quotes, no emoji>\", "
        "\"description\": \"<2 or 3 short paragraphs: what the video is about and what the viewer learns; plain text, no hashtags>\", "
        "\"tags\": [\"<5 to 10 search keywords>\"], "
        "\"thumbnail_text\": \"<the hook for the thumbnail, 2 to 4 words, max 28 characters>\"}. "
        "Write everything in %1.")
                               .arg(lang);

    QStringList user;
    if (!in.projectName.trimmed().isEmpty())
        user << QStringLiteral("Project name: %1").arg(in.projectName.trimmed());
    if (in.durationSec > 0)
        user << QStringLiteral("Duration: %1").arg(timestamp(int(in.durationSec)));
    if (!in.onScreenText.isEmpty())
        user << QStringLiteral("Text shown on screen: %1").arg(in.onScreenText.mid(0, 12).join(QStringLiteral(" | ")).left(600));
    QStringList marks;
    for (const Chapter &c : in.chapters)
        if (!c.title.trimmed().isEmpty())
            marks << QStringLiteral("%1 %2").arg(timestamp(c.seconds), c.title.trimmed());
    if (!marks.isEmpty())
        user << QStringLiteral("Chapters: %1").arg(marks.mid(0, 20).join(QStringLiteral("; ")));
    if (in.transcript.trimmed().isEmpty())
        user << QStringLiteral("There is no transcript: use the project name and the on-screen text.");
    else
        user << QStringLiteral("Transcript:\n\"\"\"\n%1\n\"\"\"").arg(condense(in.transcript, 6000));

    const QJsonObject str{{QStringLiteral("type"), QStringLiteral("string")}};
    const QJsonObject format{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("title"), str},
                     {QStringLiteral("description"), str},
                     {QStringLiteral("tags"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), str}}},
                     {QStringLiteral("thumbnail_text"), str}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("title"), QStringLiteral("description"), QStringLiteral("tags"), QStringLiteral("thumbnail_text")}}};
    const QJsonObject body{{QStringLiteral("model"), model},
                           {QStringLiteral("stream"), false},
                           {QStringLiteral("think"), false},
                           {QStringLiteral("format"), format},
                           {QStringLiteral("options"), QJsonObject{{QStringLiteral("temperature"), 0.5}, {QStringLiteral("num_predict"), 900}}},
                           {QStringLiteral("messages"),
                            QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}},
                                       QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), user.join(QLatin1Char('\n'))}}}}};
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

Result parseLlmReply(const QByteArray &reply)
{
    Result r;
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
    if (o.isEmpty())
        return r;

    r.title = cleanTitle(o.value(QStringLiteral("title")).toString());
    if (r.title.isEmpty())
        return r;
    QString desc = o.value(QStringLiteral("description")).toString();
    // paragraphs survive, trailing blanks and runs of blank lines do not
    desc.replace(QStringLiteral("\r"), QString());
    desc.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    r.description = desc.trimmed();
    QStringList tags;
    const QJsonValue tv = o.value(QStringLiteral("tags"));
    if (tv.isArray()) {
        for (const QJsonValue &v : tv.toArray())
            tags << v.toString();
    } else {
        tags = tv.toString().split(QRegularExpression(QStringLiteral("[,;\\n]")), Qt::SkipEmptyParts);
    }
    r.tags = cleanTags(tags);
    QString thumb = o.value(QStringLiteral("thumbnail_text")).toString();
    if (thumb.isEmpty())
        thumb = o.value(QStringLiteral("thumbnailText")).toString();
    r.thumbnailText = cleanTitle(thumb, 40);
    r.ok = true;
    return r;
}

// ------------------------------------------------------------------ heuristic

static const QSet<QString> &stopWords()
{
    static const QSet<QString> w = [] {
        const QStringList list = {
            // pt
            "para", "como", "mais", "mas", "com", "uma", "umas", "uns", "que", "isso", "esse", "essa", "esses", "essas", "este", "esta",
            "isto", "aqui", "ali", "ent\u00e3o", "entao", "porque", "quando", "onde", "tamb\u00e9m", "tambem", "muito", "muita", "pouco",
            "tem", "t\u00eam", "tinha", "foi", "ser", "est\u00e1", "estou", "estamos", "est\u00e3o", "vai", "vou", "vamos", "voc\u00ea", "voc\u00eas",
            "gente", "coisa", "coisas", "ainda", "sobre", "depois", "antes", "agora", "pelo", "pela", "pelos", "pelas", "dele", "dela", "nos",
            "nas", "dos", "das", "num", "numa", "seu", "sua", "seus", "suas", "meu", "minha", "n\u00e3o", "nao", "j\u00e1", "s\u00f3", "at\u00e9",
            "assim", "ent\u00e3o", "fazer", "faz", "fazendo", "pode", "podem", "posso", "ter", "tudo", "todo", "toda", "todos", "todas",
            "cada", "bem", "ent", "tipo", "ficar", "fica", "ver", "aqui", "dessa", "desse", "disso", "nessa", "nesse", "nisso",
            "hoje", "bom", "ontem", "amanhã", "primeiro", "segundo", "depois", "mostrar", "falar", "vamos", "começar", "olá",
            "gostaria", "então", "mesmo", "mesma", "outro", "outra", "algo", "alguma", "algum", "quer", "queria", "sempre",
            // en
            "that", "this", "with", "have", "from", "they", "will", "would", "there", "their", "what", "about", "which", "when", "your",
            "were", "been", "them", "then", "than", "into", "just", "like", "some", "more", "very", "also", "because", "going", "really",
            "know", "think", "make", "thing", "things", "here", "where", "these", "those", "could", "should", "other", "after", "before",
            "does", "dont", "it's", "i'm", "you're", "gonna", "want", "need", "yeah", "okay"};
        QSet<QString> s;
        for (const QString &x : list)
            s.insert(x);
        return s;
    }();
    return w;
}

static QStringList keywords(const QString &text, int count)
{
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}]+"));
    QHash<QString, int> freq;
    QHash<QString, int> first;
    int n = 0;
    auto it = word.globalMatch(text);
    while (it.hasNext()) {
        const QString w = it.next().captured().toLower();
        ++n;
        if (w.size() < 4 || stopWords().contains(w) || w.at(0).isDigit())
            continue;
        ++freq[w];
        if (!first.contains(w))
            first.insert(w, n);
    }
    QStringList words = freq.keys();
    std::sort(words.begin(), words.end(), [&](const QString &x, const QString &y) {
        return freq[x] != freq[y] ? freq[x] > freq[y] : first[x] < first[y];
    });
    return words.mid(0, count);
}

static QStringList sentences(const QString &text)
{
    static const QRegularExpression split(QStringLiteral("(?<=[.!?…])\\s+"));
    QStringList out;
    for (const QString &s : text.simplified().split(split, Qt::SkipEmptyParts))
        if (s.trimmed().size() > 3)
            out << s.trimmed();
    return out;
}

static QString capitalised(const QString &w)
{
    return w.isEmpty() ? w : w.at(0).toUpper() + w.mid(1);
}

Result heuristic(const Input &in)
{
    Result r;
    const QStringList sents = sentences(in.transcript);
    const QStringList kw = keywords(in.transcript + QLatin1Char(' ') + in.onScreenText.join(QLatin1Char(' ')), 8);

    QString title;
    if (!sents.isEmpty()) {
        title = sents.first();
        // a long opening sentence: its first clause
        if (title.size() > 70) {
            const int cut = title.indexOf(QRegularExpression(QStringLiteral("[,;:–—-]\\s")), 25);
            if (cut > 0 && cut <= 70)
                title = title.left(cut);
        }
    } else if (!in.onScreenText.isEmpty()) {
        title = in.onScreenText.first();
    } else {
        title = in.projectName;
    }
    title = cleanTitle(title, 70);
    // a trailing full stop reads badly in a title
    while (title.endsWith(QLatin1Char('.')) || title.endsWith(QLatin1Char(',')))
        title.chop(1);
    r.title = title;

    QString desc;
    for (const QString &s : sents) {
        if (desc.size() + s.size() > 420 && !desc.isEmpty())
            break;
        desc += (desc.isEmpty() ? QString() : QStringLiteral(" ")) + s;
        if (desc.size() > 280)
            break;
    }
    r.description = desc;
    r.tags = cleanTags(kw);

    if (kw.size() >= 2)
        r.thumbnailText = capitalised(kw.at(0)) + QLatin1Char(' ') + capitalised(kw.at(1));
    else if (!kw.isEmpty())
        r.thumbnailText = capitalised(kw.first());
    else
        r.thumbnailText = cleanTitle(title.section(QLatin1Char(' '), 0, 2), 30);
    r.ok = !r.title.isEmpty();
    return r;
}

// ------------------------------------------------------------------ chapters and description

QString timestamp(int seconds)
{
    seconds = std::max(0, seconds);
    const int h = seconds / 3600, m = seconds / 60 % 60, s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString chaptersBlock(const QList<Chapter> &chapters, const QString &firstTitle)
{
    QList<Chapter> list;
    for (const Chapter &c : chapters)
        if (!c.title.simplified().isEmpty())
            list.append({c.seconds, c.title.simplified()});
    std::sort(list.begin(), list.end(), [](const Chapter &a, const Chapter &b) { return a.seconds < b.seconds; });
    if (list.isEmpty())
        return {};
    if (list.first().seconds <= 2)
        list.first().seconds = 0;
    else
        list.prepend({0, firstTitle.simplified().isEmpty() ? QStringLiteral("Intro") : firstTitle.simplified()});
    // YouTube wants chapters at least 10 s long
    QList<Chapter> kept;
    for (const Chapter &c : list)
        if (kept.isEmpty() || c.seconds - kept.last().seconds >= 10)
            kept.append(c);
    if (kept.size() < 3)
        return {};
    QStringList lines;
    for (const Chapter &c : kept)
        lines << timestamp(c.seconds) + QLatin1Char(' ') + c.title;
    return lines.join(QLatin1Char('\n'));
}

QString composeDescription(const Result &r, const QList<Chapter> &chapters, const QString &firstChapterTitle)
{
    QStringList parts;
    if (!r.description.trimmed().isEmpty())
        parts << r.description.trimmed();
    const QString block = chaptersBlock(chapters, firstChapterTitle);
    if (!block.isEmpty())
        parts << block;
    // YouTube shows the first three hashtags above the title
    QStringList hashtags;
    static const QRegularExpression bad(QStringLiteral("[^\\p{L}\\p{N}_]"));
    for (const QString &t : r.tags) {
        QString h = t;
        h.remove(bad);
        if (!h.isEmpty())
            hashtags << QLatin1Char('#') + h;
        if (hashtags.size() == 3)
            break;
    }
    if (!hashtags.isEmpty())
        parts << hashtags.join(QLatin1Char(' '));
    return parts.join(QStringLiteral("\n\n"));
}

} // namespace Studio::Metadata
