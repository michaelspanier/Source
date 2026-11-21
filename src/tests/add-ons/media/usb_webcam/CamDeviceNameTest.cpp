/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


#include "CamDeviceName.h"

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
test_product_string_is_the_name()
{
	char name[64];
	CHECK(cam_device_name(name, sizeof(name), "Integrated Camera"));
	CHECK(strcmp(name, "Integrated Camera") == 0);
}


static void
test_spaces_around_the_product_string_are_dropped()
{
	// Some devices pad their strings.
	char name[64];
	CHECK(cam_device_name(name, sizeof(name), "  HD Pro Webcam C920  "));
	CHECK(strcmp(name, "HD Pro Webcam C920") == 0);
}


static void
test_missing_or_empty_product_string_gives_no_name()
{
	char name[64] = "unchanged";
	CHECK(!cam_device_name(name, sizeof(name), NULL));
	CHECK(!cam_device_name(name, sizeof(name), ""));
	CHECK(!cam_device_name(name, sizeof(name), "   "));
	CHECK(strcmp(name, "unchanged") == 0);
}


static void
test_long_name_is_cut_to_the_buffer()
{
	char name[8];
	CHECK(cam_device_name(name, sizeof(name), "Integrated Camera"));
	CHECK(strcmp(name, "Integra") == 0);
}


static void
test_cut_does_not_split_a_character()
{
	// "Kamera ä" with a two-byte ä: seven bytes fit, the ä would be split.
	char name[9];
	CHECK(cam_device_name(name, sizeof(name), "Kamera \xc3\xa4x"));
	CHECK(strcmp(name, "Kamera") == 0);
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
	RUN(test_product_string_is_the_name);
	RUN(test_spaces_around_the_product_string_are_dropped);
	RUN(test_missing_or_empty_product_string_gives_no_name);
	RUN(test_long_name_is_cut_to_the_buffer);
	RUN(test_cut_does_not_split_a_character);

	printf("%d tests, %d failures\n", sTests, sFailures);
	return sFailures == 0 ? 0 : 1;
}
