/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */


#include "UVCJpegDecoder.h"

#include <new>
#include <setjmp.h>
#include <stdio.h>

#include <jpeglib.h>

#ifndef JCS_EXTENSIONS
#	error "libjpeg-turbo is needed for decoding to B_RGB32"
#endif


static const int32_t kBytesPerPixel = 4;


//	libjpeg ends the process on errors unless the handler jumps back.
struct uvc_jpeg_errors {
	jpeg_error_mgr	manager;
	jmp_buf			jump;
};


struct UVCJpegDecoder::State {
	jpeg_decompress_struct	info;
	uvc_jpeg_errors			errors;
};


static void
uvc_jpeg_fail(j_common_ptr info)
{
	longjmp(((uvc_jpeg_errors*)info->err)->jump, 1);
}


static void
uvc_jpeg_stay_quiet(j_common_ptr)
{
}


UVCJpegDecoder::UVCJpegDecoder()
	:
	fState(new(std::nothrow) State())
{
	if (fState == NULL)
		return;

	fState->info.err = jpeg_std_error(&fState->errors.manager);
	fState->errors.manager.error_exit = uvc_jpeg_fail;
	fState->errors.manager.output_message = uvc_jpeg_stay_quiet;
	if (setjmp(fState->errors.jump) != 0) {
		// State() starts zeroed, so this is safe at every point.
		jpeg_destroy_decompress(&fState->info);
		delete fState;
		fState = NULL;
		return;
	}
	jpeg_create_decompress(&fState->info);
}


UVCJpegDecoder::~UVCJpegDecoder()
{
	if (fState == NULL)
		return;

	jpeg_destroy_decompress(&fState->info);
	delete fState;
}


bool
UVCJpegDecoder::InitCheck() const
{
	return fState != NULL;
}


/*!	Decodes \a jpeg into \a picture, which must have the size of the JPEG
	picture. Returns false for anything but a complete, undamaged picture;
	\a picture may then be partly written.
*/
bool
UVCJpegDecoder::Decode(const uint8_t* jpeg, size_t length,
	const uvc_picture& picture)
{
	if (fState == NULL || jpeg == NULL || length == 0 || picture.width <= 0
		|| picture.height <= 0
		|| picture.size / kBytesPerPixel / picture.width
			< (size_t)picture.height) {
		return false;
	}

	jpeg_decompress_struct& info = fState->info;
	if (setjmp(fState->errors.jump) != 0) {
		jpeg_abort_decompress(&info);
		return false;
	}

	jpeg_mem_src(&info, (unsigned char*)jpeg, length);
	jpeg_read_header(&info, TRUE);
	if (info.image_width != (JDIMENSION)picture.width
		|| info.image_height != (JDIMENSION)picture.height) {
		jpeg_abort_decompress(&info);
		return false;
	}

	info.out_color_space = JCS_EXT_BGRX;
	jpeg_start_decompress(&info);
	const size_t bytesPerRow = (size_t)picture.width * kBytesPerPixel;
	while (info.output_scanline < info.output_height) {
		JSAMPROW row = picture.bits + info.output_scanline * bytesPerRow;
		jpeg_read_scanlines(&info, &row, 1);
	}

	// libjpeg fills in what is missing of a damaged picture and only warns.
	const bool complete = info.err->num_warnings == 0;
	jpeg_finish_decompress(&info);
	return complete;
}
