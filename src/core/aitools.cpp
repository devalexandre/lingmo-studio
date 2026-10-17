/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "aitools.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>
#include <limits>

namespace Studio::AiTools {

WhisperEngine findWhisper()
{
    WhisperEngine e;
    for (const char *name : {"whisper-cli", "whisper-cpp", "whisper.cpp"}) {
        const QString prog = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (prog.isEmpty())
            continue;
        QStringList dirs = {qEnvironmentVariable("WHISPER_MODELS"),
                            QDir::home().filePath(QStringLiteral(".local/share/whisper.cpp/models")),
                            QDir::home().filePath(QStringLiteral(".cache/whisper.cpp")),
                            QStringLiteral("/usr/share/whisper.cpp/models"), QStringLiteral("/usr/local/share/whisper.cpp/models")};
        for (const QString &d : dirs) {
            if (d.isEmpty())
                continue;
            const QStringList models = QDir(d).entryList({QStringLiteral("ggml-*.bin")}, QDir::Files, QDir::Size | QDir::Reversed);
            if (!models.isEmpty()) {
                e.kind = QStringLiteral("whisper.cpp");
                e.program = prog;
                e.model = QDir(d).filePath(models.first());
                return e;
            }
        }
    }
    const QString fw = QStandardPaths::findExecutable(QStringLiteral("whisper-ctranslate2"));
    if (!fw.isEmpty()) {
        e.kind = QStringLiteral("faster-whisper");
        e.program = fw;
    }
    return e;
}

bool ollamaAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("ollama")).isEmpty();
}

QStringList extractSpeechAudioArguments(const QString &input, const QString &wav)
{
    return {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
            QStringLiteral("-i"), input, QStringLiteral("-vn"), QStringLiteral("-ac"), QStringLiteral("1"),
            QStringLiteral("-ar"), QStringLiteral("16000"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), wav};
}

QStringList whisperArguments(const WhisperEngine &engine, const QString &wav, const QString &outBase, const QString &language)
{
    const QString lang = language.isEmpty() ? QStringLiteral("auto") : language;
    if (engine.kind == QLatin1String("whisper.cpp"))
        return {QStringLiteral("-m"), engine.model, QStringLiteral("-f"), wav, QStringLiteral("-l"), lang,
                QStringLiteral("-osrt"), QStringLiteral("-of"), outBase};
    if (engine.kind == QLatin1String("faster-whisper")) {
        QStringList a = {wav, QStringLiteral("--output_format"), QStringLiteral("srt"), QStringLiteral("--output_dir"),
                         QFileInfo(outBase).absolutePath(), QStringLiteral("--model"), QStringLiteral("small")};
        if (lang != QLatin1String("auto"))
            a << QStringLiteral("--language") << lang;
        return a;
    }
    return {};
}

static qint64 parseTime(const QString &t)
{
    static const QRegularExpression re(QStringLiteral("(\\d+):(\\d+):(\\d+)[,.](\\d+)"));
    const auto m = re.match(t);
    if (!m.hasMatch())
        return -1;
    return ((m.captured(1).toLongLong() * 60 + m.captured(2).toLongLong()) * 60 + m.captured(3).toLongLong()) * 1000
        + m.captured(4).leftJustified(3, QLatin1Char('0')).left(3).toLongLong();
}

QList<SubtitleCue> parseSrt(const QString &text)
{
    QList<SubtitleCue> cues;
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList blocks = normalized.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        QStringList lines = block.trimmed().split(QLatin1Char('\n'));
        int i = 0;
        if (i < lines.size() && !lines.at(i).contains(QStringLiteral("-->")))
            ++i; // index line
        if (i >= lines.size())
            continue;
        const QStringList times = lines.at(i).split(QStringLiteral("-->"));
        if (times.size() != 2)
            continue;
        SubtitleCue c;
        c.startMs = parseTime(times.at(0));
        c.endMs = parseTime(times.at(1));
        if (c.startMs < 0 || c.endMs < c.startMs)
            continue;
        c.text = lines.mid(i + 1).join(QLatin1Char('\n')).trimmed();
        cues.append(c);
    }
    return cues;
}

static QString formatTime(qint64 ms)
{
    return QStringLiteral("%1:%2:%3,%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QString writeSrt(const QList<SubtitleCue> &cues)
{
    QString out;
    for (int i = 0; i < cues.size(); ++i)
        out += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(i + 1).arg(formatTime(cues.at(i).startMs), formatTime(cues.at(i).endMs), cues.at(i).text);
    return out;
}

QStringList silenceArguments(const QString &input, double noiseDb, double minSeconds)
{
    return {QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-i"), input, QStringLiteral("-vn"),
            QStringLiteral("-af"), QStringLiteral("silencedetect=noise=%1dB:d=%2").arg(noiseDb).arg(minSeconds),
            QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};
}

QList<Interval> parseSilence(const QString &out)
{
    QList<Interval> list;
    static const QRegularExpression startRe(QStringLiteral("silence_start:\\s*(-?[\\d.]+)"));
    static const QRegularExpression endRe(QStringLiteral("silence_end:\\s*([\\d.]+)"));
    double start = -1;
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        const auto s = startRe.match(line);
        if (s.hasMatch())
            start = std::max(0.0, s.captured(1).toDouble());
        const auto e = endRe.match(line);
        if (e.hasMatch() && start >= 0) {
            list.append({start, e.captured(1).toDouble()});
            start = -1;
        }
    }
    return list;
}

QStringList sceneArguments(const QString &input, double threshold)
{
    return {QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-i"), input, QStringLiteral("-an"),
            QStringLiteral("-vf"), QStringLiteral("scale=320:-2,select='gt(scene,%1)',showinfo").arg(threshold),
            QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};
}

QList<double> parseScenes(const QString &out)
{
    QList<double> times;
    static const QRegularExpression re(QStringLiteral("Parsed_showinfo.*pts_time:\\s*([\\d.]+)"));
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        const auto m = re.match(line);
        if (m.hasMatch())
            times.append(m.captured(1).toDouble());
    }
    return times;
}

QStringList loudnessArguments(const QString &input)
{
    return {QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-i"), input, QStringLiteral("-vn"),
            QStringLiteral("-af"), QStringLiteral("ebur128=framelog=quiet"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};
}

double parseIntegratedLoudness(const QString &out)
{
    // Summary block:  Integrated loudness:\n    I:         -20.3 LUFS
    const int at = out.lastIndexOf(QStringLiteral("Integrated loudness"));
    if (at < 0)
        return std::numeric_limits<double>::quiet_NaN();
    static const QRegularExpression re(QStringLiteral("I:\\s*(-?[\\d.]+|-inf)\\s*LUFS"));
    const auto m = re.match(out, at);
    if (!m.hasMatch() || m.captured(1) == QLatin1String("-inf"))
        return std::numeric_limits<double>::quiet_NaN();
    return m.captured(1).toDouble();
}

static QString fold(const QString &s)
{
    QString d = s.normalized(QString::NormalizationForm_D);
    d.remove(QRegularExpression(QStringLiteral("\\p{Mn}")));
    return d.toCaseFolded();
}

QList<SubtitleCue> search(const QList<SubtitleCue> &cues, const QString &query)
{
    QList<SubtitleCue> hits;
    const QStringList words = fold(query).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return hits;
    for (const SubtitleCue &c : cues) {
        const QString t = fold(c.text);
        bool all = true;
        for (const QString &w : words)
            all &= t.contains(w);
        if (all)
            hits.append(c);
    }
    return hits;
}

} // namespace Studio::AiTools
