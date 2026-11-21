/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UVC_FRAME_BUFFER_H
#define UVC_FRAME_BUFFER_H


#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


//	No frame of a USB camera comes near this; it only bounds what a broken
//	device can make us allocate.
static const size_t kUVCMaxFrameBufferSize = 64 * 1024 * 1024;
static const uint64_t kUVCUncompressedBytesPerPixel = 2;


/*!	Returns how much a frame may hold: what the camera promised in the probe
	(\a maxVideoFrameSize), but at least an uncompressed frame of
	\a width x \a height, as some cameras promise too little.
*/
static inline size_t
uvc_frame_buffer_size(uint32_t maxVideoFrameSize, uint32_t width,
	uint32_t height)
{
	uint64_t size = (uint64_t)width * height * kUVCUncompressedBytesPerPixel;
	if (size < maxVideoFrameSize)
		size = maxVideoFrameSize;
	if (size > kUVCMaxFrameBufferSize)
		size = kUVCMaxFrameBufferSize;
	return (size_t)size;
}


/*!	Collects the payloads of one frame in memory that is allocated once per
	stream, not per packet. A frame that does not fit is broken.
*/
class UVCFrameBuffer {
public:
								UVCFrameBuffer();
								~UVCFrameBuffer();

			bool				SetCapacity(size_t capacity);
			void				Append(const void* data, size_t length);

			void				MarkBroken() { fBroken = true; }
			void				Clear() { fLength = 0; fBroken = false; }

			const void*			Data() const { return fData; }
			size_t				Length() const { return fLength; }
			bool				IsBroken() const { return fBroken; }

private:
								UVCFrameBuffer(const UVCFrameBuffer&);
			UVCFrameBuffer&		operator=(const UVCFrameBuffer&);

			uint8_t*			fData;
			size_t				fLength;
			size_t				fCapacity;
			bool				fBroken;
};


inline
UVCFrameBuffer::UVCFrameBuffer()
	:
	fData(NULL),
	fLength(0),
	fCapacity(0),
	fBroken(false)
{
}


inline
UVCFrameBuffer::~UVCFrameBuffer()
{
	free(fData);
}


inline bool
UVCFrameBuffer::SetCapacity(size_t capacity)
{
	Clear();
	if (capacity == fCapacity)
		return true;

	uint8_t* data = (uint8_t*)realloc(fData, capacity);
	if (data == NULL && capacity > 0)
		return false;
	fData = data;
	fCapacity = capacity;
	return true;
}


inline void
UVCFrameBuffer::Append(const void* data, size_t length)
{
	if (fBroken || length > fCapacity - fLength) {
		fBroken = true;
		return;
	}
	if (length == 0)
		return;
	memcpy(fData + fLength, data, length);
	fLength += length;
}


#endif	// UVC_FRAME_BUFFER_H
