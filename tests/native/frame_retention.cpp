// SPDX-License-Identifier: GPL-3.0-only
#include "hooks/frame_retention.h"

#include <cassert>
#include <climits>
#include <limits>
#include <cstdio>

int main()
{
	const struct {
		const char *description;
		float delay;
		int snapshotRate;
		int minimum;
		int expected;
	} cases[] = {
		{"minimum with zero snapshot rate", 20.0f, 0, 128, 128},
		{"exact minimum", 1.0f, 32, 128, 128},
		{"below-one delay clamped", 0.5f, 40, 128, 160},
		{"positive retention window", 4.0f, 20, 128, 200},
		{"truncation after doubling", 2.25f, 5, 1, 32},
		{"caller sets the minimum", 1.0f, 32, 256, 256},
		{"negative rate", 20.0f, -1, 128, 128},
		{"negative delay and minimum", -10.0f, 10, -1, 40},
		{"NaN delay", std::numeric_limits<float>::quiet_NaN(), 20, 128, 128},
		{"infinite delay", std::numeric_limits<float>::infinity(), 20, 128, INT_MAX},
		{"finite overflow", std::numeric_limits<float>::max(), 20, 128, INT_MAX},
		{"maximum rate", 1.0f, INT_MAX, 128, INT_MAX},
	};
	for (const auto &test : cases) {
		const int actual = FramesToKeep(test.delay, test.snapshotRate, test.minimum);
		if (actual != test.expected) {
			std::fprintf(stderr, "frame_retention: %s: expected %d, got %d\n",
				test.description, test.expected, actual);
			return 1;
		}
	}
	struct FakeFrameStore {
		int count;
		int oldest = 0;
		int CountClientFrames() const { return count; }
		void RemoveOldestFrame() { --count; ++oldest; }
	} store{5};
	PruneFrames(store, 2);
	assert(store.count == 2 && store.oldest == 3);
	PruneFrames(store, 2);
	assert(store.count == 2 && store.oldest == 3);

	std::printf("frame_retention: %zu cases passed (%zu-bit)\n",
		sizeof(cases) / sizeof(cases[0]), sizeof(void *) * CHAR_BIT);
}
