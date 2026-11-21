/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


#include "CamReplug.h"

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
test_same_camera_plugged_in_again_takes_over()
{
	CHECK(cam_takes_over(false, "HD Pro Webcam C920", "HD Pro Webcam C920"));
}


static void
test_camera_that_still_works_is_kept()
{
	// A second camera of the same model must not steal a working stream.
	CHECK(!cam_takes_over(true, "HD Pro Webcam C920", "HD Pro Webcam C920"));
}


static void
test_other_camera_does_not_take_over()
{
	// The application chose that camera; another one is not what it asked for.
	CHECK(!cam_takes_over(false, "HD Pro Webcam C920", "Integrated Camera"));
}


static void
test_missing_names_never_match()
{
	CHECK(!cam_takes_over(false, NULL, "HD Pro Webcam C920"));
	CHECK(!cam_takes_over(false, "HD Pro Webcam C920", NULL));
	CHECK(!cam_takes_over(false, NULL, NULL));
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
	RUN(test_same_camera_plugged_in_again_takes_over);
	RUN(test_camera_that_still_works_is_kept);
	RUN(test_other_camera_does_not_take_over);
	RUN(test_missing_names_never_match);
	printf("%d tests, %d failures\n", sTests, sFailures);
	return sFailures == 0 ? 0 : 1;
}
