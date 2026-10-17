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

namespace Studio {

// Serializes a plan as MLT XML (readable by melt, Shotcut and Kdenlive's MLT).
QByteArray writeMltXml(const RenderPlan &plan, const QString &title = {}, const Props &extraRootProps = {});

} // namespace Studio
