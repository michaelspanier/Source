/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UVC_COLOR_H
#define UVC_COLOR_H


#include <stddef.h>
#include <stdint.h>


static inline uint8_t
uvc_clamp_to_byte(int32_t value)
{
	return value < 0 ? 0 : (value > 255 ? 255 : value);
}


/*!	Tells whether a YUY2 frame of \a frameLength bytes holds a whole picture of
	the given size and fits into an output buffer of \a outputSize bytes. A
	shorter frame was damaged on the way.
*/
static inline bool
uvc_yuy2_frame_fits(size_t frameLength, size_t outputSize, int32_t width,
	int32_t height)
{
	if (width <= 0 || height <= 0)
		return false;
	size_t pixels = (size_t)width * height;
	return frameLength >= pixels * 2 && outputSize >= pixels * 4;
}


/*!	Converts YUY2 (Y0 U Y1 V, limited range, BT.601) to B_RGB32. The caller
	checks once that \a src holds width * height * 2 bytes and \a dst
	width * height * 4; the loop itself does not check.
*/
static inline void
uvc_decode_yuy2(uint8_t* dst, const uint8_t* src, int32_t width,
	int32_t height)
{
	for (int32_t i = 0; i < width * height / 2; i++) {
		int32_t u = src[1] - 128;
		int32_t v = src[3] - 128;
		for (int32_t k = 0; k < 2; k++) {
			int32_t c = (src[k * 2] - 16) * 298 + 128;
			dst[0] = uvc_clamp_to_byte((c + 516 * u) >> 8);
			dst[1] = uvc_clamp_to_byte((c - 100 * u - 208 * v) >> 8);
			dst[2] = uvc_clamp_to_byte((c + 409 * v) >> 8);
			dst[3] = 255;
			dst += 4;
		}
		src += 4;
	}
}


#endif	// UVC_COLOR_H
