/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


#include "CamVideoSize.h"

#include <stdio.h>
#include <string.h>


static int sFailures = 0;
static int sTests = 0;


#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			sFailures++; \
		} \
	} while (0)


static void
test_size_survives_the_parameter_value()
{
	int32_t value = cam_size_to_parameter(1920, 1080);
	uint32_t width = 0;
	uint32_t height = 0;
	CHECK(cam_size_from_parameter(value, &width, &height));
	CHECK(width == 1920 && height == 1080);
}


static void
test_value_is_width_above_height()
{
	// Applications read the value without this header: width in the upper
	// 16 bits, height in the lower ones.
	CHECK(cam_size_to_parameter(640, 480) == (640 << 16 | 480));
}


static void
test_value_without_a_side_is_no_size()
{
	uint32_t width = 7;
	uint32_t height = 7;
	CHECK(!cam_size_from_parameter(0, &width, &height));
	CHECK(!cam_size_from_parameter(640 << 16, &width, &height));
	CHECK(!cam_size_from_parameter(480, &width, &height));
	CHECK(width == 7 && height == 7);
}


static void
test_frame_rate_from_the_interval()
{
	// Intervals count 100 ns.
	CHECK(cam_frames_per_second(333333) == 30);
	CHECK(cam_frames_per_second(666666) == 15);
	CHECK(cam_frames_per_second(2000000) == 5);
	CHECK(cam_frames_per_second(0) == 0);
}


static void
test_label_with_and_without_rate()
{
	char label[48];
	cam_size_label(label, sizeof(label), 1920, 1080, 2000000);
	CHECK(strcmp(label, "1920 \xc3\x97 1080 (5 fps)") == 0);

	cam_size_label(label, sizeof(label), 640, 480, 0);
	CHECK(strcmp(label, "640 \xc3\x97 480") == 0);
}


static void
test_only_sizes_up_to_the_limit_are_allowed()
{
	CHECK(cam_video_size_allowed(640, 480));
	CHECK(cam_video_size_allowed(kCamMaxVideoSide, kCamMaxVideoSide));
	CHECK(!cam_video_size_allowed(0, 480));
	CHECK(!cam_video_size_allowed(640, 0));
	CHECK(!cam_video_size_allowed(kCamMaxVideoSide + 1, 1));
	CHECK(!cam_video_size_allowed(32768, 32768));
}


#define RUN(test) \
	do { \
		int before = sFailures; \
		test(); \
		sTests++; \
		printf("%s %s\n", sFailures == before ? "ok  " : "FAIL", #test); \
	} while (0)


int
main()
{
	RUN(test_size_survives_the_parameter_value);
	RUN(test_value_is_width_above_height);
	RUN(test_value_without_a_side_is_no_size);
	RUN(test_frame_rate_from_the_interval);
	RUN(test_label_with_and_without_rate);
	RUN(test_only_sizes_up_to_the_limit_are_allowed);

	printf("%d tests, %d failures\n", sTests, sFailures);
	return sFailures == 0 ? 0 : 1;
}
