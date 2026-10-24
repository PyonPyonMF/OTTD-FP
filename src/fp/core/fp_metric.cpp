/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file fp_metric.cpp Implementation of the Metric Bridge. */

#include "../../stdafx.h"
#include "fp_metric.h"

#include "../../safeguards.h"

namespace fp {

/**
 * Scales that keep the speedometer honest for a given slow-down of time.
 * When every tick takes \a dilation times longer, a tile has to be
 * \a dilation times longer for vehicles to look as fast as they claim.
 * @param dilation Factor by which game time is slowed down, at least 1.
 * @param metres_per_level Metres per terrain height level.
 * @return The scales.
 */
/* static */ MetricScale MetricScale::ForTimeDilation(double dilation, double metres_per_level)
{
	MetricScale scale;
	scale.metres_per_unit = TRUE_SPEED_METRES_PER_UNIT * std::max(dilation, 1.0);
	scale.metres_per_level = metres_per_level;
	return scale;
}

} // namespace fp
