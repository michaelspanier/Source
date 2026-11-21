/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CAM_DEVICE_NAME_H
#define CAM_DEVICE_NAME_H


#include <stddef.h>
#include <string.h>


static inline bool
cam_is_space(char c)
{
	return c == ' ' || c == '\t';
}


//!	A byte that continues a UTF-8 character, which must not be cut off.
static inline bool
cam_is_utf8_continuation(char c)
{
	return ((unsigned char)c & 0xc0) == 0x80;
}


/*!	Writes the name a camera is shown with: the product string the device
	reports, without the spaces around it and cut to fit \a size without
	splitting a character. Returns false and leaves \a buffer alone if the
	device reports no product string; the caller keeps its generic name then.
*/
static inline bool
cam_device_name(char* buffer, size_t size, const char* product)
{
	if (product == NULL || size == 0)
		return false;

	while (cam_is_space(*product))
		product++;
	size_t length = strlen(product);
	while (length > 0 && cam_is_space(product[length - 1]))
		length--;
	if (length == 0)
		return false;

	if (length > size - 1) {
		length = size - 1;
		while (length > 0 && cam_is_utf8_continuation(product[length]))
			length--;
		while (length > 0 && cam_is_space(product[length - 1]))
			length--;
	}

	memcpy(buffer, product, length);
	buffer[length] = '\0';
	return true;
}


#endif	// CAM_DEVICE_NAME_H
