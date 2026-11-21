/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


#include "UVCColor.h"

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


//	B_RGB32 is blue, green, red, alpha in memory. The input is limited range
//	BT.601: Y from 16 (black) to 235 (white), U and V around 128.


static bool
pixel_is(const uint8_t* pixel, int blue, int green, int red)
{
	return pixel[0] == blue && pixel[1] == green && pixel[2] == red
		&& pixel[3] == 255;
}


static void
decode_one_pair(uint8_t y0, uint8_t u, uint8_t y1, uint8_t v, uint8_t* out)
{
	const uint8_t in[4] = { y0, u, y1, v };
	uvc_decode_yuy2(out, in, 2, 1);
}


static void
test_black_white_and_gray()
{
	uint8_t out[8];
	decode_one_pair(16, 128, 235, 128, out);
	CHECK(pixel_is(out, 0, 0, 0));
	CHECK(pixel_is(out + 4, 255, 255, 255));

	decode_one_pair(126, 128, 126, 128, out);
	CHECK(pixel_is(out, 128, 128, 128));
}


static void
test_pure_red()
{
	uint8_t out[8];
	decode_one_pair(81, 90, 81, 240, out);
	CHECK(pixel_is(out, 0, 0, 255));
}


static void
test_values_outside_the_range_are_clamped()
{
	uint8_t out[8];
	decode_one_pair(255, 255, 0, 255, out);
	// Bright Y with strong U and V runs over 255 in blue and red.
	CHECK(out[0] == 255 && out[2] == 255);
	// Y = 0 with strong U and V gives a negative green.
	CHECK(out[4 + 1] == 0);
}


static void
test_both_pixels_share_u_and_v()
{
	uint8_t out[8];
	decode_one_pair(81, 90, 145, 54, out);
	// Same color difference, different brightness: the second is lighter.
	CHECK(out[4 + 1] > out[1]);
	CHECK(out[3] == 255 && out[7] == 255);
}


static void
test_whole_picture_is_written_and_nothing_after_it()
{
	const int kWidth = 4;
	const int kHeight = 2;
	uint8_t in[kWidth * kHeight * 2];
	for (size_t i = 0; i < sizeof(in); i += 4) {
		in[i] = 235;
		in[i + 1] = 128;
		in[i + 2] = 235;
		in[i + 3] = 128;
	}
	uint8_t out[kWidth * kHeight * 4 + 4];
	memset(out, 7, sizeof(out));
	uvc_decode_yuy2(out, in, kWidth, kHeight);
	for (int i = 0; i < kWidth * kHeight; i++)
		CHECK(pixel_is(out + i * 4, 255, 255, 255));
	CHECK(out[kWidth * kHeight * 4] == 7);
}


static void
test_clamp()
{
	CHECK(uvc_clamp_to_byte(-5) == 0);
	CHECK(uvc_clamp_to_byte(0) == 0);
	CHECK(uvc_clamp_to_byte(200) == 200);
	CHECK(uvc_clamp_to_byte(300) == 255);
}


static void
test_frame_fits_only_when_both_buffers_are_large_enough()
{
	CHECK(uvc_yuy2_frame_fits(614400, 1228800, 640, 480));
	CHECK(!uvc_yuy2_frame_fits(614399, 1228800, 640, 480));
	CHECK(!uvc_yuy2_frame_fits(614400, 1228799, 640, 480));
	CHECK(!uvc_yuy2_frame_fits(0, 0, 0, 480));
	CHECK(!uvc_yuy2_frame_fits(100, 100, -2, 480));
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
	RUN(test_black_white_and_gray);
	RUN(test_pure_red);
	RUN(test_values_outside_the_range_are_clamped);
	RUN(test_both_pixels_share_u_and_v);
	RUN(test_whole_picture_is_written_and_nothing_after_it);
	RUN(test_clamp);
	RUN(test_frame_fits_only_when_both_buffers_are_large_enough);

	printf("%d tests, %d failures\n", sTests, sFailures);
	return sFailures == 0 ? 0 : 1;
}
