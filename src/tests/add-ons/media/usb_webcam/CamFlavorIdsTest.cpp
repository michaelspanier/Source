/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


#include "CamFlavorIds.h"

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
test_new_camera_gets_the_fresh_id()
{
	CamFlavorIds ids;
	CHECK(ids.Take("HD Pro Webcam C920", 5) == 5);
}


static void
test_camera_plugged_in_again_gets_its_old_id()
{
	// Its node is still there and waits for it; a new id would make a
	// second node for the same camera.
	CamFlavorIds ids;
	ids.Remember("HD Pro Webcam C920", 1);
	CHECK(ids.Take("HD Pro Webcam C920", 3) == 1);
}


static void
test_old_id_is_given_only_once()
{
	CamFlavorIds ids;
	ids.Remember("HD Pro Webcam C920", 1);
	ids.Take("HD Pro Webcam C920", 3);
	CHECK(ids.Take("HD Pro Webcam C920", 4) == 4);
}


static void
test_other_camera_gets_a_fresh_id_and_leaves_the_old_one()
{
	CamFlavorIds ids;
	ids.Remember("HD Pro Webcam C920", 1);
	CHECK(ids.Take("Integrated Camera", 3) == 3);
	CHECK(ids.Take("HD Pro Webcam C920", 4) == 1);
}


static void
test_two_cameras_of_one_model_get_both_ids_back()
{
	CamFlavorIds ids;
	ids.Remember("HD Pro Webcam C920", 1);
	ids.Remember("HD Pro Webcam C920", 2);
	CHECK(ids.Take("HD Pro Webcam C920", 7) == 1);
	CHECK(ids.Take("HD Pro Webcam C920", 8) == 2);
	CHECK(ids.Take("HD Pro Webcam C920", 9) == 9);
}


static void
test_full_table_forgets_the_oldest()
{
	CamFlavorIds ids;
	char name[16];
	for (int i = 0; i <= CamFlavorIds::kMaxRemembered; i++) {
		snprintf(name, sizeof(name), "camera %d", i);
		ids.Remember(name, i + 1);
	}
	CHECK(ids.Take("camera 0", 99) == 99);
	snprintf(name, sizeof(name), "camera %d", CamFlavorIds::kMaxRemembered);
	CHECK(ids.Take(name, 99) == CamFlavorIds::kMaxRemembered + 1);
}


static void
test_missing_name_is_never_remembered()
{
	CamFlavorIds ids;
	ids.Remember(NULL, 1);
	CHECK(ids.Take(NULL, 3) == 3);
	CHECK(ids.Take("HD Pro Webcam C920", 4) == 4);
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
	RUN(test_new_camera_gets_the_fresh_id);
	RUN(test_camera_plugged_in_again_gets_its_old_id);
	RUN(test_old_id_is_given_only_once);
	RUN(test_other_camera_gets_a_fresh_id_and_leaves_the_old_one);
	RUN(test_two_cameras_of_one_model_get_both_ids_back);
	RUN(test_full_table_forgets_the_oldest);
	RUN(test_missing_name_is_never_remembered);
	printf("%d tests, %d failures\n", sTests, sFailures);
	return sFailures == 0 ? 0 : 1;
}
