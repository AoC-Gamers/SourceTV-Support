// SPDX-License-Identifier: GPL-3.0-only
// Policy extracted from SourceTV-Support. See LICENSE and NOTICE.md.
#pragma once

#include <algorithm>
#include <climits>
#include <cmath>

inline int FramesToKeep(float delay, int snapshotRate, int minimumFrames)
{
	const int minimum = (std::max)(0, minimumFrames);
	if (snapshotRate <= 0 || std::isnan(delay))
		return minimum;

	const float effectiveDelay = (std::max)(1.0f, delay);
	const float frames = 2 * ((1 + effectiveDelay) * snapshotRate);
	if (static_cast<double>(frames) >= INT_MAX)
		return INT_MAX;

	return (std::max)(minimum, static_cast<int>(frames));
}

template <typename FrameStore>
void PruneFrames(FrameStore& store, int framesToKeep)
{
	for (int count = store.CountClientFrames(); count > framesToKeep; --count)
		store.RemoveOldestFrame();
}
