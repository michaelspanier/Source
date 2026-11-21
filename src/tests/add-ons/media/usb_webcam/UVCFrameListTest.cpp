/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


#include "UVCFrameList.h"

#include <stdio.h>


static int sFailures = 0;
static int sTests = 0;


#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			sFailures++; \
		} \
	} while (0)


static const uint32_t k30fps = 333333;
static const uint32_t k10fps = 1000000;
static const uint32_t k5fps = 2000000;


static uvc_frame
make_frame(uint16_t width, uint16_t height, uint32_t interval, bool compressed)
{
	uvc_frame frame;
	frame.width = width;
	frame.height = height;
	frame.formatIndex = compressed ? 2 : 1;
	frame.frameIndex = 7;
	frame.interval = interval;
	frame.compressed = compressed;
	return frame;
}


static void
test_empty_list_finds_nothing()
{
	uvc_frame_list list;
	uvc_frame_list_init(&list);
	CHECK(list.count == 0);
	CHECK(uvc_frame_list_find(&list, 640, 480) == NULL);
}


static void
test_found_frame_keeps_its_indices()
{
	uvc_frame_list list;
	uvc_frame_list_init(&list);
	uvc_frame frame = make_frame(640, 480, k30fps, false);
	frame.formatIndex = 3;
	frame.frameIndex = 9;
	uvc_frame_list_add(&list, frame);

	const uvc_frame* found = uvc_frame_list_find(&list, 640, 480);
	CHECK(found != NULL);
	CHECK(found != NULL && found->formatIndex == 3 && found->frameIndex == 9);
	CHECK(uvc_frame_list_find(&list, 480, 640) == NULL);
}


static void
test_faster_compressed_frame_replaces_slow_uncompressed()
{
	// The C920 sends 1920 x 1080 uncompressed at 5 and as MJPEG at 30 frames
	// per second.
	uvc_frame_list list;
	uvc_frame_list_init(&list);
	uvc_frame_list_add(&list, make_frame(1920, 1080, k5fps, false));
	uvc_frame_list_add(&list, make_frame(1920, 1080, k30fps, true));

	CHECK(list.count == 1);
	const uvc_frame* found = uvc_frame_list_find(&list, 1920, 1080);
	CHECK(found != NULL && found->compressed && found->interval == k30fps);
}


static void
test_slower_compressed_frame_does_not_replace()
{
	uvc_frame_list list;
	uvc_frame_list_init(&list);
	uvc_frame_list_add(&list, make_frame(640, 480, k30fps, false));
	uvc_frame_list_add(&list, make_frame(640, 480, k10fps, true));

	const uvc_frame* found = uvc_frame_list_find(&list, 640, 480);
	CHECK(found != NULL && !found->compressed);
}


static void
test_uncompressed_wins_at_equal_speed()
{
	// No decoding and no compression artefacts, whichever comes first.
	uvc_frame_list list;
	uvc_frame_list_init(&list);
	uvc_frame_list_add(&list, make_frame(640, 480, k30fps, true));
	uvc_frame_list_add(&list, make_frame(640, 480, k30fps, false));

	const uvc_frame* found = uvc_frame_list_find(&list, 640, 480);
	CHECK(found != NULL && !found->compressed);

	uvc_frame_list_add(&list, make_frame(640, 480, k30fps, true));
	found = uvc_frame_list_find(&list, 640, 480);
	CHECK(found != NULL && !found->compressed);
	CHECK(list.count == 1);
}


static void
test_unknown_interval_counts_as_slowest()
{
	uvc_frame_list list;
	uvc_frame_list_init(&list);
	uvc_frame_list_add(&list, make_frame(640, 480, 0, false));
	uvc_frame_list_add(&list, make_frame(640, 480, k5fps, true));
	const uvc_frame* found = uvc_frame_list_find(&list, 640, 480);
	CHECK(found != NULL && found->compressed);

	uvc_frame_list_add(&list, make_frame(320, 240, k5fps, false));
	uvc_frame_list_add(&list, make_frame(320, 240, 0, true));
	found = uvc_frame_list_find(&list, 320, 240);
	CHECK(found != NULL && !found->compressed);
}


static void
test_list_is_sorted_by_area()
{
	// Cameras list their sizes in any order, the menu shows them growing.
	uvc_frame_list list;
	uvc_frame_list_init(&list);
	uvc_frame_list_add(&list, make_frame(640, 480, k30fps, false));
	uvc_frame_list_add(&list, make_frame(1280, 720, k30fps, true));
	uvc_frame_list_add(&list, make_frame(320, 180, k30fps, false));
	uvc_frame_list_add(&list, make_frame(424, 240, k30fps, false));

	CHECK(list.count == 4);
	CHECK(list.frames[0].width == 320);
	CHECK(list.frames[1].width == 424);
	CHECK(list.frames[2].width == 640);
	CHECK(list.frames[3].width == 1280);
}


static void
test_equal_area_keeps_wider_last()
{
	uvc_frame_list list;
	uvc_frame_list_init(&list);
	uvc_frame_list_add(&list, make_frame(640, 360, k30fps, false));
	uvc_frame_list_add(&list, make_frame(480, 480, k30fps, false));
	uvc_frame_list_add(&list, make_frame(360, 640, k30fps, false));

	CHECK(list.count == 3);
	CHECK(list.frames[0].width == 360);
	CHECK(list.frames[1].width == 480);
	CHECK(list.frames[2].width == 640);
}


static void
test_full_list_drops_further_sizes()
{
	uvc_frame_list list;
	uvc_frame_list_init(&list);
	for (int32_t i = 0; i < kUvcMaxFrames + 5; i++)
		uvc_frame_list_add(&list, make_frame(16 + i, 16, k30fps, false));

	CHECK(list.count == kUvcMaxFrames);
	CHECK(uvc_frame_list_find(&list, 16 + kUvcMaxFrames - 1, 16) != NULL);
	CHECK(uvc_frame_list_find(&list, 16 + kUvcMaxFrames, 16) == NULL);

	// A known size can still change its format.
	uvc_frame_list_add(&list, make_frame(16, 16, k30fps / 2, true));
	const uvc_frame* found = uvc_frame_list_find(&list, 16, 16);
	CHECK(found != NULL && found->compressed);
}


static void
test_zero_side_is_ignored()
{
	uvc_frame_list list;
	uvc_frame_list_init(&list);
	uvc_frame_list_add(&list, make_frame(0, 480, k30fps, false));
	uvc_frame_list_add(&list, make_frame(640, 0, k30fps, false));
	CHECK(list.count == 0);
}


#define RUN(test) \
	do { \
		sTests++; \
		int before = sFailures; \
		test(); \
		printf("%s %s\n", sFailures == before ? "ok    " : "FAILED", #test); \
	} while (0)


int
main()
{
	RUN(test_empty_list_finds_nothing);
	RUN(test_found_frame_keeps_its_indices);
	RUN(test_faster_compressed_frame_replaces_slow_uncompressed);
	RUN(test_slower_compressed_frame_does_not_replace);
	RUN(test_uncompressed_wins_at_equal_speed);
	RUN(test_unknown_interval_counts_as_slowest);
	RUN(test_list_is_sorted_by_area);
	RUN(test_equal_area_keeps_wider_last);
	RUN(test_full_list_drops_further_sizes);
	RUN(test_zero_side_is_ignored);

	printf("\n%d tests, %d failures\n", sTests, sFailures);
	return sFailures == 0 ? 0 : 1;
}
