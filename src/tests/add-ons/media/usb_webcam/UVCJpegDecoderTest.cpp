/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


#include "UVCJpegDecoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include <jpeglib.h>


static int sFailures = 0;
static int sTests = 0;


#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			sFailures++; \
		} \
	} while (0)


static const int32_t kWidth = 32;
static const int32_t kHeight = 16;
static const uint8_t kUntouched = 0x5a;


//!	A picture whose left half is red and right half blue, as 4:2:2 JPEG.
static std::vector<uint8_t>
make_jpeg(int32_t width, int32_t height)
{
	jpeg_compress_struct info;
	jpeg_error_mgr errors;
	info.err = jpeg_std_error(&errors);
	jpeg_create_compress(&info);

	unsigned char* data = NULL;
	unsigned long size = 0;
	jpeg_mem_dest(&info, &data, &size);
	info.image_width = width;
	info.image_height = height;
	info.input_components = 3;
	info.in_color_space = JCS_RGB;
	jpeg_set_defaults(&info);
	jpeg_set_quality(&info, 95, TRUE);
	info.comp_info[0].h_samp_factor = 2;
	info.comp_info[0].v_samp_factor = 1;
	jpeg_start_compress(&info, TRUE);

	std::vector<uint8_t> row(width * 3);
	for (int32_t x = 0; x < width; x++) {
		bool left = x < width / 2;
		row[x * 3] = left ? 255 : 0;
		row[x * 3 + 1] = 0;
		row[x * 3 + 2] = left ? 0 : 255;
	}
	while (info.next_scanline < info.image_height) {
		JSAMPROW pointer = &row[0];
		jpeg_write_scanlines(&info, &pointer, 1);
	}
	jpeg_finish_compress(&info);
	jpeg_destroy_compress(&info);

	std::vector<uint8_t> jpeg(data, data + size);
	free(data);
	return jpeg;
}


//!	Removes the Huffman tables, as most cameras do in their MJPEG frames.
static std::vector<uint8_t>
strip_huffman_tables(const std::vector<uint8_t>& jpeg)
{
	std::vector<uint8_t> stripped(jpeg.begin(), jpeg.begin() + 2);
	size_t i = 2;
	while (i + 4 <= jpeg.size() && jpeg[i] == 0xff && jpeg[i + 1] != 0xda) {
		size_t length = 2 + (jpeg[i + 2] << 8 | jpeg[i + 3]);
		if (jpeg[i + 1] != 0xc4) {
			stripped.insert(stripped.end(), jpeg.begin() + i,
				jpeg.begin() + i + length);
		}
		i += length;
	}
	stripped.insert(stripped.end(), jpeg.begin() + i, jpeg.end());
	return stripped;
}


static bool
has_huffman_tables(const std::vector<uint8_t>& jpeg)
{
	for (size_t i = 0; i + 1 < jpeg.size(); i++) {
		if (jpeg[i] == 0xff && jpeg[i + 1] == 0xc4)
			return true;
	}
	return false;
}


struct test_picture {
	std::vector<uint8_t>	bits;
	uvc_picture				picture;

	test_picture(int32_t width, int32_t height, size_t size)
		:
		bits(size, kUntouched)
	{
		picture.bits = &bits[0];
		picture.size = size;
		picture.width = width;
		picture.height = height;
	}

	bool Untouched() const
	{
		for (size_t i = 0; i < bits.size(); i++) {
			if (bits[i] != kUntouched)
				return false;
		}
		return true;
	}
};


static bool
near(uint8_t value, uint8_t expected)
{
	return abs((int)value - (int)expected) <= 16;
}


//!	B_RGB32 is blue, green, red, alpha in memory.
static bool
is_red_then_blue(const test_picture& test)
{
	const uint8_t* left = &test.bits[(kHeight / 2 * kWidth + 4) * 4];
	const uint8_t* right = &test.bits[(kHeight / 2 * kWidth + kWidth - 4) * 4];
	return near(left[0], 0) && near(left[1], 0) && near(left[2], 255)
		&& left[3] == 255 && near(right[0], 255) && near(right[1], 0)
		&& near(right[2], 0) && right[3] == 255;
}


static void
test_decodes_to_rgb32()
{
	UVCJpegDecoder decoder;
	CHECK(decoder.InitCheck());
	std::vector<uint8_t> jpeg = make_jpeg(kWidth, kHeight);
	test_picture test(kWidth, kHeight, kWidth * kHeight * 4);

	CHECK(decoder.Decode(&jpeg[0], jpeg.size(), test.picture));
	CHECK(is_red_then_blue(test));
}


static void
test_decodes_frame_without_huffman_tables()
{
	UVCJpegDecoder decoder;
	std::vector<uint8_t> jpeg = strip_huffman_tables(make_jpeg(kWidth, kHeight));
	CHECK(!has_huffman_tables(jpeg));
	test_picture test(kWidth, kHeight, kWidth * kHeight * 4);

	CHECK(decoder.Decode(&jpeg[0], jpeg.size(), test.picture));
	CHECK(is_red_then_blue(test));
}


static void
test_rejects_other_size()
{
	UVCJpegDecoder decoder;
	std::vector<uint8_t> jpeg = make_jpeg(kWidth * 2, kHeight);
	test_picture test(kWidth, kHeight, kWidth * 2 * kHeight * 4);

	CHECK(!decoder.Decode(&jpeg[0], jpeg.size(), test.picture));
	CHECK(test.Untouched());
}


static void
test_rejects_small_output()
{
	UVCJpegDecoder decoder;
	std::vector<uint8_t> jpeg = make_jpeg(kWidth, kHeight);
	test_picture test(kWidth, kHeight, kWidth * kHeight * 4 - 1);

	CHECK(!decoder.Decode(&jpeg[0], jpeg.size(), test.picture));
	CHECK(test.Untouched());
}


static void
test_rejects_truncated_frame()
{
	// A frame that lost packets on the way.
	UVCJpegDecoder decoder;
	std::vector<uint8_t> jpeg = make_jpeg(kWidth, kHeight);
	jpeg.resize(jpeg.size() * 2 / 3);
	test_picture test(kWidth, kHeight, kWidth * kHeight * 4);

	CHECK(!decoder.Decode(&jpeg[0], jpeg.size(), test.picture));
}


static void
test_rejects_data_that_is_no_jpeg()
{
	UVCJpegDecoder decoder;
	std::vector<uint8_t> noise(4096);
	for (size_t i = 0; i < noise.size(); i++)
		noise[i] = (uint8_t)(i * 37 + 11);
	test_picture test(kWidth, kHeight, kWidth * kHeight * 4);

	CHECK(!decoder.Decode(&noise[0], noise.size(), test.picture));
	CHECK(!decoder.Decode(&noise[0], 0, test.picture));
	CHECK(!decoder.Decode(NULL, 0, test.picture));
	CHECK(test.Untouched());
}


static void
test_decodes_again_after_a_failure()
{
	UVCJpegDecoder decoder;
	std::vector<uint8_t> jpeg = make_jpeg(kWidth, kHeight);
	std::vector<uint8_t> truncated(jpeg.begin(), jpeg.begin() + jpeg.size() / 2);
	test_picture test(kWidth, kHeight, kWidth * kHeight * 4);

	CHECK(!decoder.Decode(&truncated[0], truncated.size(), test.picture));
	CHECK(decoder.Decode(&jpeg[0], jpeg.size(), test.picture));
	CHECK(is_red_then_blue(test));
	CHECK(!decoder.Decode(&truncated[0], truncated.size(), test.picture));
	CHECK(decoder.Decode(&jpeg[0], jpeg.size(), test.picture));
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
	RUN(test_decodes_to_rgb32);
	RUN(test_decodes_frame_without_huffman_tables);
	RUN(test_rejects_other_size);
	RUN(test_rejects_small_output);
	RUN(test_rejects_truncated_frame);
	RUN(test_rejects_data_that_is_no_jpeg);
	RUN(test_decodes_again_after_a_failure);

	printf("\n%d tests, %d failures\n", sTests, sFailures);
	return sFailures == 0 ? 0 : 1;
}
