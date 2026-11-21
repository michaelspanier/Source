/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CAM_REPLUG_H
#define CAM_REPLUG_H


#include <string.h>


/*!	Whether a camera just plugged in replaces the one a node streams from.
	The node keeps its camera while that one works; it takes only the same
	model back, which is how a camera unplugged and plugged in again shows up.
*/
static inline bool
cam_takes_over(bool currentPlugged, const char* currentName,
	const char* newName)
{
	if (currentPlugged || currentName == NULL || newName == NULL)
		return false;
	return strcmp(currentName, newName) == 0;
}


#endif	// CAM_REPLUG_H
