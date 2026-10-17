/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

// Effect registry: what effects exist, their parameters (for the inspector UI)
// and how each becomes one or more MLT filters. Adding an effect = one entry in
// effects.cpp; the inspector, keyframes and save/load pick it up.

#pragma once

#include "model.h"
#include "plan.h"

namespace Studio {

struct ParamDef {
    QString id;
    QString name;
    QString type; // double | int | color | text | file | choice | bool | curve | font
    QVariant def;
    double min = 0;
    double max = 1;
    double step = 0.01;
    QStringList choices;
    bool animatable = false;
    QString unit;
};

struct EffectDef {
    QString id;
    QString name;
    QString category; // transform | color | stylize | key | text | audio
    bool video = true;
    QStringList services; // MLT services needed (availability check)
    QList<ParamDef> params;
    bool hidden = false;  // created by the app (title text), not in the add menu
};

struct FilterContext {
    int width = 1920;
    int height = 1080;
    int clipIn = 0;
    int clipOut = 0;
    double pixelScale = 1.0; // preview renders at a smaller size: scales pixel values
};

class EffectRegistry
{
public:
    static const QList<EffectDef> &all();
    static const EffectDef *find(const QString &id);
    static Effect create(const QString &id);
    // Filters for an effect on a clip (filter in/out set to the clip's).
    static QList<FilterSpec> filters(const Effect &effect, const FilterContext &ctx);
    static bool isAudio(const QString &id);
    // Set by the app at startup from the MLT repository; empty = assume all available
    static void setAvailableServices(const QStringList &services);
    static bool available(const EffectDef &def);
    static QVariantList toVariantList();

private:
    static QStringList s_services;
};

// Builds "f=v;f=v" MLT animation strings from keyframes for one value.
QString animString(const QList<Keyframe> &kfs, const QVariantMap &base, const QString &key,
                   const std::function<QString(const QVariantMap &)> &format = {});

} // namespace Studio
