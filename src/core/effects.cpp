/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "effects.h"
#include "timelineops.h"

#include <QColor>
#include <QCoreApplication>
#include <cmath>

namespace Studio {

QStringList EffectRegistry::s_services;

static ParamDef num(const QString &id, const QString &name, double def, double min, double max, double step,
                    bool anim = false, const QString &unit = {})
{
    ParamDef p;
    p.id = id;
    p.name = name;
    p.type = QStringLiteral("double");
    p.def = def;
    p.min = min;
    p.max = max;
    p.step = step;
    p.animatable = anim;
    p.unit = unit;
    return p;
}

static ParamDef typed(const QString &id, const QString &name, const QString &type, const QVariant &def, const QStringList &choices = {})
{
    ParamDef p;
    p.id = id;
    p.name = name;
    p.type = type;
    p.def = def;
    p.choices = choices;
    return p;
}

const QList<EffectDef> &EffectRegistry::all()
{
    static const QList<EffectDef> defs = [] {
        QList<EffectDef> d;
        // ---- transform / compositing
        d.append({QStringLiteral("transform"), QCoreApplication::translate("Effects", "Transform"), QStringLiteral("transform"), true, {QStringLiteral("qtblend")},
                  {num(QStringLiteral("x"), QCoreApplication::translate("Effects", "Position X"), 0, -4000, 4000, 1, true, QStringLiteral("px")),
                   num(QStringLiteral("y"), QCoreApplication::translate("Effects", "Position Y"), 0, -4000, 4000, 1, true, QStringLiteral("px")),
                   num(QStringLiteral("scale"), QCoreApplication::translate("Effects", "Scale"), 100, 1, 800, 1, true, QStringLiteral("%")),
                   num(QStringLiteral("rotation"), QCoreApplication::translate("Effects", "Rotation"), 0, -360, 360, 0.5, true, QStringLiteral("°")),
                   num(QStringLiteral("opacity"), QCoreApplication::translate("Effects", "Opacity"), 100, 0, 100, 1, true, QStringLiteral("%"))}});
        d.append({QStringLiteral("crop"), QCoreApplication::translate("Effects", "Crop"), QStringLiteral("transform"), true, {QStringLiteral("qtcrop")},
                  {num(QStringLiteral("left"), QCoreApplication::translate("Effects", "Left"), 0, 0, 100, 0.5, true, QStringLiteral("%")),
                   num(QStringLiteral("right"), QCoreApplication::translate("Effects", "Right"), 0, 0, 100, 0.5, true, QStringLiteral("%")),
                   num(QStringLiteral("top"), QCoreApplication::translate("Effects", "Top"), 0, 0, 100, 0.5, true, QStringLiteral("%")),
                   num(QStringLiteral("bottom"), QCoreApplication::translate("Effects", "Bottom"), 0, 0, 100, 0.5, true, QStringLiteral("%"))}});
        d.append({QStringLiteral("mask"), QCoreApplication::translate("Effects", "Mask"), QStringLiteral("transform"), true, {QStringLiteral("qtcrop")},
                  {typed(QStringLiteral("shape"), QCoreApplication::translate("Effects", "Shape"), QStringLiteral("choice"), QStringLiteral("ellipse"),
                         {QStringLiteral("ellipse"), QStringLiteral("rectangle")}),
                   num(QStringLiteral("x"), QCoreApplication::translate("Effects", "Left"), 25, 0, 100, 0.5, true, QStringLiteral("%")),
                   num(QStringLiteral("y"), QCoreApplication::translate("Effects", "Top"), 25, 0, 100, 0.5, true, QStringLiteral("%")),
                   num(QStringLiteral("w"), QCoreApplication::translate("Effects", "Width"), 50, 0, 100, 0.5, true, QStringLiteral("%")),
                   num(QStringLiteral("h"), QCoreApplication::translate("Effects", "Height"), 50, 0, 100, 0.5, true, QStringLiteral("%")),
                   num(QStringLiteral("radius"), QCoreApplication::translate("Effects", "Corner radius"), 0, 0, 0.5, 0.01)}});
        // ---- color
        d.append({QStringLiteral("color"), QCoreApplication::translate("Effects", "Color correction"), QStringLiteral("color"), true,
                  {QStringLiteral("avfilter.eq"), QStringLiteral("avfilter.colortemperature")},
                  {num(QStringLiteral("brightness"), QCoreApplication::translate("Effects", "Brightness"), 0, -1, 1, 0.01),
                   num(QStringLiteral("contrast"), QCoreApplication::translate("Effects", "Contrast"), 1, 0, 3, 0.01),
                   num(QStringLiteral("saturation"), QCoreApplication::translate("Effects", "Saturation"), 1, 0, 3, 0.01),
                   num(QStringLiteral("gamma"), QCoreApplication::translate("Effects", "Gamma"), 1, 0.1, 3, 0.01),
                   num(QStringLiteral("temperature"), QCoreApplication::translate("Effects", "White balance"), 6500, 2000, 12000, 50, false, QStringLiteral("K"))}});
        d.append({QStringLiteral("wheels"), QCoreApplication::translate("Effects", "Color wheels"), QStringLiteral("color"), true, {QStringLiteral("lift_gamma_gain")},
                  {num(QStringLiteral("lift_r"), QCoreApplication::translate("Effects", "Lift red"), 0, -0.5, 0.5, 0.005, true),
                   num(QStringLiteral("lift_g"), QCoreApplication::translate("Effects", "Lift green"), 0, -0.5, 0.5, 0.005, true),
                   num(QStringLiteral("lift_b"), QCoreApplication::translate("Effects", "Lift blue"), 0, -0.5, 0.5, 0.005, true),
                   num(QStringLiteral("gamma_r"), QCoreApplication::translate("Effects", "Gamma red"), 1, 0.2, 3, 0.01, true),
                   num(QStringLiteral("gamma_g"), QCoreApplication::translate("Effects", "Gamma green"), 1, 0.2, 3, 0.01, true),
                   num(QStringLiteral("gamma_b"), QCoreApplication::translate("Effects", "Gamma blue"), 1, 0.2, 3, 0.01, true),
                   num(QStringLiteral("gain_r"), QCoreApplication::translate("Effects", "Gain red"), 1, 0, 4, 0.01, true),
                   num(QStringLiteral("gain_g"), QCoreApplication::translate("Effects", "Gain green"), 1, 0, 4, 0.01, true),
                   num(QStringLiteral("gain_b"), QCoreApplication::translate("Effects", "Gain blue"), 1, 0, 4, 0.01, true)}});
        d.append({QStringLiteral("curves"), QCoreApplication::translate("Effects", "Curves"), QStringLiteral("color"), true, {QStringLiteral("avfilter.curves")},
                  {typed(QStringLiteral("master"), QCoreApplication::translate("Effects", "Master"), QStringLiteral("curve"), QStringLiteral("0/0 1/1")),
                   typed(QStringLiteral("red"), QCoreApplication::translate("Effects", "Red"), QStringLiteral("curve"), QStringLiteral("0/0 1/1")),
                   typed(QStringLiteral("green"), QCoreApplication::translate("Effects", "Green"), QStringLiteral("curve"), QStringLiteral("0/0 1/1")),
                   typed(QStringLiteral("blue"), QCoreApplication::translate("Effects", "Blue"), QStringLiteral("curve"), QStringLiteral("0/0 1/1"))}});
        d.append({QStringLiteral("hsl"), QCoreApplication::translate("Effects", "HSL qualifier"), QStringLiteral("color"), true, {QStringLiteral("hslrange")},
                  {num(QStringLiteral("hue_center"), QCoreApplication::translate("Effects", "Hue center"), 120, 0, 360, 1, false, QStringLiteral("°")),
                   num(QStringLiteral("hue_range"), QCoreApplication::translate("Effects", "Hue range"), 60, 0, 360, 1, false, QStringLiteral("°")),
                   num(QStringLiteral("blend"), QCoreApplication::translate("Effects", "Softness"), 50, 0, 100, 1, false, QStringLiteral("%")),
                   num(QStringLiteral("h_shift"), QCoreApplication::translate("Effects", "Hue shift"), 0, -180, 180, 1, false, QStringLiteral("°")),
                   num(QStringLiteral("s_scale"), QCoreApplication::translate("Effects", "Saturation"), 100, 0, 500, 1, false, QStringLiteral("%")),
                   num(QStringLiteral("l_scale"), QCoreApplication::translate("Effects", "Lightness"), 100, 0, 500, 1, false, QStringLiteral("%"))}});
        d.append({QStringLiteral("lut"), QCoreApplication::translate("Effects", "LUT (.cube)"), QStringLiteral("color"), true, {QStringLiteral("avfilter.lut3d")},
                  {typed(QStringLiteral("file"), QCoreApplication::translate("Effects", "LUT file"), QStringLiteral("file"), QString())}});
        // ---- stylize
        d.append({QStringLiteral("blur"), QCoreApplication::translate("Effects", "Blur"), QStringLiteral("stylize"), true, {QStringLiteral("avfilter.gblur")},
                  {num(QStringLiteral("radius"), QCoreApplication::translate("Effects", "Radius"), 4, 0, 60, 0.5)}});
        d.append({QStringLiteral("sharpen"), QCoreApplication::translate("Effects", "Sharpen"), QStringLiteral("stylize"), true, {QStringLiteral("avfilter.unsharp")},
                  {num(QStringLiteral("amount"), QCoreApplication::translate("Effects", "Amount"), 1, 0, 3, 0.05)}});
        d.append({QStringLiteral("glow"), QCoreApplication::translate("Effects", "Glow"), QStringLiteral("stylize"), true, {QStringLiteral("frei0r.glow")},
                  {num(QStringLiteral("amount"), QCoreApplication::translate("Effects", "Amount"), 0.5, 0, 1, 0.01)}});
        d.append({QStringLiteral("vignette"), QCoreApplication::translate("Effects", "Vignette"), QStringLiteral("stylize"), true, {QStringLiteral("avfilter.vignette")},
                  {num(QStringLiteral("angle"), QCoreApplication::translate("Effects", "Strength"), 0.6, 0, 1.5, 0.01)}});
        d.append({QStringLiteral("stabilize"), QCoreApplication::translate("Effects", "Stabilize"), QStringLiteral("stylize"), true, {QStringLiteral("vidstab")},
                  {num(QStringLiteral("shakiness"), QCoreApplication::translate("Effects", "Shakiness"), 4, 1, 10, 1),
                   num(QStringLiteral("smoothing"), QCoreApplication::translate("Effects", "Smoothing"), 15, 0, 100, 1),
                   typed(QStringLiteral("results"), QCoreApplication::translate("Effects", "Analysis file"), QStringLiteral("file"), QString())}});
        // ---- keying
        d.append({QStringLiteral("chromakey"), QCoreApplication::translate("Effects", "Chroma key"), QStringLiteral("key"), true, {QStringLiteral("avfilter.chromakey")},
                  {typed(QStringLiteral("color"), QCoreApplication::translate("Effects", "Key color"), QStringLiteral("color"), QStringLiteral("#00ff00")),
                   num(QStringLiteral("similarity"), QCoreApplication::translate("Effects", "Similarity"), 0.15, 0.01, 1, 0.01),
                   num(QStringLiteral("blend"), QCoreApplication::translate("Effects", "Edge blend"), 0.1, 0, 1, 0.01)}});
        // ---- text
        d.append({QStringLiteral("text"), QCoreApplication::translate("Effects", "Text"), QStringLiteral("text"), true, {QStringLiteral("qtext")},
                  {typed(QStringLiteral("text"), QCoreApplication::translate("Effects", "Text"), QStringLiteral("text"), QCoreApplication::translate("Effects", "Title")),
                   typed(QStringLiteral("family"), QCoreApplication::translate("Effects", "Font"), QStringLiteral("font"), QStringLiteral("Sans")),
                   num(QStringLiteral("size"), QCoreApplication::translate("Effects", "Size"), 96, 8, 600, 1, false, QStringLiteral("px")),
                   typed(QStringLiteral("weight"), QCoreApplication::translate("Effects", "Weight"), QStringLiteral("choice"), QStringLiteral("bold"),
                         {QStringLiteral("normal"), QStringLiteral("bold")}),
                   typed(QStringLiteral("color"), QCoreApplication::translate("Effects", "Color"), QStringLiteral("color"), QStringLiteral("#ffffff")),
                   typed(QStringLiteral("outlineColor"), QCoreApplication::translate("Effects", "Outline color"), QStringLiteral("color"), QStringLiteral("#000000")),
                   num(QStringLiteral("outline"), QCoreApplication::translate("Effects", "Outline"), 0, 0, 20, 1, false, QStringLiteral("px")),
                   typed(QStringLiteral("background"), QCoreApplication::translate("Effects", "Background"), QStringLiteral("color"), QStringLiteral("#00000000")),
                   typed(QStringLiteral("halign"), QCoreApplication::translate("Effects", "Horizontal"), QStringLiteral("choice"), QStringLiteral("center"),
                         {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}),
                   typed(QStringLiteral("valign"), QCoreApplication::translate("Effects", "Vertical"), QStringLiteral("choice"), QStringLiteral("middle"),
                         {QStringLiteral("top"), QStringLiteral("middle"), QStringLiteral("bottom")}),
                   num(QStringLiteral("x"), QCoreApplication::translate("Effects", "Box left"), 5, -50, 100, 0.5, true, QStringLiteral("%")),
                   num(QStringLiteral("y"), QCoreApplication::translate("Effects", "Box top"), 5, -50, 100, 0.5, true, QStringLiteral("%")),
                   num(QStringLiteral("w"), QCoreApplication::translate("Effects", "Box width"), 90, 1, 200, 0.5, false, QStringLiteral("%")),
                   num(QStringLiteral("h"), QCoreApplication::translate("Effects", "Box height"), 90, 1, 200, 0.5, false, QStringLiteral("%")),
                   num(QStringLiteral("opacity"), QCoreApplication::translate("Effects", "Opacity"), 100, 0, 100, 1, true, QStringLiteral("%"))}});
        // ---- audio
        d.append({QStringLiteral("volume"), QCoreApplication::translate("Effects", "Volume"), QStringLiteral("audio"), false, {QStringLiteral("volume")},
                  {num(QStringLiteral("gain"), QCoreApplication::translate("Effects", "Gain"), 0, -60, 24, 0.1, true, QStringLiteral("dB"))}});
        d.append({QStringLiteral("pan"), QCoreApplication::translate("Effects", "Pan"), QStringLiteral("audio"), false, {QStringLiteral("panner")},
                  {num(QStringLiteral("pan"), QCoreApplication::translate("Effects", "Pan"), 0, -1, 1, 0.01, true)}});
        d.append({QStringLiteral("eq"), QCoreApplication::translate("Effects", "Equalizer"), QStringLiteral("audio"), false, {QStringLiteral("avfilter.equalizer")},
                  {num(QStringLiteral("low"), QCoreApplication::translate("Effects", "Low (100 Hz)"), 0, -24, 24, 0.5, false, QStringLiteral("dB")),
                   num(QStringLiteral("mid"), QCoreApplication::translate("Effects", "Mid (1 kHz)"), 0, -24, 24, 0.5, false, QStringLiteral("dB")),
                   num(QStringLiteral("high"), QCoreApplication::translate("Effects", "High (8 kHz)"), 0, -24, 24, 0.5, false, QStringLiteral("dB"))}});
        d.append({QStringLiteral("compressor"), QCoreApplication::translate("Effects", "Compressor"), QStringLiteral("audio"), false, {QStringLiteral("avfilter.acompressor")},
                  {num(QStringLiteral("threshold"), QCoreApplication::translate("Effects", "Threshold"), -18, -60, 0, 0.5, false, QStringLiteral("dB")),
                   num(QStringLiteral("ratio"), QCoreApplication::translate("Effects", "Ratio"), 4, 1, 20, 0.1),
                   num(QStringLiteral("attack"), QCoreApplication::translate("Effects", "Attack"), 20, 0.1, 2000, 1, false, QStringLiteral("ms")),
                   num(QStringLiteral("release"), QCoreApplication::translate("Effects", "Release"), 250, 1, 9000, 1, false, QStringLiteral("ms")),
                   num(QStringLiteral("makeup"), QCoreApplication::translate("Effects", "Makeup gain"), 0, 0, 36, 0.5, false, QStringLiteral("dB"))}});
        d.append({QStringLiteral("denoise"), QCoreApplication::translate("Effects", "Noise reduction"), QStringLiteral("audio"), false, {QStringLiteral("avfilter.afftdn")},
                  {num(QStringLiteral("amount"), QCoreApplication::translate("Effects", "Reduction"), 12, 0.01, 97, 0.5, false, QStringLiteral("dB"))}});
        d.append({QStringLiteral("autoloudness"), QCoreApplication::translate("Effects", "Auto loudness"), QStringLiteral("audio"), false, {QStringLiteral("dynamic_loudness")},
                  {num(QStringLiteral("target"), QCoreApplication::translate("Effects", "Target"), -14, -40, -5, 0.5, false, QStringLiteral("LUFS"))}});
        return d;
    }();
    return defs;
}

const EffectDef *EffectRegistry::find(const QString &id)
{
    for (const EffectDef &d : all())
        if (d.id == id)
            return &d;
    return nullptr;
}

bool EffectRegistry::isAudio(const QString &id)
{
    const EffectDef *d = find(id);
    return d && !d->video;
}

void EffectRegistry::setAvailableServices(const QStringList &services)
{
    s_services = services;
}

bool EffectRegistry::available(const EffectDef &def)
{
    if (s_services.isEmpty())
        return true;
    for (const QString &s : def.services)
        if (!s_services.contains(s))
            return false;
    return true;
}

Effect EffectRegistry::create(const QString &id)
{
    Effect e;
    e.type = id;
    if (const EffectDef *d = find(id))
        for (const ParamDef &p : d->params)
            e.params.insert(p.id, p.def);
    return e;
}

QVariantList EffectRegistry::toVariantList()
{
    QVariantList list;
    for (const EffectDef &d : all()) {
        QVariantList params;
        for (const ParamDef &p : d.params)
            params.append(QVariantMap{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name},
                                      {QStringLiteral("type"), p.type}, {QStringLiteral("def"), p.def},
                                      {QStringLiteral("min"), p.min}, {QStringLiteral("max"), p.max},
                                      {QStringLiteral("step"), p.step}, {QStringLiteral("choices"), p.choices},
                                      {QStringLiteral("animatable"), p.animatable}, {QStringLiteral("unit"), p.unit}});
        list.append(QVariantMap{{QStringLiteral("id"), d.id}, {QStringLiteral("name"), d.name},
                                {QStringLiteral("category"), d.category}, {QStringLiteral("video"), d.video},
                                {QStringLiteral("hidden"), d.hidden}, {QStringLiteral("available"), available(d)},
                                {QStringLiteral("params"), params}});
    }
    return list;
}

QString animString(const QList<Keyframe> &kfs, const QVariantMap &base, const QString &key,
                   const std::function<QString(const QVariantMap &)> &format)
{
    auto fmt = [&](const QVariantMap &v) { return format ? format(v) : formatNumber(v.value(key).toDouble()); };
    if (kfs.isEmpty())
        return fmt(base);
    QStringList parts;
    for (const Keyframe &k : kfs)
        parts << QStringLiteral("%1=%2").arg(k.frame).arg(fmt(Ops::valuesAt(kfs, base, k.frame)));
    return parts.join(QLatin1Char(';'));
}

static double dbToLinear(double db)
{
    return std::pow(10.0, db / 20.0);
}

static QString ffColor(const QString &c)
{
    QColor col(c);
    return QStringLiteral("0x%1").arg(col.name(QColor::HexRgb).mid(1).toUpper());
}

QList<FilterSpec> EffectRegistry::filters(const Effect &e, const FilterContext &ctx)
{
    QList<FilterSpec> out;
    if (!e.enabled)
        return out;
    const QVariantMap &p = e.params;
    auto n = [&](const QString &k) { return p.value(k).toDouble(); };
    auto add = [&](const QString &service, const Props &props) {
        FilterSpec f;
        f.service = service;
        f.props = props;
        f.in = ctx.clipIn;
        f.out = ctx.clipOut;
        out.append(f);
    };
    const double W = ctx.width, H = ctx.height;
    const QString t = e.type;

    if (t == QLatin1String("transform")) {
        const QString rect = animString(e.keyframes, p, QString(), [&](const QVariantMap &v) {
            const double s = v.value(QStringLiteral("scale")).toDouble() / 100.0;
            const double w = W * s, h = H * s;
            const double x = (W - w) / 2 + v.value(QStringLiteral("x")).toDouble() * ctx.pixelScale;
            const double y = (H - h) / 2 + v.value(QStringLiteral("y")).toDouble() * ctx.pixelScale;
            return QStringLiteral("%1 %2 %3 %4 %5").arg(formatNumber(x), formatNumber(y), formatNumber(w), formatNumber(h),
                                                        formatNumber(v.value(QStringLiteral("opacity")).toDouble() / 100.0));
        });
        add(QStringLiteral("qtblend"), {{QStringLiteral("rect"), rect},
                                        {QStringLiteral("rotation"), animString(e.keyframes, p, QStringLiteral("rotation"))},
                                        {QStringLiteral("compositing"), QStringLiteral("0")},
                                        {QStringLiteral("distort"), QStringLiteral("0")},
                                        {QStringLiteral("rotate_center"), QStringLiteral("1")}});
    } else if (t == QLatin1String("crop") || t == QLatin1String("mask")) {
        const bool crop = t == QLatin1String("crop");
        const QString rect = animString(e.keyframes, p, QString(), [&](const QVariantMap &v) {
            double x, y, w, h;
            if (crop) {
                x = W * v.value(QStringLiteral("left")).toDouble() / 100;
                y = H * v.value(QStringLiteral("top")).toDouble() / 100;
                w = W - x - W * v.value(QStringLiteral("right")).toDouble() / 100;
                h = H - y - H * v.value(QStringLiteral("bottom")).toDouble() / 100;
            } else {
                x = W * v.value(QStringLiteral("x")).toDouble() / 100;
                y = H * v.value(QStringLiteral("y")).toDouble() / 100;
                w = W * v.value(QStringLiteral("w")).toDouble() / 100;
                h = H * v.value(QStringLiteral("h")).toDouble() / 100;
            }
            return QStringLiteral("%1 %2 %3 %4 1").arg(formatNumber(x), formatNumber(y), formatNumber(std::max(0.0, w)),
                                                      formatNumber(std::max(0.0, h)));
        });
        add(QStringLiteral("qtcrop"), {{QStringLiteral("rect"), rect},
                                       {QStringLiteral("circle"), (!crop && p.value(QStringLiteral("shape")) == QLatin1String("ellipse")) ? QStringLiteral("1") : QStringLiteral("0")},
                                       {QStringLiteral("radius"), formatNumber(crop ? 0 : n(QStringLiteral("radius")))},
                                       {QStringLiteral("color"), QStringLiteral("#00000000")}});
    } else if (t == QLatin1String("color")) {
        add(QStringLiteral("avfilter.eq"), {{QStringLiteral("av.brightness"), formatNumber(n(QStringLiteral("brightness")))},
                                            {QStringLiteral("av.contrast"), formatNumber(n(QStringLiteral("contrast")))},
                                            {QStringLiteral("av.saturation"), formatNumber(n(QStringLiteral("saturation")))},
                                            {QStringLiteral("av.gamma"), formatNumber(n(QStringLiteral("gamma")))}});
        if (std::abs(n(QStringLiteral("temperature")) - 6500) > 1)
            add(QStringLiteral("avfilter.colortemperature"), {{QStringLiteral("av.temperature"), formatNumber(n(QStringLiteral("temperature")))}});
    } else if (t == QLatin1String("wheels")) {
        Props props;
        for (const char *k : {"lift_r", "lift_g", "lift_b", "gamma_r", "gamma_g", "gamma_b", "gain_r", "gain_g", "gain_b"})
            props.append({QString::fromLatin1(k), animString(e.keyframes, p, QString::fromLatin1(k))});
        add(QStringLiteral("lift_gamma_gain"), props);
    } else if (t == QLatin1String("curves")) {
        Props props;
        for (const char *k : {"master", "red", "green", "blue"}) {
            const QString v = p.value(QString::fromLatin1(k)).toString().trimmed();
            if (!v.isEmpty() && v != QLatin1String("0/0 1/1"))
                props.append({QStringLiteral("av.") + QString::fromLatin1(k), v});
        }
        if (!props.isEmpty())
            add(QStringLiteral("avfilter.curves"), props);
    } else if (t == QLatin1String("hsl")) {
        Props props;
        for (const char *k : {"hue_center", "hue_range", "blend", "h_shift", "s_scale", "l_scale"})
            props.append({QString::fromLatin1(k), formatNumber(n(QString::fromLatin1(k)))});
        add(QStringLiteral("hslrange"), props);
    } else if (t == QLatin1String("lut")) {
        const QString file = p.value(QStringLiteral("file")).toString();
        if (!file.isEmpty())
            add(QStringLiteral("avfilter.lut3d"), {{QStringLiteral("av.file"), file}, {QStringLiteral("av.interp"), QStringLiteral("tetrahedral")}});
    } else if (t == QLatin1String("blur")) {
        if (n(QStringLiteral("radius")) > 0)
            add(QStringLiteral("avfilter.gblur"), {{QStringLiteral("av.sigma"), formatNumber(n(QStringLiteral("radius")) * ctx.pixelScale)}});
    } else if (t == QLatin1String("sharpen")) {
        add(QStringLiteral("avfilter.unsharp"), {{QStringLiteral("av.la"), formatNumber(n(QStringLiteral("amount")))}});
    } else if (t == QLatin1String("glow")) {
        add(QStringLiteral("frei0r.glow"), {{QStringLiteral("0"), formatNumber(n(QStringLiteral("amount")))}});
    } else if (t == QLatin1String("vignette")) {
        add(QStringLiteral("avfilter.vignette"), {{QStringLiteral("av.angle"), formatNumber(n(QStringLiteral("angle")))}});
    } else if (t == QLatin1String("stabilize")) {
        const QString results = p.value(QStringLiteral("results")).toString();
        if (!results.isEmpty())
            add(QStringLiteral("vidstab"), {{QStringLiteral("filename"), results},
                                            {QStringLiteral("smoothing"), formatNumber(n(QStringLiteral("smoothing")))},
                                            {QStringLiteral("shakiness"), formatNumber(n(QStringLiteral("shakiness")))}});
    } else if (t == QLatin1String("chromakey")) {
        add(QStringLiteral("avfilter.chromakey"), {{QStringLiteral("av.color"), ffColor(p.value(QStringLiteral("color")).toString())},
                                                   {QStringLiteral("av.similarity"), formatNumber(n(QStringLiteral("similarity")))},
                                                   {QStringLiteral("av.blend"), formatNumber(n(QStringLiteral("blend")))}});
    } else if (t == QLatin1String("text")) {
        const QString geometry = animString(e.keyframes, p, QString(), [&](const QVariantMap &v) {
            return QStringLiteral("%1 %2 %3 %4 %5").arg(formatNumber(W * v.value(QStringLiteral("x")).toDouble() / 100),
                                                        formatNumber(H * v.value(QStringLiteral("y")).toDouble() / 100),
                                                        formatNumber(W * v.value(QStringLiteral("w")).toDouble() / 100),
                                                        formatNumber(H * v.value(QStringLiteral("h")).toDouble() / 100),
                                                        formatNumber(v.value(QStringLiteral("opacity"), 100).toDouble() / 100));
        });
        add(QStringLiteral("qtext"), {{QStringLiteral("argument"), p.value(QStringLiteral("text")).toString()},
                                      {QStringLiteral("geometry"), geometry},
                                      {QStringLiteral("family"), p.value(QStringLiteral("family")).toString()},
                                      {QStringLiteral("size"), formatNumber(std::max(1.0, n(QStringLiteral("size")) * ctx.pixelScale))},
                                      {QStringLiteral("weight"), p.value(QStringLiteral("weight")) == QLatin1String("bold") ? QStringLiteral("700") : QStringLiteral("400")},
                                      {QStringLiteral("fgcolour"), mltColor(p.value(QStringLiteral("color")).toString())},
                                      {QStringLiteral("olcolour"), mltColor(p.value(QStringLiteral("outlineColor")).toString())},
                                      {QStringLiteral("outline"), formatNumber(std::round(n(QStringLiteral("outline")) * ctx.pixelScale))},
                                      {QStringLiteral("bgcolour"), mltColor(p.value(QStringLiteral("background")).toString())},
                                      {QStringLiteral("halign"), p.value(QStringLiteral("halign")).toString()},
                                      {QStringLiteral("valign"), p.value(QStringLiteral("valign")).toString()}});
    } else if (t == QLatin1String("volume")) {
        add(QStringLiteral("volume"), {{QStringLiteral("level"), animString(e.keyframes, p, QStringLiteral("gain"))}});
    } else if (t == QLatin1String("pan")) {
        add(QStringLiteral("panner"), {{QStringLiteral("channel"), QStringLiteral("-1")},
                                       {QStringLiteral("split"), animString(e.keyframes, p, QString(), [](const QVariantMap &v) {
                                            return formatNumber((v.value(QStringLiteral("pan")).toDouble() + 1.0) / 2.0);
                                        })}});
    } else if (t == QLatin1String("eq")) {
        const struct { const char *key; int freq; } bands[] = {{"low", 100}, {"mid", 1000}, {"high", 8000}};
        for (const auto &b : bands) {
            const double g = n(QString::fromLatin1(b.key));
            if (std::abs(g) > 0.01)
                add(QStringLiteral("avfilter.equalizer"), {{QStringLiteral("av.frequency"), QString::number(b.freq)},
                                                           {QStringLiteral("av.width_type"), QStringLiteral("o")},
                                                           {QStringLiteral("av.width"), QStringLiteral("2")},
                                                           {QStringLiteral("av.gain"), formatNumber(g)}});
        }
    } else if (t == QLatin1String("compressor")) {
        add(QStringLiteral("avfilter.acompressor"), {{QStringLiteral("av.threshold"), formatNumber(std::clamp(dbToLinear(n(QStringLiteral("threshold"))), 0.000976563, 1.0))},
                                                     {QStringLiteral("av.ratio"), formatNumber(n(QStringLiteral("ratio")))},
                                                     {QStringLiteral("av.attack"), formatNumber(n(QStringLiteral("attack")))},
                                                     {QStringLiteral("av.release"), formatNumber(n(QStringLiteral("release")))},
                                                     {QStringLiteral("av.makeup"), formatNumber(std::clamp(dbToLinear(n(QStringLiteral("makeup"))), 1.0, 64.0))}});
    } else if (t == QLatin1String("denoise")) {
        add(QStringLiteral("avfilter.afftdn"), {{QStringLiteral("av.nr"), formatNumber(n(QStringLiteral("amount")))}});
    } else if (t == QLatin1String("autoloudness")) {
        add(QStringLiteral("dynamic_loudness"), {{QStringLiteral("target_loudness"), formatNumber(n(QStringLiteral("target")))}});
    }
    return out;
}

} // namespace Studio
