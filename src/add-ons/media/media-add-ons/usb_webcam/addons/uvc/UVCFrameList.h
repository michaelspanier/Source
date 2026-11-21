/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UVC_FRAME_LIST_H
#define UVC_FRAME_LIST_H


#include <stddef.h>
#include <stdint.h>


static const int32_t kUvcMaxFrames = 32;


struct uvc_frame {
			uint16_t			width;
			uint16_t			height;
			uint8_t				formatIndex;
			uint8_t				frameIndex;
			uint32_t			interval;
				// 100 ns units, 0 if unknown
			bool				compressed;
};


struct uvc_frame_list {
			uvc_frame			frames[kUvcMaxFrames];
			int32_t				count;
};


static inline void
uvc_frame_list_init(uvc_frame_list* list)
{
	list->count = 0;
}


static inline const uvc_frame*
uvc_frame_list_find(const uvc_frame_list* list, uint32_t width,
	uint32_t height)
{
	for (int32_t i = 0; i < list->count; i++) {
		const uvc_frame& frame = list->frames[i];
		if (frame.width == width && frame.height == height)
			return &frame;
	}
	return NULL;
}


/*!	Tells whether \a candidate should replace \a current of the same size:
	more frames per second win, at equal speed the uncompressed frame wins as
	it needs no decoding.
*/
static inline bool
uvc_frame_is_better(const uvc_frame& candidate, const uvc_frame& current)
{
	if (candidate.interval == current.interval)
		return !candidate.compressed && current.compressed;
	if (candidate.interval == 0)
		return false;
	return current.interval == 0 || candidate.interval < current.interval;
}


static inline bool
uvc_frame_is_smaller(const uvc_frame& frame, const uvc_frame& other)
{
	uint32_t area = (uint32_t)frame.width * frame.height;
	uint32_t otherArea = (uint32_t)other.width * other.height;
	if (area != otherArea)
		return area < otherArea;
	return frame.width < other.width;
}


/*!	Adds a frame of one of the camera's formats. A size offered by several
	formats is listed once, with the better format. The list stays sorted
	from the smallest to the largest picture.
*/
static inline void
uvc_frame_list_add(uvc_frame_list* list, const uvc_frame& frame)
{
	if (frame.width == 0 || frame.height == 0)
		return;

	uvc_frame* known = (uvc_frame*)uvc_frame_list_find(list, frame.width,
		frame.height);
	if (known != NULL) {
		if (uvc_frame_is_better(frame, *known))
			*known = frame;
		return;
	}
	if (list->count == kUvcMaxFrames)
		return;

	int32_t index = list->count;
	while (index > 0 && uvc_frame_is_smaller(frame, list->frames[index - 1])) {
		list->frames[index] = list->frames[index - 1];
		index--;
	}
	list->frames[index] = frame;
	list->count++;
}


#endif	// UVC_FRAME_LIST_H
