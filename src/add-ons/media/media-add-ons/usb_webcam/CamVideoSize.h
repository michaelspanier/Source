/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CAM_VIDEO_SIZE_H
#define CAM_VIDEO_SIZE_H


#include <stddef.h>
#include <stdint.h>
#include <stdio.h>


//	The sizes a camera offers are published as the items of a discrete
//	B_RESOLUTION parameter. An item's value holds the width in the upper and
//	the height in the lower 16 bits, so that applications can read the list
//	without guessing.

static const int kCamSizeShift = 16;
static const uint32_t kCamSizeMask = 0xffff;
static const uint32_t kCamIntervalsPerSecond = 10000000;
	// UVC frame intervals count 100 ns.


static inline int32_t
cam_size_to_parameter(uint32_t width, uint32_t height)
{
	return (int32_t)(((width & kCamSizeMask) << kCamSizeShift)
		| (height & kCamSizeMask));
}


//!	Returns false and leaves the sides alone if \a value holds no size.
static inline bool
cam_size_from_parameter(int32_t value, uint32_t* width, uint32_t* height)
{
	uint32_t w = ((uint32_t)value >> kCamSizeShift) & kCamSizeMask;
	uint32_t h = (uint32_t)value & kCamSizeMask;
	if (w == 0 || h == 0)
		return false;

	*width = w;
	*height = h;
	return true;
}


//	Bounds the sides of a picture, so that its size at 4 bytes per pixel stays
//	far inside 32 bits. The sides come from the camera's descriptors, and no
//	USB camera comes near this.
static const uint32_t kCamMaxVideoSide = 8192;


static inline bool
cam_video_size_allowed(uint32_t width, uint32_t height)
{
	return width > 0 && height > 0 && width <= kCamMaxVideoSide
		&& height <= kCamMaxVideoSide;
}


//!	The rounded frames per second of an \a interval, 0 if it is unknown.
static inline uint32_t
cam_frames_per_second(uint32_t interval)
{
	if (interval == 0)
		return 0;
	return (kCamIntervalsPerSecond + interval / 2) / interval;
}


struct cam_video_size {
			uint32_t			width;
			uint32_t			height;
			uint32_t			interval;
				// 100 ns units, 0 if unknown
			const char*			format;
				// how the camera sends it, NULL if it does not matter
};


//!	Writes "1920 × 1080 (30 fps, MJPEG)", leaving out what is unknown.
static inline void
cam_size_label(char* buffer, size_t bufferSize, const cam_video_size& size)
{
	int length = snprintf(buffer, bufferSize, "%u \xc3\x97 %u",
		(unsigned)size.width, (unsigned)size.height);
	if (length < 0 || (size_t)length >= bufferSize)
		return;

	uint32_t rate = cam_frames_per_second(size.interval);
	char* end = buffer + length;
	size_t left = bufferSize - length;
	if (rate != 0 && size.format != NULL)
		snprintf(end, left, " (%u fps, %s)", (unsigned)rate, size.format);
	else if (rate != 0)
		snprintf(end, left, " (%u fps)", (unsigned)rate);
	else if (size.format != NULL)
		snprintf(end, left, " (%s)", size.format);
}


#endif	// CAM_VIDEO_SIZE_H
