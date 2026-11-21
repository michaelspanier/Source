/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


#include "UVCControls.h"

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


static void
test_transfer_of_the_expected_size_succeeds()
{
	CHECK(uvc_transfer_succeeded(2, 2));
	CHECK(uvc_transfer_succeeded(1, 1));
}


static void
test_short_or_failed_transfer_does_not_succeed()
{
	CHECK(!uvc_transfer_succeeded(1, 2));
	CHECK(!uvc_transfer_succeeded(0, 2));
	CHECK(!uvc_transfer_succeeded(-1, 2));
	CHECK(!uvc_transfer_succeeded(-2147483647 - 1, 2));
}


static void
test_value_inside_the_range_stays()
{
	CHECK(uvc_clamp_control_value(50.0f, 0, 100) == 50);
	CHECK(uvc_clamp_control_value(0.0f, 0, 100) == 0);
	CHECK(uvc_clamp_control_value(100.0f, 0, 100) == 100);
}


static void
test_value_outside_the_range_is_cut()
{
	CHECK(uvc_clamp_control_value(150.0f, 0, 100) == 100);
	CHECK(uvc_clamp_control_value(-5.0f, 0, 100) == 0);
	CHECK(uvc_clamp_control_value(1e9f, -64, 64) == 64);
	CHECK(uvc_clamp_control_value(-1e9f, -64, 64) == -64);
}


static void
test_value_is_rounded_not_cut_off()
{
	CHECK(uvc_clamp_control_value(49.6f, 0, 100) == 50);
	CHECK(uvc_clamp_control_value(49.4f, 0, 100) == 49);
	CHECK(uvc_clamp_control_value(-0.6f, -64, 64) == -1);
	CHECK(uvc_clamp_control_value(-0.4f, -64, 64) == 0);
}


static void
test_negative_range_works()
{
	CHECK(uvc_clamp_control_value(-10.0f, -64, 64) == -10);
	CHECK(uvc_clamp_control_value(-65.0f, -64, 64) == -64);
}


static void
test_not_a_number_gives_the_minimum()
{
	volatile float zero = 0.0f;
	float notANumber = zero / zero;
	CHECK(uvc_clamp_control_value(notANumber, 10, 90) == 10);
}


static void
test_automatic_exposure_prefers_the_full_automatic_mode()
{
	CHECK(uvc_exposure_mode(true, 0x0b) == kUvcExposureAuto);
	CHECK(uvc_exposure_mode(true, 0x03) == kUvcExposureAuto);
}


static void
test_automatic_exposure_falls_back_to_priority_modes()
{
	CHECK(uvc_exposure_mode(true, 0x09) == kUvcExposureAperturePriority);
	CHECK(uvc_exposure_mode(true, 0x05) == kUvcExposureShutterPriority);
	CHECK(uvc_exposure_mode(true, 0x0d) == kUvcExposureAperturePriority);
}


static void
test_manual_exposure_needs_the_manual_mode()
{
	CHECK(uvc_exposure_mode(false, 0x0b) == kUvcExposureManual);
	CHECK(uvc_exposure_mode(false, 0x08) == 0);
}


static void
test_unknown_support_assumes_the_standard_modes()
{
	CHECK(uvc_exposure_mode(true, 0) == kUvcExposureAuto);
	CHECK(uvc_exposure_mode(false, 0) == kUvcExposureManual);
}


static void
test_no_automatic_mode_means_nothing_fits()
{
	CHECK(uvc_exposure_mode(true, 0x01) == 0);
}


static void
test_exposure_mode_tells_automatic_from_manual()
{
	CHECK(!uvc_exposure_mode_is_automatic(kUvcExposureManual));
	CHECK(uvc_exposure_mode_is_automatic(kUvcExposureAuto));
	CHECK(uvc_exposure_mode_is_automatic(kUvcExposureShutterPriority));
	CHECK(uvc_exposure_mode_is_automatic(kUvcExposureAperturePriority));
	CHECK(!uvc_exposure_mode_is_automatic(0));
}


static void
test_pair_is_packed_low_byte_first()
{
	uint8_t bytes[4] = { 0xee, 0xee, 0xee, 0xee };
	uvc_pack_pair(0x0102, 0x0304, bytes);
	CHECK(bytes[0] == 0x02 && bytes[1] == 0x01);
	CHECK(bytes[2] == 0x04 && bytes[3] == 0x03);
}


static void
test_pair_with_negative_values_survives_a_round_trip()
{
	uint8_t bytes[4];
	int16_t first, second;
	uvc_pack_pair(-300, 1234, bytes);
	uvc_unpack_pair(bytes, &first, &second);
	CHECK(first == -300);
	CHECK(second == 1234);
}


static void
test_u32_is_packed_low_byte_first()
{
	uint8_t bytes[4];
	uvc_pack_u32(0x01020304u, bytes);
	CHECK(bytes[0] == 0x04 && bytes[1] == 0x03);
	CHECK(bytes[2] == 0x02 && bytes[3] == 0x01);
	CHECK(uvc_unpack_u32(bytes) == 0x01020304u);
}


static void
test_u32_with_the_high_bit_set_survives_a_round_trip()
{
	uint8_t bytes[4];
	uvc_pack_u32(0xfffffff0u, bytes);
	CHECK(uvc_unpack_u32(bytes) == 0xfffffff0u);
}


static void
test_large_range_is_not_cut_to_sixteen_bits()
{
	CHECK(uvc_clamp_control_value(100000.0f, 1, 200000) == 100000);
	CHECK(uvc_clamp_control_value(300000.0f, 1, 200000) == 200000);
}


static void
test_one_byte_value_is_read_signed_or_unsigned()
{
	const uint8_t bytes[1] = { 0xfe };
	CHECK(uvc_decode_value(bytes, 1, false) == 254);
	CHECK(uvc_decode_value(bytes, 1, true) == -2);
}


static void
test_two_byte_value_is_read_signed_or_unsigned()
{
	const uint8_t bytes[2] = { 0x18, 0xfc };	// 0xfc18 = -1000
	CHECK(uvc_decode_value(bytes, 2, true) == -1000);
	CHECK(uvc_decode_value(bytes, 2, false) == 0xfc18);
}


static void
test_four_byte_value_is_read_low_byte_first()
{
	const uint8_t bytes[4] = { 0x10, 0x27, 0x00, 0x00 };	// 10000
	CHECK(uvc_decode_value(bytes, 4, false) == 10000);
}


static void
test_encoded_value_is_decoded_again()
{
	const int32_t values[] = { 0, 1, -1, 127, -128, 10000, -10000 };
	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		uint8_t bytes[4];
		uvc_encode_value(values[i], 4, bytes);
		CHECK(uvc_decode_value(bytes, 4, true) == values[i]);
		uvc_encode_value(values[i], 2, bytes);
		CHECK(uvc_decode_value(bytes, 2, true) == values[i]);
	}
}


static void
test_one_byte_value_writes_one_byte_only()
{
	uint8_t bytes[2] = { 0xaa, 0xaa };
	uvc_encode_value(2, 1, bytes);
	CHECK(bytes[0] == 2);
	CHECK(bytes[1] == 0xaa);
}


static void
run(const char* name, void (*test)())
{
	int before = sFailures;
	test();
	sTests++;
	printf("%s %s\n", sFailures == before ? "ok    " : "FAILED", name);
}


int
main()
{
	run("transfer of the expected size succeeds",
		test_transfer_of_the_expected_size_succeeds);
	run("short or failed transfer does not succeed",
		test_short_or_failed_transfer_does_not_succeed);
	run("value inside the range stays", test_value_inside_the_range_stays);
	run("value outside the range is cut", test_value_outside_the_range_is_cut);
	run("value is rounded, not cut off", test_value_is_rounded_not_cut_off);
	run("negative range works", test_negative_range_works);
	run("not a number gives the minimum", test_not_a_number_gives_the_minimum);

	run("automatic exposure prefers the full automatic mode",
		test_automatic_exposure_prefers_the_full_automatic_mode);
	run("automatic exposure falls back to priority modes",
		test_automatic_exposure_falls_back_to_priority_modes);
	run("manual exposure needs the manual mode",
		test_manual_exposure_needs_the_manual_mode);
	run("unknown support assumes the standard modes",
		test_unknown_support_assumes_the_standard_modes);
	run("no automatic mode means nothing fits",
		test_no_automatic_mode_means_nothing_fits);
	run("exposure mode tells automatic from manual",
		test_exposure_mode_tells_automatic_from_manual);
	run("pair is packed low byte first", test_pair_is_packed_low_byte_first);
	run("pair with negative values survives a round trip",
		test_pair_with_negative_values_survives_a_round_trip);
	run("u32 is packed low byte first", test_u32_is_packed_low_byte_first);
	run("u32 with the high bit set survives a round trip",
		test_u32_with_the_high_bit_set_survives_a_round_trip);
	run("large range is not cut to sixteen bits",
		test_large_range_is_not_cut_to_sixteen_bits);
	run("one byte value is read signed or unsigned",
		test_one_byte_value_is_read_signed_or_unsigned);
	run("two byte value is read signed or unsigned",
		test_two_byte_value_is_read_signed_or_unsigned);
	run("four byte value is read low byte first",
		test_four_byte_value_is_read_low_byte_first);
	run("encoded value is decoded again", test_encoded_value_is_decoded_again);
	run("one byte value writes one byte only",
		test_one_byte_value_writes_one_byte_only);

	printf("%d tests, %d failures\n", sTests, sFailures);
	return sFailures == 0 ? 0 : 1;
}
