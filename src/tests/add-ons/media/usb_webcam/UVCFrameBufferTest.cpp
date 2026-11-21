/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


#include "UVCFrameBuffer.h"

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
test_new_buffer_is_empty_and_takes_nothing()
{
	UVCFrameBuffer buffer;
	CHECK(buffer.Length() == 0);
	CHECK(!buffer.IsBroken());

	buffer.Append("ab", 2);
	CHECK(buffer.Length() == 0);
	CHECK(buffer.IsBroken());
}


static void
test_payloads_are_collected_in_order()
{
	UVCFrameBuffer buffer;
	CHECK(buffer.SetCapacity(8));
	buffer.Append("abc", 3);
	buffer.Append("de", 2);
	CHECK(buffer.Length() == 5);
	CHECK(memcmp(buffer.Data(), "abcde", 5) == 0);
	CHECK(!buffer.IsBroken());
}


static void
test_payload_that_fills_the_buffer_exactly_fits()
{
	UVCFrameBuffer buffer;
	CHECK(buffer.SetCapacity(4));
	buffer.Append("abcd", 4);
	CHECK(buffer.Length() == 4);
	CHECK(!buffer.IsBroken());
}


static void
test_payload_beyond_the_capacity_breaks_the_frame()
{
	UVCFrameBuffer buffer;
	CHECK(buffer.SetCapacity(4));
	buffer.Append("abc", 3);
	buffer.Append("de", 2);
	CHECK(buffer.Length() == 3);
	CHECK(buffer.IsBroken());
}


static void
test_broken_frame_takes_no_more_data()
{
	UVCFrameBuffer buffer;
	CHECK(buffer.SetCapacity(8));
	buffer.MarkBroken();
	buffer.Append("ab", 2);
	CHECK(buffer.Length() == 0);
	CHECK(buffer.IsBroken());
}


static void
test_clear_starts_a_new_frame_and_keeps_the_memory()
{
	UVCFrameBuffer buffer;
	CHECK(buffer.SetCapacity(4));
	buffer.Append("abc", 3);
	buffer.MarkBroken();
	const void* memory = buffer.Data();

	buffer.Clear();
	CHECK(buffer.Length() == 0);
	CHECK(!buffer.IsBroken());
	buffer.Append("xy", 2);
	CHECK(buffer.Data() == memory);
	CHECK(memcmp(buffer.Data(), "xy", 2) == 0);
}


static void
test_set_capacity_clears_the_frame()
{
	UVCFrameBuffer buffer;
	CHECK(buffer.SetCapacity(4));
	buffer.Append("abc", 3);
	CHECK(buffer.SetCapacity(16));
	CHECK(buffer.Length() == 0);
	buffer.Append("0123456789", 10);
	CHECK(buffer.Length() == 10);
}


static void
test_size_is_the_larger_of_camera_and_uncompressed_size()
{
	CHECK(uvc_frame_buffer_size(1000000, 640, 480) == 1000000);
	CHECK(uvc_frame_buffer_size(0, 640, 480) == 640 * 480 * 2);
	CHECK(uvc_frame_buffer_size(100, 1920, 1080) == 1920 * 1080 * 2);
}


static void
test_size_has_an_upper_limit()
{
	CHECK(uvc_frame_buffer_size(0xffffffff, 640, 480)
		== kUVCMaxFrameBufferSize);
	CHECK(uvc_frame_buffer_size(0, 65535, 65535) == kUVCMaxFrameBufferSize);
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
	RUN(test_new_buffer_is_empty_and_takes_nothing);
	RUN(test_payloads_are_collected_in_order);
	RUN(test_payload_that_fills_the_buffer_exactly_fits);
	RUN(test_payload_beyond_the_capacity_breaks_the_frame);
	RUN(test_broken_frame_takes_no_more_data);
	RUN(test_clear_starts_a_new_frame_and_keeps_the_memory);
	RUN(test_set_capacity_clears_the_frame);
	RUN(test_size_is_the_larger_of_camera_and_uncompressed_size);
	RUN(test_size_has_an_upper_limit);

	printf("%d tests, %d failures\n", sTests, sFailures);
	return sFailures == 0 ? 0 : 1;
}
