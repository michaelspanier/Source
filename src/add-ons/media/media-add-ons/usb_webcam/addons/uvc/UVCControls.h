/*
 * Copyright 2026, Michael Spanier.
 * Distributed under the terms of the MIT License.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef UVC_CONTROLS_H
#define UVC_CONTROLS_H


#include <stddef.h>
#include <stdint.h>


/*!	Tells whether a control transfer moved exactly the \a expected bytes. A
	negative \a transferred is the error code of the transfer.
*/
static inline bool
uvc_transfer_succeeded(int32_t transferred, size_t expected)
{
	return transferred >= 0 && (size_t)transferred == expected;
}


/*!	Rounds a slider \a value to the nearest whole step and keeps it between
	\a minimum and \a maximum, the limits the camera reported.
*/
static inline int32_t
uvc_clamp_control_value(float value, int32_t minimum, int32_t maximum)
{
	if (!(value == value) || value <= (float)minimum)
		return minimum;
	if (value >= (float)maximum)
		return maximum;

	// Round half away from zero. The range check above keeps the result
	// between the limits. Above 2^24 a float does not hold every whole
	// number, so such values are already rounded to the nearest float.
	return (int32_t)(value < 0.0f ? value - 0.5f : value + 0.5f);
}


enum {
	kUvcExposureManual = 0x01,
	kUvcExposureAuto = 0x02,
	kUvcExposureShutterPriority = 0x04,
	kUvcExposureAperturePriority = 0x08
};


/*!	Picks the exposure mode bit for "automatic" or "manual" out of the modes
	the camera \a supported (a bit mask). Automatic prefers the full automatic
	mode, then aperture priority, then shutter priority. A \a supported mask of
	0 means the camera did not tell, then the two standard modes are assumed.
	Returns 0 when no mode fits.
*/
static inline uint8_t
uvc_exposure_mode(bool automatic, uint8_t supported)
{
	if (supported == 0)
		supported = kUvcExposureManual | kUvcExposureAuto;

	if (!automatic)
		return (supported & kUvcExposureManual) != 0 ? kUvcExposureManual : 0;

	if ((supported & kUvcExposureAuto) != 0)
		return kUvcExposureAuto;
	if ((supported & kUvcExposureAperturePriority) != 0)
		return kUvcExposureAperturePriority;
	if ((supported & kUvcExposureShutterPriority) != 0)
		return kUvcExposureShutterPriority;
	return 0;
}


/*!	Tells whether the current exposure mode \a mode lets the camera choose the
	exposure time.
*/
static inline bool
uvc_exposure_mode_is_automatic(uint8_t mode)
{
	return (mode & (kUvcExposureAuto | kUvcExposureShutterPriority
		| kUvcExposureAperturePriority)) != 0;
}


/*!	Two 16 bit values as the camera wants them: first, then second, each low
	byte first. The white balance component control (blue, red) uses this.
*/
static inline void
uvc_pack_pair(int16_t first, int16_t second, uint8_t* out)
{
	out[0] = (uint8_t)((uint16_t)first & 0xff);
	out[1] = (uint8_t)((uint16_t)first >> 8);
	out[2] = (uint8_t)((uint16_t)second & 0xff);
	out[3] = (uint8_t)((uint16_t)second >> 8);
}


static inline void
uvc_unpack_pair(const uint8_t* in, int16_t* first, int16_t* second)
{
	*first = (int16_t)(uint16_t)(in[0] | (in[1] << 8));
	*second = (int16_t)(uint16_t)(in[2] | (in[3] << 8));
}


/*!	A 32 bit value, low byte first. The absolute exposure time uses this. */
static inline void
uvc_pack_u32(uint32_t value, uint8_t* out)
{
	out[0] = (uint8_t)(value & 0xff);
	out[1] = (uint8_t)((value >> 8) & 0xff);
	out[2] = (uint8_t)((value >> 16) & 0xff);
	out[3] = (uint8_t)(value >> 24);
}


static inline uint32_t
uvc_unpack_u32(const uint8_t* in)
{
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16)
		| ((uint32_t)in[3] << 24);
}


/*!	Reads a control value of \a length bytes (1, 2 or 4), low byte first. A
	\a isSigned value is extended with its sign.
*/
static inline int32_t
uvc_decode_value(const uint8_t* in, uint8_t length, bool isSigned)
{
	if (length == 1)
		return isSigned ? (int32_t)(int8_t)in[0] : (int32_t)in[0];
	if (length == 2) {
		uint16_t value = (uint16_t)(in[0] | (in[1] << 8));
		return isSigned ? (int32_t)(int16_t)value : (int32_t)value;
	}
	return (int32_t)uvc_unpack_u32(in);
}


/*!	Writes \a value as \a length bytes (1, 2 or 4), low byte first. */
static inline void
uvc_encode_value(int32_t value, uint8_t length, uint8_t* out)
{
	for (uint8_t i = 0; i < length; i++)
		out[i] = (uint8_t)(((uint32_t)value >> (8 * i)) & 0xff);
}


// The parameters of the camera, in the order of their numbers. The number
// plus the first parameter number of the device is the parameter ID.
enum {
	kUvcParameterBrightness = 0,
	kUvcParameterContrast,
	kUvcParameterHue,
	kUvcParameterHueAuto,
	kUvcParameterSaturation,
	kUvcParameterSharpness,
	kUvcParameterGamma,
	kUvcParameterWhiteBalanceTemperature,
	kUvcParameterWhiteBalanceTemperatureAuto,
	kUvcParameterWhiteBalanceBlue,
	kUvcParameterWhiteBalanceComponentAuto,
	kUvcParameterBacklightCompensation,
	kUvcParameterGain,
	kUvcParameterPowerLineFrequency,
	kUvcParameterWhiteBalanceRed,
	kUvcParameterExposureAuto,
	kUvcParameterExposureTime,
	kUvcParameterExposureFrameRate,
	kUvcParameterCount
};


enum UVCControlKind {
	kUvcControlNone,
	kUvcControlRange,
	kUvcControlSwitch,
	kUvcControlChoice,
	kUvcControlExposureMode
};


// What is known about one control of the camera.
struct UVCControl {
			UVCControlKind		kind;
			uint8_t				selector;
			uint16_t			requestIndex;
			uint8_t				length;
			int32_t				minimum;
			int32_t				maximum;
			int32_t				value;
			int32_t				autoParameter;
};


#endif	// UVC_CONTROLS_H
