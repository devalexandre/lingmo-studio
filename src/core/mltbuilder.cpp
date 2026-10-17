/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include "mltbuilder.h"

#include <Mlt.h>
#include <QDebug>
#include <mutex>

namespace Studio {

static Mlt::Repository *s_repository = nullptr;

void initMlt()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // Keep the console readable: duplicate frei0r registrations etc.
        if (!qEnvironmentVariableIsSet("STUDIO_MLT_DEBUG"))
            mlt_log_set_level(MLT_LOG_ERROR);
        s_repository = Mlt::Factory::init();
    });
}

QStringList availableMltServices()
{
    initMlt();
    QStringList out;
    if (!s_repository)
        return out;
    for (Mlt::Properties *list : {s_repository->filters(), s_repository->transitions(), s_repository->producers(), s_repository->links()}) {
        if (!list)
            continue;
        for (int i = 0; i < list->count(); ++i)
            out << QString::fromUtf8(list->get_name(i));
        delete list;
    }
    return out;
}

static void applyProps(Mlt::Properties &target, const Props &props)
{
    for (const auto &kv : props)
        target.set(kv.first.toUtf8().constData(), kv.second.toUtf8().constData());
}

static int hideValue(const QString &hide)
{
    if (hide == QLatin1String("video"))
        return 1;
    if (hide == QLatin1String("audio"))
        return 2;
    if (hide == QLatin1String("both"))
        return 3;
    return 0;
}

MltBuilder::MltBuilder(Mlt::Profile &profile)
    : m_profile(profile)
{
    initMlt();
}

MltBuilder::~MltBuilder() = default;

void MltBuilder::clearCache()
{
    m_cache.clear();
}

void MltBuilder::prune()
{
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (!m_used.contains(it.key()))
            it = m_cache.erase(it);
        else
            ++it;
    }
}

std::shared_ptr<Mlt::Producer> MltBuilder::producer(const ProducerSpec &spec)
{
    if (!spec.nested && spec.service != QLatin1String("chain")) {
        const auto it = m_cache.constFind(spec.key);
        if (it != m_cache.cend()) {
            m_used.insert(spec.key);
            return it.value();
        }
    }
    std::shared_ptr<Mlt::Producer> p;
    const QByteArray resource = spec.resource.toUtf8();
    if (spec.nested) {
        std::unique_ptr<Mlt::Tractor> t = buildTractor(*spec.nested, 1);
        if (t)
            p = std::make_shared<Mlt::Producer>(t->get_producer());
    } else if (spec.service == QLatin1String("chain")) {
        auto chain = std::make_shared<Mlt::Chain>(m_profile, resource.constData());
        if (chain->is_valid()) {
            for (const FilterSpec &l : spec.links) {
                Mlt::Link link(l.service.toUtf8().constData());
                if (!link.is_valid()) {
                    m_errors << QStringLiteral("missing link %1").arg(l.service);
                    continue;
                }
                applyProps(link, l.props);
                chain->attach(link);
            }
            p = std::make_shared<Mlt::Producer>(chain->get_producer());
        }
    } else if (spec.service == QLatin1String("avformat") || spec.service == QLatin1String("qimage")) {
        // Through the loader, so normalizing filters (scale, pad, resample) are attached
        p = std::make_shared<Mlt::Producer>(m_profile, resource.constData());
    } else {
        p = std::make_shared<Mlt::Producer>(m_profile, spec.service.toUtf8().constData(), resource.constData());
    }
    if (!p || !p->is_valid()) {
        m_errors << QStringLiteral("could not open %1").arg(spec.resource);
        return {};
    }
    applyProps(*p, spec.props);
    if (!spec.nested && spec.service != QLatin1String("chain")) {
        m_cache.insert(spec.key, p);
        m_used.insert(spec.key);
    }
    return p;
}

static void attachFilters(Mlt::Profile &profile, Mlt::Service &service, const QList<FilterSpec> &filters, QStringList &errors)
{
    for (const FilterSpec &f : filters) {
        Mlt::Filter filter(profile, f.service.toUtf8().constData());
        if (!filter.is_valid()) {
            errors << QStringLiteral("missing filter %1").arg(f.service);
            continue;
        }
        applyProps(filter, f.props);
        if (f.in >= 0)
            filter.set_in_and_out(f.in, f.out);
        service.attach(filter);
    }
}

std::unique_ptr<Mlt::Tractor> MltBuilder::build(const RenderPlan &plan)
{
    m_errors.clear();
    m_used.clear();
    return buildTractor(plan, 0);
}

std::unique_ptr<Mlt::Tractor> MltBuilder::buildTractor(const RenderPlan &plan, int depth)
{
    Q_UNUSED(depth)
    QList<std::shared_ptr<Mlt::Producer>> producers;
    for (const ProducerSpec &spec : plan.producers)
        producers << producer(spec);

    auto appendCut = [&](Mlt::Playlist &pl, const CutSpec &cut) -> bool {
        const auto &p = producers.value(cut.producer);
        if (!p) {
            pl.blank(cut.out - cut.in);
            return false;
        }
        pl.append(*p, cut.in, cut.out);
        std::unique_ptr<Mlt::Producer> clip(pl.get_clip(pl.count() - 1));
        if (clip && clip->is_valid()) {
            applyProps(*clip, cut.props);
            attachFilters(m_profile, *clip, cut.filters, m_errors);
        }
        return true;
    };

    auto tractor = std::make_unique<Mlt::Tractor>(m_profile);
    for (int t = 0; t < plan.tracks.size(); ++t) {
        const TrackSpec &ts = plan.tracks.at(t);
        Mlt::Playlist pl(m_profile);
        applyProps(pl, ts.props);
        for (const SegmentSpec &seg : ts.segments) {
            if (seg.type == SegmentSpec::Blank) {
                pl.blank(seg.length - 1);
            } else if (seg.type == SegmentSpec::Cut) {
                appendCut(pl, seg.cut);
            } else {
                Mlt::Tractor mix(m_profile);
                Mlt::Playlist a(m_profile), b(m_profile);
                appendCut(a, seg.a);
                appendCut(b, seg.b);
                mix.set_track(a, 0);
                mix.set_track(b, 1);
                for (const TransitionSpec &tr : seg.transitions) {
                    Mlt::Transition transition(m_profile, tr.service.toUtf8().constData());
                    if (!transition.is_valid()) {
                        m_errors << QStringLiteral("missing transition %1").arg(tr.service);
                        continue;
                    }
                    applyProps(transition, tr.props);
                    if (tr.in >= 0)
                        transition.set_in_and_out(tr.in, tr.out);
                    mix.plant_transition(transition, 0, 1);
                }
                applyProps(mix, seg.props);
                pl.append(mix, 0, seg.length - 1);
            }
        }
        attachFilters(m_profile, pl, ts.filters, m_errors);
        const int hide = hideValue(ts.hide);
        if (hide)
            pl.set("hide", hide);
        tractor->set_track(pl, t);
    }
    for (const TransitionSpec &tr : plan.transitions) {
        Mlt::Transition transition(m_profile, tr.service.toUtf8().constData());
        if (!transition.is_valid()) {
            m_errors << QStringLiteral("missing transition %1").arg(tr.service);
            continue;
        }
        applyProps(transition, tr.props);
        if (tr.in >= 0)
            transition.set_in_and_out(tr.in, tr.out);
        tractor->plant_transition(transition, tr.aTrack, tr.bTrack);
    }
    attachFilters(m_profile, *tractor, plan.filters, m_errors);
    applyProps(*tractor, plan.props);
    if (!m_errors.isEmpty() && depth == 0)
        qWarning() << "lingmo-studio: MLT build:" << m_errors;
    return tractor;
}

} // namespace Studio
