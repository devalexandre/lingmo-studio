/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#pragma once

#include "plan.h"

#include <QHash>
#include <QSet>
#include <memory>

namespace Mlt {
class Profile;
class Producer;
class Tractor;
}

namespace Studio {

// Turns a plan into live MLT objects. Media producers are cached by key, so a
// rebuild after an edit does not reopen files. Must not run while a consumer
// is pulling frames from a previous build (the player stops it first).
class MltBuilder
{
public:
    explicit MltBuilder(Mlt::Profile &profile);
    ~MltBuilder();

    std::unique_ptr<Mlt::Tractor> build(const RenderPlan &plan);
    void clearCache();
    void prune(); // drops producers unused by the last build
    QStringList errors() const { return m_errors; }

private:
    std::shared_ptr<Mlt::Producer> producer(const ProducerSpec &spec);
    std::unique_ptr<Mlt::Tractor> buildTractor(const RenderPlan &plan, int depth);

    Mlt::Profile &m_profile;
    QHash<QString, std::shared_ptr<Mlt::Producer>> m_cache;
    QSet<QString> m_used;
    QStringList m_errors;
    QList<std::shared_ptr<void>> m_keepAlive;
};

// MLT must be initialized once per process (thread safe, idempotent).
void initMlt();
QStringList availableMltServices(); // filters + transitions + producers

} // namespace Studio
