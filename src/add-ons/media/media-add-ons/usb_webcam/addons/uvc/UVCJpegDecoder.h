/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UVC_JPEG_DECODER_H
#define UVC_JPEG_DECODER_H


#include <stddef.h>
#include <stdint.h>


struct uvc_picture {
			uint8_t*			bits;
			size_t				size;
			int32_t				width;
			int32_t				height;
};


/*!	Decodes the JPEG pictures of an MJPEG stream into B_RGB32. Keeps one
	decompressor for the whole stream.
*/
class UVCJpegDecoder {
public:
								UVCJpegDecoder();
								~UVCJpegDecoder();

			bool				InitCheck() const;
			bool				Decode(const uint8_t* jpeg, size_t length,
									const uvc_picture& picture);

private:
								UVCJpegDecoder(const UVCJpegDecoder&);
			UVCJpegDecoder&		operator=(const UVCJpegDecoder&);

			struct				State;
			State*				fState;
};


#endif	// UVC_JPEG_DECODER_H
